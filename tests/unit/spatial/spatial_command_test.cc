#include <gtest/gtest.h>

#include <cmath>
#include <cstdint>
#include <limits>
#include <memory>
#include <type_traits>
#include <utility>

#include "lavanda/core/audio_buffer.h"
#include "lavanda/core/status.h"
#include "lavanda/core/time.h"
#include "lavanda/runtime/audio_runtime.h"
#include "lavanda/runtime/command.h"
#include "lavanda/runtime/handles.h"
#include "lavanda/runtime/mixing.h"
#include "lavanda/runtime/runtime_stats.h"
#include "lavanda/spatial/quaternion.h"
#include "lavanda/spatial/spatial_state.h"
#include "test_support/fake_audio_device.h"

namespace lavanda {
namespace {

static_assert(std::is_trivially_copyable_v<Command>);
static_assert(sizeof(Command) <= 128,
              "Command is copied through the SPSC queue and lives in the "
              "audio-thread event heap; keep it compact");
static_assert(static_cast<int>(CommandType::kSetListenerState) ==
              static_cast<int>(CommandType::kRampBusGain) + 1);
static_assert(static_cast<int>(CommandType::kSetVoiceEmitterState) ==
              static_cast<int>(CommandType::kRampBusGain) + 2);
static_assert(static_cast<int>(CommandType::kSetVoiceSpatialization) ==
              static_cast<int>(CommandType::kRampBusGain) + 3);

constexpr float kNaN = std::numeric_limits<float>::quiet_NaN();
constexpr float kInf = std::numeric_limits<float>::infinity();
constexpr std::uint32_t kBlockFrames = 64;
constexpr std::uint32_t kChannels = 2;
constexpr FramePosition kFarFuture = FramePosition{1000000};

void ExpectInvalid(const Status& status) {
  EXPECT_FALSE(status.ok());
  EXPECT_EQ(status.code(), ErrorCode::kInvalidArgument);
}

Command ListenerCommand(const ListenerState& state) {
  Command command;
  command.type = CommandType::kSetListenerState;
  command.spatial = state;

  return command;
}

Command EmitterCommand(VoiceId voice, const EmitterState& state) {
  Command command;

  command.type = CommandType::kSetVoiceEmitterState;
  command.voice_id = voice;
  command.spatial.position = state.position;
  command.spatial.velocity = state.velocity;

  return command;
}

Command SpatializationCommand(VoiceId voice, float flag) {
  Command command;

  command.type = CommandType::kSetVoiceSpatialization;
  command.voice_id = voice;
  command.value = flag;

  return command;
}

TEST(SpatialCommandLayoutTest, DefaultPayloadIsAnIdentityPoseAtRest) {
  const Command command;

  EXPECT_EQ(command.spatial.position.x, 0.0f);
  EXPECT_EQ(command.spatial.position.y, 0.0f);
  EXPECT_EQ(command.spatial.position.z, 0.0f);
  EXPECT_EQ(command.spatial.velocity.x, 0.0f);
  EXPECT_EQ(command.spatial.orientation.w, 1.0f);
  EXPECT_EQ(command.spatial.orientation.x, 0.0f);
  EXPECT_EQ(command.spatial.orientation.y, 0.0f);
  EXPECT_EQ(command.spatial.orientation.z, 0.0f);
}

TEST(SpatialCommandLayoutTest, ExistingDesignatedInitialisationIsUnaffected) {
  const Command command{.type = CommandType::kSetVoiceGain,
                        .value = 0.5f,
                        .voice_id = VoiceId{3, 1},
                        .duration_frames = 7};

  EXPECT_EQ(command.type, CommandType::kSetVoiceGain);
  EXPECT_EQ(command.value, 0.5f);
  EXPECT_EQ(command.voice_id, (VoiceId{3, 1}));
  EXPECT_EQ(command.duration_frames, 7u);
  EXPECT_EQ(command.spatial.orientation.w, 1.0f);
}

class SpatialCommandTest : public ::testing::Test {
 protected:
  void SetUp() override {
    auto device = std::make_unique<test_support::FakeAudioDevice>();

    device_ = device.get();
    runtime_ = std::make_unique<AudioRuntime>(std::move(device));

    ASSERT_TRUE(runtime_->Start().ok());

    StatusOr<Voice> voice = runtime_->CreateVoice();

    ASSERT_TRUE(voice.ok());

    voice_ = voice.value();
  }

  Status Schedule(const Command& command, FramePosition frame = kFarFuture) {
    return runtime_->ScheduleAtFrame(command, frame);
  }

  test_support::FakeAudioDevice* device_ = nullptr;
  std::unique_ptr<AudioRuntime> runtime_;
  Voice voice_;
};

TEST_F(SpatialCommandTest, AcceptsAValidListenerStateWithoutAVoiceId) {
  ListenerState state;
  state.position = Vec3{10.0f, 1.7f, -4.0f};
  state.orientation = Quaternion{2.0f, 0.0f, 0.0f, 0.0f};  // not unit length
  state.velocity = Vec3{0.0f, 0.0f, -3.0f};

  const Command command = ListenerCommand(state);

  ASSERT_FALSE(command.voice_id.is_valid());
  EXPECT_TRUE(Schedule(command).ok());
}

TEST_F(SpatialCommandTest, RejectsInvalidListenerStates) {
  ListenerState nan_position;
  nan_position.position.y = kNaN;

  ListenerState too_far;
  too_far.position.z = 2.0e6f;

  ListenerState too_fast;
  too_fast.velocity.x = 2.0e5f;

  ListenerState zero_orientation;
  zero_orientation.orientation = Quaternion{0.0f, 0.0f, 0.0f, 0.0f};

  ListenerState infinite_orientation;
  infinite_orientation.orientation.x = kInf;

  for (const ListenerState& bad : {nan_position, too_far, too_fast,
                                   zero_orientation, infinite_orientation}) {
    ExpectInvalid(Schedule(ListenerCommand(bad)));
  }
}

TEST_F(SpatialCommandTest, AcceptsAValidEmitterStateAndIgnoresOrientation) {
  Command command = EmitterCommand(
      voice_.id(),
      EmitterState{Vec3{3.0f, 0.0f, -4.0f}, Vec3{0.0f, 0.0f, 1.0f}});

  EXPECT_TRUE(Schedule(command).ok());

  command.spatial.orientation = Quaternion{0.0f, 0.0f, 0.0f, 0.0f};

  EXPECT_TRUE(Schedule(command).ok());
}

TEST_F(SpatialCommandTest, RejectsInvalidEmitterStatesAndMissingVoice) {
  EmitterState nan_position;
  nan_position.position.x = kNaN;

  EmitterState too_fast;
  too_fast.velocity.y = 2.0e5f;

  ExpectInvalid(Schedule(EmitterCommand(voice_.id(), nan_position)));
  ExpectInvalid(Schedule(EmitterCommand(voice_.id(), too_fast)));
  ExpectInvalid(Schedule(EmitterCommand(VoiceId{}, EmitterState{})));
}

TEST_F(SpatialCommandTest, AcceptsOnlyZeroOrOneAsTheSpatializationFlag) {
  EXPECT_TRUE(Schedule(SpatializationCommand(voice_.id(), 0.0f)).ok());
  EXPECT_TRUE(Schedule(SpatializationCommand(voice_.id(), 1.0f)).ok());

  for (float bad : {0.5f, 2.0f, -1.0f, kNaN, kInf}) {
    ExpectInvalid(Schedule(SpatializationCommand(voice_.id(), bad)));
  }

  ExpectInvalid(Schedule(SpatializationCommand(VoiceId{}, 1.0f)));
}

TEST_F(SpatialCommandTest, RejectedCommandsDoNotConsumeEventCapacity) {
  ListenerState bad;
  bad.position.x = kNaN;

  for (int i = 0; i < 3; ++i) {
    ExpectInvalid(Schedule(ListenerCommand(bad)));
    ExpectInvalid(Schedule(SpatializationCommand(voice_.id(), 0.5f)));
  }

  EXPECT_EQ(runtime_->stats().scheduled_events_accepted, 0u);
  EXPECT_EQ(runtime_->stats().scheduled_events_pending, 0u);

  ASSERT_TRUE(Schedule(ListenerCommand(ListenerState{})).ok());

  EXPECT_EQ(runtime_->stats().scheduled_events_accepted, 1u);
  EXPECT_EQ(runtime_->stats().scheduled_events_pending, 1u);
}

TEST_F(SpatialCommandTest, ExecutesAllThreeCommandTypesAsRegularEvents) {
  ASSERT_TRUE(
      Schedule(ListenerCommand(ListenerState{}), FramePosition{0}).ok());
  ASSERT_TRUE(
      Schedule(EmitterCommand(voice_.id(), EmitterState{}), FramePosition{16})
          .ok());
  ASSERT_TRUE(
      Schedule(SpatializationCommand(voice_.id(), 1.0f), FramePosition{32})
          .ok());

  AudioBuffer buffer(kBlockFrames, kChannels);

  ASSERT_TRUE(device_->PumpRender(buffer.View()));

  const RuntimeStats stats = runtime_->stats();

  EXPECT_EQ(stats.scheduled_events_accepted, 3u);
  EXPECT_EQ(stats.scheduled_events_executed, 3u);
  EXPECT_EQ(stats.scheduled_events_pending, 0u);
  EXPECT_EQ(stats.scheduled_events_discarded, 0u);
  EXPECT_EQ(stats.scheduled_events_dropped, 0u);

  for (std::uint32_t frame = 0; frame < kBlockFrames; ++frame) {
    for (std::uint32_t channel = 0; channel < kChannels; ++channel) {
      EXPECT_TRUE(std::isfinite(buffer(frame, channel)));
    }
  }
}

}  // namespace
}  // namespace lavanda
