#include <gtest/gtest.h>
#include <objbase.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <random>

#include "webicam/background_library.h"
#include "webicam/image_codec.h"

using namespace webicam;
namespace fs = std::filesystem;

namespace {

class ImageTest : public ::testing::Test {
 protected:
  void SetUp() override {
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    dir_ = fs::temp_directory_path() / ("webicam-test-" + std::to_string(std::random_device{}()));
    fs::create_directories(dir_);
  }
  void TearDown() override {
    std::error_code ec;
    fs::remove_all(dir_, ec);
    CoUninitialize();
  }
  fs::path Write(const std::string& name, const std::vector<uint8_t>& bytes) {
    const auto path = dir_ / name;
    std::ofstream(path, std::ios::binary).write(reinterpret_cast<const char*>(bytes.data()), std::streamsize(bytes.size()));
    return path;
  }
  size_t FileCount(const fs::path& dir) {
    return size_t(std::distance(fs::directory_iterator(dir), fs::directory_iterator()));
  }
  fs::path dir_;
};

Preset UsingImage(std::string name, std::string image) {
  Preset p;
  p.name = std::move(name);
  p.backgroundImage = std::move(image);
  return p;
}

}  // namespace

TEST_F(ImageTest, JpegRoundTrip) {
  const auto jpeg = EncodeJpeg(RenderBuiltinBackground(1, 320, 180));
  ASSERT_GE(jpeg.size(), 2u);
  EXPECT_EQ(jpeg[0], 0xFF);
  EXPECT_EQ(jpeg[1], 0xD8);
  const auto decoded = DecodeImage(jpeg);
  ASSERT_TRUE(decoded);
  EXPECT_EQ(decoded->width, 320u);
  EXPECT_EQ(decoded->height, 180u);
}

TEST_F(ImageTest, CoverResizeAspect) {
  // 400×100: left half dark, centre column bright, right half dark. Cover to 16:9 keeps the centre.
  Bgra wide{400, 100, std::vector<uint8_t>(400 * 100 * 4, 20)};
  for (uint32_t y = 0; y < 100; ++y) {
    for (uint32_t x = 190; x < 210; ++x) {
      for (int c = 0; c < 3; ++c) wide.pixels[(y * 400 + x) * 4 + c] = 230;
    }
  }
  const auto out = CoverResize(wide, 160, 90);
  ASSERT_EQ(out.width, 160u);
  ASSERT_EQ(out.height, 90u);
  const size_t centre = (45 * 160 + 80) * 4;
  EXPECT_NEAR(out.pixels[centre], 230, 8);
  EXPECT_NEAR(out.pixels[(45 * 160 + 2) * 4], 20, 8);
}

TEST_F(ImageTest, Jpeg4kFitsFrameLimit) {
  Bgra noise{3840, 2160, std::vector<uint8_t>(size_t(3840) * 2160 * 4)};
  std::mt19937 rng(7);
  for (auto& b : noise.pixels) b = uint8_t(rng());
  const auto jpeg = EncodeJpeg(noise);
  EXPECT_FALSE(jpeg.empty());
  EXPECT_LT(jpeg.size(), 16u * 1024 * 1024);
}

TEST_F(ImageTest, LoadRejectsGarbage) {
  const std::string text = "not an image";
  EXPECT_FALSE(LoadImageFile(Write("bad.png", {text.begin(), text.end()})));
}

TEST_F(ImageTest, LibraryAddDedupes) {
  const auto source = Write("Photo.JPG", EncodeJpeg(RenderBuiltinBackground(2, 64, 36)));
  BackgroundLibrary library(dir_ / "backgrounds");
  const auto first = library.Add(source);
  const auto second = library.Add(source);
  ASSERT_TRUE(first) << first.error();
  ASSERT_TRUE(second);
  EXPECT_EQ(*first, *second);
  EXPECT_EQ(first->size(), 16u + 4u);
  EXPECT_TRUE(first->ends_with(".jpg"));
  EXPECT_EQ(library.List(), std::vector<std::string>{*first});
  EXPECT_EQ(FileCount(dir_ / "backgrounds"), 1u);
}

TEST_F(ImageTest, LibraryAddShrinksLargePictures) {
  BackgroundLibrary library(dir_ / "backgrounds");
  const auto name = library.Add(Write("wide.jpg", EncodeJpeg(RenderBuiltinBackground(1, 5000, 100))));
  ASSERT_TRUE(name) << name.error();
  const auto stored = LoadImageFile(dir_ / "backgrounds" / *name);
  ASSERT_TRUE(stored);
  EXPECT_EQ(stored->width, 3840u);
  EXPECT_EQ(stored->height, 77u);
}

TEST_F(ImageTest, LibraryAddRejectsGarbage) {
  const std::string text = "not an image";
  BackgroundLibrary library(dir_ / "backgrounds");
  const auto result = library.Add(Write("bad.png", {text.begin(), text.end()}));
  ASSERT_FALSE(result);
  EXPECT_TRUE(result.error().starts_with("Не удалось открыть картинку"));
}

TEST_F(ImageTest, LibraryRemoveRefusesReferenced) {
  BackgroundLibrary library(dir_ / "backgrounds");
  const auto name = library.Add(Write("a.jpg", EncodeJpeg(RenderBuiltinBackground(5, 64, 36))));
  ASSERT_TRUE(name);
  const auto refused = library.Remove(*name, {UsingImage("Мой стол", *name)});
  ASSERT_FALSE(refused);
  EXPECT_EQ(refused.error(), "Используется в пресете „Мой стол“");
  EXPECT_EQ(library.List().size(), 1u);
  EXPECT_TRUE(library.Remove(*name, {UsingImage("Другой", "builtin:1")}));
  EXPECT_TRUE(library.List().empty());
}

TEST_F(ImageTest, LibraryResolveBuiltinAndMissing) {
  BackgroundLibrary library(dir_ / "backgrounds");
  const auto builtin = library.Resolve("builtin:3", 64, 36);
  ASSERT_TRUE(builtin);
  EXPECT_EQ(builtin->width, 64u);
  EXPECT_EQ(builtin->height, 36u);
  EXPECT_FALSE(library.Resolve("nope.png", 64, 36));
  EXPECT_FALSE(library.Resolve("builtin:9", 64, 36));
}
