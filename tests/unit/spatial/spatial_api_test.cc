#include <gtest/gtest.h>

#include <cstdint>
#include <limits>
#include <memory>
#include <string>
#include <utility>

#include "lavanda/core/status.h"
#include "lavanda/core/time.h"
#include "lavanda/resources/audio_asset_id.h"
#include "lavanda/runtime/audio_runtime.h"
#include "lavanda/runtime/command.h"
#include "lavanda/runtime/mixing.h"
#include "lavanda/runtime/runtime_config.h"
#include "lavanda/runtime/runtime_stats.h"
#include "lavanda/spatial/spatial_state.h"
#include "test_support/fake_audio_device.h"

namespace lavanda {
namespace {

constexpr float kNaN = std::numeric_limits<float>::quiet_NaN();
constexpr FramePosition kFarFuture = FramePosition{1000000};

std::string FixturePath(const char* name) {
  return std::string(LAVANDA_FIXTURE_DIR) + "/" + name;
}

void ExpectInvalid(const Status& status) {
  EXPECT_FALSE(status.ok());
  EXPECT_EQ(status.code(), ErrorCode::kInvalidArgument);
}

std::unique_ptr<AudioRuntime> MakeRuntime(
    const RuntimeConfig& config,
    test_support::FakeAudioDevice** device_out = nullptr) {
  auto device = std::make_unique<test_support::FakeAudioDevice>();

  if (device_out != nullptr) {
    *device_out = device.get();
  }

  return std::make_unique<AudioRuntime>(std::move(device), config);
}

Command SpatializationCommand(VoiceId voice, float flag) {
  Command command;

  command.type = CommandType::kSetVoiceSpatialization;
  command.voice_id = voice;
  command.value = flag;

  return command;
}

TEST(SpatialStartTest,
     StartRejectsAnInvalidSpatialConfigBeforeOpeningTheDevice) {
  RuntimeConfig config;
  config.spatial.speed_of_sound_mps = 0.0f;

  test_support::FakeAudioDevice* device = nullptr;
  std::unique_ptr<AudioRuntime> runtime = MakeRuntime(config, &device);

  ExpectInvalid(runtime->Start());

  EXPECT_FALSE(device->is_open());
  EXPECT_FALSE(device->is_running());
}

TEST(SpatialStartTest, StartAcceptsACustomValidSpatialConfig) {
  RuntimeConfig config;
  config.spatial.doppler_factor = 0.0f;
  config.spatial.smoothing_seconds = 0.0f;

  test_support::FakeAudioDevice* device = nullptr;
  std::unique_ptr<AudioRuntime> runtime = MakeRuntime(config, &device);

  EXPECT_TRUE(runtime->Start().ok());
  EXPECT_TRUE(device->is_running());
}

class SpatialApiTest : public ::testing::Test {
 protected:
  void SetUp() override {
    runtime_ = MakeRuntime(RuntimeConfig());

    ASSERT_TRUE(runtime_->Start().ok());
  }

  Voice MakeOscillator() {
    StatusOr<Voice> created = runtime_->CreateVoice();

    EXPECT_TRUE(created.ok());

    return created.ok() ? created.value() : Voice();
  }

  Voice MakeAssetVoice(const char* fixture) {
    StatusOr<AudioAssetId> id = runtime_->LoadAudioAsset(FixturePath(fixture));

    EXPECT_TRUE(id.ok());

    if (!id.ok()) {
      return Voice();
    }

    StatusOr<Voice> created = runtime_->CreateVoice(id.value());

    EXPECT_TRUE(created.ok());

    return created.ok() ? created.value() : Voice();
  }

  std::unique_ptr<AudioRuntime> runtime_;
};

TEST_F(SpatialApiTest, SetListenerStateAcceptsValidAndRejectsInvalidStates) {
  ListenerState state;
  state.position = Vec3{1.0f, 2.0f, 3.0f};
  state.orientation = Quaternion{2.0f, 0.0f, 0.0f, 0.0f};  // not unit length

  EXPECT_TRUE(runtime_->SetListenerState(state).ok());

  ListenerState nan_position;
  nan_position.position.x = kNaN;

  ExpectInvalid(runtime_->SetListenerState(nan_position));

  ListenerState zero_orientation;
  zero_orientation.orientation = Quaternion{0.0f, 0.0f, 0.0f, 0.0f};

  ExpectInvalid(runtime_->SetListenerState(zero_orientation));
}

TEST_F(SpatialApiTest, SetEmitterStateAcceptsValidAndRejectsInvalidStates) {
  Voice voice = MakeOscillator();

  EXPECT_TRUE(
      voice.SetEmitterState(EmitterState{Vec3{3.0f, 0.0f, -4.0f}, Vec3{}})
          .ok());

  EmitterState nan_position;
  nan_position.position.z = kNaN;

  ExpectInvalid(voice.SetEmitterState(nan_position));

  EmitterState too_fast;
  too_fast.velocity.x = 2.0e5f;

  ExpectInvalid(voice.SetEmitterState(too_fast));

  Voice invalid;

  ExpectInvalid(invalid.SetEmitterState(EmitterState{}));
  ExpectInvalid(invalid.SetSpatialized(true));
}

TEST_F(SpatialApiTest, OscillatorVoicesCanBeSpatialized) {
  Voice voice = MakeOscillator();

  EXPECT_TRUE(voice.SetSpatialized(true).ok());
  EXPECT_TRUE(voice.SetSpatialized(false).ok());
}

TEST_F(SpatialApiTest, MonoAssetVoicesCanBeSpatialized) {
  Voice voice = MakeAssetVoice("mono_pcm16.wav");

  ASSERT_TRUE(voice.is_valid());
  EXPECT_TRUE(voice.SetSpatialized(true).ok());
}

TEST_F(SpatialApiTest, StereoAssetVoicesCannotBeSpatializedButCanBeDisabled) {
  Voice voice = MakeAssetVoice("stereo_pcm16.wav");

  ASSERT_TRUE(voice.is_valid());

  ExpectInvalid(voice.SetSpatialized(true));

  EXPECT_TRUE(voice.SetSpatialized(false).ok());
}

TEST_F(SpatialApiTest, ScheduledSpatializationOfAStereoVoiceIsRejectedCheaply) {
  Voice stereo = MakeAssetVoice("stereo_pcm16.wav");

  ASSERT_TRUE(stereo.is_valid());

  ExpectInvalid(runtime_->ScheduleAtFrame(
      SpatializationCommand(stereo.id(), 1.0f), kFarFuture));

  EXPECT_EQ(runtime_->stats().scheduled_events_accepted, 0u);
  EXPECT_EQ(runtime_->stats().scheduled_events_pending, 0u);

  EXPECT_TRUE(runtime_
                  ->ScheduleAtFrame(SpatializationCommand(stereo.id(), 0.0f),
                                    kFarFuture)
                  .ok());
  EXPECT_EQ(runtime_->stats().scheduled_events_pending, 1u);
}

TEST_F(SpatialApiTest, ScheduledSpatializationOfAMonoVoiceIsAccepted) {
  Voice voice = MakeOscillator();

  EXPECT_TRUE(
      runtime_
          ->ScheduleAtFrame(SpatializationCommand(voice.id(), 1.0f), kFarFuture)
          .ok());
}

TEST_F(SpatialApiTest, DestroyedVoiceHandlesCannotBeSpatialized) {
  Voice voice = MakeOscillator();
  Voice stale = voice;

  ASSERT_TRUE(voice.Destroy().ok());

  ExpectInvalid(stale.SetSpatialized(true));
  ExpectInvalid(runtime_->ScheduleAtFrame(
      SpatializationCommand(stale.id(), 1.0f), kFarFuture));
}

TEST_F(SpatialApiTest, SlotReuseForgetsTheOldChannelCount) {
  Voice stereo = MakeAssetVoice("stereo_pcm16.wav");

  ASSERT_TRUE(stereo.is_valid());

  const std::uint32_t slot = stereo.id().index;

  ExpectInvalid(stereo.SetSpatialized(true));

  ASSERT_TRUE(stereo.Destroy().ok());

  Voice reused = MakeOscillator();

  ASSERT_TRUE(reused.is_valid());
  ASSERT_EQ(reused.id().index, slot);

  EXPECT_TRUE(reused.SetSpatialized(true).ok());
}

}  // namespace
}  // namespace lavanda
