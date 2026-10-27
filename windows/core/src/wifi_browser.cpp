#include "webicam/wifi_browser.h"

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windns.h>

#include <algorithm>
#include <chrono>
#include <mutex>
#include <optional>

namespace webicam {

namespace {

constexpr wchar_t kServiceQuery[] = L"_webicam._tcp.local";
constexpr wchar_t kServiceSuffix[] = L"._webicam._tcp.local";
// A browse is restarted this often, so phones that joined without being announced to us still show up.
constexpr auto kRebrowseInterval = std::chrono::seconds(30);
// A resolve that never called back may be started again after this.
constexpr auto kResolveTimeout = std::chrono::seconds(5);

std::string Utf8(const wchar_t* text) {
  if (!text || !*text) return {};
  const int size = WideCharToMultiByte(CP_UTF8, 0, text, -1, nullptr, 0, nullptr, nullptr);
  std::string out(size_t(std::max(size - 1, 0)), '\0');
  WideCharToMultiByte(CP_UTF8, 0, text, -1, out.data(), size, nullptr, nullptr);
  return out;
}

// "iPhone\032Андрея._webicam._tcp.local" → "iPhone Андрея" (DNS-SD escapes: \DDD decimal, \c literal).
std::string DisplayName(const std::wstring& instance) {
  std::wstring name = instance;
  const std::wstring suffix = kServiceSuffix;
  if (name.size() > suffix.size() && _wcsicmp(name.c_str() + name.size() - suffix.size(), suffix.c_str()) == 0) {
    name.resize(name.size() - suffix.size());
  }
  std::wstring out;
  for (size_t i = 0; i < name.size(); ++i) {
    if (name[i] != L'\\' || i + 1 >= name.size()) {
      out += name[i];
    } else if (i + 3 < name.size() && iswdigit(name[i + 1]) && iswdigit(name[i + 2]) && iswdigit(name[i + 3])) {
      out += wchar_t((name[i + 1] - L'0') * 100 + (name[i + 2] - L'0') * 10 + (name[i + 3] - L'0'));
      i += 3;
    } else {
      out += name[++i];
    }
  }
  return Utf8(out.c_str());
}

}  // namespace

struct WifiBrowser::State {
  struct Entry {
    std::wstring instance;
    std::optional<WifiPhone> phone;
    std::optional<std::chrono::steady_clock::time_point> resolvingSince;
  };

  std::mutex mutex;
  bool running = false;
  bool browsing = false;
  std::chrono::steady_clock::time_point browseStarted;
  // DNS-SD keeps writing to the cancel handle until the browse is cancelled, so it lives on the heap.
  std::unique_ptr<DNS_SERVICE_CANCEL> browseCancel;
  std::vector<Entry> entries;
  // Handed to DNS-SD as the browse context; outlives every browse callback.
  std::weak_ptr<State>* self = nullptr;
};

namespace {

struct ResolveContext {
  std::weak_ptr<WifiBrowser::State> state;
  std::wstring instance;
  DNS_SERVICE_CANCEL cancel{};  // must stay valid until the resolve completes
};

void WINAPI OnResolved(DWORD status, PVOID context, PDNS_SERVICE_INSTANCE instance) {
  std::unique_ptr<ResolveContext> ctx(static_cast<ResolveContext*>(context));
  if (auto state = ctx->state.lock()) {
    std::lock_guard lock(state->mutex);
    auto entry = std::ranges::find(state->entries, ctx->instance, &WifiBrowser::State::Entry::instance);
    if (entry != state->entries.end()) {
      entry->resolvingSince.reset();
      entry->phone.reset();
      if (status == ERROR_SUCCESS && instance && instance->wPort) {
        WifiPhone phone;
        phone.name = DisplayName(ctx->instance);
        phone.port = instance->wPort;
        if (instance->ip4Address) {
          char text[INET_ADDRSTRLEN] = {};
          in_addr address{};
          address.s_addr = *instance->ip4Address;
          inet_ntop(AF_INET, &address, text, sizeof(text));
          phone.host = text;
        } else {
          phone.host = Utf8(instance->pszHostName);
        }
        for (DWORD i = 0; i < instance->dwPropertyCount; ++i) {
          if (instance->keys[i] && _wcsicmp(instance->keys[i], L"id") == 0) phone.id = Utf8(instance->values[i]);
        }
        if (phone.id.empty()) phone.id = "name:" + Utf8(ctx->instance.c_str());
        if (!phone.host.empty()) entry->phone = std::move(phone);
      }
    }
  }
  if (instance) DnsServiceFreeInstance(instance);
}

// Starts a resolve of `instance` unless one is running; call without the state's lock.
void Resolve(const std::shared_ptr<WifiBrowser::State>& state, const std::wstring& instance) {
  {
    std::lock_guard lock(state->mutex);
    auto entry = std::ranges::find(state->entries, instance, &WifiBrowser::State::Entry::instance);
    if (entry == state->entries.end() || !state->running) return;
    const auto now = std::chrono::steady_clock::now();
    if (entry->resolvingSince && now - *entry->resolvingSince < kResolveTimeout) return;
    entry->resolvingSince = now;
  }
  auto* ctx = new ResolveContext{state, instance};
  DNS_SERVICE_RESOLVE_REQUEST request{};
  request.Version = DNS_QUERY_REQUEST_VERSION1;
  request.QueryName = const_cast<PWSTR>(ctx->instance.c_str());
  request.pResolveCompletionCallback = OnResolved;
  request.pQueryContext = ctx;
  if (DnsServiceResolve(&request, &ctx->cancel) != DNS_REQUEST_PENDING) {
    delete ctx;
    std::lock_guard lock(state->mutex);
    auto entry = std::ranges::find(state->entries, instance, &WifiBrowser::State::Entry::instance);
    if (entry != state->entries.end()) entry->resolvingSince.reset();
  }
}

void WINAPI OnBrowse(DWORD status, PVOID context, PDNS_RECORD records) {
  auto state = static_cast<std::weak_ptr<WifiBrowser::State>*>(context)->lock();
  std::vector<std::wstring> found;
  if (state) {
    std::lock_guard lock(state->mutex);
    if (status != ERROR_SUCCESS) state->browsing = false;
    for (auto* r = records; status == ERROR_SUCCESS && r; r = r->pNext) {
      if (r->wType != DNS_TYPE_PTR || !r->Data.PTR.pNameHost) continue;
      const std::wstring instance = r->Data.PTR.pNameHost;
      auto entry = std::ranges::find(state->entries, instance, &WifiBrowser::State::Entry::instance);
      if (r->dwTtl == 0) {  // goodbye: the phone left or the app was closed
        if (entry != state->entries.end()) state->entries.erase(entry);
        continue;
      }
      if (entry == state->entries.end()) state->entries.push_back({instance, std::nullopt, std::nullopt});
      found.push_back(instance);
    }
  }
  if (records) DnsRecordListFree(records, DnsFreeRecordList);
  for (const auto& instance : found) Resolve(state, instance);
}

// Restarts the browse when it ended or is due; call without the state's lock.
// Cancels a running browse and frees its handle; call without the state's lock.
void CancelBrowse(std::unique_ptr<DNS_SERVICE_CANCEL> cancel, bool browsing) {
  if (cancel && browsing) DnsServiceBrowseCancel(cancel.get());
}

void Rebrowse(const std::shared_ptr<WifiBrowser::State>& state, bool force) {
  std::unique_ptr<DNS_SERVICE_CANCEL> old;
  bool wasBrowsing = false;
  {
    std::lock_guard lock(state->mutex);
    if (!state->running) return;
    const auto now = std::chrono::steady_clock::now();
    if (!force && state->browsing && now - state->browseStarted < kRebrowseInterval) return;
    wasBrowsing = state->browsing;
    old = std::move(state->browseCancel);
    state->browsing = false;
    state->browseStarted = now;
  }
  CancelBrowse(std::move(old), wasBrowsing);
  DNS_SERVICE_BROWSE_REQUEST request{};
  request.Version = DNS_QUERY_REQUEST_VERSION1;
  request.QueryName = kServiceQuery;
  request.pBrowseCallback = OnBrowse;
  request.pQueryContext = state->self;
  auto cancel = std::make_unique<DNS_SERVICE_CANCEL>();
  const bool pending = DnsServiceBrowse(&request, cancel.get()) == DNS_REQUEST_PENDING;
  std::unique_ptr<DNS_SERVICE_CANCEL> stale;
  {
    std::lock_guard lock(state->mutex);
    // Another Rebrowse or Stop may have run meanwhile; keep only one browse alive.
    if (state->running && !state->browsing) {
      state->browsing = pending;
      state->browseCancel = std::move(cancel);
    } else {
      stale = std::move(cancel);
    }
  }
  CancelBrowse(std::move(stale), pending);
}

}  // namespace

WifiBrowser::WifiBrowser() : state_(std::make_shared<State>()) {
  // Never freed: a late browse callback may still read it after the browser is gone.
  state_->self = new std::weak_ptr<State>(state_);
}

WifiBrowser::~WifiBrowser() { Stop(); }

bool WifiBrowser::Start() {
  {
    std::lock_guard lock(state_->mutex);
    state_->running = true;
  }
  Rebrowse(state_, true);
  std::lock_guard lock(state_->mutex);
  return state_->browsing;
}

void WifiBrowser::Stop() {
  std::unique_ptr<DNS_SERVICE_CANCEL> cancel;
  bool browsing = false;
  {
    std::lock_guard lock(state_->mutex);
    state_->running = false;
    browsing = state_->browsing;
    cancel = std::move(state_->browseCancel);
    state_->browsing = false;
  }
  CancelBrowse(std::move(cancel), browsing);
}

std::vector<WifiPhone> WifiBrowser::Phones() const {
  Rebrowse(state_, false);
  std::lock_guard lock(state_->mutex);
  std::vector<WifiPhone> phones;
  for (const auto& entry : state_->entries) {
    if (entry.phone) phones.push_back(*entry.phone);
  }
  return phones;
}

void WifiBrowser::Refresh(const std::string& id) {
  std::wstring instance;
  {
    std::lock_guard lock(state_->mutex);
    for (const auto& entry : state_->entries) {
      if (entry.phone && entry.phone->id == id) instance = entry.instance;
    }
  }
  if (!instance.empty()) Resolve(state_, instance);
  Rebrowse(state_, true);
}

}  // namespace webicam
