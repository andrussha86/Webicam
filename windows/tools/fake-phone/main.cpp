// Stands in for the iPhone app: speaks the Webicam protocol over TCP and
// streams the synthetic H.264 test pattern.
//   fake-phone [--port 47800] [--width 1920 --height 1080 --fps 60]
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <mutex>
#include <string>
#include <thread>

#include "test_encoder.h"
#include "webicam/device_session.h"
#include "webicam/net.h"
#include "webicam/protocol.h"

using namespace webicam;

namespace {

struct Options {
  uint16_t port = proto::kPhonePort;
  proto::Configure initial{1920, 1080, 60, 25000};
};

void Serve(Socket client, const Options& options) {
  std::mutex sendMutex;
  auto send = [&](const proto::Message& m) {
    const auto bytes = proto::Encode(m);
    std::lock_guard lock(sendMutex);
    return client.SendAll(bytes);
  };
  send(proto::Hello{proto::kProtocolVersion, "FakePhone", {{1280, 720, 60}, {1920, 1080, 60}, {3840, 2160, 30}}});

  std::mutex configMutex;
  std::optional<proto::Configure> config;
  std::atomic<bool> keyframe{true};
  std::atomic<bool> stop{false};

  std::thread streamer([&] {
    std::unique_ptr<TestPatternEncoder> encoder;
    proto::Configure active{};
    auto next = std::chrono::steady_clock::now();
    while (!stop) {
      {
        std::lock_guard lock(configMutex);
        if (config && (!encoder || config->width != active.width || config->height != active.height ||
                       config->fps != active.fps || config->bitrateKbps != active.bitrateKbps)) {
          active = *config;
          encoder = TestPatternEncoder::Create(active.width, active.height, active.fps, active.bitrateKbps);
          keyframe = true;
          std::printf("configured %ux%u@%u %u kbps\n", active.width, active.height, active.fps, active.bitrateKbps);
        }
      }
      if (!encoder) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        continue;
      }
      next += std::chrono::microseconds(1'000'000 / active.fps);
      const uint64_t captureUs = NowUs();
      uint64_t index = 0;
      bool isKey = false;
      auto bytes = encoder->EncodeNext(keyframe.exchange(false), index, isKey);
      if (!bytes.empty() && !send(proto::VideoFrame{captureUs, isKey, std::move(bytes)})) break;
      std::this_thread::sleep_until(next);
    }
    stop = true;
  });

  // Battery slowly draining while plugged in, so the PC's top bar has something to show.
  std::thread telemetry([&] {
    uint8_t battery = 87;
    while (!stop) {
      send(proto::Telemetry{battery, proto::Charging::Charging, 1});
      for (int i = 0; i < 20 && !stop; ++i) std::this_thread::sleep_for(std::chrono::milliseconds(100));
      battery = battery > 5 ? uint8_t(battery - 1) : uint8_t(100);
    }
  });

  // A quiet 440 Hz tone in 20 ms chunks while the PC wants the microphone.
  std::atomic<bool> mic{false};
  std::thread microphone([&] {
    constexpr size_t kChunk = proto::kAudioSampleRate / 50;
    double phase = 0;
    auto next = std::chrono::steady_clock::now();
    while (!stop) {
      next += std::chrono::milliseconds(20);
      if (mic) {
        proto::AudioFrame frame{NowUs(), std::vector<int16_t>(kChunk)};
        for (auto& sample : frame.samples) {
          sample = int16_t(3000 * std::sin(phase));
          phase += 2 * 3.14159265358979 * 440 / proto::kAudioSampleRate;
        }
        send(frame);
      }
      std::this_thread::sleep_until(next);
    }
  });

  proto::MessageParser parser;
  std::array<uint8_t, 4096> buffer{};
  while (!stop) {
    const int r = client.Recv(buffer, 100);
    if (r == -2) continue;
    if (r <= 0) break;
    parser.Feed(std::span(buffer.data(), size_t(r)));
    proto::Message m;
    while (parser.Next(m) == proto::MessageParser::Result::Message) {
      if (auto* c = std::get_if<proto::Configure>(&m)) {
        std::lock_guard lock(configMutex);
        config = *c;
      } else if (auto* settings = std::get_if<proto::MicSettings>(&m)) {
        mic = settings->enabled;
        std::printf("microphone %s\n", settings->enabled ? "on" : "off");
      } else if (std::holds_alternative<proto::RequestKeyframe>(m)) {
        keyframe = true;
      } else if (auto* p = std::get_if<proto::Ping>(&m)) {
        send(proto::Pong{p->pcTimeUs, NowUs()});
      } else if (auto* s = std::get_if<proto::CameraSettings>(&m)) {
        // Echo the settings back as the lens state, with plausible iPhone ranges.
        std::printf("camera: lens %u zoom %.2f blur %u\n", unsigned(s->lens), s->zoom, unsigned(s->blur));
        send(proto::CameraState{0x1F, s->lens, 50, 6400, 30, 33333, -8, 8, 6, s->lens != proto::Lens::Front,
                                s->manualExposure ? s->iso : 200, s->manualExposure ? s->shutterUs : 10000,
                                s->manualWhiteBalance ? s->temperatureK : 5200, s->manualWhiteBalance ? s->tint : 0,
                                s->manualFocus ? s->lensPosition : 0.6f});
      }
    }
  }
  stop = true;
  streamer.join();
  telemetry.join();
  microphone.join();
  std::printf("client disconnected\n");
}

}  // namespace

int main(int argc, char** argv) {
  std::setvbuf(stdout, nullptr, _IONBF, 0);
  Options options;
  for (int i = 1; i + 1 < argc; i += 2) {
    const std::string arg = argv[i];
    const int value = std::atoi(argv[i + 1]);
    if (arg == "--port") options.port = uint16_t(value);
    if (arg == "--width") options.initial.width = uint16_t(value);
    if (arg == "--height") options.initial.height = uint16_t(value);
    if (arg == "--fps") options.initial.fps = uint8_t(value);
  }
  auto listener = Socket::Listen("127.0.0.1", options.port);
  if (!listener) {
    std::fprintf(stderr, "cannot listen on 127.0.0.1:%u\n", options.port);
    return 1;
  }
  std::printf("fake-phone listening on 127.0.0.1:%u\n", options.port);
  while (true) {
    Socket client = listener->Accept();
    if (!client.valid()) continue;
    std::printf("client connected\n");
    Serve(std::move(client), options);
  }
}
