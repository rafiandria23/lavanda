#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <utility>

#include "lavanda/core/audio_buffer.h"
#include "lavanda/core/status.h"
#include "lavanda/core/time.h"
#include "lavanda/graph/node_id.h"
#include "lavanda/runtime/audio_runtime.h"
#include "lavanda/runtime/command.h"
#include "lavanda/runtime/handles.h"
#include "lavanda/runtime/mixing.h"
#include "lavanda/runtime/runtime_config.h"
#include "lavanda/runtime/runtime_stats.h"
#include "test_support/fake_audio_device.h"

namespace lavanda {
namespace {

constexpr std::uint32_t kChannels = 2;
constexpr std::uint32_t kBlockFrames = 64;
constexpr std::uint64_t kFarFuture = 1000000;
constexpr float kNaN = std::numeric_limits<float>::quiet_NaN();
constexpr float kInf = std::numeric_limits<float>::infinity();

ErrorCode CodeOf(const Status& status) { return status.code(); }

NodeId ValidNodeId() { return NodeId{.index = 0, .generation = 1}; }

class Env {
 public:
  explicit Env(RuntimeConfig config = RuntimeConfig()) {
    auto device = std::make_unique<test_support::FakeAudioDevice>();

    device_ = device.get();
    runtime_ = std::make_unique<AudioRuntime>(std::move(device), config);
  }

  AudioRuntime& runtime() { return *runtime_; }

  void Start() { ASSERT_TRUE(runtime_->Start().ok()); }

  void Pump(std::uint32_t blocks = 1) {
    for (std::uint32_t i = 0; i < blocks; ++i) {
      AudioBuffer buffer(kBlockFrames, kChannels);

      ASSERT_TRUE(device_->PumpRender(buffer.View()));
    }
  }

  Status Schedule(const Command& command, std::uint64_t frame) {
    return runtime_->ScheduleAtFrame(command, FramePosition{frame});
  }

  Status ScheduleMasterGain(std::uint64_t frame, float gain = 1.0f) {
    return Schedule({.type = CommandType::kSetBusGain,
                     .value = gain,
                     .bus_id = kMasterBusId},
                    frame);
  }

  RuntimeStats Stats() const { return runtime_->stats(); }

 private:
  test_support::FakeAudioDevice* device_ = nullptr;
  std::unique_ptr<AudioRuntime> runtime_;
};

void ExpectAccounting(const RuntimeStats& stats) {
  EXPECT_EQ(stats.scheduled_events_dropped, 0u);
  EXPECT_EQ(stats.scheduled_events_accepted,
            stats.scheduled_events_executed + stats.scheduled_events_discarded +
                stats.scheduled_events_dropped +
                stats.scheduled_events_pending);
}

TEST(SchedulingApiTest, RejectsCommandTypesThatCannotBeScheduled) {
  Env env;
  const CommandType types[] = {
      CommandType::kCreateVoice,       CommandType::kDestroyVoice,
      CommandType::kCreateBus,         CommandType::kDestroyBus,
      CommandType::kActivateGraphPlan, CommandType::kStartTone,
      CommandType::kStopTone,          CommandType::kSetFrequency,
      CommandType::kSetGain,           CommandType::kSetVoiceBus};

  for (CommandType type : types) {
    Command command;
    command.type = type;

    const Status status = env.Schedule(command, 0);

    EXPECT_FALSE(status.ok());
    EXPECT_EQ(CodeOf(status), ErrorCode::kInvalidArgument);
  }

  const RuntimeStats stats = env.Stats();

  EXPECT_EQ(stats.scheduled_events_accepted, 0u);
  EXPECT_EQ(stats.scheduled_events_pending, 0u);
  EXPECT_EQ(stats.scheduled_events_capacity_failures, 0u);
}

TEST(SchedulingApiTest, RejectsInvalidValuesWithoutConsumingCapacity) {
  Env env;
  StatusOr<Voice> voice = env.runtime().CreateVoice();

  ASSERT_TRUE(voice.ok());

  struct Case {
    CommandType type;
    float value;
  };

  const Case bad[] = {{CommandType::kSetVoiceGain, kNaN},
                      {CommandType::kSetVoiceGain, kInf},
                      {CommandType::kSetVoiceGain, -0.5f},
                      {CommandType::kSetBusGain, -1.0f},
                      {CommandType::kSetNodeGain, kNaN},
                      {CommandType::kSetVoicePan, kNaN},
                      {CommandType::kSetVoicePan, kInf},
                      {CommandType::kSetNodePan, kNaN},
                      {CommandType::kSetVoiceFrequency, 0.0f},
                      {CommandType::kSetVoiceFrequency, -440.0f},
                      {CommandType::kSetVoiceFrequency, kNaN},
                      {CommandType::kSetNodeFrequency, 0.0f},
                      {CommandType::kSetNodeCutoff, 0.0f},
                      {CommandType::kSetNodeCutoff, kInf},
                      {CommandType::kSetNodeDelayFrames, -1.0f},
                      {CommandType::kSetNodeDelayFrames, kNaN}};

  for (const Case& test_case : bad) {
    const Status status = env.Schedule({.type = test_case.type,
                                        .value = test_case.value,
                                        .voice_id = voice.value().id(),
                                        .bus_id = kMasterBusId,
                                        .node_id = ValidNodeId()},
                                       0);

    EXPECT_FALSE(status.ok());
    EXPECT_EQ(CodeOf(status), ErrorCode::kInvalidArgument);
  }

  const RuntimeStats stats = env.Stats();

  EXPECT_EQ(stats.scheduled_events_accepted, 0u);
  EXPECT_EQ(stats.scheduled_events_pending, 0u);
}

TEST(SchedulingApiTest, RejectsStructurallyInvalidIds) {
  Env env;

  EXPECT_FALSE(
      env.Schedule({.type = CommandType::kStartVoice, .voice_id = VoiceId{}}, 0)
          .ok());
  EXPECT_FALSE(env.Schedule({.type = CommandType::kSetVoiceGain,
                             .value = 0.5f,
                             .voice_id = VoiceId{}},
                            0)
                   .ok());
  EXPECT_FALSE(env.Schedule({.type = CommandType::kSetBusGain,
                             .value = 0.5f,
                             .bus_id = BusId{}},
                            0)
                   .ok());
  EXPECT_FALSE(env.Schedule({.type = CommandType::kSetNodeGain,
                             .value = 0.5f,
                             .node_id = NodeId{}},
                            0)
                   .ok());
  EXPECT_EQ(env.Stats().scheduled_events_pending, 0u);
}

TEST(SchedulingApiTest, AcceptsEverySchedulableCommandType) {
  Env env;
  StatusOr<Voice> voice = env.runtime().CreateVoice();

  ASSERT_TRUE(voice.ok());

  struct Case {
    CommandType type;
    float value;
  };

  const Case good[] = {{CommandType::kStartVoice, 0.0f},
                       {CommandType::kStopVoice, 0.0f},
                       {CommandType::kSetVoiceGain, 0.5f},
                       {CommandType::kSetVoicePan, -0.5f},
                       {CommandType::kSetVoiceFrequency, 220.0f},
                       {CommandType::kSetBusGain, 0.5f},
                       {CommandType::kSetNodeGain, 0.5f},
                       {CommandType::kSetNodeFrequency, 220.0f},
                       {CommandType::kSetNodeCutoff, 1000.0f},
                       {CommandType::kSetNodePan, 0.0f},
                       {CommandType::kSetNodeDelayFrames, 0.0f}};

  for (const Case& test_case : good) {
    EXPECT_TRUE(env.Schedule({.type = test_case.type,
                              .value = test_case.value,
                              .voice_id = voice.value().id(),
                              .bus_id = kMasterBusId,
                              .node_id = ValidNodeId()},
                             0)
                    .ok());
  }

  EXPECT_EQ(env.Stats().scheduled_events_accepted, 11u);

  env.Start();
  env.Pump();

  const RuntimeStats stats = env.Stats();

  EXPECT_EQ(stats.scheduled_events_executed, 11u);
  EXPECT_EQ(stats.scheduled_events_late, 0u);

  ExpectAccounting(stats);
}

TEST(SchedulingApiTest,
     CapacityExhaustionIsReportedAndNeverLosesAcceptedEvents) {
  RuntimeConfig config;
  config.max_scheduled_events = 4;
  config.command_queue_capacity = 16;

  Env env(config);
  env.Start();

  for (std::uint64_t i = 0; i < 4; ++i) {
    ASSERT_TRUE(env.ScheduleMasterGain(10 * (i + 1)).ok());
  }

  const Status full = env.ScheduleMasterGain(50);

  EXPECT_FALSE(full.ok());
  EXPECT_EQ(CodeOf(full), ErrorCode::kResourceExhausted);

  RuntimeStats stats = env.Stats();

  EXPECT_EQ(stats.scheduled_events_accepted, 4u);
  EXPECT_EQ(stats.scheduled_events_capacity_failures, 1u);
  EXPECT_EQ(stats.scheduled_events_pending, 4u);

  env.Pump();

  stats = env.Stats();

  EXPECT_EQ(stats.scheduled_events_executed, 4u);
  EXPECT_EQ(stats.scheduled_events_pending, 0u);

  ExpectAccounting(stats);

  EXPECT_TRUE(env.ScheduleMasterGain(100).ok());
}

TEST(SchedulingApiTest, QueueFullRollsBackTheReservation) {
  RuntimeConfig config;
  config.command_queue_capacity = 2;
  config.max_scheduled_events = 16;

  Env env(config);

  ASSERT_TRUE(env.ScheduleMasterGain(0).ok());
  ASSERT_TRUE(env.ScheduleMasterGain(0).ok());

  const Status full = env.ScheduleMasterGain(0);

  EXPECT_FALSE(full.ok());
  EXPECT_EQ(CodeOf(full), ErrorCode::kQueueFull);

  RuntimeStats stats = env.Stats();

  EXPECT_EQ(stats.scheduled_events_accepted, 2u);
  EXPECT_EQ(stats.scheduled_events_pending, 2u);
  EXPECT_EQ(stats.scheduled_events_capacity_failures, 0u);
  EXPECT_EQ(stats.command_failures, 1u);

  env.Start();
  env.Pump();

  stats = env.Stats();

  EXPECT_EQ(stats.scheduled_events_executed, 2u);
  EXPECT_EQ(stats.scheduled_events_pending, 0u);

  ExpectAccounting(stats);

  EXPECT_TRUE(env.ScheduleMasterGain(0).ok());
}

TEST(SchedulingApiTest, LateEventsAreCountedWithTheirLateness) {
  Env env;
  env.Start();
  env.Pump();

  ASSERT_TRUE(env.ScheduleMasterGain(10).ok());

  env.Pump();

  RuntimeStats stats = env.Stats();

  EXPECT_EQ(stats.scheduled_events_late, 1u);
  EXPECT_EQ(stats.scheduled_events_max_lateness_frames, 54u);

  ASSERT_TRUE(env.ScheduleMasterGain(100).ok());

  env.Pump();

  stats = env.Stats();

  EXPECT_EQ(stats.scheduled_events_late, 2u);
  EXPECT_EQ(stats.scheduled_events_max_lateness_frames, 54u);

  ASSERT_TRUE(env.ScheduleMasterGain(200).ok());

  env.Pump();

  stats = env.Stats();

  EXPECT_EQ(stats.scheduled_events_executed, 3u);
  EXPECT_EQ(stats.scheduled_events_late, 2u);

  ExpectAccounting(stats);
}

TEST(SchedulingApiTest, EventAtACallbackEndFrameRunsInTheNextCallbackOnTime) {
  Env env;
  env.Start();

  ASSERT_TRUE(env.ScheduleMasterGain(kBlockFrames).ok());

  env.Pump();

  EXPECT_EQ(env.Stats().scheduled_events_executed, 0u);

  env.Pump();

  const RuntimeStats stats = env.Stats();

  EXPECT_EQ(stats.scheduled_events_executed, 1u);
  EXPECT_EQ(stats.scheduled_events_late, 0u);
}

TEST(SchedulingApiTest, SchedulingBeforeStartExecutesAtTheTargetFrame) {
  Env env;

  ASSERT_TRUE(env.ScheduleMasterGain(100).ok());

  env.Start();
  env.Pump();

  EXPECT_EQ(env.Stats().scheduled_events_executed, 0u);

  env.Pump();

  const RuntimeStats stats = env.Stats();

  EXPECT_EQ(stats.scheduled_events_executed, 1u);
  EXPECT_EQ(stats.scheduled_events_late, 0u);
}

TEST(SchedulingApiTest, StopRetainsPendingEventsAndTheyRunAfterRestart) {
  Env env;
  env.Start();

  ASSERT_TRUE(env.ScheduleMasterGain(100).ok());

  env.Pump();

  EXPECT_EQ(env.Stats().scheduled_events_pending, 1u);

  ASSERT_TRUE(env.runtime().Stop().ok());

  env.Start();
  env.Pump();

  const RuntimeStats stats = env.Stats();

  EXPECT_EQ(stats.scheduled_events_executed, 1u);
  EXPECT_EQ(stats.scheduled_events_late, 0u);
  EXPECT_EQ(stats.scheduled_events_discarded, 0u);

  ExpectAccounting(stats);
}

TEST(SchedulingApiTest, ShutdownDiscardsPendingEventsAndReturnsTheirCapacity) {
  RuntimeConfig config;
  config.max_scheduled_events = 8;

  Env env(config);
  env.Start();

  for (int i = 0; i < 3; ++i) {
    ASSERT_TRUE(env.ScheduleMasterGain(kFarFuture).ok());
  }

  env.Pump();

  for (int i = 0; i < 2; ++i) {
    ASSERT_TRUE(env.ScheduleMasterGain(kFarFuture).ok());
  }

  ASSERT_TRUE(env.runtime().Shutdown().ok());

  EXPECT_EQ(env.Stats().scheduled_events_pending, 5u);

  env.Start();
  env.Pump();

  const RuntimeStats stats = env.Stats();

  EXPECT_EQ(stats.scheduled_events_discarded, 5u);
  EXPECT_EQ(stats.scheduled_events_executed, 0u);
  EXPECT_EQ(stats.scheduled_events_pending, 0u);

  ExpectAccounting(stats);

  for (int i = 0; i < 8; ++i) {
    EXPECT_TRUE(env.ScheduleMasterGain(kFarFuture).ok()) << "slot " << i;
  }
}

TEST(SchedulingApiTest, EventsScheduledAfterShutdownBelongToTheNewSession) {
  Env env;
  env.Start();

  ASSERT_TRUE(env.ScheduleMasterGain(kFarFuture).ok());

  env.Pump();

  ASSERT_TRUE(env.runtime().Shutdown().ok());
  ASSERT_TRUE(env.ScheduleMasterGain(100).ok());

  env.Start();
  env.Pump();
  env.Pump(0);

  const RuntimeStats stats = env.Stats();

  EXPECT_EQ(stats.scheduled_events_discarded, 1u);
  EXPECT_EQ(stats.scheduled_events_executed, 1u);
  EXPECT_EQ(stats.scheduled_events_late, 0u);

  ExpectAccounting(stats);
}

}  // namespace
}  // namespace lavanda
