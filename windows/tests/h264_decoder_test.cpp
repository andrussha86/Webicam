#include <gtest/gtest.h>

#include <cstdlib>

#include "test_encoder.h"
#include "webicam/h264_decoder.h"

using namespace webicam;
using Microsoft::WRL::ComPtr;

namespace {

uint8_t CenterLuma(const D3DContext& d3d, const DecodedFrame& frame) {
  D3D11_TEXTURE2D_DESC desc{};
  frame.texture->GetDesc(&desc);
  desc.ArraySize = 1;
  desc.MipLevels = 1;
  desc.Usage = D3D11_USAGE_STAGING;
  desc.BindFlags = 0;
  desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
  desc.MiscFlags = 0;
  ComPtr<ID3D11Texture2D> staging;
  if (FAILED(d3d.device->CreateTexture2D(&desc, nullptr, &staging))) return 0;
  d3d.context->CopySubresourceRegion(staging.Get(), 0, 0, 0, 0, frame.texture.Get(), frame.subresource, nullptr);
  D3D11_MAPPED_SUBRESOURCE mapped{};
  if (FAILED(d3d.context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped))) return 0;
  const auto* rows = static_cast<const uint8_t*>(mapped.pData);
  const uint8_t value = rows[size_t(frame.height / 2) * mapped.RowPitch + frame.width / 2];
  d3d.context->Unmap(staging.Get(), 0);
  return value;
}

struct Fixture {
  Fixture(uint32_t width, uint32_t height, uint32_t fps)
      : d3d(*D3DContext::Create()),
        encoder(TestPatternEncoder::Create(width, height, fps, 8000)),
        decoder(H264Decoder::Create(d3d)) {}
  D3DContext d3d;
  std::unique_ptr<TestPatternEncoder> encoder;
  std::unique_ptr<H264Decoder> decoder;
};

constexpr int64_t kPeriod30 = 10'000'000 / 30;

}  // namespace

TEST(H264Decoder, DecodesTestPattern) {
  Fixture f(1280, 720, 30);
  ASSERT_TRUE(f.encoder && f.decoder);
  int frames = 0;
  for (int n = 0; n < 60; ++n) {
    uint64_t index = 0;
    bool key = false;
    auto bytes = f.encoder->EncodeNext(n == 0, index, key);
    if (bytes.empty()) continue;
    DecodedFrame frame;
    if (f.decoder->Decode(bytes, int64_t(index) * kPeriod30, frame) != H264Decoder::Result::Frame) continue;
    ++frames;
    EXPECT_EQ(frame.width, 1280u);
    EXPECT_EQ(frame.height, 720u);
    const uint64_t decodedIndex = uint64_t((frame.timestamp100ns + kPeriod30 / 2) / kPeriod30);
    EXPECT_NEAR(CenterLuma(f.d3d, frame), TestPatternEncoder::LumaFor(decodedIndex), 8) << "frame " << decodedIndex;
  }
  EXPECT_GE(frames, 58);
}

TEST(H264Decoder, ReportsDisplaySizeFor1080p) {
  Fixture f(1920, 1080, 30);
  ASSERT_TRUE(f.encoder && f.decoder);
  for (int n = 0; n < 10; ++n) {
    uint64_t index = 0;
    bool key = false;
    auto bytes = f.encoder->EncodeNext(n == 0, index, key);
    DecodedFrame frame;
    if (!bytes.empty() && f.decoder->Decode(bytes, int64_t(index), frame) == H264Decoder::Result::Frame) {
      EXPECT_EQ(frame.height, 1080u);
      return;
    }
  }
  FAIL() << "no frame decoded";
}

TEST(H264Decoder, GarbageReturnsErrorThenRecoversOnKeyframe) {
  Fixture f(1280, 720, 30);
  ASSERT_TRUE(f.encoder && f.decoder);
  std::vector<uint8_t> garbage(64, 0xAB);
  DecodedFrame frame;
  EXPECT_EQ(f.decoder->Decode(garbage, 0, frame), H264Decoder::Result::Error);
  f.decoder->Reset();
  for (int n = 0; n < 5; ++n) {
    uint64_t index = 0;
    bool key = false;
    auto bytes = f.encoder->EncodeNext(n == 0, index, key);
    if (!bytes.empty() && f.decoder->Decode(bytes, int64_t(index), frame) == H264Decoder::Result::Frame) return;
  }
  FAIL() << "decoder did not recover";
}

TEST(H264Decoder, LowLatencyNoFrameDelay) {
  Fixture f(1280, 720, 30);
  ASSERT_TRUE(f.encoder && f.decoder);
  uint64_t index = 0;
  bool key = false;
  std::vector<uint8_t> bytes;
  for (int n = 0; n < 5 && bytes.empty(); ++n) bytes = f.encoder->EncodeNext(n == 0, index, key);
  ASSERT_FALSE(bytes.empty());
  EXPECT_TRUE(key);
  DecodedFrame frame;
  EXPECT_EQ(f.decoder->Decode(bytes, 0, frame), H264Decoder::Result::Frame);
}
