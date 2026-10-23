#include <gtest/gtest.h>

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>

#include "lavanda/core/audio_buffer.h"
#include "lavanda/core/time.h"
#include "lavanda/resources/audio_asset_id.h"
#include "lavanda/runtime/audio_runtime.h"
#include "lavanda/runtime/command.h"
#include "lavanda/runtime/mixing.h"
#include "lavanda/runtime/runtime_stats.h"
#include "lavanda/spatial/quaternion.h"
#include "lavanda/spatial/spatial_state.h"
#include "test_support/allocation_guard.h"
#include "test_support/fake_audio_device.h"

namespace lavanda {
namespace {

constexpr std::uint32_t kWarmupFrames = 64;
constexpr float kSampleBound = 4.0f;

std::string FixturePath(const char* name) {
  return std::string(LAVANDA_FIXTURE_DIR) + "/" + name;
}

struct RenderResult {
  std::size_t allocations = 0;
  std::size_t bad_samples = 0;
  float first_bad_value = 0.0f;
  bool heard = false;
};

Command ListenerNudge(const Vec3& position) {
  Command command;

  command.type = CommandType::kSetListenerState;
  command.spatial.position = position;

  return command;
}

Command EmitterNudge(VoiceId voice, const Vec3& position) {
  Command command;

  command.type = CommandType::kSetVoiceEmitterState;
  command.voice_id = voice;
  command.spatial.position = position;

  return command;
}

Command SpatializationFlag(VoiceId voice, float flag) {
  Command command;

  command.type = CommandType::kSetVoiceSpatialization;
  command.voice_id = voice;
  command.value = flag;

  return command;
}

class SpatialRealtimeSafetyTest : public ::testing::Test {
 protected:
  void SetUp() override {
    auto owned = std::make_unique<test_support::FakeAudioDevice>();

    device_ = owned.get();
    runtime_ = std::make_unique<AudioRuntime>(std::move(owned));

    ASSERT_TRUE(runtime_->Start().ok());

    tone_ = MakeOscillator(440.0f, 0.5f);

    StatusOr<Bus> bus = runtime_->CreateBus();

    ASSERT_TRUE(bus.ok());

    bus_ = bus.value();
    bus_tone_ = MakeOscillator(330.0f, 0.5f);

    ASSERT_TRUE(bus_tone_.SetBus(bus_).ok());

    plain_ = MakeOscillator(220.0f, 0.2f);
    clip_ = MakeAssetVoice("mono_pcm16.wav", 0.5f);
    stereo_ = MakeAssetVoice("stereo_pcm16.wav", 0.3f);

    ASSERT_TRUE(tone_.SetSpatialized(true).ok());
    ASSERT_TRUE(bus_tone_.SetSpatialized(true).ok());
    ASSERT_TRUE(clip_.SetSpatialized(true).ok());
    ASSERT_TRUE(clip_.Start().ok());
    ASSERT_TRUE(stereo_.Start().ok());

    RenderResult warmup;

    RenderOne(kWarmupFrames, warmup);

    ASSERT_TRUE(warmup.heard);
  }

  Voice MakeOscillator(float hz, float gain) {
    StatusOr<Voice> created = runtime_->CreateVoice();

    EXPECT_TRUE(created.ok());

    Voice voice = created.value();

    EXPECT_TRUE(voice.SetFrequency(hz).ok());
    EXPECT_TRUE(voice.SetGain(gain).ok());
    EXPECT_TRUE(voice.Start().ok());

    return voice;
  }

  Voice MakeAssetVoice(const char* fixture, float gain) {
    StatusOr<AudioAssetId> id = runtime_->LoadAudioAsset(FixturePath(fixture));

    EXPECT_TRUE(id.ok());

    StatusOr<Voice> created = runtime_->CreateVoice(id.value());

    EXPECT_TRUE(created.ok());

    Voice voice = created.value();

    EXPECT_TRUE(voice.SetGain(gain).ok());

    return voice;
  }

  std::uint64_t Now() const { return ToInteger(runtime_->frame_position()); }

  void RenderOne(std::uint32_t frames, RenderResult& result) {
    AudioBuffer buffer(frames, 2);
    bool ok = false;

    {
      test_support::AllocationGuard guard;

      ok = device_->PumpRender(buffer.View());
      result.allocations += guard.allocation_count();
    }

    EXPECT_TRUE(ok);

    for (std::uint32_t frame = 0; frame < frames; ++frame) {
      for (std::uint32_t channel = 0; channel < 2; ++channel) {
        const float sample = buffer(frame, channel);

        if (sample != 0.0f) {
          result.heard = true;
        }

        if (!std::isfinite(sample) || std::fabs(sample) > kSampleBound) {
          if (result.bad_samples == 0) {
            result.first_bad_value = sample;
          }

          ++result.bad_samples;
        }
      }
    }
  }

  void Drive(std::uint32_t step, std::uint32_t frames) {
    const float t = static_cast<float>(step);

    ListenerState listener;

    listener.position =
        Vec3{20.0f * std::cos(0.05f * t), 1.5f, 20.0f * std::sin(0.05f * t)};
    listener.orientation =
        QuaternionFromAxisAngle(Vec3{0.0f, 1.0f, 0.0f}, 0.03f * t);
    listener.velocity = Vec3{-std::sin(0.05f * t), 0.0f, std::cos(0.05f * t)};

    EXPECT_TRUE(runtime_->SetListenerState(listener).ok());

    EmitterState near_emitter;
    near_emitter.position = Vec3{8.0f * std::cos(0.11f * t), 0.0f,
                                 -6.0f + 4.0f * std::sin(0.11f * t)};
    near_emitter.velocity = Vec3{3.0f, 0.0f, -1.0f};

    EmitterState extreme_emitter;

    if (step % 11 == 0) {
      extreme_emitter.position = Vec3{1.0e6f, -1.0e6f, 1.0e6f};
      extreme_emitter.velocity = Vec3{1.0e5f, 1.0e5f, -1.0e5f};
    } else {
      extreme_emitter.position = Vec3{-30.0f, 2.0f, 12.0f};
      extreme_emitter.velocity = Vec3{0.0f, 0.0f, 25.0f};
    }

    EXPECT_TRUE(tone_.SetEmitterState(near_emitter).ok());
    EXPECT_TRUE(bus_tone_.SetEmitterState(extreme_emitter).ok());
    EXPECT_TRUE(clip_.SetEmitterState(near_emitter).ok());

    if (step % 7 == 0) {
      EXPECT_TRUE(tone_.SetSpatialized((step / 7) % 2 == 0).ok());
    }

    EXPECT_TRUE(clip_.Start().ok());
    EXPECT_TRUE(stereo_.Start().ok());

    const std::uint64_t base = Now();

    EXPECT_TRUE(runtime_
                    ->ScheduleAtFrame(ListenerNudge(Vec3{1.0f, 0.0f, 2.0f}),
                                      FramePosition{base + step % frames})
                    .ok());
    EXPECT_TRUE(runtime_
                    ->ScheduleAtFrame(
                        EmitterNudge(tone_.id(), Vec3{-4.0f, 0.0f, -4.0f}),
                        FramePosition{base + (step + 1) % frames})
                    .ok());
    EXPECT_TRUE(
        runtime_
            ->ScheduleAtFrame(
                SpatializationFlag(bus_tone_.id(), step % 2 == 0 ? 1.0f : 0.0f),
                FramePosition{base + (step + 2) % frames})
            .ok());
  }

  RenderResult RunStorm(std::uint32_t frames, std::uint32_t steps) {
    RenderResult result;

    for (std::uint32_t step = 0; step < steps; ++step) {
      Drive(step, frames);
      RenderOne(frames, result);
    }

    return result;
  }

  void ExpectCleanRun(const RenderResult& result) {
    EXPECT_EQ(result.allocations, 0u);
    EXPECT_EQ(result.bad_samples, 0u)
        << "first bad sample: " << result.first_bad_value;
    EXPECT_TRUE(result.heard);

    const RuntimeStats stats = runtime_->stats();

    EXPECT_EQ(stats.scheduled_events_pending, 0u);
    EXPECT_EQ(stats.scheduled_events_discarded, 0u);
    EXPECT_EQ(stats.scheduled_events_dropped, 0u);
    EXPECT_EQ(stats.scheduled_events_capacity_failures, 0u);
    EXPECT_EQ(stats.command_failures, 0u);
    EXPECT_EQ(stats.scheduled_events_accepted, stats.scheduled_events_executed);
  }

  test_support::FakeAudioDevice* device_ = nullptr;
  std::unique_ptr<AudioRuntime> runtime_;
  Voice tone_;
  Voice bus_tone_;
  Voice plain_;
  Voice clip_;
  Voice stereo_;
  Bus bus_;
};

TEST_F(SpatialRealtimeSafetyTest, ControlStormRendersWithoutAllocating) {
  ExpectCleanRun(RunStorm(64, 400));
}

TEST_F(SpatialRealtimeSafetyTest, OddBlockSizesRenderWithoutAllocating) {
  ExpectCleanRun(RunStorm(37, 300));
}

TEST_F(SpatialRealtimeSafetyTest, TinyBlocksRenderWithoutAllocating) {
  ExpectCleanRun(RunStorm(5, 300));
}

TEST_F(SpatialRealtimeSafetyTest, SingleFrameBlocksRenderWithoutAllocating) {
  ExpectCleanRun(RunStorm(1, 300));
}

TEST_F(SpatialRealtimeSafetyTest,
       OversizedCallbackDoesNotAllocateOrGoNonFinite) {
  const RenderResult result = RunStorm(8192, 20);

  EXPECT_EQ(result.allocations, 0u);
  EXPECT_EQ(result.bad_samples, 0u);
}

}  // namespace
}  // namespace lavanda
