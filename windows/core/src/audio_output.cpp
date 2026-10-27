#include "webicam/audio_output.h"

#include <windows.h>
#include <audioclient.h>
#include <mmdeviceapi.h>
#include <wrl/client.h>
// PKEY_Device_FriendlyName is defined here rather than taken from a library.
#include <initguid.h>
#include <propkeydef.h>
#include <functiondiscoverykeys_devpkey.h>

#include <algorithm>
#include <chrono>
#include <cmath>

namespace webicam {

using Microsoft::WRL::ComPtr;

// ---- AudioJitterBuffer -------------------------------------------------------

void AudioJitterBuffer::Push(std::span<const int16_t> samples) {
  int peak = 0;
  for (int16_t s : samples) peak = std::max(peak, std::abs(int(s)));
  std::lock_guard lock(mutex_);
  samples_.insert(samples_.end(), samples.begin(), samples.end());
  // Fell behind (the sound card runs slower than the phone, or a burst arrived): catch up at once.
  if (samples_.size() > limit_) samples_.erase(samples_.begin(), samples_.end() - ptrdiff_t(target_));
  level_ = std::max(float(peak) / 32768.0f, level_ * 0.85f);
}

void AudioJitterBuffer::Pull(std::span<int16_t> out) {
  std::lock_guard lock(mutex_);
  if (!playing_ && samples_.size() >= target_) playing_ = true;
  size_t copied = 0;
  if (playing_) {
    copied = std::min(out.size(), samples_.size());
    std::copy_n(samples_.begin(), copied, out.begin());
    samples_.erase(samples_.begin(), samples_.begin() + ptrdiff_t(copied));
    // Ran dry: wait for a full cushion again instead of crackling sample by sample.
    if (copied < out.size()) playing_ = false;
  }
  std::fill(out.begin() + ptrdiff_t(copied), out.end(), int16_t(0));
}

void AudioJitterBuffer::Reset() {
  std::lock_guard lock(mutex_);
  samples_.clear();
  playing_ = false;
  level_ = 0;
}

size_t AudioJitterBuffer::Buffered() const {
  std::lock_guard lock(mutex_);
  return samples_.size();
}

float AudioJitterBuffer::Level() const {
  std::lock_guard lock(mutex_);
  return level_;
}

// ---- Devices -----------------------------------------------------------------

namespace {

std::string Utf8(const wchar_t* text) {
  if (!text || !*text) return {};
  const int size = WideCharToMultiByte(CP_UTF8, 0, text, -1, nullptr, 0, nullptr, nullptr);
  std::string out(size_t(std::max(size - 1, 0)), '\0');
  WideCharToMultiByte(CP_UTF8, 0, text, -1, out.data(), size, nullptr, nullptr);
  return out;
}

std::wstring Wide(const std::string& text) {
  if (text.empty()) return {};
  const int size = MultiByteToWideChar(CP_UTF8, 0, text.c_str(), int(text.size()), nullptr, 0);
  std::wstring out(size_t(size), L'\0');
  MultiByteToWideChar(CP_UTF8, 0, text.c_str(), int(text.size()), out.data(), size);
  return out;
}

// COM for the calling thread, released only if this call was the one that started it.
struct ComScope {
  ComScope() : owned(SUCCEEDED(CoInitializeEx(nullptr, COINIT_MULTITHREADED))) {}
  ~ComScope() {
    if (owned) CoUninitialize();
  }
  bool owned;
};

}  // namespace

std::vector<AudioDevice> ListAudioOutputs() {
  ComScope com;
  std::vector<AudioDevice> devices;
  ComPtr<IMMDeviceEnumerator> enumerator;
  ComPtr<IMMDeviceCollection> collection;
  if (FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, IID_PPV_ARGS(&enumerator))) ||
      FAILED(enumerator->EnumAudioEndpoints(eRender, DEVICE_STATE_ACTIVE, &collection))) {
    return devices;
  }
  UINT count = 0;
  collection->GetCount(&count);
  for (UINT i = 0; i < count; ++i) {
    ComPtr<IMMDevice> device;
    ComPtr<IPropertyStore> properties;
    LPWSTR id = nullptr;
    if (FAILED(collection->Item(i, &device)) || FAILED(device->GetId(&id))) continue;
    AudioDevice entry{Utf8(id), {}};
    CoTaskMemFree(id);
    if (SUCCEEDED(device->OpenPropertyStore(STGM_READ, &properties))) {
      PROPVARIANT name;
      PropVariantInit(&name);
      if (SUCCEEDED(properties->GetValue(PKEY_Device_FriendlyName, &name)) && name.vt == VT_LPWSTR) {
        entry.name = Utf8(name.pwszVal);
      }
      PropVariantClear(&name);
    }
    if (entry.name.empty()) entry.name = entry.id;
    devices.push_back(std::move(entry));
  }
  return devices;
}

std::string FindVirtualCable(const std::vector<AudioDevice>& devices) {
  for (const auto& d : devices) {
    if (d.name.find("CABLE Input") != std::string::npos) return d.id;
  }
  return {};
}

// ---- AudioOutput ---------------------------------------------------------------

AudioOutput::AudioOutput() : thread_([this] { Run(); }) {}

AudioOutput::~AudioOutput() {
  stop_ = true;
  thread_.join();
}

void AudioOutput::SetDevice(const std::string& deviceId) {
  {
    std::lock_guard lock(mutex_);
    if (device_ == deviceId) return;
    device_ = deviceId;
    error_.clear();
  }
  ++generation_;
}

std::string AudioOutput::Device() const {
  std::lock_guard lock(mutex_);
  return device_;
}

std::string AudioOutput::Error() const {
  std::lock_guard lock(mutex_);
  return error_;
}

void AudioOutput::SetError(std::string error) {
  std::lock_guard lock(mutex_);
  error_ = std::move(error);
}

void AudioOutput::Run() {
  ComScope com;
  while (!stop_) {
    const uint32_t generation = generation_;
    const std::string id = Device();
    if (id.empty()) {
      std::this_thread::sleep_for(std::chrono::milliseconds(50));
      continue;
    }

    // One playback session; any failure falls through to a retry a second later.
    [&] {
      ComPtr<IMMDeviceEnumerator> enumerator;
      ComPtr<IMMDevice> device;
      ComPtr<IAudioClient> client;
      ComPtr<IAudioRenderClient> render;
      if (FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, IID_PPV_ARGS(&enumerator))) ||
          FAILED(enumerator->GetDevice(Wide(id).c_str(), &device))) {
        SetError("Устройство вывода не найдено");
        return;
      }
      if (FAILED(device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, &client))) {
        SetError("Не удалось открыть устройство вывода");
        return;
      }
      // The device's own mix format is converted to by Windows (AUTOCONVERTPCM), so the phone's
      // 48 kHz mono goes in as is. 20 ms of device buffer.
      WAVEFORMATEX format{WAVE_FORMAT_PCM, 1, 48000, 48000 * 2, 2, 16, 0};
      const DWORD flags = AUDCLNT_STREAMFLAGS_EVENTCALLBACK | AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM |
                          AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY;
      if (FAILED(client->Initialize(AUDCLNT_SHAREMODE_SHARED, flags, 200'000, 0, &format, nullptr))) {
        SetError("Устройство вывода не принимает звук");
        return;
      }
      HANDLE event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
      UINT32 frames = 0;
      if (!event || FAILED(client->SetEventHandle(event)) || FAILED(client->GetBufferSize(&frames)) ||
          FAILED(client->GetService(IID_PPV_ARGS(&render))) || FAILED(client->Start())) {
        if (event) CloseHandle(event);
        SetError("Не удалось запустить вывод звука");
        return;
      }
      SetError("");
      while (!stop_ && generation_ == generation) {
        WaitForSingleObject(event, 100);
        UINT32 padding = 0;
        if (FAILED(client->GetCurrentPadding(&padding))) {
          SetError("Устройство вывода отключено");  // e.g. AUDCLNT_E_DEVICE_INVALIDATED
          break;
        }
        const UINT32 available = frames - padding;
        if (available == 0) continue;
        BYTE* data = nullptr;
        if (FAILED(render->GetBuffer(available, &data))) break;
        buffer_.Pull(std::span(reinterpret_cast<int16_t*>(data), available));
        render->ReleaseBuffer(available, 0);
      }
      client->Stop();
      CloseHandle(event);
    }();

    // Wait before reopening after a failure, but follow a new device at once.
    for (int i = 0; i < 20 && !stop_ && generation_ == generation && !Error().empty(); ++i) {
      std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
  }
}

}  // namespace webicam
