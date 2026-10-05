#include "lavanda/graph/nodes/audio_asset_source_node.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <memory>
#include <utility>

#include "lavanda/core/audio_buffer.h"
#include "lavanda/resources/audio_asset.h"

namespace lavanda {
namespace {

// ---- definitions -----------------------------------------------------------

constexpr std::uint32_t kRate = 48000;
constexpr std::uint32_t kFrames = 4;
constexpr float kSentinel = 9.0f;

float Expected(std::uint32_t frame, std::uint32_t channel) {
  return static_cast<float>(frame * 10 + channel + 1) / 100.0f;
}

AudioAsset MakeAsset(std::uint32_t channels) {
  AudioBuffer buffer(kFrames, channels);

  for (std::uint32_t f = 0; f < kFrames; ++f) {
    for (std::uint32_t c = 0; c < channels; ++c) {
      buffer(f, c) = Expected(f, c);
    }
  }

  StatusOr<AudioAsset> asset = AudioAsset::Create(std::move(buffer), kRate);

  EXPECT_TRUE(asset.ok());

  return std::move(asset.value());
}

AudioBuffer Filled(std::uint32_t frames, std::uint32_t channels) {
  AudioBuffer buffer(frames, channels);

  for (std::uint32_t f = 0; f < frames; ++f) {
    for (std::uint32_t c = 0; c < channels; ++c) {
      buffer(f, c) = kSentinel;
    }
  }

  return buffer;
}

NodeProcessContext ContextFor(AudioBuffer& out, std::uint32_t frames) {
  NodeProcessContext context;

  context.output = out.View();
  context.frame_count = frames;
  context.sample_rate_hz = static_cast<double>(kRate);

  return context;
}

AudioAssetId SomeId() {
  AudioAssetId id;
  id.index = 3;
  id.generation = 7;

  return id;
}

// ---- tests -----------------------------------------------------------------

TEST(AudioAssetSourceNodeTest, ReportsItsShapeAndRequiredAsset) {
  AudioAssetSourceNode node(SomeId(), 2);

  EXPECT_EQ(node.input_channel_count(), 0u);
  EXPECT_EQ(node.output_channel_count(), 2u);
  EXPECT_EQ(node.expected_input_count(), 0u);
  EXPECT_EQ(node.required_asset(), SomeId());
}

TEST(AudioAssetSourceNodeTest, UnboundNodeOutputsSilence) {
  AudioAssetSourceNode node(SomeId(), 1);
  AudioBuffer out = Filled(kFrames, 1);
  NodeProcessContext context = ContextFor(out, kFrames);

  node.Process(context);

  for (std::uint32_t f = 0; f < kFrames; ++f) {
    EXPECT_FLOAT_EQ(out(f, 0), 0.0f);
  }
}

TEST(AudioAssetSourceNodeTest, PlaysMonoFramesInOrderAcrossBlocks) {
  const AudioAsset asset = MakeAsset(1);

  AudioAssetSourceNode node(SomeId(), 1);
  node.BindAsset(&asset);

  AudioBuffer first = Filled(2, 1);
  NodeProcessContext first_context = ContextFor(first, 2);

  node.Process(first_context);

  EXPECT_FLOAT_EQ(first(0, 0), Expected(0, 0));
  EXPECT_FLOAT_EQ(first(1, 0), Expected(1, 0));

  AudioBuffer second = Filled(2, 1);
  NodeProcessContext second_context = ContextFor(second, 2);

  node.Process(second_context);

  EXPECT_FLOAT_EQ(second(0, 0), Expected(2, 0));
  EXPECT_FLOAT_EQ(second(1, 0), Expected(3, 0));
}

TEST(AudioAssetSourceNodeTest, EndOfAssetZeroFillsThenStaysSilent) {
  const AudioAsset asset = MakeAsset(1);

  AudioAssetSourceNode node(SomeId(), 1);
  node.BindAsset(&asset);

  AudioBuffer out = Filled(6, 1);
  NodeProcessContext context = ContextFor(out, 6);

  node.Process(context);

  for (std::uint32_t f = 0; f < kFrames; ++f) {
    EXPECT_FLOAT_EQ(out(f, 0), Expected(f, 0));
  }

  EXPECT_FLOAT_EQ(out(4, 0), 0.0f);
  EXPECT_FLOAT_EQ(out(5, 0), 0.0f);

  AudioBuffer after = Filled(3, 1);
  NodeProcessContext after_context = ContextFor(after, 3);

  node.Process(after_context);

  for (std::uint32_t f = 0; f < 3; ++f) {
    EXPECT_FLOAT_EQ(after(f, 0), 0.0f);
  }
}

TEST(AudioAssetSourceNodeTest, StereoCopiesInterleavedChannels) {
  const AudioAsset asset = MakeAsset(2);

  AudioAssetSourceNode node(SomeId(), 2);
  node.BindAsset(&asset);

  AudioBuffer out = Filled(kFrames, 2);
  NodeProcessContext context = ContextFor(out, kFrames);

  node.Process(context);

  for (std::uint32_t f = 0; f < kFrames; ++f) {
    EXPECT_FLOAT_EQ(out(f, 0), Expected(f, 0));
    EXPECT_FLOAT_EQ(out(f, 1), Expected(f, 1));
  }
}

TEST(AudioAssetSourceNodeTest, OutputChannelMismatchIsSilentAndDoesNotAdvance) {
  const AudioAsset asset = MakeAsset(1);

  AudioAssetSourceNode node(SomeId(), 1);
  node.BindAsset(&asset);

  AudioBuffer wrong = Filled(kFrames, 2);
  NodeProcessContext wrong_context = ContextFor(wrong, kFrames);

  node.Process(wrong_context);

  EXPECT_FLOAT_EQ(wrong(1, 0), 0.0f);

  AudioBuffer right = Filled(2, 1);
  NodeProcessContext right_context = ContextFor(right, 2);

  node.Process(right_context);

  EXPECT_FLOAT_EQ(right(0, 0), Expected(0, 0));
}

TEST(AudioAssetSourceNodeTest, CloneKeepsIdentityButNotBindingOrPosition) {
  const AudioAsset asset = MakeAsset(1);

  AudioAssetSourceNode node(SomeId(), 1);
  node.BindAsset(&asset);

  AudioBuffer warmup = Filled(2, 1);
  NodeProcessContext warmup_context = ContextFor(warmup, 2);

  node.Process(warmup_context);

  std::unique_ptr<AudioNode> clone = node.Clone();

  EXPECT_EQ(clone->required_asset(), SomeId());
  EXPECT_EQ(clone->output_channel_count(), 1u);

  AudioBuffer unbound = Filled(2, 1);
  NodeProcessContext unbound_context = ContextFor(unbound, 2);

  clone->Process(unbound_context);

  EXPECT_FLOAT_EQ(unbound(1, 0), 0.0f);

  clone->BindAsset(&asset);

  AudioBuffer bound = Filled(2, 1);
  NodeProcessContext bound_context = ContextFor(bound, 2);

  clone->Process(bound_context);

  EXPECT_FLOAT_EQ(bound(0, 0), Expected(0, 0));
}

TEST(AudioAssetSourceNodeTest, BindingAgainRestartsPlayback) {
  const AudioAsset asset = MakeAsset(1);

  AudioAssetSourceNode node(SomeId(), 1);
  node.BindAsset(&asset);

  AudioBuffer first = Filled(2, 1);
  NodeProcessContext first_context = ContextFor(first, 2);

  node.Process(first_context);
  node.BindAsset(&asset);

  AudioBuffer again = Filled(1, 1);
  NodeProcessContext again_context = ContextFor(again, 1);

  node.Process(again_context);

  EXPECT_FLOAT_EQ(again(0, 0), Expected(0, 0));
}

}  // namespace
}  // namespace lavanda
