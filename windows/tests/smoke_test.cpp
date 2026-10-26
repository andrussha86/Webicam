#include <gtest/gtest.h>

#include "webicam/version.h"

TEST(Smoke, CoreLinks) { EXPECT_EQ(webicam::kCoreVersion, 1); }
