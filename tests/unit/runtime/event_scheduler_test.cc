#include "lavanda/runtime/event_scheduler.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <random>
#include <vector>

#include "lavanda/core/time.h"
#include "lavanda/runtime/command.h"

namespace lavanda {
namespace {

constexpr std::size_t kCapacity = 8;
constexpr std::size_t kLargeCapacity = 256;

FramePosition At(std::uint64_t frame) { return FramePosition{frame}; }

Command Tagged(float tag) {
  Command command;
  command.type = CommandType::kSetVoiceGain;
  command.value = tag;

  return command;
}

std::vector<float> DrainDue(EventScheduler& scheduler, FramePosition now) {
  std::vector<float> tags;
  ScheduledEvent event;

  while (scheduler.PopDue(now, event)) {
    tags.push_back(event.command.value);
  }

  return tags;
}

using Tags = std::vector<float>;

TEST(EventSchedulerTest, StartsEmpty) {
  EventScheduler scheduler(kCapacity);
  FramePosition next = At(0);
  ScheduledEvent event;

  EXPECT_TRUE(scheduler.empty());
  EXPECT_EQ(scheduler.size(), 0u);
  EXPECT_EQ(scheduler.capacity(), kCapacity);
  EXPECT_FALSE(scheduler.PeekNextTarget(next));
  EXPECT_FALSE(scheduler.PopDue(At(1000), event));
}

TEST(EventSchedulerTest, FutureEventIsNotReleasedBeforeItsTarget) {
  EventScheduler scheduler(kCapacity);
  ScheduledEvent event;

  ASSERT_TRUE(scheduler.Push(At(100), Tagged(7.0f)));

  EXPECT_FALSE(scheduler.PopDue(At(99), event));
  EXPECT_EQ(scheduler.size(), 1u);

  ASSERT_TRUE(scheduler.PopDue(At(100), event));
  EXPECT_FLOAT_EQ(event.command.value, 7.0f);
  EXPECT_EQ(ToInteger(event.target_frame), 100u);
  EXPECT_TRUE(scheduler.empty());
}

TEST(EventSchedulerTest, ReleasesInChronologicalOrder) {
  EventScheduler scheduler(kCapacity);

  ASSERT_TRUE(scheduler.Push(At(10), Tagged(1.0f)));
  ASSERT_TRUE(scheduler.Push(At(20), Tagged(2.0f)));
  ASSERT_TRUE(scheduler.Push(At(30), Tagged(3.0f)));

  EXPECT_EQ(DrainDue(scheduler, At(1000)), (Tags{1.0f, 2.0f, 3.0f}));
}

TEST(EventSchedulerTest, OutOfOrderSubmissionDoesNotBlockEarlierEvents) {
  EventScheduler scheduler(kCapacity);

  ASSERT_TRUE(scheduler.Push(At(500), Tagged(5.0f)));
  ASSERT_TRUE(scheduler.Push(At(100), Tagged(1.0f)));
  ASSERT_TRUE(scheduler.Push(At(300), Tagged(3.0f)));

  EXPECT_EQ(DrainDue(scheduler, At(100)), (Tags{1.0f}));
  EXPECT_EQ(DrainDue(scheduler, At(400)), (Tags{3.0f}));
  EXPECT_EQ(DrainDue(scheduler, At(500)), (Tags{5.0f}));
}

TEST(EventSchedulerTest, SameTargetPreservesSubmissionOrder) {
  EventScheduler scheduler(kCapacity);

  for (int i = 0; i < 5; ++i) {
    ASSERT_TRUE(scheduler.Push(At(50), Tagged(static_cast<float>(i))));
  }

  EXPECT_EQ(DrainDue(scheduler, At(50)), (Tags{0.0f, 1.0f, 2.0f, 3.0f, 4.0f}));
}

TEST(EventSchedulerTest, SameTargetOrderHoldsWhenInterleavedWithOtherTargets) {
  EventScheduler scheduler(kCapacity);

  ASSERT_TRUE(scheduler.Push(At(50), Tagged(0.0f)));
  ASSERT_TRUE(scheduler.Push(At(10), Tagged(1.0f)));
  ASSERT_TRUE(scheduler.Push(At(50), Tagged(2.0f)));
  ASSERT_TRUE(scheduler.Push(At(10), Tagged(3.0f)));
  ASSERT_TRUE(scheduler.Push(At(50), Tagged(4.0f)));

  EXPECT_EQ(DrainDue(scheduler, At(1000)),
            (Tags{1.0f, 3.0f, 0.0f, 2.0f, 4.0f}));
}

TEST(EventSchedulerTest, PopDueReleasesOnlyEventsAtOrBeforeTheFrame) {
  EventScheduler scheduler(kCapacity);
  FramePosition next = At(0);

  ASSERT_TRUE(scheduler.Push(At(10), Tagged(1.0f)));
  ASSERT_TRUE(scheduler.Push(At(20), Tagged(2.0f)));
  ASSERT_TRUE(scheduler.Push(At(30), Tagged(3.0f)));

  EXPECT_EQ(DrainDue(scheduler, At(20)), (Tags{1.0f, 2.0f}));
  EXPECT_EQ(scheduler.size(), 1u);
  ASSERT_TRUE(scheduler.PeekNextTarget(next));
  EXPECT_EQ(ToInteger(next), 30u);
}

TEST(EventSchedulerTest, PeekNextTargetTracksTheMinimum) {
  EventScheduler scheduler(kCapacity);
  FramePosition next = At(0);
  ScheduledEvent event;

  ASSERT_TRUE(scheduler.Push(At(40), Tagged(0.0f)));
  ASSERT_TRUE(scheduler.PeekNextTarget(next));
  EXPECT_EQ(ToInteger(next), 40u);

  ASSERT_TRUE(scheduler.Push(At(15), Tagged(1.0f)));
  ASSERT_TRUE(scheduler.PeekNextTarget(next));
  EXPECT_EQ(ToInteger(next), 15u);

  ASSERT_TRUE(scheduler.PopDue(At(15), event));
  ASSERT_TRUE(scheduler.PeekNextTarget(next));
  EXPECT_EQ(ToInteger(next), 40u);
}

TEST(EventSchedulerTest, LateEventsStillPopAndReportTheirOriginalTarget) {
  EventScheduler scheduler(kCapacity);
  ScheduledEvent event;

  ASSERT_TRUE(scheduler.Push(At(5), Tagged(9.0f)));
  ASSERT_TRUE(scheduler.PopDue(At(100), event));

  EXPECT_EQ(ToInteger(event.target_frame), 5u);
  EXPECT_FLOAT_EQ(event.command.value, 9.0f);
}

TEST(EventSchedulerTest, PushFailsWhenFullAndLeavesContentsIntact) {
  EventScheduler scheduler(3);

  ASSERT_TRUE(scheduler.Push(At(30), Tagged(3.0f)));
  ASSERT_TRUE(scheduler.Push(At(10), Tagged(1.0f)));
  ASSERT_TRUE(scheduler.Push(At(20), Tagged(2.0f)));

  EXPECT_FALSE(scheduler.Push(At(5), Tagged(99.0f)));
  EXPECT_EQ(scheduler.size(), 3u);
  EXPECT_EQ(DrainDue(scheduler, At(1000)), (Tags{1.0f, 2.0f, 3.0f}));
}

TEST(EventSchedulerTest, CapacityIsReusableAfterPops) {
  EventScheduler scheduler(4);

  for (int cycle = 0; cycle < 50; ++cycle) {
    for (int i = 0; i < 4; ++i) {
      ASSERT_TRUE(scheduler.Push(At(static_cast<std::uint64_t>(cycle) * 10 + 3 -
                                    static_cast<std::uint64_t>(i)),
                                 Tagged(static_cast<float>(i))));
    }

    EXPECT_FALSE(scheduler.Push(At(0), Tagged(-1.0f)));
    EXPECT_EQ(DrainDue(scheduler, At(1000000)).size(), 4u);
    EXPECT_TRUE(scheduler.empty());
  }
}

TEST(EventSchedulerTest, OrdersCorrectlyAtMaximumSupportedCount) {
  EventScheduler scheduler(kLargeCapacity);

  for (std::uint64_t i = 0; i < kLargeCapacity; ++i) {
    ASSERT_TRUE(scheduler.Push(At(kLargeCapacity - i),
                               Tagged(static_cast<float>(kLargeCapacity - i))));
  }

  EXPECT_FALSE(scheduler.Push(At(1), Tagged(0.0f)));

  ScheduledEvent event;
  std::uint64_t previous = 0;
  std::size_t popped = 0;

  while (scheduler.PopDue(At(kLargeCapacity), event)) {
    EXPECT_GT(ToInteger(event.target_frame), previous);

    previous = ToInteger(event.target_frame);
    ++popped;
  }

  EXPECT_EQ(popped, kLargeCapacity);
}

TEST(EventSchedulerTest, ClearDiscardsPendingEventsAndKeepsCapacity) {
  EventScheduler scheduler(kCapacity);

  ASSERT_TRUE(scheduler.Push(At(10), Tagged(1.0f)));
  ASSERT_TRUE(scheduler.Push(At(20), Tagged(2.0f)));

  scheduler.Clear();

  EXPECT_TRUE(scheduler.empty());
  EXPECT_EQ(scheduler.capacity(), kCapacity);
  EXPECT_TRUE(DrainDue(scheduler, At(1000)).empty());

  ASSERT_TRUE(scheduler.Push(At(7), Tagged(3.0f)));
  ASSERT_TRUE(scheduler.Push(At(7), Tagged(4.0f)));
  EXPECT_EQ(DrainDue(scheduler, At(7)), (Tags{3.0f, 4.0f}));
}

TEST(EventSchedulerTest, ZeroCapacityRejectsEverything) {
  EventScheduler scheduler(0);
  ScheduledEvent event;

  EXPECT_EQ(scheduler.capacity(), 0u);
  EXPECT_FALSE(scheduler.Push(At(1), Tagged(1.0f)));
  EXPECT_FALSE(scheduler.PopDue(At(1000), event));
}

TEST(EventSchedulerTest, MatchesAReferenceModelUnderRandomizedUse) {
  struct ModelEvent {
    std::uint64_t target;
    std::uint64_t sequence;
    float tag;
  };

  constexpr std::size_t kModelCapacity = 64;
  constexpr int kOperations = 20000;

  EventScheduler scheduler(kModelCapacity);
  std::vector<ModelEvent> model;
  std::mt19937 rng(12345u);
  std::uint64_t sequence = 0;
  std::uint64_t now = 0;
  float next_tag = 0.0f;

  for (int i = 0; i < kOperations; ++i) {
    if (rng() % 3 != 0) {
      std::uint64_t target = now + (rng() % 40);

      if (rng() % 5 == 0) {
        target = now - std::min<std::uint64_t>(now, rng() % 10);
      }

      const bool accepted = scheduler.Push(At(target), Tagged(next_tag));

      ASSERT_EQ(accepted, model.size() < kModelCapacity);

      if (accepted) {
        model.push_back({target, sequence++, next_tag});
        next_tag += 1.0f;
      }
    } else {
      now += rng() % 25;

      for (;;) {
        auto best = model.end();

        for (auto it = model.begin(); it != model.end(); ++it) {
          if (it->target > now) {
            continue;
          }

          if (best == model.end() || it->target < best->target ||
              (it->target == best->target && it->sequence < best->sequence)) {
            best = it;
          }
        }

        ScheduledEvent popped;
        const bool got = scheduler.PopDue(At(now), popped);

        ASSERT_EQ(got, best != model.end());

        if (!got) {
          break;
        }

        ASSERT_FLOAT_EQ(popped.command.value, best->tag);
        ASSERT_EQ(ToInteger(popped.target_frame), best->target);

        model.erase(best);
      }

      ASSERT_EQ(scheduler.size(), model.size());
    }
  }
}

TEST(EventReservationsTest, StartsWithNothingOutstanding) {
  EventReservations reservations(4);

  EXPECT_EQ(reservations.outstanding(), 0u);
  EXPECT_EQ(reservations.capacity(), 4u);
}

TEST(EventReservationsTest, GrantsExactlyCapacityReservations) {
  EventReservations reservations(3);

  EXPECT_TRUE(reservations.TryReserve());
  EXPECT_TRUE(reservations.TryReserve());
  EXPECT_TRUE(reservations.TryReserve());
  EXPECT_FALSE(reservations.TryReserve());
  EXPECT_EQ(reservations.outstanding(), 3u);
}

TEST(EventReservationsTest, FailedReserveDoesNotChangeOutstanding) {
  EventReservations reservations(1);

  ASSERT_TRUE(reservations.TryReserve());

  for (int i = 0; i < 10; ++i) {
    EXPECT_FALSE(reservations.TryReserve());
  }

  EXPECT_EQ(reservations.outstanding(), 1u);
}

TEST(EventReservationsTest, ReleaseMakesRoomAgain) {
  EventReservations reservations(2);

  ASSERT_TRUE(reservations.TryReserve());
  ASSERT_TRUE(reservations.TryReserve());
  ASSERT_FALSE(reservations.TryReserve());

  reservations.Release();

  EXPECT_EQ(reservations.outstanding(), 1u);
  EXPECT_TRUE(reservations.TryReserve());
  EXPECT_FALSE(reservations.TryReserve());
}

TEST(EventReservationsTest, ReserveReleaseCyclesReturnToZero) {
  EventReservations reservations(5);

  for (int cycle = 0; cycle < 100; ++cycle) {
    for (int i = 0; i < 5; ++i) {
      ASSERT_TRUE(reservations.TryReserve());
    }

    for (int i = 0; i < 5; ++i) {
      reservations.Release();
    }
  }

  EXPECT_EQ(reservations.outstanding(), 0u);
}

TEST(EventReservationsTest, ZeroCapacityGrantsNothing) {
  EventReservations reservations(0);

  EXPECT_FALSE(reservations.TryReserve());
  EXPECT_EQ(reservations.outstanding(), 0u);
}

TEST(EventSchedulingContractTest, EveryReservedEventFitsTheScheduler) {
  constexpr std::size_t kContractCapacity = 16;

  EventReservations reservations(kContractCapacity);
  EventScheduler scheduler(kContractCapacity);
  std::size_t reserved = 0;

  while (reservations.TryReserve()) {
    ASSERT_TRUE(scheduler.Push(At(1000 - reserved), Tagged(1.0f)))
        << "reservation " << reserved << " had no heap slot";

    ++reserved;
  }

  EXPECT_EQ(reserved, kContractCapacity);

  ScheduledEvent event;

  while (scheduler.PopDue(At(1000), event)) {
    reservations.Release();
  }

  EXPECT_EQ(reservations.outstanding(), 0u);
}

}  // namespace
}  // namespace lavanda
