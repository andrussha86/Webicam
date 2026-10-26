#include <gtest/gtest.h>

#include "test_encoder.h"
#include "webicam/frame_scaler.h"

using namespace webicam;

namespace {

// Encodes and decodes the first frame of the test pattern at the given size.
std::optional<DecodedFrame> FirstFrame(const D3DContext& d3d, H264Decoder& decoder, uint32_t width, uint32_t height) {
  auto encoder = TestPatternEncoder::Create(width, height, 30, 8000);
  if (!encoder) return std::nullopt;
  for (int n = 0; n < 5; ++n) {
    uint64_t index = 0;
    bool key = false;
    auto bytes = encoder->EncodeNext(n == 0, index, key);
    DecodedFrame frame;
    if (!bytes.empty() && decoder.Decode(bytes, 0, frame) == H264Decoder::Result::Frame) return frame;
  }
  return std::nullopt;
}

}  // namespace

TEST(FrameScaler, ScalesTo720pNv12) {
  auto d3d = D3DContext::Create();
  ASSERT_TRUE(d3d.has_value());
  auto decoder = H264Decoder::Create(*d3d);
  if (!decoder) GTEST_SKIP() << "no D3D11 H.264 decoder on this machine";
  auto frame = FirstFrame(*d3d, *decoder, 1920, 1080);
  ASSERT_TRUE(frame.has_value());
  auto scaler = FrameScaler::Create(*d3d);
  ASSERT_TRUE(scaler);
  std::vector<uint8_t> out;
  ASSERT_TRUE(scaler->ToNv12Cpu(*frame, 1280, 720, out));
  EXPECT_EQ(out.size(), 1280u * 720 * 3 / 2);
  EXPECT_NEAR(out[size_t(360) * 1280 + 640], TestPatternEncoder::LumaFor(0), 8);
  EXPECT_NEAR(out[size_t(1280) * 720 + 180 * 1280 + 640], 128, 8);
}

TEST(FrameScaler, SquareCropKeepsCenter) {
  auto d3d = D3DContext::Create();
  ASSERT_TRUE(d3d.has_value());
  auto decoder = H264Decoder::Create(*d3d);
  if (!decoder) GTEST_SKIP() << "no D3D11 H.264 decoder on this machine";
  auto frame = FirstFrame(*d3d, *decoder, 1920, 1080);
  ASSERT_TRUE(frame.has_value());
  auto scaler = FrameScaler::Create(*d3d);
  std::vector<uint8_t> out;
  ASSERT_TRUE(scaler->ToNv12Cpu(*frame, 1080, 1080, out));
  EXPECT_EQ(out.size(), 1080u * 1080 * 3 / 2);
  EXPECT_NEAR(out[size_t(540) * 1080 + 540], TestPatternEncoder::LumaFor(0), 8);
}

TEST(FrameScaler, PreviewFitsInside) {
  auto d3d = D3DContext::Create();
  ASSERT_TRUE(d3d.has_value());
  auto decoder = H264Decoder::Create(*d3d);
  if (!decoder) GTEST_SKIP() << "no D3D11 H.264 decoder on this machine";
  auto frame = FirstFrame(*d3d, *decoder, 1280, 720);
  ASSERT_TRUE(frame.has_value());
  auto scaler = FrameScaler::Create(*d3d);
  auto* srv = scaler->ToPreview(*frame, 640, 640);
  ASSERT_NE(srv, nullptr);
  Microsoft::WRL::ComPtr<ID3D11Resource> resource;
  srv->GetResource(&resource);
  Microsoft::WRL::ComPtr<ID3D11Texture2D> texture;
  resource.As(&texture);
  D3D11_TEXTURE2D_DESC desc{};
  texture->GetDesc(&desc);
  EXPECT_EQ(desc.Width, 640u);
  EXPECT_EQ(desc.Height, 360u);
}
