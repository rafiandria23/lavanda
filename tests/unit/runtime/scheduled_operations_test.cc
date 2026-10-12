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
#include "lavanda/core/time.h"
#include "lavanda/resources/audio_asset_id.h"
#include "lavanda/resources/audio_asset_info.h"
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
constexpr float kVoiceHz = 440.0f;
constexpr float kRoutedHz = 330.0f;
constexpr float kVoiceGain = 0.5f;
constexpr float kRoutedGain = 0.5f;
constexpr std::size_t kNoMismatch = std::numeric_limits<std::size_t>::max();
constexpr std::uint32_t kCallbackSizes[] = {192, 64, 7};
constexpr std::uint64_t kTargets[] = {0, 1, 17, 63, 64, 65, 128, 191, 192, 500};
constexpr const char* kMono = "mono_pcm16.wav";
constexpr const char* kStereo = "stereo_pcm16.wav";

struct Options {
  bool start_voice = true;
  bool oscillators = true;
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

    if (!options.oscillators) {
      return;
    }

    voice_ = MakeOscillator(kVoiceHz, kVoiceGain, options.start_voice);

    StatusOr<Bus> bus = runtime_->CreateBus();

    EXPECT_TRUE(bus.ok());

    bus_ = bus.value();
    routed_ = MakeOscillator(kRoutedHz, kRoutedGain, true);

    EXPECT_TRUE(routed_.SetBus(bus_).ok());
  }

  AudioRuntime& runtime() { return *runtime_; }
  VoiceId voice_id() const { return voice_.id(); }
  BusId bus_id() const { return bus_.id(); }
  RuntimeStats Stats() const { return runtime_->stats(); }

  Status Schedule(const Command& command, std::uint64_t frame) {
    return runtime_->ScheduleAtFrame(command, FramePosition{frame});
  }

  Status Submit(const Command& command) { return runtime_->Submit(command); }

  VoiceId ReplaceMainVoice() {
    const VoiceId old = voice_.id();

    EXPECT_TRUE(voice_.Destroy().ok());

    voice_ = MakeOscillator(kVoiceHz, kVoiceGain, true);

    EXPECT_EQ(voice_.id().index, old.index);
    EXPECT_NE(voice_.id().generation, old.generation);

    return old;
  }

  BusId ReplaceUserBus() {
    const BusId old = bus_.id();

    EXPECT_TRUE(bus_.Destroy().ok());

    StatusOr<Bus> bus = runtime_->CreateBus();

    EXPECT_TRUE(bus.ok());

    bus_ = bus.value();

    EXPECT_EQ(bus_.id().index, old.index);
    EXPECT_NE(bus_.id().generation, old.generation);
    EXPECT_TRUE(routed_.SetBus(bus_).ok());

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

  std::uint32_t AssetFrames(AudioAssetId id) {
    StatusOr<AudioAssetInfo> info = runtime_->GetAudioAssetInfo(id);

    EXPECT_TRUE(info.ok());

    return info.ok() ? info.value().frame_count : 0;
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
  Voice MakeOscillator(float hz, float gain, bool start) {
    StatusOr<Voice> created = runtime_->CreateVoice();

    EXPECT_TRUE(created.ok());

    Voice voice = created.value();

    EXPECT_TRUE(voice.SetFrequency(hz).ok());
    EXPECT_TRUE(voice.SetGain(gain).ok());

    if (start) {
      EXPECT_TRUE(voice.Start().ok());
    }

    return voice;
  }

  test_support::FakeAudioDevice* device_ = nullptr;
  std::unique_ptr<AudioRuntime> runtime_;
  Voice voice_;
  Voice routed_;
  Bus bus_;
};

Command CmdVoiceGain(VoiceId id, float value) {
  return {.type = CommandType::kSetVoiceGain, .value = value, .voice_id = id};
}

Command CmdVoicePan(VoiceId id, float value) {
  return {.type = CommandType::kSetVoicePan, .value = value, .voice_id = id};
}

Command CmdVoiceFrequency(VoiceId id, float value) {
  return {
      .type = CommandType::kSetVoiceFrequency, .value = value, .voice_id = id};
}

Command CmdStartVoice(VoiceId id) {
  return {.type = CommandType::kStartVoice, .voice_id = id};
}

Command CmdStopVoice(VoiceId id) {
  return {.type = CommandType::kStopVoice, .voice_id = id};
}

Command CmdBusGain(BusId id, float value) {
  return {.type = CommandType::kSetBusGain, .value = value, .bus_id = id};
}

struct Scenario {
  const char* name;
  Options options;
  Command (*make)(const Session&);
};

const Scenario kScenarios[] = {
    {"VoiceGain", Options{},
     [](const Session& s) { return CmdVoiceGain(s.voice_id(), 0.1f); }},
    {"VoicePan", Options{},
     [](const Session& s) { return CmdVoicePan(s.voice_id(), 0.8f); }},
    {"VoiceFrequency", Options{},
     [](const Session& s) { return CmdVoiceFrequency(s.voice_id(), 880.0f); }},
    {"VoiceStart", Options{.start_voice = false},
     [](const Session& s) { return CmdStartVoice(s.voice_id()); }},
    {"VoiceStop", Options{},
     [](const Session& s) { return CmdStopVoice(s.voice_id()); }},
    {"UserBusGain", Options{},
     [](const Session& s) { return CmdBusGain(s.bus_id(), 0.2f); }},
    {"MasterBusGain", Options{},
     [](const Session&) { return CmdBusGain(kMasterBusId, 0.3f); }},
};

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

void ScheduleInOrder(Session& session, const std::vector<Step>& steps,
                     const std::vector<std::size_t>& order) {
  for (std::size_t index : order) {
    ASSERT_TRUE(
        session.Schedule(steps[index].command, steps[index].target).ok());
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

bool AllZeroInFrames(const std::vector<float>& samples, std::size_t first,
                     std::size_t last) {
  for (std::size_t f = first; f < last; ++f) {
    for (std::size_t c = 0; c < kChannels; ++c) {
      if (samples[f * kChannels + c] != 0.0f) {
        return false;
      }
    }
  }

  return true;
}

bool AnyNonZeroFromFrame(const std::vector<float>& samples, std::size_t first) {
  return !AllZeroInFrames(samples, first, samples.size() / kChannels);
}

void ExpectEventCounts(const Session& session, std::uint64_t executed,
                       std::uint64_t pending) {
  const RuntimeStats stats = session.Stats();

  EXPECT_EQ(stats.scheduled_events_executed, executed);
  EXPECT_EQ(stats.scheduled_events_pending, pending);
  EXPECT_EQ(stats.scheduled_events_late, 0u);
  EXPECT_EQ(stats.scheduled_events_dropped, 0u);
  EXPECT_EQ(stats.scheduled_events_accepted, executed + pending);
}

// ---- every operation, exact frame ------------------------------------------

TEST(ScheduledOperationsTest, EveryOperationLandsExactlyOnTheTargetFrame) {
  for (const Scenario& scenario : kScenarios) {
    Session baseline_session(scenario.options);
    const std::vector<float> baseline =
        baseline_session.Render(kTotalFrames, kTotalFrames);

    for (std::uint64_t target : kTargets) {
      Session reference(scenario.options);
      const std::vector<float> expected =
          RenderWithImmediates(reference, {{target, scenario.make(reference)}});
      const std::size_t effective =
          std::min<std::uint64_t>(target, kTotalFrames);
      const bool executes = target < kTotalFrames;

      for (std::uint32_t callback_frames : kCallbackSizes) {
        SCOPED_TRACE(testing::Message()
                     << scenario.name << ", target frame " << target
                     << ", callback size " << callback_frames);

        Session session(scenario.options);

        ASSERT_TRUE(session.Schedule(scenario.make(session), target).ok());

        const std::vector<float> actual =
            session.Render(kTotalFrames, callback_frames);

        ExpectIdentical(actual, expected);

        EXPECT_GE(FirstMismatchFrame(actual, baseline), effective)
            << "the operation must have no effect before its target frame";

        ExpectEventCounts(session, executes ? 1u : 0u, executes ? 0u : 1u);
      }
    }
  }
}

TEST(ScheduledOperationsTest, EveryScenarioIsObservableAfterItsTarget) {
  constexpr std::uint64_t kTarget = 40;

  for (const Scenario& scenario : kScenarios) {
    SCOPED_TRACE(scenario.name);

    Session baseline_session(scenario.options);
    const std::vector<float> baseline =
        baseline_session.Render(kTotalFrames, kTotalFrames);

    Session reference(scenario.options);
    const std::vector<float> changed =
        RenderWithImmediates(reference, {{kTarget, scenario.make(reference)}});

    const std::size_t first = FirstMismatchFrame(baseline, changed);

    ASSERT_NE(first, kNoMismatch) << "the scenario changes nothing audible";
    EXPECT_GE(first, kTarget);
  }
}

// ---- ordering across operations ---------------------------------------------

std::vector<Step> MixedSteps(const Session& s) {
  return {{10, CmdVoiceGain(s.voice_id(), 0.1f)},
          {40, CmdVoicePan(s.voice_id(), 0.8f)},
          {40, CmdVoiceGain(s.voice_id(), 0.9f)},
          {40, CmdVoiceGain(s.voice_id(), 0.2f)},
          {90, CmdVoiceFrequency(s.voice_id(), 880.0f)},
          {120, CmdBusGain(s.bus_id(), 0.3f)},
          {150, CmdStopVoice(s.voice_id())}};
}

TEST(ScheduledOperationsTest, MixedOperationsMatchAnImmediateReplay) {
  Session baseline_session;
  const std::vector<float> baseline =
      baseline_session.Render(kTotalFrames, kTotalFrames);

  Session reference;
  const std::vector<float> expected =
      RenderWithImmediates(reference, MixedSteps(reference));

  ASSERT_NE(expected, baseline);

  const std::vector<std::size_t> order = {6, 2, 3, 5, 1, 4, 0};

  for (std::uint32_t callback_frames : kCallbackSizes) {
    SCOPED_TRACE(testing::Message() << "callback size " << callback_frames);

    Session session;

    ScheduleInOrder(session, MixedSteps(session), order);
    ExpectIdentical(session.Render(kTotalFrames, callback_frames), expected);
    ExpectEventCounts(session, 7, 0);
  }
}

// ---- stale ids --------------------------------------------------------------

TEST(ScheduledOperationsTest, EventsForADestroyedVoiceNeverTouchAReusedSlot) {
  Session reference;
  reference.ReplaceMainVoice();

  const std::vector<float> expected =
      reference.Render(kTotalFrames, kTotalFrames);

  for (bool schedule_after_replace : {false, true}) {
    SCOPED_TRACE(testing::Message()
                 << "scheduled after replace: " << schedule_after_replace);

    Session session;

    const VoiceId stale = session.voice_id();
    const std::vector<Step> stale_events = {
        {20, CmdVoiceGain(stale, 0.0f)},
        {30, CmdVoicePan(stale, 1.0f)},
        {40, CmdVoiceFrequency(stale, 880.0f)},
        {50, CmdStopVoice(stale)}};

    if (!schedule_after_replace) {
      ScheduleAll(session, stale_events);
    }

    const VoiceId old = session.ReplaceMainVoice();

    ASSERT_TRUE(old == stale);

    if (schedule_after_replace) {
      ScheduleAll(session, stale_events);
    }

    ExpectIdentical(session.Render(kTotalFrames, kTotalFrames), expected);
    ExpectEventCounts(session, 4, 0);
  }
}

TEST(ScheduledOperationsTest, EventsForADestroyedBusNeverTouchAReusedSlot) {
  Session reference;
  reference.ReplaceUserBus();

  const std::vector<float> expected =
      reference.Render(kTotalFrames, kTotalFrames);

  for (bool schedule_after_replace : {false, true}) {
    SCOPED_TRACE(testing::Message()
                 << "scheduled after replace: " << schedule_after_replace);

    Session session;

    const BusId stale = session.bus_id();
    const std::vector<Step> stale_events = {{50, CmdBusGain(stale, 0.0f)}};

    if (!schedule_after_replace) {
      ScheduleAll(session, stale_events);
    }

    const BusId old = session.ReplaceUserBus();

    ASSERT_TRUE(old == stale);

    if (schedule_after_replace) {
      ScheduleAll(session, stale_events);
    }

    ExpectIdentical(session.Render(kTotalFrames, kTotalFrames), expected);
    ExpectEventCounts(session, 1, 0);
  }
}

// ---- asset-backed voices ----------------------------------------------------

TEST(ScheduledOperationsTest, AssetVoiceStartsAtTheTargetFrame) {
  const std::uint64_t targets[] = {0, 1, 17, 63, 64, 100};

  for (std::uint64_t target : targets) {
    Session reference(Options{.oscillators = false});
    const Voice reference_voice =
        reference.MakeAssetVoice(reference.LoadAsset(kMono));
    const std::vector<float> expected = RenderWithImmediates(
        reference, {{target, CmdStartVoice(reference_voice.id())}});

    for (std::uint32_t callback_frames : kCallbackSizes) {
      SCOPED_TRACE(testing::Message() << "target frame " << target
                                      << ", callback size " << callback_frames);

      Session session(Options{.oscillators = false});
      Voice voice = session.MakeAssetVoice(session.LoadAsset(kMono));

      ASSERT_TRUE(session.Schedule(CmdStartVoice(voice.id()), target).ok());

      const std::vector<float> actual =
          session.Render(kTotalFrames, callback_frames);

      ExpectIdentical(actual, expected);

      EXPECT_TRUE(AllZeroInFrames(actual, 0, target));
      EXPECT_TRUE(AnyNonZeroFromFrame(actual, target));
      EXPECT_FALSE(voice.IsPlaying()) << "the asset finished inside the render";

      ExpectEventCounts(session, 1, 0);
    }
  }
}

TEST(ScheduledOperationsTest, FinishedAssetVoiceIsRestartedByALaterEvent) {
  Session reference(Options{.oscillators = false});
  const AudioAssetId reference_asset = reference.LoadAsset(kMono);
  const std::uint32_t frames = reference.AssetFrames(reference_asset);

  ASSERT_GT(frames, 0u);
  ASSERT_LT(2 * frames + 40, kTotalFrames);

  const std::uint64_t first = 10;
  const std::uint64_t second = first + frames + 20;
  const Voice reference_voice = reference.MakeAssetVoice(reference_asset);
  const std::vector<float> expected = RenderWithImmediates(
      reference, {{first, CmdStartVoice(reference_voice.id())},
                  {second, CmdStartVoice(reference_voice.id())}});

  for (std::uint32_t callback_frames : kCallbackSizes) {
    SCOPED_TRACE(testing::Message() << "callback size " << callback_frames);

    Session session(Options{.oscillators = false});
    Voice voice = session.MakeAssetVoice(session.LoadAsset(kMono));

    ASSERT_TRUE(session.Schedule(CmdStartVoice(voice.id()), second).ok());
    ASSERT_TRUE(session.Schedule(CmdStartVoice(voice.id()), first).ok());

    const std::vector<float> actual =
        session.Render(kTotalFrames, callback_frames);

    ExpectIdentical(actual, expected);

    EXPECT_TRUE(AllZeroInFrames(actual, first + frames, second))
        << "silent between the end of the first play and the restart";
    EXPECT_TRUE(AnyNonZeroFromFrame(actual, second));

    ExpectEventCounts(session, 2, 0);
  }
}

TEST(ScheduledOperationsTest, ScheduledStopRewindsAnAssetVoice) {
  Session probe(Options{.oscillators = false});
  const AudioAssetId probe_asset = probe.LoadAsset(kMono);
  const std::uint32_t frames = probe.AssetFrames(probe_asset);

  ASSERT_GE(frames, 8u);

  const std::uint64_t stop_at = frames / 2;
  const std::uint64_t restart_at = stop_at + 10;

  Session plain(Options{.oscillators = false});
  Voice plain_voice = plain.MakeAssetVoice(plain.LoadAsset(kMono));

  ASSERT_TRUE(plain.Schedule(CmdStartVoice(plain_voice.id()), 0).ok());

  const std::vector<float> uninterrupted =
      plain.Render(kTotalFrames, kTotalFrames);

  const auto steps_for = [&](VoiceId id) {
    return std::vector<Step>{{0, CmdStartVoice(id)},
                             {stop_at, CmdStopVoice(id)},
                             {restart_at, CmdStartVoice(id)}};
  };

  Session reference(Options{.oscillators = false});
  const Voice reference_voice =
      reference.MakeAssetVoice(reference.LoadAsset(kMono));
  const std::vector<float> expected =
      RenderWithImmediates(reference, steps_for(reference_voice.id()));

  for (std::uint32_t callback_frames : kCallbackSizes) {
    SCOPED_TRACE(testing::Message() << "callback size " << callback_frames);

    Session session(Options{.oscillators = false});
    Voice voice = session.MakeAssetVoice(session.LoadAsset(kMono));

    ScheduleAll(session, steps_for(voice.id()));

    const std::vector<float> actual =
        session.Render(kTotalFrames, callback_frames);

    ExpectIdentical(actual, expected);

    for (std::size_t i = 0; i < frames; ++i) {
      for (std::size_t c = 0; c < kChannels; ++c) {
        ASSERT_EQ(actual[(restart_at + i) * kChannels + c],
                  uninterrupted[i * kChannels + c])
            << "restart frame offset " << i;
      }
    }

    ExpectEventCounts(session, 3, 0);
  }
}

TEST(ScheduledOperationsTest, ScheduledPanOnAStereoAssetIsFrameExact) {
  Session probe(Options{.oscillators = false});
  const std::uint32_t frames = probe.AssetFrames(probe.LoadAsset(kStereo));

  ASSERT_GE(frames, 4u);

  bool left_channel_was_exercised = false;

  for (std::uint64_t target = 1; target < frames; ++target) {
    const auto steps_for = [&](VoiceId id) {
      return std::vector<Step>{{0, CmdStartVoice(id)},
                               {target, CmdVoicePan(id, 1.0f)}};
    };

    Session plain(Options{.oscillators = false});
    Voice plain_voice = plain.MakeAssetVoice(plain.LoadAsset(kStereo));

    ASSERT_TRUE(plain.Schedule(CmdStartVoice(plain_voice.id()), 0).ok());

    const std::vector<float> uninterrupted =
        plain.Render(kTotalFrames, kTotalFrames);

    for (std::size_t f = target; f < frames; ++f) {
      if (uninterrupted[f * kChannels] != 0.0f) {
        left_channel_was_exercised = true;
      }
    }

    Session reference(Options{.oscillators = false});
    const Voice reference_voice =
        reference.MakeAssetVoice(reference.LoadAsset(kStereo));
    const std::vector<float> expected =
        RenderWithImmediates(reference, steps_for(reference_voice.id()));

    for (std::uint32_t callback_frames : kCallbackSizes) {
      SCOPED_TRACE(testing::Message() << "target frame " << target
                                      << ", callback size " << callback_frames);

      Session session(Options{.oscillators = false});
      Voice voice = session.MakeAssetVoice(session.LoadAsset(kStereo));

      ScheduleAll(session, steps_for(voice.id()));

      const std::vector<float> actual =
          session.Render(kTotalFrames, callback_frames);

      ExpectIdentical(actual, expected);

      for (std::size_t f = 0; f < target; ++f) {
        EXPECT_EQ(actual[f * kChannels], uninterrupted[f * kChannels]);
        EXPECT_EQ(actual[f * kChannels + 1], uninterrupted[f * kChannels + 1]);
      }

      for (std::size_t f = target; f < frames; ++f) {
        EXPECT_NEAR(actual[f * kChannels], 0.0f, 1e-6f)
            << "pan +1 silences the left channel from the target frame on";
      }
    }
  }

  EXPECT_TRUE(left_channel_was_exercised)
      << "the stereo fixture has no left-channel signal after frame 0, so "
         "this test could not detect a pan applied at the wrong frame";
}

TEST(ScheduledOperationsTest, ScheduledStartSurvivesReleaseOfTheAsset) {
  const std::uint64_t target = 20;

  Session reference(Options{.oscillators = false});
  const AudioAssetId reference_asset = reference.LoadAsset(kMono);
  const Voice reference_voice = reference.MakeAssetVoice(reference_asset);

  ASSERT_TRUE(reference.runtime().ReleaseAudioAsset(reference_asset).ok());

  const std::vector<float> expected = RenderWithImmediates(
      reference, {{target, CmdStartVoice(reference_voice.id())}});

  Session session(Options{.oscillators = false});
  const AudioAssetId asset = session.LoadAsset(kMono);
  Voice voice = session.MakeAssetVoice(asset);

  ASSERT_TRUE(session.runtime().ReleaseAudioAsset(asset).ok());
  ASSERT_TRUE(session.Schedule(CmdStartVoice(voice.id()), target).ok());

  const std::vector<float> actual = session.Render(kTotalFrames, 64);

  ExpectIdentical(actual, expected);

  EXPECT_TRUE(AnyNonZeroFromFrame(actual, target))
      << "the voice's pin keeps the released asset alive until it plays";

  ExpectEventCounts(session, 1, 0);
}

TEST(ScheduledOperationsTest, DestroyedAssetVoiceDropsItsScheduledStart) {
  Session session(Options{.oscillators = false});
  const AudioAssetId asset = session.LoadAsset(kMono);
  Voice voice = session.MakeAssetVoice(asset);

  ASSERT_TRUE(session.runtime().ReleaseAudioAsset(asset).ok());
  ASSERT_TRUE(session.Schedule(CmdStartVoice(voice.id()), 50).ok());
  ASSERT_TRUE(voice.Destroy().ok());

  const std::vector<float> actual = session.Render(kTotalFrames, 64);

  EXPECT_TRUE(AllZeroInFrames(actual, 0, kTotalFrames))
      << "the stale start must not resurrect the voice";

  ExpectEventCounts(session, 1, 0);

  session.runtime().ReclaimAssets();

  const RuntimeStats stats = session.Stats();

  EXPECT_EQ(stats.retiring_asset_count, 0u);
  EXPECT_EQ(stats.resident_asset_count, 0u);
}

}  // namespace
}  // namespace lavanda
