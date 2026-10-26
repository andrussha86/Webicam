#pragma once

#include <compare>
#include <cstdint>
#include <span>
#include <string>
#include <variant>
#include <vector>

namespace webicam::proto {

inline constexpr uint16_t kProtocolVersion = 1;
inline constexpr uint16_t kPhonePort = 47800;
inline constexpr uint32_t kMaxPayloadBytes = 16u * 1024 * 1024;

enum class MsgType : uint8_t { Hello = 1, Configure, VideoFrame, RequestKeyframe, Ping, Pong, Status };

struct CaptureFormat {
  uint16_t width;
  uint16_t height;
  uint8_t maxFps;
  auto operator<=>(const CaptureFormat&) const = default;
};

// The PC sends only `version`; the phone also sends model and formats.
struct Hello {
  uint16_t version = kProtocolVersion;
  std::string model;
  std::vector<CaptureFormat> formats;
  bool operator==(const Hello&) const = default;
};

struct Configure {
  uint16_t width;
  uint16_t height;
  uint8_t fps;
  uint32_t bitrateKbps;
  bool operator==(const Configure&) const = default;
};

struct VideoFrame {
  uint64_t captureTimeUs;
  bool keyframe;
  std::vector<uint8_t> annexB;
  bool operator==(const VideoFrame&) const = default;
};

struct RequestKeyframe {
  bool operator==(const RequestKeyframe&) const = default;
};

struct Ping {
  uint64_t pcTimeUs;
  bool operator==(const Ping&) const = default;
};

struct Pong {
  uint64_t pcTimeUs;
  uint64_t phoneTimeUs;
  bool operator==(const Pong&) const = default;
};

enum class StreamState : uint8_t { Streaming = 0, StoppedByUser = 1, ThermalThrottled = 2, Error = 3 };

// thermal: 0 nominal, 1 fair, 2 serious, 3 critical.
struct Status {
  StreamState state;
  uint8_t thermal;
  std::string message;
  bool operator==(const Status&) const = default;
};

using Message = std::variant<Hello, Configure, VideoFrame, RequestKeyframe, Ping, Pong, Status>;

std::vector<uint8_t> Encode(const Message& message);

class MessageParser {
 public:
  enum class Result { NeedMore, Message, Error };

  void Feed(std::span<const uint8_t> bytes);
  // Once Error is returned the parser stays in the error state.
  Result Next(Message& out);
  size_t Buffered() const { return buffer_.size() - offset_; }

 private:
  std::vector<uint8_t> buffer_;
  size_t offset_ = 0;
  bool failed_ = false;
};

uint32_t DefaultBitrateKbps(uint16_t height, uint8_t fps);

}  // namespace webicam::proto
