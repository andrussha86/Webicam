#include "webicam/protocol.h"

#include <bit>
#include <optional>

namespace webicam::proto {

namespace {

class Writer {
 public:
  void U8(uint8_t v) { out.push_back(v); }
  void U16(uint16_t v) { Le(v, 2); }
  void U32(uint32_t v) { Le(v, 4); }
  void U64(uint64_t v) { Le(v, 8); }
  void F32(float v) { U32(std::bit_cast<uint32_t>(v)); }
  void Bool(bool v) { U8(v ? 1 : 0); }
  void Str(const std::string& s) {
    U16(uint16_t(s.size()));
    out.insert(out.end(), s.begin(), s.end());
  }
  void Bytes(std::span<const uint8_t> b) { out.insert(out.end(), b.begin(), b.end()); }
  std::vector<uint8_t> out;

 private:
  void Le(uint64_t v, int n) {
    for (int i = 0; i < n; ++i) out.push_back(uint8_t(v >> (8 * i)));
  }
};

class Reader {
 public:
  explicit Reader(std::span<const uint8_t> data) : data_(data) {}
  std::optional<uint8_t> U8() { return Le<uint8_t>(1); }
  std::optional<uint16_t> U16() { return Le<uint16_t>(2); }
  std::optional<uint32_t> U32() { return Le<uint32_t>(4); }
  std::optional<uint64_t> U64() { return Le<uint64_t>(8); }
  std::optional<float> F32() {
    auto v = U32();
    if (!v) return std::nullopt;
    return std::bit_cast<float>(*v);
  }
  std::optional<bool> Bool() {
    auto v = U8();
    if (!v || *v > 1) return std::nullopt;
    return *v == 1;
  }
  std::optional<std::string> Str() {
    auto length = U16();
    if (!length || Remaining() < *length) return std::nullopt;
    std::string s(reinterpret_cast<const char*>(data_.data() + pos_), *length);
    pos_ += *length;
    return s;
  }
  std::vector<uint8_t> Rest() {
    std::vector<uint8_t> rest(data_.begin() + pos_, data_.end());
    pos_ = data_.size();
    return rest;
  }
  size_t Remaining() const { return data_.size() - pos_; }

 private:
  template <typename T>
  std::optional<T> Le(int n) {
    if (Remaining() < size_t(n)) return std::nullopt;
    uint64_t v = 0;
    for (int i = 0; i < n; ++i) v |= uint64_t(data_[pos_ + i]) << (8 * i);
    pos_ += n;
    return T(v);
  }
  std::span<const uint8_t> data_;
  size_t pos_ = 0;
};

std::optional<Message> DecodePayload(MsgType type, std::span<const uint8_t> payload) {
  Reader r(payload);
  switch (type) {
    case MsgType::Hello: {
      Hello hello;
      auto version = r.U16();
      if (!version) return std::nullopt;
      hello.version = *version;
      if (r.Remaining() > 0) {
        auto model = r.Str();
        auto count = r.U16();
        if (!model || !count) return std::nullopt;
        hello.model = *model;
        for (uint16_t i = 0; i < *count; ++i) {
          auto w = r.U16();
          auto h = r.U16();
          auto fps = r.U8();
          if (!w || !h || !fps) return std::nullopt;
          hello.formats.push_back({*w, *h, *fps});
        }
      }
      if (r.Remaining() != 0) return std::nullopt;
      return hello;
    }
    case MsgType::Configure: {
      auto w = r.U16();
      auto h = r.U16();
      auto fps = r.U8();
      auto bitrate = r.U32();
      if (!w || !h || !fps || !bitrate || r.Remaining() != 0) return std::nullopt;
      return Configure{*w, *h, *fps, *bitrate};
    }
    case MsgType::VideoFrame: {
      auto time = r.U64();
      auto flags = r.U8();
      if (!time || !flags) return std::nullopt;
      return VideoFrame{*time, (*flags & 1) != 0, r.Rest()};
    }
    case MsgType::RequestKeyframe:
      if (r.Remaining() != 0) return std::nullopt;
      return RequestKeyframe{};
    case MsgType::Ping: {
      auto pc = r.U64();
      if (!pc || r.Remaining() != 0) return std::nullopt;
      return Ping{*pc};
    }
    case MsgType::Pong: {
      auto pc = r.U64();
      auto phone = r.U64();
      if (!pc || !phone || r.Remaining() != 0) return std::nullopt;
      return Pong{*pc, *phone};
    }
    case MsgType::Status: {
      auto state = r.U8();
      auto thermal = r.U8();
      auto message = r.Str();
      if (!state || !thermal || !message || r.Remaining() != 0 || *state > 3) return std::nullopt;
      return Status{StreamState(*state), *thermal, *message};
    }
    case MsgType::CameraSettings: {
      auto lens = r.U8();
      auto zoom = r.F32();
      auto manualExposure = r.Bool();
      auto iso = r.F32();
      auto shutter = r.U32();
      auto bias = r.F32();
      auto manualWb = r.Bool();
      auto temperature = r.F32();
      auto tint = r.F32();
      auto manualFocus = r.Bool();
      auto lensPosition = r.F32();
      auto blur = r.U8();
      auto rotate = r.Bool();
      if (!lens || !zoom || !manualExposure || !iso || !shutter || !bias || !manualWb || !temperature || !tint ||
          !manualFocus || !lensPosition || !blur || !rotate || r.Remaining() != 0 || *lens >= kLensCount) {
        return std::nullopt;
      }
      return CameraSettings{Lens(*lens), *zoom, *manualExposure, *iso,         *shutter,      *bias, *manualWb,
                            *temperature, *tint, *manualFocus,    *lensPosition, *blur, *rotate};
    }
    case MsgType::CameraState: {
      auto lenses = r.U8();
      auto lens = r.U8();
      auto isoMin = r.F32();
      auto isoMax = r.F32();
      auto shutterMin = r.U32();
      auto shutterMax = r.U32();
      auto biasMin = r.F32();
      auto biasMax = r.F32();
      auto zoomMax = r.F32();
      auto focus = r.Bool();
      auto iso = r.F32();
      auto shutter = r.U32();
      auto temperature = r.F32();
      auto tint = r.F32();
      auto lensPosition = r.F32();
      if (!lenses || !lens || !isoMin || !isoMax || !shutterMin || !shutterMax || !biasMin || !biasMax || !zoomMax ||
          !focus || !iso || !shutter || !temperature || !tint || !lensPosition || r.Remaining() != 0 ||
          *lens >= kLensCount) {
        return std::nullopt;
      }
      return CameraState{*lenses,  Lens(*lens), *isoMin, *isoMax, *shutterMin, *shutterMax,  *biasMin, *biasMax,
                         *zoomMax, *focus,      *iso,    *shutter, *temperature, *tint, *lensPosition};
    }
  }
  return std::nullopt;
}

}  // namespace

std::vector<uint8_t> Encode(const Message& message) {
  Writer payload;
  MsgType type{};
  std::visit(
      [&](const auto& m) {
        using T = std::decay_t<decltype(m)>;
        if constexpr (std::is_same_v<T, Hello>) {
          type = MsgType::Hello;
          payload.U16(m.version);
          if (!m.model.empty() || !m.formats.empty()) {
            payload.Str(m.model);
            payload.U16(uint16_t(m.formats.size()));
            for (const auto& f : m.formats) {
              payload.U16(f.width);
              payload.U16(f.height);
              payload.U8(f.maxFps);
            }
          }
        } else if constexpr (std::is_same_v<T, Configure>) {
          type = MsgType::Configure;
          payload.U16(m.width);
          payload.U16(m.height);
          payload.U8(m.fps);
          payload.U32(m.bitrateKbps);
        } else if constexpr (std::is_same_v<T, VideoFrame>) {
          type = MsgType::VideoFrame;
          payload.U64(m.captureTimeUs);
          payload.U8(m.keyframe ? 1 : 0);
          payload.Bytes(m.annexB);
        } else if constexpr (std::is_same_v<T, RequestKeyframe>) {
          type = MsgType::RequestKeyframe;
        } else if constexpr (std::is_same_v<T, Ping>) {
          type = MsgType::Ping;
          payload.U64(m.pcTimeUs);
        } else if constexpr (std::is_same_v<T, Pong>) {
          type = MsgType::Pong;
          payload.U64(m.pcTimeUs);
          payload.U64(m.phoneTimeUs);
        } else if constexpr (std::is_same_v<T, Status>) {
          type = MsgType::Status;
          payload.U8(uint8_t(m.state));
          payload.U8(m.thermal);
          payload.Str(m.message);
        } else if constexpr (std::is_same_v<T, CameraSettings>) {
          type = MsgType::CameraSettings;
          payload.U8(uint8_t(m.lens));
          payload.F32(m.zoom);
          payload.Bool(m.manualExposure);
          payload.F32(m.iso);
          payload.U32(m.shutterUs);
          payload.F32(m.exposureBias);
          payload.Bool(m.manualWhiteBalance);
          payload.F32(m.temperatureK);
          payload.F32(m.tint);
          payload.Bool(m.manualFocus);
          payload.F32(m.lensPosition);
          payload.U8(m.blur);
          payload.Bool(m.rotate180);
        } else if constexpr (std::is_same_v<T, CameraState>) {
          type = MsgType::CameraState;
          payload.U8(m.lenses);
          payload.U8(uint8_t(m.lens));
          payload.F32(m.isoMin);
          payload.F32(m.isoMax);
          payload.U32(m.shutterMinUs);
          payload.U32(m.shutterMaxUs);
          payload.F32(m.biasMin);
          payload.F32(m.biasMax);
          payload.F32(m.zoomMax);
          payload.Bool(m.manualFocusSupported);
          payload.F32(m.iso);
          payload.U32(m.shutterUs);
          payload.F32(m.temperatureK);
          payload.F32(m.tint);
          payload.F32(m.lensPosition);
        }
      },
      message);

  Writer frame;
  frame.U8(uint8_t(type));
  frame.U32(uint32_t(payload.out.size()));
  frame.Bytes(payload.out);
  return std::move(frame.out);
}

void MessageParser::Feed(std::span<const uint8_t> bytes) {
  if (offset_ > 0 && offset_ == buffer_.size()) {
    buffer_.clear();
    offset_ = 0;
  }
  buffer_.insert(buffer_.end(), bytes.begin(), bytes.end());
}

MessageParser::Result MessageParser::Next(Message& out) {
  if (failed_) return Result::Error;
  if (Buffered() < 5) return Result::NeedMore;
  const uint8_t* head = buffer_.data() + offset_;
  const uint8_t type = head[0];
  const uint32_t length = uint32_t(head[1]) | uint32_t(head[2]) << 8 | uint32_t(head[3]) << 16 | uint32_t(head[4]) << 24;
  if (type < uint8_t(MsgType::Hello) || type > uint8_t(MsgType::CameraState) || length > kMaxPayloadBytes) {
    failed_ = true;
    return Result::Error;
  }
  if (Buffered() < 5 + size_t(length)) return Result::NeedMore;
  auto decoded = DecodePayload(MsgType(type), std::span(head + 5, length));
  if (!decoded) {
    failed_ = true;
    return Result::Error;
  }
  offset_ += 5 + size_t(length);
  if (offset_ > (1u << 20) && offset_ * 2 > buffer_.size()) {
    buffer_.erase(buffer_.begin(), buffer_.begin() + offset_);
    offset_ = 0;
  }
  out = std::move(*decoded);
  return Result::Message;
}

uint32_t DefaultBitrateKbps(uint16_t height, uint8_t fps) {
  if (height == 720) return fps > 30 ? 12000 : 8000;
  if (height == 1080) return fps > 30 ? 25000 : 16000;
  if (height == 2160) return 45000;
  return 16000;
}

}  // namespace webicam::proto
