#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <iterator>

#include "webicam/protocol.h"
#include "webicam/testvectors.h"

using namespace webicam::proto;

namespace {

std::vector<Message> Parse(std::span<const uint8_t> bytes, size_t chunk) {
  MessageParser parser;
  std::vector<Message> out;
  for (size_t i = 0; i < bytes.size(); i += chunk) {
    parser.Feed(bytes.subspan(i, std::min(chunk, bytes.size() - i)));
    Message m;
    MessageParser::Result r;
    while ((r = parser.Next(m)) == MessageParser::Result::Message) out.push_back(m);
    EXPECT_NE(r, MessageParser::Result::Error);
  }
  return out;
}

std::vector<uint8_t> Header(uint8_t type, uint32_t length) {
  return {type, uint8_t(length), uint8_t(length >> 8), uint8_t(length >> 16), uint8_t(length >> 24)};
}

}  // namespace

TEST(Protocol, RoundTripEveryType) {
  for (const auto& vector : TestVectors()) {
    auto parsed = Parse(Encode(vector.message), 1 << 20);
    ASSERT_EQ(parsed.size(), 1u) << vector.file;
    EXPECT_EQ(parsed[0], vector.message) << vector.file;
  }
}

TEST(Protocol, ParserByteByByteFeedYieldsSameMessages) {
  std::vector<uint8_t> stream;
  for (const auto& vector : TestVectors()) {
    auto bytes = Encode(vector.message);
    stream.insert(stream.end(), bytes.begin(), bytes.end());
  }
  auto parsed = Parse(stream, 1);
  ASSERT_EQ(parsed.size(), TestVectors().size());
  for (size_t i = 0; i < parsed.size(); ++i) EXPECT_EQ(parsed[i], TestVectors()[i].message);
}

TEST(Protocol, ParserTwoMessagesInOneFeed) {
  auto a = Encode(Ping{1});
  auto b = Encode(Ping{2});
  a.insert(a.end(), b.begin(), b.end());
  auto parsed = Parse(a, a.size());
  ASSERT_EQ(parsed.size(), 2u);
  EXPECT_EQ(std::get<Ping>(parsed[1]).pcTimeUs, 2u);
}

TEST(Protocol, ParserOversizeLengthIsError) {
  MessageParser parser;
  parser.Feed(Header(uint8_t(MsgType::VideoFrame), kMaxPayloadBytes + 1));
  Message m;
  EXPECT_EQ(parser.Next(m), MessageParser::Result::Error);
  EXPECT_EQ(parser.Next(m), MessageParser::Result::Error);
}

TEST(Protocol, ParserUnknownTypeIsError) {
  MessageParser parser;
  parser.Feed(Header(0x0B, 0));
  Message m;
  EXPECT_EQ(parser.Next(m), MessageParser::Result::Error);
}

TEST(Protocol, ParserTruncatedStringIsError) {
  // Status with a string length of 10 but only 2 bytes of string.
  auto bytes = Header(uint8_t(MsgType::Status), 6);
  bytes.insert(bytes.end(), {0x00, 0x00, 0x0A, 0x00, 'a', 'b'});
  MessageParser parser;
  parser.Feed(bytes);
  Message m;
  EXPECT_EQ(parser.Next(m), MessageParser::Result::Error);
}

TEST(Protocol, ParserPartialMessageNeedsMore) {
  auto bytes = Encode(VideoFrame{1, false, std::vector<uint8_t>(100, 7)});
  MessageParser parser;
  parser.Feed(std::span(bytes).first(50));
  Message m;
  EXPECT_EQ(parser.Next(m), MessageParser::Result::NeedMore);
}

TEST(Protocol, DefaultBitrateTable) {
  EXPECT_EQ(DefaultBitrateKbps(1080, 60), 25000u);
  EXPECT_EQ(DefaultBitrateKbps(1080, 30), 16000u);
  EXPECT_EQ(DefaultBitrateKbps(2160, 30), 45000u);
  EXPECT_EQ(DefaultBitrateKbps(720, 30), 8000u);
  EXPECT_EQ(DefaultBitrateKbps(720, 60), 12000u);
}

TEST(Protocol, VectorsMatchFiles) {
  const std::filesystem::path dir = WEBICAM_TESTVECTORS_DIR;
  for (const auto& vector : TestVectors()) {
    std::ifstream file(dir / vector.file, std::ios::binary);
    ASSERT_TRUE(file) << vector.file;
    std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    EXPECT_EQ(Encode(vector.message), bytes) << vector.file;
    auto parsed = Parse(bytes, bytes.size());
    ASSERT_EQ(parsed.size(), 1u) << vector.file;
    EXPECT_EQ(parsed[0], vector.message) << vector.file;
  }
}

namespace {

// camera_settings.bin with one payload byte replaced.
MessageParser::Result ParseSettingsWith(size_t payloadOffset, std::span<const uint8_t> patch) {
  auto bytes = Encode(TestVectors()[8].message);
  std::copy(patch.begin(), patch.end(), bytes.begin() + 5 + payloadOffset);
  MessageParser parser;
  parser.Feed(bytes);
  Message m;
  return parser.Next(m);
}

}  // namespace

TEST(Protocol, BackgroundImageBytes) {
  EXPECT_EQ(Encode(BackgroundImage{{0xFF, 0xD8, 0xFF}}), (std::vector<uint8_t>{0x0A, 3, 0, 0, 0, 0xFF, 0xD8, 0xFF}));
  EXPECT_EQ(Encode(BackgroundImage{}), (std::vector<uint8_t>{0x0A, 0, 0, 0, 0}));
}

TEST(Protocol, RejectsBackgroundModeFour) {
  // v2 payload is 34 bytes, then face_focus (34), background_mode (35), background_color (36..39).
  const uint8_t mode = 4;
  EXPECT_EQ(ParseSettingsWith(35, std::span(&mode, 1)), MessageParser::Result::Error);
}

TEST(Protocol, RejectsColorHighByte) {
  const uint8_t color[4] = {0x00, 0x00, 0x00, 0x01};
  EXPECT_EQ(ParseSettingsWith(36, color), MessageParser::Result::Error);
}
