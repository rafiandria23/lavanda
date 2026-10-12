#include "lavanda/runtime/command_queue.h"

#include <gtest/gtest.h>

#include "lavanda/core/time.h"

namespace lavanda {
namespace {

TEST(CommandQueueTest, StartsEmpty) {
  CommandQueue queue(4);
  Command command;

  EXPECT_FALSE(queue.TryPop(command));
}

TEST(CommandQueueTest, ReportsConstructedCapacity) {
  CommandQueue queue(8);

  EXPECT_EQ(queue.capacity(), 8u);
}

TEST(CommandQueueTest, ZeroCapacityIsClampedToAtLeastOne) {
  CommandQueue queue(0);

  EXPECT_GE(queue.capacity(), 1u);
}

TEST(CommandQueueTest, PushThenPopRoundTrips) {
  CommandQueue queue(4);
  Command sent;

  sent.type = CommandType::kSetGain;
  sent.value = 0.5f;

  ASSERT_TRUE(queue.TryPush(sent));

  Command received;

  ASSERT_TRUE(queue.TryPop(received));
  EXPECT_EQ(received.type, CommandType::kSetGain);
  EXPECT_FLOAT_EQ(received.value, 0.5f);
}

TEST(CommandQueueTest, PreservesFifoOrder) {
  CommandQueue queue(4);

  for (int i = 0; i < 3; ++i) {
    Command command;

    command.type = CommandType::kSetFrequency;
    command.value = static_cast<float>(i);

    ASSERT_TRUE(queue.TryPush(command));
  }
  for (int i = 0; i < 3; ++i) {
    Command received;

    ASSERT_TRUE(queue.TryPop(received));
    EXPECT_FLOAT_EQ(received.value, static_cast<float>(i));
  }
}

TEST(CommandQueueTest, RejectsPushWhenFull) {
  CommandQueue queue(2);
  Command command;

  EXPECT_TRUE(queue.TryPush(command));
  EXPECT_TRUE(queue.TryPush(command));
  EXPECT_FALSE(queue.TryPush(command));  // full -- rejected, not blocked
}

TEST(CommandQueueTest, PoppingMakesRoomForMorePushes) {
  CommandQueue queue(2);
  Command command;

  ASSERT_TRUE(queue.TryPush(command));
  ASSERT_TRUE(queue.TryPush(command));
  ASSERT_FALSE(queue.TryPush(command));

  Command received;

  ASSERT_TRUE(queue.TryPop(received));
  EXPECT_TRUE(queue.TryPush(command));  // room freed by the pop above
}

TEST(CommandQueueTest, WrapsAroundCorrectly) {
  CommandQueue queue(2);

  for (int cycle = 0; cycle < 5; ++cycle) {
    Command sent;

    sent.type = CommandType::kSetFrequency;
    sent.value = static_cast<float>(cycle);

    ASSERT_TRUE(queue.TryPush(sent));

    Command received;

    ASSERT_TRUE(queue.TryPop(received));
    EXPECT_FLOAT_EQ(received.value, static_cast<float>(cycle));
  }
}

TEST(CommandQueueTest, PlainCommandsAreQueuedAsImmediate) {
  CommandQueue queue(4);
  Command command;

  command.type = CommandType::kSetGain;

  ASSERT_TRUE(queue.TryPush(command));

  QueuedCommand received;

  ASSERT_TRUE(queue.TryPop(received));
  EXPECT_FALSE(received.scheduled);
  EXPECT_EQ(received.command.type, CommandType::kSetGain);
}

TEST(CommandQueueTest, QueuedCommandCarriesSchedulingMetadata) {
  CommandQueue queue(4);
  QueuedCommand sent;

  sent.command.type = CommandType::kSetVoiceGain;
  sent.command.value = 0.5f;
  sent.target_frame = FramePosition{12345};
  sent.scheduled = true;

  ASSERT_TRUE(queue.TryPush(sent));

  QueuedCommand received;

  ASSERT_TRUE(queue.TryPop(received));
  EXPECT_TRUE(received.scheduled);
  EXPECT_EQ(ToInteger(received.target_frame), 12345u);
  EXPECT_FLOAT_EQ(received.command.value, 0.5f);
}

TEST(CommandQueueTest, ImmediateAndScheduledItemsShareOneFifoOrder) {
  CommandQueue queue(4);
  Command immediate;
  QueuedCommand scheduled;

  scheduled.scheduled = true;

  immediate.value = 1.0f;

  ASSERT_TRUE(queue.TryPush(immediate));

  scheduled.command.value = 2.0f;

  ASSERT_TRUE(queue.TryPush(scheduled));

  immediate.value = 3.0f;

  ASSERT_TRUE(queue.TryPush(immediate));

  QueuedCommand received;

  ASSERT_TRUE(queue.TryPop(received));
  EXPECT_FLOAT_EQ(received.command.value, 1.0f);
  EXPECT_FALSE(received.scheduled);

  ASSERT_TRUE(queue.TryPop(received));
  EXPECT_FLOAT_EQ(received.command.value, 2.0f);
  EXPECT_TRUE(received.scheduled);

  ASSERT_TRUE(queue.TryPop(received));
  EXPECT_FLOAT_EQ(received.command.value, 3.0f);
  EXPECT_FALSE(received.scheduled);
}

}  // namespace
}  // namespace lavanda
