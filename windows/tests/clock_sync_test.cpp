#include <gtest/gtest.h>

#include "webicam/clock_sync.h"

using webicam::ClockSync;

TEST(ClockSync, EmptyHasNoOffset) { EXPECT_FALSE(ClockSync{}.OffsetUs().has_value()); }

TEST(ClockSync, SingleSample) {
  ClockSync sync;
  sync.AddSample(1000, 500, 1200);
  EXPECT_EQ(sync.OffsetUs(), 600);
}

TEST(ClockSync, PicksMinimumRtt) {
  ClockSync sync;
  sync.AddSample(0, 0, 200);      // rtt 200, offset 100
  sync.AddSample(1000, 0, 1050);  // rtt 50, offset 1025
  sync.AddSample(2000, 0, 2900);  // rtt 900, offset 2450
  EXPECT_EQ(sync.OffsetUs(), 1025);
}

TEST(ClockSync, WindowEvictsOldest) {
  ClockSync sync;
  sync.AddSample(0, 0, 10);  // best, offset 5
  for (int i = 1; i <= 9; ++i) sync.AddSample(1000 * i, 0, 1000 * i + 100);
  EXPECT_EQ(sync.OffsetUs(), 5);
  sync.AddSample(20000, 0, 20100);  // 11th sample evicts the first
  EXPECT_EQ(sync.OffsetUs(), 1050);
}
