#include <gtest/gtest.h>

#include <cstdint>
#include <string>
#include <vector>

#include "lavanda/core/audio_buffer.h"
#include "lavanda/resources/audio_asset_id.h"
#include "lavanda/resources/resource_config.h"
#include "lavanda/resources/resource_loader.h"
#include "lavanda/resources/resource_store.h"
#include "lavanda/runtime/command.h"
#include "lavanda/runtime/voice_pool.h"

namespace lavanda {
namespace {

// ---- definitions -----------------------------------------------------------

constexpr std::uint32_t kMonoRate = 48000;    // mono_pcm16.wav: 8 frames, i/8
constexpr std::uint32_t kStereoRate = 44100;  // stereo_pcm16.wav: 4 frames
constexpr std::uint32_t kMonoFrames = 8;
constexpr std::uint32_t kStereoFrames = 4;
constexpr double kDeviceRate = 48000.0;
constexpr float kSentinel = 9.0f;  // proves the renderer overwrites every frame

constexpr float kStereoLeft[kStereoFrames] = {0.0f, 0.5f, -0.5f, -1.0f};
constexpr float kStereoRight[kStereoFrames] = {0.25f, -0.25f, 0.125f, -0.125f};

AudioBuffer MakeBuffer(std::uint32_t frames, std::uint32_t channels) {
  AudioBuffer buffer(frames, channels);

  for (std::uint32_t f = 0; f < frames; ++f) {
    for (std::uint32_t c = 0; c < channels; ++c) {
      buffer(f, c) = kSentinel;
    }
  }

  return buffer;
}

struct Harness {
  ResourceConfig config;
  ResourceStore store{config};
  ResourceLoader loader{store, config};
  VoicePool pool{4, &store};

  AudioAssetId Load(const char* file, std::uint32_t rate) {
    StatusOr<AudioAssetId> id =
        loader.Load(std::string(LAVANDA_FIXTURE_DIR) + "/" + file, rate);

    if (!id.ok()) {
      ADD_FAILURE() << "fixture load failed: " << file;
      return AudioAssetId{};
    }

    return id.value();
  }

  VoiceId CreateVoice(AudioAssetId asset = AudioAssetId{}, bool pin = true) {
    if (asset.is_valid() && pin) {
      EXPECT_TRUE(store.Pin(asset) != nullptr);
    }

    StatusOr<VoiceId> id = pool.ReserveSlot();

    EXPECT_TRUE(id.ok());

    Command command;

    command.type = CommandType::kCreateVoice;
    command.voice_id = id.value();
    command.asset_id = asset;

    pool.ApplyCommand(command);

    return id.value();
  }

  void Send(CommandType type, VoiceId id) {
    Command command;
    command.type = type;
    command.voice_id = id;

    pool.ApplyCommand(command);
  }

  void Render(VoiceId id, AudioBuffer& buffer) {
    pool.RenderVoiceSource(id.index, buffer.View(), kDeviceRate);
  }
};

// ---- tests -----------------------------------------------------------------

TEST(VoicePoolAssetsTest, MonoVoicePlaysSamplesInOrderAcrossBlocks) {
  Harness h;
  const AudioAssetId asset = h.Load("mono_pcm16.wav", kMonoRate);
  const VoiceId v = h.CreateVoice(asset);

  EXPECT_EQ(h.pool.observed_state(v), VoiceState::kStopped);

  h.Send(CommandType::kStartVoice, v);

  EXPECT_EQ(h.pool.observed_state(v), VoiceState::kPlaying);

  AudioBuffer first = MakeBuffer(4, 1);
  h.Render(v, first);

  for (std::uint32_t i = 0; i < 4; ++i) {
    EXPECT_FLOAT_EQ(first(i, 0), static_cast<float>(i) / 8.0f);
  }

  EXPECT_EQ(h.pool.observed_state(v), VoiceState::kPlaying);

  AudioBuffer second = MakeBuffer(4, 1);
  h.Render(v, second);

  for (std::uint32_t i = 0; i < 4; ++i) {
    EXPECT_FLOAT_EQ(second(i, 0), static_cast<float>(i + 4) / 8.0f);
  }

  EXPECT_EQ(h.pool.observed_state(v), VoiceState::kStopped);
}

TEST(VoicePoolAssetsTest, EndOfAssetZeroFillsTailAndStopsButKeepsHandleAndPin) {
  Harness h;
  const AudioAssetId asset = h.Load("mono_pcm16.wav", kMonoRate);
  const VoiceId v = h.CreateVoice(asset);

  h.Send(CommandType::kStartVoice, v);

  AudioBuffer out = MakeBuffer(12, 1);
  h.Render(v, out);

  for (std::uint32_t i = 0; i < kMonoFrames; ++i) {
    EXPECT_FLOAT_EQ(out(i, 0), static_cast<float>(i) / 8.0f);
  }

  for (std::uint32_t i = kMonoFrames; i < 12; ++i) {
    EXPECT_FLOAT_EQ(out(i, 0), 0.0f);
  }

  EXPECT_EQ(h.pool.observed_state(v), VoiceState::kStopped);
  EXPECT_FALSE(h.pool.is_active(v.index));
  EXPECT_EQ(h.store.pin_count(asset), 1u);
}

TEST(VoicePoolAssetsTest, StartAfterEndReplaysFromTheBeginning) {
  Harness h;
  const AudioAssetId asset = h.Load("mono_pcm16.wav", kMonoRate);
  const VoiceId v = h.CreateVoice(asset);

  h.Send(CommandType::kStartVoice, v);

  AudioBuffer drain = MakeBuffer(kMonoFrames, 1);
  h.Render(v, drain);

  ASSERT_EQ(h.pool.observed_state(v), VoiceState::kStopped);

  h.Send(CommandType::kStartVoice, v);

  AudioBuffer replay = MakeBuffer(2, 1);
  h.Render(v, replay);

  EXPECT_FLOAT_EQ(replay(0, 0), 0.0f);
  EXPECT_FLOAT_EQ(replay(1, 0), 1.0f / 8.0f);
}

TEST(VoicePoolAssetsTest, StopResetsPositionSoNextStartRestarts) {
  Harness h;
  const AudioAssetId asset = h.Load("mono_pcm16.wav", kMonoRate);
  const VoiceId v = h.CreateVoice(asset);

  h.Send(CommandType::kStartVoice, v);

  AudioBuffer partial = MakeBuffer(3, 1);
  h.Render(v, partial);

  h.Send(CommandType::kStopVoice, v);
  h.Send(CommandType::kStartVoice, v);

  AudioBuffer again = MakeBuffer(2, 1);
  h.Render(v, again);

  EXPECT_FLOAT_EQ(again(0, 0), 0.0f);
  EXPECT_FLOAT_EQ(again(1, 0), 1.0f / 8.0f);
}

TEST(VoicePoolAssetsTest, StartWhilePlayingDoesNotRestart) {
  Harness h;
  const AudioAssetId asset = h.Load("mono_pcm16.wav", kMonoRate);
  const VoiceId v = h.CreateVoice(asset);

  h.Send(CommandType::kStartVoice, v);

  AudioBuffer first = MakeBuffer(3, 1);
  h.Render(v, first);

  h.Send(CommandType::kStartVoice, v);

  AudioBuffer next = MakeBuffer(2, 1);
  h.Render(v, next);

  EXPECT_FLOAT_EQ(next(0, 0), 3.0f / 8.0f);
  EXPECT_FLOAT_EQ(next(1, 0), 4.0f / 8.0f);
}

TEST(VoicePoolAssetsTest, DestroyUnpinsExactlyOnce) {
  Harness h;
  const AudioAssetId asset = h.Load("mono_pcm16.wav", kMonoRate);
  const VoiceId v = h.CreateVoice(asset);

  ASSERT_EQ(h.store.pin_count(asset), 1u);

  h.Send(CommandType::kDestroyVoice, v);

  EXPECT_EQ(h.store.pin_count(asset), 0u);
  EXPECT_EQ(h.pool.observed_state(v), VoiceState::kInactive);

  h.Send(CommandType::kDestroyVoice, v);  // stale: must not underflow

  EXPECT_EQ(h.store.pin_count(asset), 0u);
}

TEST(VoicePoolAssetsTest, RecreatingOverALiveAssetSlotReleasesItsPin) {
  Harness h;
  const AudioAssetId asset = h.Load("mono_pcm16.wav", kMonoRate);
  const VoiceId v = h.CreateVoice(asset);

  ASSERT_EQ(h.store.pin_count(asset), 1u);

  Command recreate;

  recreate.type = CommandType::kCreateVoice;
  recreate.voice_id = v;
  recreate.voice_id.generation = v.generation + 1;

  h.pool.ApplyCommand(recreate);

  EXPECT_EQ(h.store.pin_count(asset), 0u);
}

TEST(VoicePoolAssetsTest, StereoAssetRendersInterleavedFrames) {
  Harness h;
  const AudioAssetId asset = h.Load("stereo_pcm16.wav", kStereoRate);
  const VoiceId v = h.CreateVoice(asset);

  h.Send(CommandType::kStartVoice, v);

  EXPECT_EQ(h.pool.source_channel_count(v.index), 2u);

  AudioBuffer out = MakeBuffer(kStereoFrames, 2);
  h.Render(v, out);

  for (std::uint32_t i = 0; i < kStereoFrames; ++i) {
    EXPECT_FLOAT_EQ(out(i, 0), kStereoLeft[i]);
    EXPECT_FLOAT_EQ(out(i, 1), kStereoRight[i]);
  }

  EXPECT_EQ(h.pool.observed_state(v), VoiceState::kStopped);
}

TEST(VoicePoolAssetsTest, ChannelMismatchIsSilentAndDoesNotAdvance) {
  Harness h;
  const AudioAssetId asset = h.Load("stereo_pcm16.wav", kStereoRate);
  const VoiceId v = h.CreateVoice(asset);

  h.Send(CommandType::kStartVoice, v);

  AudioBuffer wrong = MakeBuffer(kStereoFrames, 1);
  h.Render(v, wrong);

  for (std::uint32_t i = 0; i < kStereoFrames; ++i) {
    EXPECT_FLOAT_EQ(wrong(i, 0), 0.0f);
  }

  EXPECT_EQ(h.pool.observed_state(v), VoiceState::kPlaying);

  AudioBuffer right = MakeBuffer(kStereoFrames, 2);
  h.Render(v, right);

  EXPECT_FLOAT_EQ(right(1, 0), kStereoLeft[1]);
  EXPECT_FLOAT_EQ(right(1, 1), kStereoRight[1]);
}

TEST(VoicePoolAssetsTest, TwoVoicesShareOneAssetWithIndependentPositions) {
  Harness h;
  const AudioAssetId asset = h.Load("mono_pcm16.wav", kMonoRate);
  const VoiceId a = h.CreateVoice(asset);
  const VoiceId b = h.CreateVoice(asset);

  ASSERT_EQ(h.store.pin_count(asset), 2u);

  h.Send(CommandType::kStartVoice, a);
  h.Send(CommandType::kStartVoice, b);

  AudioBuffer a_out = MakeBuffer(4, 1);
  h.Render(a, a_out);

  AudioBuffer b_out = MakeBuffer(2, 1);
  h.Render(b, b_out);

  EXPECT_FLOAT_EQ(a_out(3, 0), 3.0f / 8.0f);
  EXPECT_FLOAT_EQ(b_out(0, 0), 0.0f);
  EXPECT_FLOAT_EQ(b_out(1, 0), 1.0f / 8.0f);

  h.Send(CommandType::kDestroyVoice, a);

  EXPECT_EQ(h.store.pin_count(asset), 1u);

  h.Send(CommandType::kDestroyVoice, b);

  EXPECT_EQ(h.store.pin_count(asset), 0u);
}

TEST(VoicePoolAssetsTest, ObservedStateIgnoresStaleGenerations) {
  Harness h;
  const VoiceId first = h.CreateVoice();

  h.Send(CommandType::kDestroyVoice, first);
  h.pool.ReleaseSlot(first);

  const VoiceId second = h.CreateVoice();

  ASSERT_EQ(second.index, first.index);
  ASSERT_NE(second.generation, first.generation);

  EXPECT_EQ(h.pool.observed_state(first), VoiceState::kInactive);
  EXPECT_EQ(h.pool.observed_state(second), VoiceState::kStopped);
}

TEST(VoicePoolAssetsTest, OscillatorVoiceStillWorksAndPublishesState) {
  Harness h;
  const VoiceId v = h.CreateVoice();

  EXPECT_EQ(h.pool.source_channel_count(v.index), 1u);

  h.Send(CommandType::kStartVoice, v);

  EXPECT_EQ(h.pool.observed_state(v), VoiceState::kPlaying);

  AudioBuffer out = MakeBuffer(64, 1);
  h.Render(v, out);

  bool any_nonzero = false;

  for (std::uint32_t i = 0; i < 64; ++i) {
    any_nonzero = any_nonzero || out(i, 0) != 0.0f;
  }

  EXPECT_TRUE(any_nonzero);
}

TEST(VoicePoolAssetsTest, UnresolvableAssetIdYieldsAVoiceThatCannotStart) {
  Harness h;

  AudioAssetId bogus;
  bogus.index = 3;
  bogus.generation = 99;

  const VoiceId v = h.CreateVoice(bogus, /*pin=*/false);
  h.Send(CommandType::kStartVoice, v);

  EXPECT_EQ(h.pool.observed_state(v), VoiceState::kStopped);

  AudioBuffer out = MakeBuffer(4, 1);
  h.Render(v, out);

  EXPECT_FLOAT_EQ(out(0, 0), 0.0f);
}

}  // namespace
}  // namespace lavanda
