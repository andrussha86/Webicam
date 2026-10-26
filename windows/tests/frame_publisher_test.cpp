#include <gtest/gtest.h>

#include <windows.h>

#include <string>
#include <vector>

#include "webicam/frame_publisher.h"

using namespace webicam;

TEST(FramePublisher, NoSectionReturnsFalse) {
  FramePublisher publisher(L"Local\\WebicamTest-NoSuchSection", L"Local\\WebicamTest-NoSuchEvent");
  EXPECT_FALSE(publisher.TryAttach());
  EXPECT_FALSE(publisher.Attached());
  EXPECT_FALSE(publisher.Requested().has_value());
  std::vector<uint8_t> frame(16 * 16 * 3 / 2);
  EXPECT_FALSE(publisher.Publish(frame, 16, 16, 0));
  publisher.Heartbeat(true);  // no-op, must not crash
}

TEST(FramePublisher, PublishesIntoSectionCreatedByCamera) {
  const std::wstring name = L"Local\\WebicamTest-Pub-" + std::to_wstring(GetCurrentProcessId());
  auto section = SharedSection::Create(name.c_str(), FrameRing::RequiredBytes(), shm::kSectionSddl);
  ASSERT_TRUE(section.has_value());
  auto camera = FrameRing::Initialize(section->data());
  camera.SetRequestedFormat(32, 16, 30);

  FramePublisher publisher(name.c_str(), L"Local\\WebicamTest-NoSuchEvent");
  ASSERT_TRUE(publisher.TryAttach());
  ASSERT_TRUE(publisher.Requested().has_value());
  EXPECT_EQ(publisher.Requested()->width, 32u);
  std::vector<uint8_t> frame(32 * 16 * 3 / 2, 99);
  ASSERT_TRUE(publisher.Publish(frame, 32, 16, 7));
  publisher.Heartbeat(true);

  FrameInfo info{};
  std::vector<uint8_t> out;
  ASSERT_TRUE(camera.ReadLatest(0, info, out).has_value());
  EXPECT_EQ(info.timestamp100ns, 7);
  EXPECT_EQ(out[0], 99);
  EXPECT_TRUE(camera.Connected());
}
