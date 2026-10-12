#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <utility>
#include <vector>

#include "lavanda/core/audio_buffer.h"
#include "lavanda/core/time.h"
#include "lavanda/runtime/audio_runtime.h"
#include "lavanda/runtime/command.h"
#include "lavanda/runtime/mixing.h"
#include "test_support/fake_audio_device.h"

namespace lavanda {
namespace {

constexpr std::uint32_t kChannels = 2;
constexpr std::uint32_t kTotalFrames = 192;
constexpr std::uint32_t kBlockFrames = 64;
constexpr float kGainA = 0.25f;
constexpr float kGainB = 0.75f;
constexpr float kGainC = 0.5f;
constexpr std::size_t kNoMismatch = std::numeric_limits<std::size_t>::max();

struct Event {
  std::uint64_t frame;
  float gain;
};

class Rig {
 public:
  explicit Rig(float initial_gain) {
    auto device = std::make_unique<test_support::FakeAudioDevice>();

    device_ = device.get();
    runtime_ = std::make_unique<AudioRuntime>(std::move(device));

    EXPECT_TRUE(runtime_->Start().ok());

    StatusOr<Voice> voice = runtime_->CreateVoice();

    EXPECT_TRUE(voice.ok());

    voice_ = voice.value();

    EXPECT_TRUE(voice_.SetGain(initial_gain).ok());
    EXPECT_TRUE(voice_.Start().ok());
  }

  Status Schedule(std::uint64_t frame, float gain) {
    return runtime_->ScheduleAtFrame({.type = CommandType::kSetVoiceGain,
                                      .value = gain,
                                      .voice_id = voice_.id()},
                                     FramePosition{frame});
  }

  Voice& voice() { return voice_; }

  std::vector<float> Render(std::uint32_t frames,
                            std::uint32_t block_frames = kBlockFrames) {
    std::vector<float> samples;

    samples.reserve(static_cast<std::size_t>(frames) * kChannels);

    for (std::uint32_t done = 0; done < frames; done += block_frames) {
      AudioBuffer buffer(block_frames, kChannels);

      EXPECT_TRUE(device_->PumpRender(buffer.View()));

      for (std::uint32_t f = 0; f < block_frames; ++f) {
        for (std::uint32_t c = 0; c < kChannels; ++c) {
          samples.push_back(buffer(f, c));
        }
      }
    }

    return samples;
  }

 private:
  test_support::FakeAudioDevice* device_ = nullptr;
  std::unique_ptr<AudioRuntime> runtime_;
  Voice voice_;
};

std::vector<float> RenderConstantGain(float gain) {
  Rig rig(gain);

  return rig.Render(kTotalFrames);
}

const std::vector<float>& Reference(float gain) {
  static const std::vector<float> a = RenderConstantGain(kGainA);
  static const std::vector<float> b = RenderConstantGain(kGainB);
  static const std::vector<float> c = RenderConstantGain(kGainC);

  if (gain == kGainB) {
    return b;
  }

  if (gain == kGainC) {
    return c;
  }

  return a;
}

std::vector<float> Expected(float initial_gain,
                            const std::vector<Event>& events) {
  std::vector<float> expected(static_cast<std::size_t>(kTotalFrames) *
                              kChannels);

  for (std::uint32_t f = 0; f < kTotalFrames; ++f) {
    float gain = initial_gain;

    for (const Event& event : events) {
      if (event.frame <= f) {
        gain = event.gain;
      }
    }

    const std::vector<float>& reference = Reference(gain);

    for (std::uint32_t c = 0; c < kChannels; ++c) {
      const std::size_t index = static_cast<std::size_t>(f) * kChannels + c;

      expected[index] = reference[index];
    }
  }

  return expected;
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

bool AnyNonZero(const std::vector<float>& samples) {
  for (float sample : samples) {
    if (sample != 0.0f) {
      return true;
    }
  }

  return false;
}

void ExpectMatches(const std::vector<float>& actual,
                   const std::vector<float>& expected) {
  ASSERT_EQ(actual.size(), expected.size());
  EXPECT_EQ(FirstMismatchFrame(actual, expected), kNoMismatch);
}

TEST(ScheduledTimingTest, ReferenceRendersAreAudibleAndDistinct) {
  EXPECT_TRUE(AnyNonZero(Reference(kGainA)));
  EXPECT_TRUE(AnyNonZero(Reference(kGainB)));
  EXPECT_TRUE(AnyNonZero(Reference(kGainC)));
  EXPECT_NE(Reference(kGainA), Reference(kGainB));
  EXPECT_NE(Reference(kGainB), Reference(kGainC));
}

TEST(ScheduledTimingTest, GainChangeLandsExactlyOnTheTargetFrame) {
  const std::uint64_t targets[] = {0, 1, 17, 63, 64, 65, 128, 191, 192, 500};

  for (std::uint64_t target : targets) {
    SCOPED_TRACE(testing::Message() << "target frame " << target);

    Rig rig(kGainA);

    ASSERT_TRUE(rig.Schedule(target, kGainB).ok());

    ExpectMatches(rig.Render(kTotalFrames),
                  Expected(kGainA, {{target, kGainB}}));
  }
}

TEST(ScheduledTimingTest, MultipleEventsInAndAcrossCallbacks) {
  const std::vector<Event> events = {
      {10, kGainB}, {40, kGainC}, {64, kGainA}, {100, kGainC}, {191, kGainB}};

  Rig rig(kGainA);

  for (const Event& event : events) {
    ASSERT_TRUE(rig.Schedule(event.frame, event.gain).ok());
  }

  ExpectMatches(rig.Render(kTotalFrames), Expected(kGainA, events));
}

TEST(ScheduledTimingTest, OutOfOrderSubmissionExecutesInTimeOrder) {
  Rig rig(kGainA);

  ASSERT_TRUE(rig.Schedule(100, kGainC).ok());
  ASSERT_TRUE(rig.Schedule(20, kGainB).ok());
  ASSERT_TRUE(rig.Schedule(60, kGainA).ok());

  ExpectMatches(rig.Render(kTotalFrames),
                Expected(kGainA, {{20, kGainB}, {60, kGainA}, {100, kGainC}}));
}

TEST(ScheduledTimingTest, SameFrameEventsApplyInSubmissionOrder) {
  {
    Rig rig(kGainA);

    ASSERT_TRUE(rig.Schedule(50, kGainB).ok());
    ASSERT_TRUE(rig.Schedule(50, kGainC).ok());

    ExpectMatches(rig.Render(kTotalFrames),
                  Expected(kGainA, {{50, kGainB}, {50, kGainC}}));
  }

  {
    Rig rig(kGainA);

    ASSERT_TRUE(rig.Schedule(50, kGainC).ok());
    ASSERT_TRUE(rig.Schedule(50, kGainB).ok());

    ExpectMatches(rig.Render(kTotalFrames),
                  Expected(kGainA, {{50, kGainC}, {50, kGainB}}));
  }
}

TEST(ScheduledTimingTest, LateEventAppliesAtTheStartOfTheNextCallback) {
  Rig rig(kGainA);

  std::vector<float> samples = rig.Render(kBlockFrames);

  ASSERT_TRUE(rig.Schedule(10, kGainB).ok());

  const std::vector<float> rest = rig.Render(kTotalFrames - kBlockFrames);

  samples.insert(samples.end(), rest.begin(), rest.end());

  ExpectMatches(samples, Expected(kGainA, {{kBlockFrames, kGainB}}));
}

TEST(ScheduledTimingTest,
     ImmediateCommandsApplyBeforeScheduledEventsAtTheSameFrame) {
  Rig rig(kGainA);

  ASSERT_TRUE(rig.Schedule(0, kGainB).ok());
  ASSERT_TRUE(rig.voice().SetGain(kGainC).ok());

  ExpectMatches(rig.Render(kTotalFrames), Expected(kGainA, {{0, kGainB}}));
}

TEST(ScheduledTimingTest, ResultIsIndependentOfCallbackSize) {
  const std::vector<Event> events = {
      {10, kGainB}, {40, kGainC}, {64, kGainA}, {100, kGainC}, {191, kGainB}};
  const std::vector<float> expected = Expected(kGainA, events);
  const std::uint32_t block_sizes[] = {1, 3, 8, 64, 192};

  for (std::uint32_t block_frames : block_sizes) {
    SCOPED_TRACE(testing::Message() << "callback size " << block_frames);

    Rig rig(kGainA);

    for (const Event& event : events) {
      ASSERT_TRUE(rig.Schedule(event.frame, event.gain).ok());
    }

    ExpectMatches(rig.Render(kTotalFrames, block_frames), expected);
  }
}

}  // namespace
}  // namespace lavanda
