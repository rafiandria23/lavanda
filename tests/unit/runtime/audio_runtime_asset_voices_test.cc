#include <gtest/gtest.h>

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>

#include "lavanda/core/audio_buffer.h"
#include "lavanda/resources/audio_asset_id.h"
#include "lavanda/runtime/audio_runtime.h"
#include "lavanda/runtime/command.h"
#include "test_support/fake_audio_device.h"

namespace lavanda {
namespace {

// ---- definitions -----------------------------------------------------------

constexpr std::size_t kTinyQueueCapacity = 2;
constexpr std::uint32_t kBlockFrames = 2;
constexpr int kMaxBlocks = 32;

struct Harness {
  explicit Harness(RuntimeConfig config = RuntimeConfig()) {
    auto owned = std::make_unique<test_support::FakeAudioDevice>();
    device = owned.get();
    runtime = std::make_unique<AudioRuntime>(std::move(owned), config);

    EXPECT_TRUE(runtime->Start().ok());
  }

  AudioAssetId Load(const char* file) {
    StatusOr<AudioAssetId> id =
        runtime->LoadAudioAsset(std::string(LAVANDA_FIXTURE_DIR) + "/" + file);

    if (!id.ok()) {
      ADD_FAILURE() << "fixture load failed: " << file;

      return AudioAssetId{};
    }

    return id.value();
  }

  AudioBuffer Pump(std::uint32_t frames) {
    AudioBuffer buffer(frames, 2);

    EXPECT_TRUE(device->PumpRender(buffer.View()));

    return buffer;
  }

  test_support::FakeAudioDevice* device = nullptr;
  std::unique_ptr<AudioRuntime> runtime;
};

double SumAbs(const AudioBuffer& buffer, std::uint32_t frames,
              std::uint32_t channel) {
  double sum = 0.0;

  for (std::uint32_t f = 0; f < frames; ++f) {
    sum += std::fabs(static_cast<double>(buffer(f, channel)));
  }

  return sum;
}

// ---- tests -----------------------------------------------------------------

TEST(AudioRuntimeAssetVoicesTest,
     MonoAssetVoiceIsAudibleCenteredAndStopsAtEnd) {
  Harness h;
  StatusOr<Voice> created = h.runtime->CreateVoice(h.Load("mono_pcm16.wav"));

  ASSERT_TRUE(created.ok());

  Voice& voice = created.value();

  ASSERT_TRUE(voice.Start().ok());

  AudioBuffer first = h.Pump(kBlockFrames);

  EXPECT_TRUE(voice.IsPlaying());
  EXPECT_GT(first(1, 0), 0.0f);
  EXPECT_FLOAT_EQ(first(1, 0), first(1, 1));  // centered mono: L == R

  int blocks = 1;

  while (voice.IsPlaying() && blocks < kMaxBlocks) {
    h.Pump(kBlockFrames);
    ++blocks;
  }

  EXPECT_FALSE(voice.IsPlaying());
  EXPECT_LT(blocks, kMaxBlocks);
  EXPECT_TRUE(voice.is_valid());  // finished, but the handle stays usable
}

TEST(AudioRuntimeAssetVoicesTest, FinishedVoiceCanBeStartedAgain) {
  Harness h;
  StatusOr<Voice> created = h.runtime->CreateVoice(h.Load("mono_pcm16.wav"));

  ASSERT_TRUE(created.ok());

  Voice& voice = created.value();

  ASSERT_TRUE(voice.Start().ok());

  h.Pump(64);

  ASSERT_FALSE(voice.IsPlaying());

  ASSERT_TRUE(voice.Start().ok());

  AudioBuffer replay = h.Pump(kBlockFrames);

  EXPECT_TRUE(voice.IsPlaying());
  EXPECT_GT(replay(1, 0), 0.0f);
}

TEST(AudioRuntimeAssetVoicesTest, StereoAssetHonoursBalanceAtTheEdges) {
  for (float pan : {1.0f, -1.0f}) {
    Harness h;
    StatusOr<Voice> created =
        h.runtime->CreateVoice(h.Load("stereo_pcm16.wav"));

    ASSERT_TRUE(created.ok());

    Voice& voice = created.value();

    ASSERT_TRUE(voice.SetPan(pan).ok());
    ASSERT_TRUE(voice.Start().ok());

    AudioBuffer out = h.Pump(8);
    const double left = SumAbs(out, 8, 0);
    const double right = SumAbs(out, 8, 1);

    if (pan > 0.0f) {
      EXPECT_NEAR(left, 0.0, 1e-6);
      EXPECT_GT(right, 0.0);
    } else {
      EXPECT_GT(left, 0.0);
      EXPECT_NEAR(right, 0.0, 1e-6);
    }
  }
}

TEST(AudioRuntimeAssetVoicesTest, ReleasedAssetCannotBackANewVoice) {
  Harness h;
  const AudioAssetId asset = h.Load("mono_pcm16.wav");

  ASSERT_TRUE(h.runtime->ReleaseAudioAsset(asset).ok());

  StatusOr<Voice> created = h.runtime->CreateVoice(asset);

  EXPECT_FALSE(created.ok());
  EXPECT_EQ(h.runtime->stats().voice_creation_failures, 1u);
}

TEST(AudioRuntimeAssetVoicesTest, InvalidAssetIdIsRejected) {
  Harness h;
  StatusOr<Voice> created = h.runtime->CreateVoice(AudioAssetId{});

  EXPECT_FALSE(created.ok());
  EXPECT_EQ(h.runtime->stats().voice_creation_failures, 1u);
}

TEST(AudioRuntimeAssetVoicesTest, PlayingVoiceKeepsAReleasedAssetAlive) {
  Harness h;
  const AudioAssetId asset = h.Load("mono_pcm16.wav");
  StatusOr<Voice> created = h.runtime->CreateVoice(asset);

  ASSERT_TRUE(created.ok());

  Voice& voice = created.value();

  ASSERT_TRUE(voice.Start().ok());

  h.Pump(kBlockFrames);

  ASSERT_TRUE(h.runtime->ReleaseAudioAsset(asset).ok());
  EXPECT_EQ(h.runtime->stats().retiring_asset_count, 1u);
  EXPECT_GT(h.runtime->stats().resident_asset_bytes, 0u);

  AudioBuffer next = h.Pump(kBlockFrames);  // still renders from retired data

  EXPECT_GT(SumAbs(next, kBlockFrames, 0), 0.0);
}

TEST(AudioRuntimeAssetVoicesTest,
     ReleaseBeforeTheCreateCommandIsAppliedStillPlays) {
  Harness h;
  const AudioAssetId asset = h.Load("mono_pcm16.wav");
  StatusOr<Voice> created = h.runtime->CreateVoice(asset);

  ASSERT_TRUE(created.ok());

  Voice& voice = created.value();

  ASSERT_TRUE(h.runtime->ReleaseAudioAsset(asset).ok());  // before any Pump
  ASSERT_TRUE(voice.Start().ok());

  AudioBuffer out = h.Pump(kBlockFrames);

  EXPECT_TRUE(voice.IsPlaying());
  EXPECT_GT(SumAbs(out, kBlockFrames, 0), 0.0);
}

TEST(AudioRuntimeAssetVoicesTest, DestroyedVoiceLetsAReleasedAssetBeReclaimed) {
  Harness h;
  const AudioAssetId asset = h.Load("mono_pcm16.wav");
  StatusOr<Voice> created = h.runtime->CreateVoice(asset);

  ASSERT_TRUE(created.ok());
  ASSERT_TRUE(h.runtime->ReleaseAudioAsset(asset).ok());
  ASSERT_TRUE(created.value().Destroy().ok());

  h.Pump(kBlockFrames);  // audio thread applies the destroy and unpins

  // Reclamation currently runs on the next load.
  h.Load("stereo_pcm16.wav");

  EXPECT_EQ(h.runtime->stats().retiring_asset_count, 0u);
  EXPECT_EQ(h.runtime->stats().resident_asset_count, 1u);
}

TEST(AudioRuntimeAssetVoicesTest, QueueFullRollsBackThePinAndTheSlot) {
  Harness h{RuntimeConfig(kTinyQueueCapacity)};
  const AudioAssetId asset = h.Load("mono_pcm16.wav");

  Command filler;
  filler.type = CommandType::kSetGain;
  filler.value = 0.1f;

  for (int i = 0; i < 16 && h.runtime->Submit(filler).ok(); ++i) {
  }

  StatusOr<Voice> created = h.runtime->CreateVoice(asset);

  ASSERT_FALSE(created.ok());
  EXPECT_EQ(created.status().code(), ErrorCode::kQueueFull);

  // A leaked pin would leave the asset Retiring after Release + reclaim.
  ASSERT_TRUE(h.runtime->ReleaseAudioAsset(asset).ok());

  h.Load("stereo_pcm16.wav");

  EXPECT_EQ(h.runtime->stats().retiring_asset_count, 0u);
  EXPECT_EQ(h.runtime->stats().resident_asset_count, 1u);
}

TEST(AudioRuntimeAssetVoicesTest,
     IsPlayingTracksOscillatorVoicesAndInvalidHandles) {
  Harness h;

  EXPECT_FALSE(Voice().IsPlaying());

  StatusOr<Voice> created = h.runtime->CreateVoice();

  ASSERT_TRUE(created.ok());

  Voice& voice = created.value();

  ASSERT_TRUE(voice.Start().ok());

  h.Pump(kBlockFrames);

  EXPECT_TRUE(voice.IsPlaying());

  ASSERT_TRUE(voice.Stop().ok());

  h.Pump(kBlockFrames);

  EXPECT_FALSE(voice.IsPlaying());
}

}  // namespace
}  // namespace lavanda
