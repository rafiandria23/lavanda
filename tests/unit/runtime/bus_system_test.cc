#include "lavanda/runtime/bus_system.h"

#include <gtest/gtest.h>

namespace lavanda {
namespace {

TEST(BusSystemTest, ReportsConstructedCapacities) {
  BusSystem buses(3, 256);

  EXPECT_EQ(buses.user_bus_capacity(), 3u);
  EXPECT_EQ(buses.total_capacity(), 4u);  // 3 user buses + master
  EXPECT_EQ(buses.max_frames_per_block(), 256u);
}

TEST(BusSystemTest, MasterExistsFromConstruction) {
  BusSystem buses(1, 64);

  EXPECT_TRUE(buses.is_active(kMasterBusId.index));
  EXPECT_FLOAT_EQ(buses.gain(kMasterBusId.index), 1.0f);
}

TEST(BusSystemTest, ReserveSlotNeverReturnsMasterIndex) {
  BusSystem buses(2, 64);
  StatusOr<BusId> first = buses.ReserveSlot();
  StatusOr<BusId> second = buses.ReserveSlot();

  ASSERT_TRUE(first.ok());
  ASSERT_TRUE(second.ok());
  EXPECT_NE(first.value().index, 0u);
  EXPECT_NE(second.value().index, 0u);
}

TEST(BusSystemTest, ReserveSlotFailsWhenUserCapacityExhausted) {
  BusSystem buses(1, 64);

  ASSERT_TRUE(buses.ReserveSlot().ok());

  StatusOr<BusId> second = buses.ReserveSlot();

  EXPECT_FALSE(second.ok());
  EXPECT_EQ(second.status().code(), ErrorCode::kResourceExhausted);
}

TEST(BusSystemTest, ReleaseSlotFreesIndexForReuse) {
  BusSystem buses(1, 64);
  StatusOr<BusId> first = buses.ReserveSlot();

  ASSERT_TRUE(first.ok());

  buses.ReleaseSlot(first.value());

  StatusOr<BusId> second = buses.ReserveSlot();

  ASSERT_TRUE(second.ok());
  EXPECT_EQ(second.value().index, first.value().index);
  EXPECT_NE(second.value().generation, first.value().generation);
}

TEST(BusSystemTest, ReleaseSlotOnMasterIsSafeNoOp) {
  BusSystem buses(1, 64);
  buses.ReleaseSlot(kMasterBusId);

  StatusOr<BusId> reserved = buses.ReserveSlot();

  ASSERT_TRUE(reserved.ok());
  EXPECT_NE(reserved.value().index, 0u);
}

TEST(BusSystemTest, CreateBusOnMasterIndexIsRejected) {
  BusSystem buses(1, 64);
  buses.ApplyCommand({.type = CommandType::kCreateBus, .bus_id = kMasterBusId});

  EXPECT_TRUE(buses.is_active(kMasterBusId.index));
}

TEST(BusSystemTest, DestroyBusOnMasterIsRejected) {
  BusSystem buses(1, 64);
  buses.ApplyCommand(
      {.type = CommandType::kDestroyBus, .bus_id = kMasterBusId});

  EXPECT_TRUE(buses.is_active(kMasterBusId.index));
}

TEST(BusSystemTest, CreateBusActivatesReservedSlot) {
  BusSystem buses(1, 64);
  StatusOr<BusId> id = buses.ReserveSlot();

  ASSERT_TRUE(id.ok());

  buses.ApplyCommand({.type = CommandType::kCreateBus, .bus_id = id.value()});

  EXPECT_TRUE(buses.is_active(id.value().index));
  EXPECT_FLOAT_EQ(buses.gain(id.value().index), 1.0f);
}

TEST(BusSystemTest, DestroyBusDeactivatesSlot) {
  BusSystem buses(1, 64);
  StatusOr<BusId> id = buses.ReserveSlot();

  ASSERT_TRUE(id.ok());

  buses.ApplyCommand({.type = CommandType::kCreateBus, .bus_id = id.value()});
  buses.ApplyCommand({.type = CommandType::kDestroyBus, .bus_id = id.value()});

  EXPECT_FALSE(buses.is_active(id.value().index));
}

TEST(BusSystemTest, SetBusGainUpdatesGain) {
  BusSystem buses(1, 64);
  StatusOr<BusId> id = buses.ReserveSlot();

  ASSERT_TRUE(id.ok());

  buses.ApplyCommand({.type = CommandType::kCreateBus, .bus_id = id.value()});
  buses.ApplyCommand(
      {.type = CommandType::kSetBusGain, .value = 0.4f, .bus_id = id.value()});

  EXPECT_FLOAT_EQ(buses.gain(id.value().index), 0.4f);
}

TEST(BusSystemTest, SetBusGainOnMasterUpdatesMasterGain) {
  BusSystem buses(1, 64);
  buses.ApplyCommand({.type = CommandType::kSetBusGain,
                      .value = 0.6f,
                      .bus_id = kMasterBusId});

  EXPECT_FLOAT_EQ(buses.gain(kMasterBusId.index), 0.6f);
}

TEST(BusSystemTest, StaleSetBusGainDoesNotFallThroughToMaster) {
  BusSystem buses(1, 64);
  StatusOr<BusId> id = buses.ReserveSlot();

  ASSERT_TRUE(id.ok());

  buses.ApplyCommand({.type = CommandType::kCreateBus, .bus_id = id.value()});
  buses.ApplyCommand({.type = CommandType::kDestroyBus, .bus_id = id.value()});
  buses.ReleaseSlot(id.value());
  buses.ApplyCommand(
      {.type = CommandType::kSetBusGain, .value = 0.01f, .bus_id = id.value()});

  EXPECT_FLOAT_EQ(buses.gain(kMasterBusId.index), 1.0f)
      << "a stale SetBusGain must not have touched master's gain";
}

TEST(BusSystemTest, StaleHandleCannotControlReusedBus) {
  BusSystem buses(1, 64);
  StatusOr<BusId> bus_a = buses.ReserveSlot();

  ASSERT_TRUE(bus_a.ok());

  buses.ApplyCommand(
      {.type = CommandType::kCreateBus, .bus_id = bus_a.value()});
  buses.ApplyCommand({.type = CommandType::kSetBusGain,
                      .value = 0.1f,
                      .bus_id = bus_a.value()});
  buses.ApplyCommand(
      {.type = CommandType::kDestroyBus, .bus_id = bus_a.value()});
  buses.ReleaseSlot(bus_a.value());

  StatusOr<BusId> bus_b = buses.ReserveSlot();

  ASSERT_TRUE(bus_b.ok());
  EXPECT_EQ(bus_b.value().index, bus_a.value().index);
  EXPECT_NE(bus_b.value().generation, bus_a.value().generation);

  buses.ApplyCommand(
      {.type = CommandType::kCreateBus, .bus_id = bus_b.value()});
  buses.ApplyCommand({.type = CommandType::kSetBusGain,
                      .value = 0.9f,
                      .bus_id = bus_b.value()});
  buses.ApplyCommand({.type = CommandType::kSetBusGain,
                      .value = 0.0f,
                      .bus_id = bus_a.value()});
  buses.ApplyCommand(
      {.type = CommandType::kDestroyBus, .bus_id = bus_a.value()});

  EXPECT_TRUE(buses.is_active(bus_b.value().index));
  EXPECT_FLOAT_EQ(buses.gain(bus_b.value().index), 0.9f);
}

TEST(BusSystemTest, ResolveBusIndexReturnsMasterForKMasterBusId) {
  BusSystem buses(1, 64);

  EXPECT_EQ(buses.ResolveBusIndex(kMasterBusId), 0u);
}

TEST(BusSystemTest, ResolveBusIndexReturnsActualIndexForValidBus) {
  BusSystem buses(1, 64);
  StatusOr<BusId> id = buses.ReserveSlot();

  ASSERT_TRUE(id.ok());

  buses.ApplyCommand({.type = CommandType::kCreateBus, .bus_id = id.value()});

  EXPECT_EQ(buses.ResolveBusIndex(id.value()), id.value().index);
}

TEST(BusSystemTest, ResolveBusIndexFallsBackToMasterForDestroyedBus) {
  BusSystem buses(1, 64);
  StatusOr<BusId> id = buses.ReserveSlot();

  ASSERT_TRUE(id.ok());

  buses.ApplyCommand({.type = CommandType::kCreateBus, .bus_id = id.value()});
  buses.ApplyCommand({.type = CommandType::kDestroyBus, .bus_id = id.value()});

  EXPECT_EQ(buses.ResolveBusIndex(id.value()), 0u)
      << "a voice targeting a destroyed bus must resolve to master";
}

TEST(BusSystemTest, ResolveBusIndexFallsBackToMasterForNeverCreatedBus) {
  BusSystem buses(1, 64);
  BusId never_created{5, 1};

  EXPECT_EQ(buses.ResolveBusIndex(never_created), 0u);
}

TEST(BusSystemTest, ClearActiveAccumulatorsZeroesMasterAndActiveBusesOnly) {
  BusSystem buses(1, 8);
  StatusOr<BusId> id = buses.ReserveSlot();

  ASSERT_TRUE(id.ok());

  buses.ApplyCommand({.type = CommandType::kCreateBus, .bus_id = id.value()});

  AudioBufferView master_view = buses.MutableAccumulator(0, 4);
  AudioBufferView bus_view = buses.MutableAccumulator(id.value().index, 4);

  for (std::uint32_t f = 0; f < 4; ++f) {
    master_view(f, 0) = 1.0f;
    master_view(f, 1) = 1.0f;
    bus_view(f, 0) = 1.0f;
    bus_view(f, 1) = 1.0f;
  }

  buses.ClearActiveAccumulators(4);

  for (std::uint32_t f = 0; f < 4; ++f) {
    EXPECT_FLOAT_EQ(buses.MutableAccumulator(0, 4)(f, 0), 0.0f);
    EXPECT_FLOAT_EQ(buses.MutableAccumulator(id.value().index, 4)(f, 0), 0.0f);
  }
}

TEST(BusSystemTest, MutableAccumulatorHonorsRequestedFrameCount) {
  BusSystem buses(1, 64);  // capacity 64, but this block only needs 8
  AudioBufferView view = buses.MutableAccumulator(0, 8);
  EXPECT_EQ(view.frame_count(), 8u);
}

}  // namespace
}  // namespace lavanda
