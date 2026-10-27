#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace webicam {

// An iPhone running Webicam on the local network.
struct WifiPhone {
  std::string id;    // TXT "id" (stable per phone); "addr:<host>" for an address typed by hand
  std::string name;  // what the user sees: the Bonjour instance name or the typed address
  std::string host;  // IPv4 literal or a name to resolve
  uint16_t port = 0;
  bool operator==(const WifiPhone&) const = default;
};

// Watches the network for the `_webicam._tcp` Bonjour service (Windows DNS-SD, dnsapi) and resolves
// each phone it finds to an address. Callbacks run on system threads; Phones() is thread-safe.
class WifiBrowser {
 public:
  WifiBrowser();
  WifiBrowser(const WifiBrowser&) = delete;
  WifiBrowser& operator=(const WifiBrowser&) = delete;
  ~WifiBrowser();

  // False when DNS-SD is unavailable; the typed address still works then.
  bool Start();
  void Stop();
  // Resolved phones only, in the order they were found.
  std::vector<WifiPhone> Phones() const;
  // Resolves the phone again, e.g. after a connection to its old address failed.
  void Refresh(const std::string& id);

  struct State;

 private:
  std::shared_ptr<State> state_;
};

}  // namespace webicam
