#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <numbers>
#include <string>
#include <utility>
#include <vector>

#include "lavanda/core/audio_buffer.h"
#include "lavanda/core/time.h"
#include "lavanda/resources/audio_asset_id.h"
#include "lavanda/runtime/audio_runtime.h"
#include "lavanda/runtime/command.h"
#include "lavanda/runtime/mixing.h"
#include "lavanda/runtime/runtime_config.h"
#include "lavanda/spatial/quaternion.h"
#include "lavanda/spatial/spatial_state.h"
#include "test_support/fake_audio_device.h"

namespace lavanda {
namespace {

constexpr std::uint32_t kBlockFrames = 256;
constexpr std::uint32_t kChannels = 2;
constexpr float kHalfPi = std::numbers::pi_v<float> / 2.0f;

std::string FixturePath(const char* name) {
  return std::string(LAVANDA_FIXTURE_DIR) + "/" + name;
}

struct Capture {
  std::vector<float> left;
  std::vector<float> right;
};

float Peak(const std::vector<float>& samples, std::size_t from = 0) {
  float peak = 0.0f;

  for (std::size_t i = from; i < samples.size(); ++i) {
    peak = std::max(peak, std::fabs(samples[i]));
  }

  return peak;
}

std::size_t LastBlockStart(const std::vector<float>& samples) {
  return samples.size() >= kBlockFrames ? samples.size() - kBlockFrames : 0;
}

int PositiveCrossings(const std::vector<float>& samples) {
  int count = 0;

  for (std::size_t i = 1; i < samples.size(); ++i) {
    if (samples[i - 1] < 0.0f && samples[i] >= 0.0f) {
      ++count;
    }
  }

  return count;
}

Command ListenerMove(const Vec3& position) {
  Command command;
  command.type = CommandType::kSetListenerState;
  command.spatial.position = position;

  return command;
}

Command EmitterMove(VoiceId voice, const Vec3& position) {
  Command command;

  command.type = CommandType::kSetVoiceEmitterState;
  command.voice_id = voice;
  command.spatial.position = position;

  return command;
}

class SpatialRenderTest : public ::testing::Test {
 protected:
  void Boot(float smoothing_seconds = 0.0f, float doppler_factor = 1.0f) {
    RuntimeConfig config;
    config.spatial.smoothing_seconds = smoothing_seconds;
    config.spatial.doppler_factor = doppler_factor;

    auto device = std::make_unique<test_support::FakeAudioDevice>();

    device_ = device.get();
    runtime_ = std::make_unique<AudioRuntime>(std::move(device), config);

    ASSERT_TRUE(runtime_->Start().ok());
  }

  Voice MakeOscillator(float hz = 440.0f) {
    StatusOr<Voice> created = runtime_->CreateVoice();

    EXPECT_TRUE(created.ok());

    Voice voice = created.value();

    EXPECT_TRUE(voice.SetFrequency(hz).ok());
    EXPECT_TRUE(voice.SetGain(1.0f).ok());

    return voice;
  }

  Voice MakeAssetVoice(const char* fixture) {
    StatusOr<AudioAssetId> id = runtime_->LoadAudioAsset(FixturePath(fixture));

    EXPECT_TRUE(id.ok());

    StatusOr<Voice> created = runtime_->CreateVoice(id.value());

    EXPECT_TRUE(created.ok());

    Voice voice = created.value();

    EXPECT_TRUE(voice.SetGain(1.0f).ok());

    return voice;
  }

  void Place(Voice& voice, const Vec3& position,
             const Vec3& velocity = Vec3{}) {
    EXPECT_TRUE(voice.SetEmitterState(EmitterState{position, velocity}).ok());
  }

  Capture Render(std::uint32_t frames, std::uint32_t block = kBlockFrames) {
    Capture capture;

    for (std::uint32_t done = 0; done < frames; done += block) {
      AudioBuffer buffer(block, kChannels);

      EXPECT_TRUE(device_->PumpRender(buffer.View()));

      for (std::uint32_t frame = 0; frame < block; ++frame) {
        capture.left.push_back(buffer(frame, 0));
        capture.right.push_back(buffer(frame, 1));
      }
    }

    return capture;
  }

  Capture RenderOscillatorAt(const Vec3& position, const Vec3& velocity,
                             std::uint32_t frames,
                             float doppler_factor = 1.0f) {
    Boot(0.0f, doppler_factor);

    Voice voice = MakeOscillator();

    Place(voice, position, velocity);

    EXPECT_TRUE(voice.SetSpatialized(true).ok());
    EXPECT_TRUE(voice.Start().ok());

    return Render(frames);
  }

  double sample_rate() const { return device_->format().sample_rate_hz(); }

  test_support::FakeAudioDevice* device_ = nullptr;
  std::unique_ptr<AudioRuntime> runtime_;
};

TEST_F(SpatialRenderTest, PansHardRightOrLeftWithDistanceAttenuation) {
  const Capture right =
      RenderOscillatorAt(Vec3{5.0f, 0.0f, 0.0f}, Vec3{}, 1024);

  EXPECT_NEAR(Peak(right.right), 0.2f, 0.01f);
  EXPECT_LT(Peak(right.left), 1.0e-4f);

  const Capture left =
      RenderOscillatorAt(Vec3{-5.0f, 0.0f, 0.0f}, Vec3{}, 1024);

  EXPECT_NEAR(Peak(left.left), 0.2f, 0.01f);
  EXPECT_LT(Peak(left.right), 1.0e-4f);
}

TEST_F(SpatialRenderTest, CentresASourceAheadAndAttenuatesItByDistance) {
  const Capture ahead =
      RenderOscillatorAt(Vec3{0.0f, 0.0f, -10.0f}, Vec3{}, 1024);

  EXPECT_NEAR(Peak(ahead.left), 0.1f * 0.70710678f, 0.003f);
  EXPECT_NEAR(Peak(ahead.right), 0.1f * 0.70710678f, 0.003f);
}

TEST_F(SpatialRenderTest, ManualPanIsIgnoredWhileSpatializedAndRestoredAfter) {
  Boot();

  Voice voice = MakeOscillator();

  ASSERT_TRUE(voice.SetPan(-1.0f).ok());  // hard left, manually

  Place(voice, Vec3{5.0f, 0.0f, 0.0f});  // but spatially hard right

  ASSERT_TRUE(voice.SetSpatialized(true).ok());
  ASSERT_TRUE(voice.Start().ok());

  const Capture spatial = Render(512);

  EXPECT_NEAR(Peak(spatial.right), 0.2f, 0.01f);
  EXPECT_LT(Peak(spatial.left), 1.0e-4f);

  ASSERT_TRUE(voice.SetSpatialized(false).ok());

  const Capture manual = Render(512);

  EXPECT_NEAR(Peak(manual.left), 1.0f, 0.01f);
  EXPECT_LT(Peak(manual.right), 1.0e-4f);
}

TEST_F(SpatialRenderTest, ListenerOrientationMovesTheSourceAcrossTheStage) {
  Boot();

  Voice voice = MakeOscillator();

  Place(voice, Vec3{0.0f, 0.0f, -5.0f});  // straight ahead of the default pose
  ASSERT_TRUE(voice.SetSpatialized(true).ok());

  // Turn the listener to face world -X: the source is now on its right.
  ListenerState listener;
  listener.orientation =
      QuaternionFromAxisAngle(Vec3{0.0f, 1.0f, 0.0f}, kHalfPi);

  ASSERT_TRUE(runtime_->SetListenerState(listener).ok());
  ASSERT_TRUE(voice.Start().ok());

  const Capture capture = Render(1024);

  EXPECT_NEAR(Peak(capture.right), 0.2f, 0.01f);
  EXPECT_LT(Peak(capture.left), 1.0e-4f);
}

TEST_F(SpatialRenderTest, SpatialChangesGlideOverTheSmoothingTime) {
  Boot(0.05f);  // 50 ms

  Voice voice = MakeOscillator();

  Place(voice, Vec3{-5.0f, 0.0f, 0.0f});

  ASSERT_TRUE(voice.SetSpatialized(true).ok());
  ASSERT_TRUE(voice.Start().ok());

  const Capture settled = Render(512);

  EXPECT_NEAR(Peak(settled.left), 0.2f, 0.01f);

  Place(voice, Vec3{5.0f, 0.0f, 0.0f});

  const Capture start = Render(64, 64);

  EXPECT_GT(Peak(start.left), 0.1f);
  EXPECT_LT(Peak(start.right), 0.05f);

  const std::uint32_t frames = static_cast<std::uint32_t>(sample_rate() * 0.3);
  const Capture finished = Render(frames);

  EXPECT_LT(Peak(finished.left, LastBlockStart(finished.left)), 0.01f);
  EXPECT_NEAR(Peak(finished.right, LastBlockStart(finished.right)), 0.2f,
              0.01f);
}

TEST_F(SpatialRenderTest, DopplerLowersThePitchOfARecedingSource) {
  // Moving away from a listener at the origin: radial speed 34.3 m/s, so the
  // pitch ratio is 343 / (343 + 34.3) = 0.90909.
  const Capture capture = RenderOscillatorAt(Vec3{0.0f, 0.0f, -10.0f},
                                             Vec3{0.0f, 0.0f, -34.3f}, 48000);

  const double seconds =
      static_cast<double>(capture.left.size()) / sample_rate();

  EXPECT_NEAR(static_cast<double>(PositiveCrossings(capture.left)),
              440.0 * 0.9090909 * seconds, 3.0);
}

TEST_F(SpatialRenderTest, ZeroDopplerFactorKeepsThePitch) {
  const Capture capture = RenderOscillatorAt(
      Vec3{0.0f, 0.0f, -10.0f}, Vec3{0.0f, 0.0f, -34.3f}, 48000, 0.0f);

  const double seconds =
      static_cast<double>(capture.left.size()) / sample_rate();

  EXPECT_NEAR(static_cast<double>(PositiveCrossings(capture.left)),
              440.0 * seconds, 3.0);
}

TEST_F(SpatialRenderTest,
       SpatializedMonoAssetMatchesTheManualPathAtUnityRatio) {
  Boot();

  Voice manual = MakeAssetVoice("mono_pcm16.wav");

  ASSERT_TRUE(manual.Start().ok());

  const Capture expected = Render(2048);

  ASSERT_GT(Peak(expected.left), 0.0f);

  Boot();

  Voice spatial = MakeAssetVoice("mono_pcm16.wav");

  Place(spatial, Vec3{0.0f, 0.0f, -1.0f});

  ASSERT_TRUE(spatial.SetSpatialized(true).ok());
  ASSERT_TRUE(spatial.Start().ok());

  const Capture actual = Render(2048);

  ASSERT_EQ(actual.left.size(), expected.left.size());

  for (std::size_t i = 0; i < expected.left.size(); ++i) {
    ASSERT_FLOAT_EQ(actual.left[i], expected.left[i]) << "frame " << i;
    ASSERT_FLOAT_EQ(actual.right[i], expected.right[i]) << "frame " << i;
  }
}

TEST_F(SpatialRenderTest, SpatializedAssetVoiceStopsAtTheEnd) {
  Boot();

  Voice voice = MakeAssetVoice("mono_pcm16.wav");

  Place(voice, Vec3{0.0f, 0.0f, -1.0f}, Vec3{0.0f, 0.0f, -34.3f});

  ASSERT_TRUE(voice.SetSpatialized(true).ok());
  ASSERT_TRUE(voice.Start().ok());

  float peak = 0.0f;
  bool ended = false;

  for (int block = 0; block < 2000 && !ended; ++block) {
    const Capture capture = Render(kBlockFrames);

    peak = std::max({peak, Peak(capture.left), Peak(capture.right)});
    ended = !voice.IsPlaying();
  }

  EXPECT_TRUE(ended);
  EXPECT_GT(peak, 0.0f);
}

TEST_F(SpatialRenderTest, AudioThreadIgnoresSpatializationOfAStereoVoice) {
  Boot();

  Voice plain = MakeAssetVoice("stereo_pcm16.wav");

  ASSERT_TRUE(plain.Start().ok());

  const Capture expected = Render(2048);

  ASSERT_GT(Peak(expected.left), 0.0f);

  Boot();

  Voice voice = MakeAssetVoice("stereo_pcm16.wav");

  Place(voice, Vec3{50.0f, 0.0f, 0.0f});  // would be far right and quiet

  Command command;

  command.type = CommandType::kSetVoiceSpatialization;
  command.value = 1.0f;
  command.voice_id = voice.id();

  ASSERT_TRUE(runtime_->Submit(command).ok());
  ASSERT_TRUE(voice.Start().ok());

  const Capture actual = Render(2048);

  ASSERT_EQ(actual.left.size(), expected.left.size());

  for (std::size_t i = 0; i < expected.left.size(); ++i) {
    ASSERT_FLOAT_EQ(actual.left[i], expected.left[i]) << "frame " << i;
    ASSERT_FLOAT_EQ(actual.right[i], expected.right[i]) << "frame " << i;
  }
}

TEST_F(SpatialRenderTest,
       ListenerAndEmitterUpdatesAtTheSameFrameAreSeenTogether) {
  Boot();

  Voice voice = MakeOscillator();

  Place(voice, Vec3{5.0f, 0.0f, 0.0f});

  ASSERT_TRUE(voice.SetSpatialized(true).ok());
  ASSERT_TRUE(voice.Start().ok());

  Render(kBlockFrames);

  // Both move by (+10, 0, 0) at the same frame: the relative geometry never
  // changes, so the source must stay hard right throughout.
  const std::uint64_t now = ToInteger(runtime_->frame_position());
  const FramePosition frame = FramePosition{now + 100};

  ASSERT_TRUE(
      runtime_->ScheduleAtFrame(ListenerMove(Vec3{10.0f, 0.0f, 0.0f}), frame)
          .ok());
  ASSERT_TRUE(runtime_
                  ->ScheduleAtFrame(
                      EmitterMove(voice.id(), Vec3{15.0f, 0.0f, 0.0f}), frame)
                  .ok());

  const Capture capture = Render(512, 64);

  EXPECT_LT(Peak(capture.left), 1.0e-4f);
  EXPECT_NEAR(Peak(capture.right), 0.2f, 0.01f);
}

TEST_F(SpatialRenderTest,
       ControlCaseUpdatesOnDifferentFramesShowTheIntermediateState) {
  Boot();

  Voice voice = MakeOscillator();

  Place(voice, Vec3{5.0f, 0.0f, 0.0f});

  ASSERT_TRUE(voice.SetSpatialized(true).ok());
  ASSERT_TRUE(voice.Start().ok());

  Render(kBlockFrames);

  const std::uint64_t now = ToInteger(runtime_->frame_position());

  ASSERT_TRUE(runtime_
                  ->ScheduleAtFrame(ListenerMove(Vec3{10.0f, 0.0f, 0.0f}),
                                    FramePosition{now + 100})
                  .ok());
  ASSERT_TRUE(
      runtime_
          ->ScheduleAtFrame(EmitterMove(voice.id(), Vec3{15.0f, 0.0f, 0.0f}),
                            FramePosition{now + 108})
          .ok());

  const Capture capture = Render(512, 64);

  EXPECT_GT(Peak(capture.left), 0.02f);
}

}  // namespace
}  // namespace lavanda
