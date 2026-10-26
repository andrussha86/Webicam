#include <gtest/gtest.h>

#include <windows.h>

#include <string>

#include "webicam/shared_section.h"
#include "webicam/shm_layout.h"

using namespace webicam;

TEST(SharedSection, CreateThenOpenSharesMemory) {
  const std::wstring name = L"Local\\WebicamTest-" + std::to_wstring(GetCurrentProcessId());
  auto created = SharedSection::Create(name.c_str(), 4096, shm::kSectionSddl);
  ASSERT_TRUE(created.has_value());
  EXPECT_FALSE(created->existed());
  static_cast<uint8_t*>(created->data())[100] = 0x5A;

  auto opened = SharedSection::Open(name.c_str(), 4096);
  ASSERT_TRUE(opened.has_value());
  EXPECT_EQ(static_cast<uint8_t*>(opened->data())[100], 0x5A);
}

TEST(SharedSection, OpenMissingReturnsNullopt) {
  EXPECT_FALSE(SharedSection::Open(L"Local\\WebicamTest-DoesNotExist", 4096).has_value());
}
