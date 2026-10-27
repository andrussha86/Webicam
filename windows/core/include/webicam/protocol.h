#pragma once

#include <compare>
#include <cstdint>
#include <span>
#include <string>
#include <variant>
#include <vector>

namespace webicam::proto {

inline constexpr uint16_t kProtocolVersion = 6;
inline constexpr uint16_t kPhonePort = 47800;
inline constexpr uint32_t kMaxPayloadBytes = 16u * 1024 * 1024;

enum class MsgType : uint8_t {
  Hello = 1, Configure, VideoFrame, RequestKeyframe, Ping, Pong, Status, CameraSettings, CameraState, BackgroundImage,
  Pair, Telemetry, MicSettings, AudioFrame
};

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

// PairingRejected: a Wi-Fi connection sent no Pair or the wrong code; the phone closes it next.
enum class StreamState : uint8_t { Streaming = 0, StoppedByUser = 1, ThermalThrottled = 2, Error = 3, PairingRejected = 4 };

// thermal: 0 nominal, 1 fair, 2 serious, 3 critical.
struct Status {
  StreamState state;
  uint8_t thermal;
  std::string message;
  bool operator==(const Status&) const = default;
};

enum class Lens : uint8_t { UltraWide = 0, Wide = 1, Wide2x = 2, Telephoto = 3, Front = 4 };
inline constexpr int kLensCount = 5;

enum class BackgroundMode : uint8_t { Off = 0, Blur = 1, Image = 2, Color = 3 };
inline constexpr int kBackgroundModeCount = 4;

// Auto follows how the phone is held, but only ever turns the picture by 180°.
enum class Rotation : uint8_t { Auto = 0, Upright = 1, Flipped = 2 };
inline constexpr int kRotationCount = 3;

// Full snapshot of the PC's camera choices; the phone clamps every value to the active lens.
struct CameraSettings {
  Lens lens = Lens::Wide;
  float zoom = 1.0f;  // ≥ 1, multiplier within the lens
  bool manualExposure = false;
  float iso = 100.0f;
  uint32_t shutterUs = 10000;
  float exposureBias = 0.0f;  // EV, used in auto exposure
  bool manualWhiteBalance = false;
  float temperatureK = 5000.0f;
  float tint = 0.0f;
  bool manualFocus = false;
  float lensPosition = 0.5f;  // 0 near … 1 far
  uint8_t blur = 0;           // 0 off … 100 strongest
  Rotation rotation = Rotation::Auto;
  bool faceFocus = false;  // face-driven focus/exposure while those are on auto
  BackgroundMode backgroundMode = BackgroundMode::Off;
  uint32_t backgroundColor = 0x1F2125;  // 0x00RRGGBB
  bool mirrorHorizontal = false;
  bool mirrorVertical = false;
  // Centre of the zoomed area in the output frame, 0…1 from the top left; the phone keeps the area inside.
  float panX = 0.5f;
  float panY = 0.5f;
  bool operator==(const CameraSettings&) const = default;
};

// Ranges of the active lens and its current (auto or manual) values.
struct CameraState {
  uint8_t lenses = 0;  // bit i set = Lens(i) is available
  Lens lens = Lens::Wide;
  float isoMin = 0, isoMax = 0;
  uint32_t shutterMinUs = 0, shutterMaxUs = 0;
  float biasMin = 0, biasMax = 0;
  float zoomMax = 1;
  bool manualFocusSupported = false;
  float iso = 0;
  uint32_t shutterUs = 0;
  float temperatureK = 0;
  float tint = 0;
  float lensPosition = 0;
  bool reactionsSupported = false;  // by the active format
  bool reactionsEnabled = false;    // iOS Control Center, Video Effects
  bool gesturesEnabled = false;
  bool flipped = false;  // the picture is currently turned by 180° (manually or by Auto)
  bool operator==(const CameraState&) const = default;
};

// JPEG the phone puts behind the person in BackgroundMode::Image; empty = no picture.
struct BackgroundImage {
  std::vector<uint8_t> jpeg;
  bool operator==(const BackgroundImage&) const = default;
};

// Wi-Fi only: the code the phone shows on its screen, sent right after Hello.
struct Pair {
  std::string code;
  bool operator==(const Pair&) const = default;
};

enum class Charging : uint8_t { Unknown = 0, Unplugged = 1, Charging = 2, Full = 3 };
inline constexpr uint8_t kBatteryUnknown = 255;

// The phone's power and heat, every 2 s.
struct Telemetry {
  uint8_t battery = kBatteryUnknown;  // percent, 0…100, or kBatteryUnknown
  Charging charging = Charging::Unknown;
  uint8_t thermal = 0;  // 0 nominal, 1 fair, 2 serious, 3 critical
  bool operator==(const Telemetry&) const = default;
};

// Whether the phone should send its microphone; a full snapshot like CameraSettings.
struct MicSettings {
  bool enabled = false;
  bool operator==(const MicSettings&) const = default;
};

inline constexpr uint32_t kAudioSampleRate = 48000;

// Microphone samples: mono, signed 16-bit, kAudioSampleRate.
struct AudioFrame {
  uint64_t captureTimeUs;
  std::vector<int16_t> samples;
  bool operator==(const AudioFrame&) const = default;
};

using Message = std::variant<Hello, Configure, VideoFrame, RequestKeyframe, Ping, Pong, Status, CameraSettings,
                             CameraState, BackgroundImage, Pair, Telemetry, MicSettings, AudioFrame>;

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
// Any frame shape: 16:9 either way round uses the table above, others scale it by pixel count.
uint32_t DefaultBitrateKbps(uint16_t width, uint16_t height, uint8_t fps);

}  // namespace webicam::proto
