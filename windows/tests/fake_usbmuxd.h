#pragma once

#include <atomic>
#include <functional>
#include <mutex>
#include <thread>

#include "webicam/net.h"
#include "webicam/plist.h"

// Scripted stand-in for Apple's usbmuxd. For every accepted connection it reads
// one plist request and lets `handler` answer; if the reply's Number is 0 and the
// request was Connect, the connection is then echoed back byte for byte.
class FakeUsbmuxd {
 public:
  using Handler = std::function<webicam::PlistValue(const webicam::PlistValue& request)>;

  explicit FakeUsbmuxd(Handler handler);
  ~FakeUsbmuxd();
  uint16_t port() const { return port_; }
  webicam::PlistValue lastRequest() const;

 private:
  void Run();

  Handler handler_;
  webicam::Socket listener_;
  uint16_t port_ = 0;
  std::atomic<bool> stop_{false};
  mutable std::mutex mutex_;
  webicam::PlistValue lastRequest_;
  std::thread thread_;
};

webicam::PlistValue ResultReply(int64_t number);
webicam::PlistValue DeviceListReply(std::vector<std::pair<int64_t, std::string>> idsAndTypes);
