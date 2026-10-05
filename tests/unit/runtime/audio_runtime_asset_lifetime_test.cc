#include <gtest/gtest.h>

#include <cstdint>
#include <memory>
#include <string>
#include <utility>

#include "lavanda/core/audio_buffer.h"
#include "lavanda/graph/nodes/oscillator_node.h"
#include "lavanda/graph/nodes/output_node.h"
#include "lavanda/graph/nodes/pan_node.h"
#include "lavanda/resources/audio_asset_id.h"
#include "lavanda/runtime/audio_runtime.h"
#include "test_support/fake_audio_device.h"

namespace lavanda {
namespace {

// ---- definitions -----------------------------------------------------------

using test_support::FakeAudioDevice;

constexpr std::uint32_t kLongBlock = 64;

struct Chain {
  NodeId source;
  NodeId pan;
  NodeId output;
};

struct Harness {
  explicit Harness(RuntimeConfig config = RuntimeConfig()) {
    auto owned = std::make_unique<FakeAudioDevice>();

    device = owned.get();
    runtime = std::make_unique<AudioRuntime>(std::move(owned), config);

    EXPECT_TRUE(runtime->Start().ok());
  }

  static std::string Path(const char* file) {
    return std::string(LAVANDA_FIXTURE_DIR) + "/" + file;
  }

  AudioAssetId Load(const char* file) {
    StatusOr<AudioAssetId> id = runtime->LoadAudioAsset(Path(file));

    if (!id.ok()) {
      ADD_FAILURE() << "fixture load failed: " << file;
      return AudioAssetId{};
    }

    return id.value();
  }

  void Pump(std::uint32_t frames) {
    AudioBuffer buffer(frames, 2);

    EXPECT_TRUE(device->PumpRender(buffer.View()));
  }

  Chain BuildMonoAssetChain(AudioAssetId asset) {
    Chain chain;

    StatusOr<NodeId> source = runtime->AddAssetSourceNode(asset);
    StatusOr<NodeId> pan =
        runtime->graph().AddNode(std::make_unique<PanNode>());
    StatusOr<NodeId> output =
        runtime->graph().AddNode(std::make_unique<OutputNode>(2));

    EXPECT_TRUE(source.ok());
    EXPECT_TRUE(runtime->graph().Connect(source.value(), pan.value(), 0).ok());
    EXPECT_TRUE(runtime->graph().Connect(pan.value(), output.value(), 0).ok());
    EXPECT_TRUE(runtime->graph().SetOutput(output.value()).ok());

    chain.source = source.value();
    chain.pan = pan.value();
    chain.output = output.value();

    return chain;
  }

  void StageAndActivate() {
    StatusOr<GraphPlanHandle> plan = runtime->CompileAndStageGraph();

    ASSERT_TRUE(plan.ok());
    ASSERT_TRUE(runtime->ActivateGraphPlan(plan.value()).ok());

    Pump(2);
  }

  FakeAudioDevice* device = nullptr;
  std::unique_ptr<AudioRuntime> runtime;
};

RuntimeConfig SingleAssetConfig() {
  RuntimeConfig config;
  config.resource_config.max_assets = 1;

  return config;
}

// ---- tests -----------------------------------------------------------------

TEST(AudioRuntimeAssetLifetimeTest, ReleasingAnUnusedAssetFreesItImmediately) {
  Harness h;
  const AudioAssetId asset = h.Load("mono_pcm16.wav");

  EXPECT_EQ(h.runtime->stats().resident_asset_count, 1u);
  EXPECT_GT(h.runtime->stats().resident_asset_bytes, 0u);

  ASSERT_TRUE(h.runtime->ReleaseAudioAsset(asset).ok());

  EXPECT_EQ(h.runtime->stats().resident_asset_count, 0u);
  EXPECT_EQ(h.runtime->stats().retiring_asset_count, 0u);
  EXPECT_EQ(h.runtime->stats().resident_asset_bytes, 0u);
}

TEST(AudioRuntimeAssetLifetimeTest,
     VoiceHoldsAReleasedAssetUntilItIsDestroyed) {
  Harness h;
  const AudioAssetId asset = h.Load("mono_pcm16.wav");
  StatusOr<Voice> created = h.runtime->CreateVoice(asset);

  ASSERT_TRUE(created.ok());
  ASSERT_TRUE(h.runtime->ReleaseAudioAsset(asset).ok());

  EXPECT_EQ(h.runtime->stats().retiring_asset_count, 1u);

  ASSERT_TRUE(created.value().Destroy().ok());

  h.runtime->ReclaimAssets();

  EXPECT_EQ(h.runtime->stats().retiring_asset_count, 1u);

  h.Pump(2);
  h.runtime->ReclaimAssets();

  EXPECT_EQ(h.runtime->stats().resident_asset_count, 0u);
  EXPECT_EQ(h.runtime->stats().resident_asset_bytes, 0u);
}

TEST(AudioRuntimeAssetLifetimeTest,
     FinishedVoiceStillHoldsItsAssetUntilDestroyed) {
  Harness h;
  const AudioAssetId asset = h.Load("mono_pcm16.wav");
  StatusOr<Voice> created = h.runtime->CreateVoice(asset);

  ASSERT_TRUE(created.ok());

  Voice& voice = created.value();

  ASSERT_TRUE(voice.Start().ok());

  h.Pump(kLongBlock);

  ASSERT_FALSE(voice.IsPlaying());

  ASSERT_TRUE(h.runtime->ReleaseAudioAsset(asset).ok());
  h.runtime->ReclaimAssets();

  EXPECT_EQ(h.runtime->stats().retiring_asset_count, 1u);

  ASSERT_TRUE(voice.Destroy().ok());

  h.Pump(2);
  h.runtime->ReclaimAssets();

  EXPECT_EQ(h.runtime->stats().resident_asset_count, 0u);
}

TEST(AudioRuntimeAssetLifetimeTest,
     ActivePlanHoldsItsAssetUntilReplacedAndReclaimed) {
  Harness h;
  const AudioAssetId asset = h.Load("mono_pcm16.wav");
  const Chain chain = h.BuildMonoAssetChain(asset);

  h.StageAndActivate();

  ASSERT_TRUE(h.runtime->ReleaseAudioAsset(asset).ok());

  h.runtime->ReclaimAssets();

  EXPECT_EQ(h.runtime->stats().retiring_asset_count, 1u);

  ASSERT_TRUE(h.runtime->graph().RemoveNode(chain.source).ok());

  StatusOr<NodeId> osc =
      h.runtime->graph().AddNode(std::make_unique<OscillatorNode>());

  ASSERT_TRUE(osc.ok());
  ASSERT_TRUE(h.runtime->graph().Connect(osc.value(), chain.pan, 0).ok());

  h.StageAndActivate();

  EXPECT_EQ(h.runtime->stats().retiring_asset_count, 1u);

  h.runtime->ReclaimAssets();

  EXPECT_EQ(h.runtime->stats().resident_asset_count, 0u);
  EXPECT_EQ(h.runtime->stats().resident_asset_bytes, 0u);
}

TEST(AudioRuntimeAssetLifetimeTest,
     StaleIdsAreRejectedAndSlotsRecycleWithNewGenerations) {
  Harness h{SingleAssetConfig()};
  const AudioAssetId first = h.Load("mono_pcm16.wav");

  ASSERT_TRUE(h.runtime->ReleaseAudioAsset(first).ok());

  const AudioAssetId second = h.Load("mono_pcm16.wav");

  ASSERT_EQ(first.index, second.index);
  EXPECT_NE(first, second);

  EXPECT_FALSE(h.runtime->GetAudioAssetInfo(first).ok());
  EXPECT_FALSE(h.runtime->CreateVoice(first).ok());
  EXPECT_FALSE(h.runtime->AddAssetSourceNode(first).ok());
  EXPECT_TRUE(h.runtime->GetAudioAssetInfo(second).ok());
}

TEST(AudioRuntimeAssetLifetimeTest,
     StoreCapacityIsRecycledOnlyOnceTheLastHolderIsGone) {
  Harness h{SingleAssetConfig()};
  const AudioAssetId asset = h.Load("mono_pcm16.wav");
  StatusOr<Voice> created = h.runtime->CreateVoice(asset);

  ASSERT_TRUE(created.ok());
  ASSERT_TRUE(h.runtime->ReleaseAudioAsset(asset).ok());

  EXPECT_FALSE(
      h.runtime->LoadAudioAsset(Harness::Path("stereo_pcm16.wav")).ok());
  EXPECT_EQ(h.runtime->stats().asset_load_failures, 1u);

  ASSERT_TRUE(created.value().Destroy().ok());

  h.Pump(2);

  EXPECT_TRUE(
      h.runtime->LoadAudioAsset(Harness::Path("stereo_pcm16.wav")).ok());
  EXPECT_EQ(h.runtime->stats().resident_asset_count, 1u);
  EXPECT_EQ(h.runtime->stats().retiring_asset_count, 0u);
}

TEST(AudioRuntimeAssetLifetimeTest,
     ShutdownWithLiveVoicesAndPlansHoldingAssetsIsClean) {
  Harness h;
  const AudioAssetId asset = h.Load("mono_pcm16.wav");
  StatusOr<Voice> created = h.runtime->CreateVoice(asset);

  ASSERT_TRUE(created.ok());
  ASSERT_TRUE(created.value().Start().ok());

  h.BuildMonoAssetChain(asset);
  h.StageAndActivate();

  ASSERT_TRUE(h.runtime->ReleaseAudioAsset(asset).ok());

  EXPECT_TRUE(h.runtime->Shutdown().ok());
}

}  // namespace
}  // namespace lavanda
