#include <gtest/gtest.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "lavanda/core/audio_buffer.h"
#include "lavanda/core/status.h"
#include "lavanda/core/time.h"
#include "lavanda/resources/audio_asset_id.h"
#include "lavanda/runtime/audio_runtime.h"
#include "lavanda/runtime/command.h"
#include "lavanda/runtime/handles.h"
#include "lavanda/runtime/mixing.h"
#include "lavanda/runtime/runtime_stats.h"
#include "test_support/fake_audio_device.h"

namespace lavanda {
namespace {

// ---- definitions -----------------------------------------------------------

constexpr std::uint32_t kChannels = 2;
constexpr std::uint32_t kTotalFrames = 192;
constexpr float kMainHz = 440.0f;
constexpr float kBusVoiceHz = 330.0f;
constexpr float kTolerance = 2e-6f;
constexpr float kStepGain = 0.7f;
constexpr std::size_t kNoMismatch = std::numeric_limits<std::size_t>::max();
constexpr std::uint32_t kCallbackSizes[] = {192, 64, 7};
constexpr std::uint64_t kTargets[] = {0, 1, 17, 63, 64, 65, 128, 191, 192, 500};
constexpr float kNaN = std::numeric_limits<float>::quiet_NaN();
constexpr float kInf = std::numeric_limits<float>::infinity();

struct Options {
  bool main_voice = true;  // 440 Hz, gain 1, centered, on the master bus
  bool bus_voice = false;  // 330 Hz, gain 1, centered, on a user bus
};

struct Step {
  std::uint64_t target;
  Command command;
};

class Session {
 public:
  explicit Session(Options options = Options()) {
    auto device = std::make_unique<test_support::FakeAudioDevice>();

    device_ = device.get();
    runtime_ = std::make_unique<AudioRuntime>(std::move(device));

    EXPECT_TRUE(runtime_->Start().ok());

    if (options.main_voice) {
      main_ = MakeOscillator(kMainHz);
    }

    if (options.bus_voice) {
      StatusOr<Bus> bus = runtime_->CreateBus();

      EXPECT_TRUE(bus.ok());

      bus_ = bus.value();
      bus_voice_ = MakeOscillator(kBusVoiceHz);

      EXPECT_TRUE(bus_voice_.SetBus(bus_).ok());
    }
  }

  AudioRuntime& runtime() { return *runtime_; }
  Voice& main_voice() { return main_; }
  Bus& user_bus() { return bus_; }
  VoiceId voice_id() const { return main_.id(); }
  BusId bus_id() const { return bus_.id(); }
  RuntimeStats Stats() const { return runtime_->stats(); }

  Status Schedule(const Command& command, std::uint64_t frame) {
    return runtime_->ScheduleAtFrame(command, FramePosition{frame});
  }

  Status Submit(const Command& command) { return runtime_->Submit(command); }

  VoiceId ReplaceMainVoice() {
    const VoiceId old = main_.id();

    EXPECT_TRUE(main_.Destroy().ok());

    main_ = MakeOscillator(kMainHz);

    EXPECT_EQ(main_.id().index, old.index);
    EXPECT_NE(main_.id().generation, old.generation);

    return old;
  }

  AudioAssetId LoadAsset(const char* file) {
    StatusOr<AudioAssetId> id =
        runtime_->LoadAudioAsset(std::string(LAVANDA_FIXTURE_DIR) + "/" + file);

    if (!id.ok()) {
      ADD_FAILURE() << "fixture load failed: " << file;

      return AudioAssetId{};
    }

    return id.value();
  }

  Voice MakeAssetVoice(AudioAssetId asset) {
    StatusOr<Voice> created = runtime_->CreateVoice(asset);

    EXPECT_TRUE(created.ok());

    return created.ok() ? created.value() : Voice();
  }

  std::vector<float> Render(std::uint32_t frames,
                            std::uint32_t callback_frames) {
    std::vector<float> samples;

    samples.reserve(static_cast<std::size_t>(frames) * kChannels);

    for (std::uint32_t done = 0; done < frames;) {
      const std::uint32_t chunk = std::min(callback_frames, frames - done);
      AudioBuffer buffer(chunk, kChannels);

      EXPECT_TRUE(device_->PumpRender(buffer.View()));

      for (std::uint32_t f = 0; f < chunk; ++f) {
        for (std::uint32_t c = 0; c < kChannels; ++c) {
          samples.push_back(buffer(f, c));
        }
      }

      done += chunk;
    }

    return samples;
  }

 private:
  Voice MakeOscillator(float hz) {
    StatusOr<Voice> created = runtime_->CreateVoice();

    EXPECT_TRUE(created.ok());

    Voice voice = created.value();

    EXPECT_TRUE(voice.SetFrequency(hz).ok());
    EXPECT_TRUE(voice.SetGain(1.0f).ok());
    EXPECT_TRUE(voice.Start().ok());

    return voice;
  }

  test_support::FakeAudioDevice* device_ = nullptr;
  std::unique_ptr<AudioRuntime> runtime_;
  Voice main_;
  Voice bus_voice_;
  Bus bus_;
};

Command CmdRampVoice(VoiceId id, float target, std::uint32_t duration) {
  return {.type = CommandType::kRampVoiceGain,
          .value = target,
          .voice_id = id,
          .duration_frames = duration};
}

Command CmdRampBus(BusId id, float target, std::uint32_t duration) {
  return {.type = CommandType::kRampBusGain,
          .value = target,
          .bus_id = id,
          .duration_frames = duration};
}

Command CmdVoiceGain(VoiceId id, float value) {
  return {.type = CommandType::kSetVoiceGain, .value = value, .voice_id = id};
}

Command CmdStartVoice(VoiceId id) {
  return {.type = CommandType::kStartVoice, .voice_id = id};
}

double LinearRamp(double from, double to, std::uint64_t start,
                  std::uint64_t duration, std::uint64_t frame) {
  if (frame <= start) {
    return from;
  }

  if (frame >= start + duration) {
    return to;
  }

  return from + (to - from) * static_cast<double>(frame - start) /
                    static_cast<double>(duration);
}

template <typename GainFn>
void ExpectUnitTimesGain(const std::vector<float>& actual,
                         const std::vector<float>& unit, GainFn gain_at) {
  ASSERT_EQ(actual.size(), unit.size());

  for (std::size_t f = 0; f < actual.size() / kChannels; ++f) {
    const std::uint64_t frame = f;
    const double gain = gain_at(frame);

    for (std::size_t c = 0; c < kChannels; ++c) {
      const std::size_t index = f * kChannels + c;
      const float expected =
          static_cast<float>(static_cast<double>(unit[index]) * gain);

      ASSERT_NEAR(actual[index], expected, kTolerance)
          << "frame " << f << " channel " << c;
    }
  }
}

std::vector<float> RenderWithImmediates(Session& session,
                                        const std::vector<Step>& steps) {
  std::vector<float> samples;
  std::uint64_t cursor = 0;

  auto append = [&samples](const std::vector<float>& part) {
    samples.insert(samples.end(), part.begin(), part.end());
  };

  for (const Step& step : steps) {
    const std::uint64_t at = std::min<std::uint64_t>(step.target, kTotalFrames);

    if (at > cursor) {
      append(session.Render(static_cast<std::uint32_t>(at - cursor),
                            kTotalFrames));
      cursor = at;
    }

    EXPECT_TRUE(session.Submit(step.command).ok());
  }

  append(session.Render(static_cast<std::uint32_t>(kTotalFrames - cursor),
                        kTotalFrames));

  return samples;
}

void ScheduleAll(Session& session, const std::vector<Step>& steps) {
  for (const Step& step : steps) {
    ASSERT_TRUE(session.Schedule(step.command, step.target).ok());
  }
}

std::size_t FirstMismatchFrame(const std::vector<float>& actual,
                               const std::vector<float>& expected) {
  for (std::size_t i = 0; i < actual.size(); ++i) {
    if (actual[i] != expected[i]) {
      return i / kChannels;
    }
  }

  return kNoMismatch;
}

void ExpectIdentical(const std::vector<float>& actual,
                     const std::vector<float>& expected) {
  ASSERT_EQ(actual.size(), expected.size());
  EXPECT_EQ(FirstMismatchFrame(actual, expected), kNoMismatch);
}

bool AnyNonZeroFromFrame(const std::vector<float>& samples, std::size_t first) {
  for (std::size_t i = first * kChannels; i < samples.size(); ++i) {
    if (samples[i] != 0.0f) {
      return true;
    }
  }

  return false;
}

struct RampCase {
  std::uint64_t start;
  float target;
  std::uint32_t duration;
};

const RampCase kRampCases[] = {{0, 0.0f, 64}, {17, 0.25f, 50}, {63, 2.0f, 100},
                               {40, 0.5f, 1}, {10, 0.0f, 400}, {0, 0.0f, 192}};

// ---- voice gain ramps -------------------------------------------------------

TEST(ScheduledRampTest, VoiceRampFollowsTheLinearContract) {
  Session unit_session;
  const std::vector<float> unit =
      unit_session.Render(kTotalFrames, kTotalFrames);

  for (const RampCase& ramp : kRampCases) {
    for (std::uint32_t callback_frames : kCallbackSizes) {
      SCOPED_TRACE(testing::Message()
                   << "start " << ramp.start << ", target " << ramp.target
                   << ", duration " << ramp.duration << ", callback size "
                   << callback_frames);

      Session session;

      ASSERT_TRUE(session
                      .Schedule(CmdRampVoice(session.voice_id(), ramp.target,
                                             ramp.duration),
                                ramp.start)
                      .ok());

      ExpectUnitTimesGain(session.Render(kTotalFrames, callback_frames), unit,
                          [&ramp](std::uint64_t frame) {
                            return LinearRamp(1.0,
                                              static_cast<double>(ramp.target),
                                              ramp.start, ramp.duration, frame);
                          });
    }
  }
}

TEST(ScheduledRampTest, VoiceRampStartsExactlyOnTheTargetFrame) {
  for (std::uint64_t target : kTargets) {
    Session reference;
    const std::vector<float> expected = RenderWithImmediates(
        reference, {{target, CmdRampVoice(reference.voice_id(), 0.25f, 50)}});
    const bool executes = target < kTotalFrames;

    for (std::uint32_t callback_frames : kCallbackSizes) {
      SCOPED_TRACE(testing::Message() << "target frame " << target
                                      << ", callback size " << callback_frames);

      Session session;

      ASSERT_TRUE(
          session.Schedule(CmdRampVoice(session.voice_id(), 0.25f, 50), target)
              .ok());

      ExpectIdentical(session.Render(kTotalFrames, callback_frames), expected);

      const RuntimeStats stats = session.Stats();

      EXPECT_EQ(stats.scheduled_events_executed, executes ? 1u : 0u);
      EXPECT_EQ(stats.scheduled_events_pending, executes ? 0u : 1u);
      EXPECT_EQ(stats.scheduled_events_late, 0u);
    }
  }
}

TEST(ScheduledRampTest, ReplacementRampStartsFromTheExactCurrentValue) {
  Session unit_session;
  const std::vector<float> unit =
      unit_session.Render(kTotalFrames, kTotalFrames);

  const auto steps_for = [](VoiceId id) {
    return std::vector<Step>{{0, CmdRampVoice(id, 0.0f, 100)},
                             {50, CmdRampVoice(id, 1.0f, 50)}};
  };

  Session reference;
  const std::vector<float> replay =
      RenderWithImmediates(reference, steps_for(reference.voice_id()));

  for (std::uint32_t callback_frames : kCallbackSizes) {
    SCOPED_TRACE(testing::Message() << "callback size " << callback_frames);

    Session session;

    ScheduleAll(session, steps_for(session.voice_id()));

    const std::vector<float> actual =
        session.Render(kTotalFrames, callback_frames);

    ExpectIdentical(actual, replay);
    ExpectUnitTimesGain(actual, unit, [](std::uint64_t frame) {
      return frame < 50 ? LinearRamp(1.0, 0.0, 0, 100, frame)
                        : LinearRamp(0.5, 1.0, 50, 50, frame);
    });
  }
}

TEST(ScheduledRampTest, AStepGainCancelsARunningRampWithoutResidue) {
  Session unit_session;
  const std::vector<float> unit =
      unit_session.Render(kTotalFrames, kTotalFrames);

  for (std::uint32_t callback_frames : kCallbackSizes) {
    SCOPED_TRACE(testing::Message() << "callback size " << callback_frames);

    Session session;

    ASSERT_TRUE(
        session.Schedule(CmdRampVoice(session.voice_id(), 0.0f, 100), 0).ok());
    ASSERT_TRUE(
        session.Schedule(CmdVoiceGain(session.voice_id(), kStepGain), 40).ok());

    ExpectUnitTimesGain(session.Render(kTotalFrames, callback_frames), unit,
                        [](std::uint64_t frame) {
                          return frame < 40
                                     ? LinearRamp(1.0, 0.0, 0, 100, frame)
                                     : static_cast<double>(kStepGain);
                        });
  }
}

TEST(ScheduledRampTest, ZeroDurationRampEqualsAStepGain) {
  Session ramp_session;
  Session step_session;

  ASSERT_TRUE(
      ramp_session.Schedule(CmdRampVoice(ramp_session.voice_id(), 0.3f, 0), 40)
          .ok());
  ASSERT_TRUE(
      step_session.Schedule(CmdVoiceGain(step_session.voice_id(), 0.3f), 40)
          .ok());

  ExpectIdentical(ramp_session.Render(kTotalFrames, 64),
                  step_session.Render(kTotalFrames, 64));
}

TEST(ScheduledRampTest, ImmediateRampStartsAtTheNextCallback) {
  Session unit_session;
  const std::vector<float> unit =
      unit_session.Render(kTotalFrames, kTotalFrames);

  constexpr std::uint32_t kFirst = 10;

  Session session;
  std::vector<float> actual = session.Render(kFirst, kFirst);

  ASSERT_TRUE(session.main_voice().RampGain(0.0f, 100).ok());

  const std::vector<float> rest = session.Render(kTotalFrames - kFirst, 64);

  actual.insert(actual.end(), rest.begin(), rest.end());

  ExpectUnitTimesGain(actual, unit, [](std::uint64_t frame) {
    return LinearRamp(1.0, 0.0, kFirst, 100, frame);
  });
}

// ---- bus gain ramps ---------------------------------------------------------

TEST(ScheduledRampTest, UserBusRampFollowsTheLinearContract) {
  const Options options{.main_voice = false, .bus_voice = true};

  Session unit_session(options);
  const std::vector<float> unit =
      unit_session.Render(kTotalFrames, kTotalFrames);

  Session reference(options);
  const std::vector<float> replay = RenderWithImmediates(
      reference, {{20, CmdRampBus(reference.bus_id(), 0.0f, 64)}});

  for (std::uint32_t callback_frames : kCallbackSizes) {
    SCOPED_TRACE(testing::Message() << "callback size " << callback_frames);

    Session session(options);

    ASSERT_TRUE(
        session.Schedule(CmdRampBus(session.bus_id(), 0.0f, 64), 20).ok());

    const std::vector<float> actual =
        session.Render(kTotalFrames, callback_frames);

    ExpectIdentical(actual, replay);
    ExpectUnitTimesGain(actual, unit, [](std::uint64_t frame) {
      return LinearRamp(1.0, 0.0, 20, 64, frame);
    });
  }
}

TEST(ScheduledRampTest, MasterBusRampFollowsTheLinearContract) {
  Session unit_session;
  const std::vector<float> unit =
      unit_session.Render(kTotalFrames, kTotalFrames);

  Session reference;
  const std::vector<float> replay = RenderWithImmediates(
      reference, {{30, CmdRampBus(kMasterBusId, 0.25f, 100)}});

  for (std::uint32_t callback_frames : kCallbackSizes) {
    SCOPED_TRACE(testing::Message() << "callback size " << callback_frames);

    Session session;

    ASSERT_TRUE(
        session.Schedule(CmdRampBus(kMasterBusId, 0.25f, 100), 30).ok());

    const std::vector<float> actual =
        session.Render(kTotalFrames, callback_frames);

    ExpectIdentical(actual, replay);
    ExpectUnitTimesGain(actual, unit, [](std::uint64_t frame) {
      return LinearRamp(1.0, 0.25, 30, 100, frame);
    });
  }
}

// ---- asset-backed voices ----------------------------------------------------

TEST(ScheduledRampTest, RampOnAStereoAssetVoiceFollowsTheContract) {
  constexpr std::uint32_t kRampFrames = 4;

  Session unit_session(Options{.main_voice = false});
  Voice unit_voice =
      unit_session.MakeAssetVoice(unit_session.LoadAsset("stereo_pcm16.wav"));

  ASSERT_TRUE(unit_session.Schedule(CmdStartVoice(unit_voice.id()), 0).ok());

  const std::vector<float> unit =
      unit_session.Render(kTotalFrames, kTotalFrames);

  ASSERT_TRUE(AnyNonZeroFromFrame(unit, 1))
      << "the fixture must have signal after frame 0 for the ramp to matter";

  for (std::uint32_t callback_frames : kCallbackSizes) {
    SCOPED_TRACE(testing::Message() << "callback size " << callback_frames);

    Session session(Options{.main_voice = false});
    Voice voice = session.MakeAssetVoice(session.LoadAsset("stereo_pcm16.wav"));

    ASSERT_TRUE(session.Schedule(CmdStartVoice(voice.id()), 0).ok());
    ASSERT_TRUE(
        session.Schedule(CmdRampVoice(voice.id(), 0.0f, kRampFrames), 0).ok());

    ExpectUnitTimesGain(session.Render(kTotalFrames, callback_frames), unit,
                        [](std::uint64_t frame) {
                          return LinearRamp(1.0, 0.0, 0, kRampFrames, frame);
                        });
  }
}

// ---- stale ids and validation -----------------------------------------------

TEST(ScheduledRampTest, RampForADestroyedVoiceNeverTouchesAReusedSlot) {
  Session reference;
  reference.ReplaceMainVoice();

  const std::vector<float> expected =
      reference.Render(kTotalFrames, kTotalFrames);

  Session session;

  const VoiceId stale = session.voice_id();

  ASSERT_TRUE(session.Schedule(CmdRampVoice(stale, 0.0f, 10), 20).ok());

  session.ReplaceMainVoice();

  ExpectIdentical(session.Render(kTotalFrames, kTotalFrames), expected);

  EXPECT_EQ(session.Stats().scheduled_events_executed, 1u);
}

TEST(ScheduledRampTest, RampCommandsAreValidatedLikeGainCommands) {
  Session session;

  const float bad_targets[] = {kNaN, kInf, -kInf, -0.5f};

  for (float target : bad_targets) {
    SCOPED_TRACE(target);

    EXPECT_EQ(session.Schedule(CmdRampVoice(session.voice_id(), target, 10), 30)
                  .code(),
              ErrorCode::kInvalidArgument);
    EXPECT_EQ(session.Schedule(CmdRampBus(kMasterBusId, target, 10), 30).code(),
              ErrorCode::kInvalidArgument);
    EXPECT_FALSE(session.main_voice().RampGain(target, 10).ok());
    EXPECT_FALSE(session.runtime().MasterBus().RampGain(target, 10).ok());
  }

  EXPECT_EQ(session.Schedule(CmdRampVoice(VoiceId{}, 0.5f, 10), 30).code(),
            ErrorCode::kInvalidArgument);
  EXPECT_EQ(session.Schedule(CmdRampBus(BusId{}, 0.5f, 10), 30).code(),
            ErrorCode::kInvalidArgument);
  EXPECT_FALSE(Voice().RampGain(0.5f, 10).ok());
  EXPECT_FALSE(Bus().RampGain(0.5f, 10).ok());

  RuntimeStats stats = session.Stats();

  EXPECT_EQ(stats.scheduled_events_accepted, 0u);
  EXPECT_EQ(stats.scheduled_events_capacity_failures, 0u);

  EXPECT_TRUE(
      session
          .Schedule(CmdRampVoice(session.voice_id(), 0.5f,
                                 std::numeric_limits<std::uint32_t>::max()),
                    30)
          .ok());
  EXPECT_TRUE(session.Schedule(CmdRampBus(kMasterBusId, 0.5f, 0), 30).ok());

  stats = session.Stats();

  EXPECT_EQ(stats.scheduled_events_accepted, 2u);
}

}  // namespace
}  // namespace lavanda
