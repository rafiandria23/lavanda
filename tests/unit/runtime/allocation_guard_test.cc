#include "test_support/allocation_guard.h"

#include <gtest/gtest.h>

#include <vector>

namespace lavanda {
namespace {

TEST(AllocationGuardTest, ReportsZeroWhenNoAllocationOccurs) {
  test_support::AllocationGuard guard;
  int x = 5;

  x += 1;
  (void)x;

  EXPECT_EQ(guard.allocation_count(), 0u);
}

TEST(AllocationGuardTest, DetectsHeapAllocation) {
  test_support::AllocationGuard guard;
  std::vector<int> values;

  values.push_back(1);

  EXPECT_GT(guard.allocation_count(), 0u);
}

TEST(AllocationGuardTest, OnlyCountsAllocationsWithinItsOwnScope) {
  std::vector<int> before;
  before.reserve(64);

  test_support::AllocationGuard guard;

  EXPECT_EQ(guard.allocation_count(), 0u);
}

TEST(AllocationGuardTest, StopsCountingAfterGoingOutOfScope) {
  {
    test_support::AllocationGuard guard;
    std::vector<int> tracked;

    tracked.reserve(8);

    EXPECT_GT(guard.allocation_count(), 0u);
  }

  std::vector<int> untracked;
  untracked.reserve(8);

  test_support::AllocationGuard second_guard;

  EXPECT_EQ(second_guard.allocation_count(), 0u);
}

}  // namespace
}  // namespace lavanda
