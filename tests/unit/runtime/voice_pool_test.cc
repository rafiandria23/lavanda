#include "lavanda/runtime/voice_pool.h"

#include <gtest/gtest.h>

#include <cmath>

namespace lavanda {
namespace {

TEST(VoicePoolTest, ReportsConstructedCapacity) {
  VoicePool pool(4);

  EXPECT_EQ(pool.capacity(), 4u);
}

TEST(VoicePoolTest, ZeroCapacityIsClampedToAtLeastOne) {
  VoicePool pool(0);

  EXPECT_GE(pool.capacity(), 1u);
}

TEST(VoicePoolTest, ReserveSlotSucceedsUpToCapacity) {
  VoicePool pool(2);
  StatusOr<VoiceId> first = pool.ReserveSlot();
  StatusOr<VoiceId> second = pool.ReserveSlot();

  ASSERT_TRUE(first.ok());
  ASSERT_TRUE(second.ok());
  EXPECT_NE(first.value().index, second.value().index);
}

TEST(VoicePoolTest, ReserveSlotFailsWhenExhausted) {
  VoicePool pool(1);

  ASSERT_TRUE(pool.ReserveSlot().ok());

  StatusOr<VoiceId> third = pool.ReserveSlot();

  EXPECT_FALSE(third.ok());
  EXPECT_EQ(third.status().code(), ErrorCode::kResourceExhausted);
}

TEST(VoicePoolTest, ReleaseSlotFreesIndexForReuse) {
  VoicePool pool(1);
  StatusOr<VoiceId> first = pool.ReserveSlot();

  ASSERT_TRUE(first.ok());

  pool.ReleaseSlot(first.value());

  StatusOr<VoiceId> second = pool.ReserveSlot();

  ASSERT_TRUE(second.ok());
  EXPECT_EQ(second.value().index, first.value().index);
  EXPECT_NE(second.value().generation, first.value().generation);
}

TEST(VoicePoolTest, ReleaseSlotIsSafeOnAlreadyReleasedId) {
  VoicePool pool(1);
  StatusOr<VoiceId> id = pool.ReserveSlot();

  ASSERT_TRUE(id.ok());

  pool.ReleaseSlot(id.value());
  pool.ReleaseSlot(id.value());

  StatusOr<VoiceId> reused = pool.ReserveSlot();

  EXPECT_TRUE(reused.ok());
}

TEST(VoicePoolTest, ReleaseSlotWithStaleGenerationDoesNotFreeNewerReservation) {
  VoicePool pool(1);
  StatusOr<VoiceId> first = pool.ReserveSlot();

  ASSERT_TRUE(first.ok());

  pool.ReleaseSlot(first.value());

  StatusOr<VoiceId> second = pool.ReserveSlot();

  ASSERT_TRUE(second.ok());

  pool.ReleaseSlot(first.value());

  StatusOr<VoiceId> third = pool.ReserveSlot();

  EXPECT_FALSE(third.ok())
      << "pool should still be exhausted -- the stale release must have "
         "been ignored";
}

TEST(VoicePoolTest, CreateVoiceTransitionsToStoppedNotPlaying) {
  VoicePool pool(1);
  StatusOr<VoiceId> id = pool.ReserveSlot();

  ASSERT_TRUE(id.ok());

  pool.ApplyCommand(
      {.type = CommandType::kCreateVoice, .voice_id = id.value()});

  EXPECT_FALSE(pool.is_active(id.value().index))
      << "created but not started -- is_active() should still be false";
}

TEST(VoicePoolTest, CreateVoiceResetsFieldsToDefaultsOnReuse) {
  VoicePool pool(1);
  StatusOr<VoiceId> first = pool.ReserveSlot();

  ASSERT_TRUE(first.ok());

  pool.ApplyCommand(
      {.type = CommandType::kCreateVoice, .voice_id = first.value()});
  pool.ApplyCommand({.type = CommandType::kSetVoiceGain,
                     .value = 0.1f,
                     .voice_id = first.value()});
  pool.ApplyCommand({.type = CommandType::kSetVoicePan,
                     .value = -1.0f,
                     .voice_id = first.value()});
  pool.ApplyCommand(
      {.type = CommandType::kDestroyVoice, .voice_id = first.value()});
  pool.ReleaseSlot(first.value());

  StatusOr<VoiceId> second = pool.ReserveSlot();

  ASSERT_TRUE(second.ok());

  pool.ApplyCommand(
      {.type = CommandType::kCreateVoice, .voice_id = second.value()});

  EXPECT_FLOAT_EQ(pool.gain(second.value().index), 1.0f);
  EXPECT_FLOAT_EQ(pool.pan(second.value().index), 0.0f);
}

TEST(VoicePoolTest, StartVoiceMakesItActive) {
  VoicePool pool(1);
  StatusOr<VoiceId> id = pool.ReserveSlot();

  ASSERT_TRUE(id.ok());

  pool.ApplyCommand(
      {.type = CommandType::kCreateVoice, .voice_id = id.value()});
  pool.ApplyCommand({.type = CommandType::kStartVoice, .voice_id = id.value()});

  EXPECT_TRUE(pool.is_active(id.value().index));
}

TEST(VoicePoolTest, StopVoiceMakesItInactive) {
  VoicePool pool(1);
  StatusOr<VoiceId> id = pool.ReserveSlot();

  ASSERT_TRUE(id.ok());

  pool.ApplyCommand(
      {.type = CommandType::kCreateVoice, .voice_id = id.value()});
  pool.ApplyCommand({.type = CommandType::kStartVoice, .voice_id = id.value()});
  pool.ApplyCommand({.type = CommandType::kStopVoice, .voice_id = id.value()});

  EXPECT_FALSE(pool.is_active(id.value().index));
}

TEST(VoicePoolTest, DuplicateStartIsIdempotent) {
  VoicePool pool(1);
  StatusOr<VoiceId> id = pool.ReserveSlot();

  ASSERT_TRUE(id.ok());

  pool.ApplyCommand(
      {.type = CommandType::kCreateVoice, .voice_id = id.value()});
  pool.ApplyCommand({.type = CommandType::kStartVoice, .voice_id = id.value()});
  pool.ApplyCommand({.type = CommandType::kStartVoice, .voice_id = id.value()});

  EXPECT_TRUE(pool.is_active(id.value().index));
}

TEST(VoicePoolTest, DuplicateStopIsIdempotent) {
  VoicePool pool(1);
  StatusOr<VoiceId> id = pool.ReserveSlot();

  ASSERT_TRUE(id.ok());

  pool.ApplyCommand(
      {.type = CommandType::kCreateVoice, .voice_id = id.value()});
  pool.ApplyCommand({.type = CommandType::kStopVoice, .voice_id = id.value()});
  pool.ApplyCommand({.type = CommandType::kStopVoice, .voice_id = id.value()});

  EXPECT_FALSE(pool.is_active(id.value().index));
}

TEST(VoicePoolTest, DestroyWhilePlayingSucceeds) {
  VoicePool pool(1);
  StatusOr<VoiceId> id = pool.ReserveSlot();

  ASSERT_TRUE(id.ok());

  pool.ApplyCommand(
      {.type = CommandType::kCreateVoice, .voice_id = id.value()});
  pool.ApplyCommand({.type = CommandType::kStartVoice, .voice_id = id.value()});
  pool.ApplyCommand(
      {.type = CommandType::kDestroyVoice, .voice_id = id.value()});

  EXPECT_FALSE(pool.is_active(id.value().index));
}

TEST(VoicePoolTest, DuplicateDestroyIsSafe) {
  VoicePool pool(1);
  StatusOr<VoiceId> id = pool.ReserveSlot();

  ASSERT_TRUE(id.ok());

  pool.ApplyCommand(
      {.type = CommandType::kCreateVoice, .voice_id = id.value()});
  pool.ApplyCommand(
      {.type = CommandType::kDestroyVoice, .voice_id = id.value()});
  pool.ApplyCommand(
      {.type = CommandType::kDestroyVoice, .voice_id = id.value()});

  EXPECT_FALSE(pool.is_active(id.value().index));
}

TEST(VoicePoolTest, CommandsAfterDestroyAreIgnored) {
  VoicePool pool(1);
  StatusOr<VoiceId> id = pool.ReserveSlot();

  ASSERT_TRUE(id.ok());

  pool.ApplyCommand(
      {.type = CommandType::kCreateVoice, .voice_id = id.value()});
  pool.ApplyCommand(
      {.type = CommandType::kDestroyVoice, .voice_id = id.value()});
  pool.ApplyCommand({.type = CommandType::kStartVoice, .voice_id = id.value()});

  EXPECT_FALSE(pool.is_active(id.value().index))
      << "StartVoice on a destroyed voice must not resurrect it";
}

TEST(VoicePoolTest, InvalidOutOfRangeIndexIsIgnored) {
  VoicePool pool(2);
  VoiceId out_of_range{99, 0};

  pool.ApplyCommand(
      {.type = CommandType::kStartVoice, .voice_id = out_of_range});

  SUCCEED();
}

TEST(VoicePoolTest, StaleHandleCannotControlReusedVoice) {
  VoicePool pool(1);
  StatusOr<VoiceId> voice_a = pool.ReserveSlot();

  ASSERT_TRUE(voice_a.ok());

  pool.ApplyCommand(
      {.type = CommandType::kCreateVoice, .voice_id = voice_a.value()});
  pool.ApplyCommand({.type = CommandType::kSetVoiceFrequency,
                     .value = 220.0f,
                     .voice_id = voice_a.value()});
  pool.ApplyCommand(
      {.type = CommandType::kDestroyVoice, .voice_id = voice_a.value()});
  pool.ReleaseSlot(voice_a.value());

  StatusOr<VoiceId> voice_b = pool.ReserveSlot();

  ASSERT_TRUE(voice_b.ok());
  EXPECT_EQ(voice_b.value().index, voice_a.value().index);
  EXPECT_NE(voice_b.value().generation, voice_a.value().generation);

  pool.ApplyCommand(
      {.type = CommandType::kCreateVoice, .voice_id = voice_b.value()});
  pool.ApplyCommand(
      {.type = CommandType::kStartVoice, .voice_id = voice_b.value()});
  pool.ApplyCommand({.type = CommandType::kSetVoiceFrequency,
                     .value = 880.0f,
                     .voice_id = voice_b.value()});
  pool.ApplyCommand(
      {.type = CommandType::kStopVoice, .voice_id = voice_a.value()});
  pool.ApplyCommand({.type = CommandType::kSetVoiceGain,
                     .value = 0.0f,
                     .voice_id = voice_a.value()});
  pool.ApplyCommand({.type = CommandType::kSetVoiceFrequency,
                     .value = 1.0f,
                     .voice_id = voice_a.value()});

  EXPECT_TRUE(pool.is_active(voice_b.value().index))
      << "A's stale StopVoice must not have stopped B";
  EXPECT_FLOAT_EQ(pool.gain(voice_b.value().index), 1.0f)
      << "A's stale SetVoiceGain must not have touched B's gain";
}

TEST(VoicePoolTest, SetVoiceGainUpdatesGain) {
  VoicePool pool(1);
  StatusOr<VoiceId> id = pool.ReserveSlot();

  ASSERT_TRUE(id.ok());

  pool.ApplyCommand(
      {.type = CommandType::kCreateVoice, .voice_id = id.value()});
  pool.ApplyCommand({.type = CommandType::kSetVoiceGain,
                     .value = 0.3f,
                     .voice_id = id.value()});

  EXPECT_FLOAT_EQ(pool.gain(id.value().index), 0.3f);
}

TEST(VoicePoolTest, SetVoicePanUpdatesPan) {
  VoicePool pool(1);
  StatusOr<VoiceId> id = pool.ReserveSlot();

  ASSERT_TRUE(id.ok());

  pool.ApplyCommand(
      {.type = CommandType::kCreateVoice, .voice_id = id.value()});
  pool.ApplyCommand({.type = CommandType::kSetVoicePan,
                     .value = -0.5f,
                     .voice_id = id.value()});

  EXPECT_FLOAT_EQ(pool.pan(id.value().index), -0.5f);
}

TEST(VoicePoolTest, SetVoiceBusUpdatesTargetBus) {
  VoicePool pool(1);
  StatusOr<VoiceId> id = pool.ReserveSlot();

  ASSERT_TRUE(id.ok());

  BusId sfx_bus{1, 1};

  pool.ApplyCommand(
      {.type = CommandType::kCreateVoice, .voice_id = id.value()});
  pool.ApplyCommand({.type = CommandType::kSetVoiceBus,
                     .voice_id = id.value(),
                     .bus_id = sfx_bus});

  EXPECT_EQ(pool.target_bus(id.value().index), sfx_bus);
}

TEST(VoicePoolTest, DefaultTargetBusIsMaster) {
  VoicePool pool(1);
  StatusOr<VoiceId> id = pool.ReserveSlot();

  ASSERT_TRUE(id.ok());

  pool.ApplyCommand(
      {.type = CommandType::kCreateVoice, .voice_id = id.value()});

  EXPECT_EQ(pool.target_bus(id.value().index), kMasterBusId);
}

TEST(VoicePoolTest, RenderVoiceSourceProducesSilenceWhenStopped) {
  VoicePool pool(1);
  StatusOr<VoiceId> id = pool.ReserveSlot();

  ASSERT_TRUE(id.ok());

  pool.ApplyCommand(
      {.type = CommandType::kCreateVoice, .voice_id = id.value()});

  AudioBuffer buffer(8, 1);

  for (std::uint32_t f = 0; f < 8; ++f) buffer(f, 0) = 1.0f;

  pool.RenderVoiceSource(id.value().index, buffer.View(), 48000.0);

  for (std::uint32_t f = 0; f < 8; ++f) {
    EXPECT_FLOAT_EQ(buffer(f, 0), 0.0f);
  }
}

TEST(VoicePoolTest, RenderVoiceSourceProducesNonSilenceWhenPlaying) {
  VoicePool pool(1);
  StatusOr<VoiceId> id = pool.ReserveSlot();

  ASSERT_TRUE(id.ok());

  pool.ApplyCommand(
      {.type = CommandType::kCreateVoice, .voice_id = id.value()});
  pool.ApplyCommand({.type = CommandType::kSetVoiceFrequency,
                     .value = 440.0f,
                     .voice_id = id.value()});
  pool.ApplyCommand({.type = CommandType::kStartVoice, .voice_id = id.value()});

  AudioBuffer buffer(64, 1);
  pool.RenderVoiceSource(id.value().index, buffer.View(), 48000.0);

  bool any_nonzero = false;

  for (std::uint32_t f = 0; f < 64; ++f) {
    if (buffer(f, 0) != 0.0f) any_nonzero = true;
  }

  EXPECT_TRUE(any_nonzero);
}

TEST(VoicePoolTest, RenderVoiceSourcePhaseIsContinuousAcrossCalls) {
  VoicePool one_shot_pool(1);
  StatusOr<VoiceId> one_shot_id = one_shot_pool.ReserveSlot();

  ASSERT_TRUE(one_shot_id.ok());

  one_shot_pool.ApplyCommand(
      {.type = CommandType::kCreateVoice, .voice_id = one_shot_id.value()});
  one_shot_pool.ApplyCommand({.type = CommandType::kSetVoiceFrequency,
                              .value = 440.0f,
                              .voice_id = one_shot_id.value()});
  one_shot_pool.ApplyCommand(
      {.type = CommandType::kStartVoice, .voice_id = one_shot_id.value()});

  AudioBuffer one_shot_buffer(8, 1);
  one_shot_pool.RenderVoiceSource(one_shot_id.value().index,
                                  one_shot_buffer.View(), 48000.0);

  VoicePool split_pool(1);
  StatusOr<VoiceId> split_id = split_pool.ReserveSlot();

  ASSERT_TRUE(split_id.ok());

  split_pool.ApplyCommand(
      {.type = CommandType::kCreateVoice, .voice_id = split_id.value()});
  split_pool.ApplyCommand({.type = CommandType::kSetVoiceFrequency,
                           .value = 440.0f,
                           .voice_id = split_id.value()});
  split_pool.ApplyCommand(
      {.type = CommandType::kStartVoice, .voice_id = split_id.value()});

  AudioBuffer first_half(4, 1);
  AudioBuffer second_half(4, 1);

  split_pool.RenderVoiceSource(split_id.value().index, first_half.View(),
                               48000.0);
  split_pool.RenderVoiceSource(split_id.value().index, second_half.View(),
                               48000.0);

  for (std::uint32_t f = 0; f < 4; ++f) {
    EXPECT_FLOAT_EQ(one_shot_buffer(f, 0), first_half(f, 0));
  }

  for (std::uint32_t f = 0; f < 4; ++f) {
    EXPECT_FLOAT_EQ(one_shot_buffer(f + 4, 0), second_half(f, 0));
  }
}

TEST(VoicePoolTest, RenderVoiceSourceOnOutOfRangeIndexIsSafe) {
  VoicePool pool(1);
  AudioBuffer buffer(4, 1);

  for (std::uint32_t f = 0; f < 4; ++f) buffer(f, 0) = 1.0f;

  pool.RenderVoiceSource(99, buffer.View(), 48000.0);

  for (std::uint32_t f = 0; f < 4; ++f) {
    EXPECT_FLOAT_EQ(buffer(f, 0), 0.0f);
  }
}

}  // namespace
}  // namespace lavanda
