#include <gtest/gtest.h>

#include <cmath>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>

#include "lavanda/core/audio_buffer.h"
#include "lavanda/graph/nodes/output_node.h"
#include "lavanda/graph/nodes/pan_node.h"
#include "lavanda/resources/audio_asset_id.h"
#include "lavanda/runtime/audio_runtime.h"
#include "test_support/fake_audio_device.h"

namespace lavanda {
namespace {

// ---- definitions -----------------------------------------------------------

using test_support::FakeAudioDevice;

struct Harness {
  Harness() {
    auto owned = std::make_unique<FakeAudioDevice>();

    device = owned.get();
    runtime = std::make_unique<AudioRuntime>(std::move(owned));

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

  StatusOr<GraphPlanHandle> StageMonoAssetGraph(AudioAssetId asset) {
    StatusOr<NodeId> source = runtime->AddAssetSourceNode(asset);
    StatusOr<NodeId> pan =
        runtime->graph().AddNode(std::make_unique<PanNode>());
    StatusOr<NodeId> output =
        runtime->graph().AddNode(std::make_unique<OutputNode>(2));

    EXPECT_TRUE(source.ok());
    EXPECT_TRUE(runtime->graph().Connect(source.value(), pan.value(), 0).ok());
    EXPECT_TRUE(runtime->graph().Connect(pan.value(), output.value(), 0).ok());
    EXPECT_TRUE(runtime->graph().SetOutput(output.value()).ok());

    return runtime->CompileAndStageGraph();
  }

  FakeAudioDevice* device = nullptr;
  std::unique_ptr<AudioRuntime> runtime;
};

double SumAbs(AudioBuffer& buffer, std::uint32_t frames,
              std::uint32_t channel) {
  double sum = 0.0;

  for (std::uint32_t f = 0; f < frames; ++f) {
    sum += std::fabs(static_cast<double>(buffer(f, channel)));
  }

  return sum;
}

// ---- tests -----------------------------------------------------------------

TEST(AudioRuntimeAssetGraphTest, AssetNodePlaysThroughTheGraphAndThenEnds) {
  Harness h;
  StatusOr<GraphPlanHandle> plan =
      h.StageMonoAssetGraph(h.Load("mono_pcm16.wav"));

  ASSERT_TRUE(plan.ok());
  ASSERT_TRUE(h.runtime->ActivateGraphPlan(plan.value()).ok());

  AudioBuffer head = h.Pump(2);

  EXPECT_GT(SumAbs(head, 2, 0), 0.0);
  EXPECT_GT(SumAbs(head, 2, 1), 0.0);

  h.Pump(64);

  AudioBuffer tail = h.Pump(8);

  EXPECT_EQ(SumAbs(tail, 8, 0), 0.0);
  EXPECT_EQ(SumAbs(tail, 8, 1), 0.0);
}

TEST(AudioRuntimeAssetGraphTest, CompiledPlanKeepsAReleasedAssetAlive) {
  Harness h;
  const AudioAssetId asset = h.Load("mono_pcm16.wav");
  StatusOr<GraphPlanHandle> plan = h.StageMonoAssetGraph(asset);

  ASSERT_TRUE(plan.ok());
  ASSERT_TRUE(h.runtime->ActivateGraphPlan(plan.value()).ok());
  ASSERT_TRUE(h.runtime->ReleaseAudioAsset(asset).ok());

  EXPECT_EQ(h.runtime->stats().retiring_asset_count, 1u);

  AudioBuffer out = h.Pump(4);

  EXPECT_GT(SumAbs(out, 4, 0), 0.0);
}

TEST(AudioRuntimeAssetGraphTest,
     CompilingAfterReleaseFailsAndCountsTheFailure) {
  Harness h;
  const AudioAssetId asset = h.Load("mono_pcm16.wav");

  StatusOr<NodeId> source = h.runtime->AddAssetSourceNode(asset);

  ASSERT_TRUE(source.ok());
  ASSERT_TRUE(h.runtime->ReleaseAudioAsset(asset).ok());

  StatusOr<NodeId> output =
      h.runtime->graph().AddNode(std::make_unique<OutputNode>(1));

  ASSERT_TRUE(
      h.runtime->graph().Connect(source.value(), output.value(), 0).ok());
  ASSERT_TRUE(h.runtime->graph().SetOutput(output.value()).ok());

  EXPECT_FALSE(h.runtime->CompileAndStageGraph().ok());
  EXPECT_EQ(h.runtime->stats().graph_compilation_failures, 1u);
}

TEST(AudioRuntimeAssetGraphTest, AddAssetSourceNodeRejectsAnUnknownAsset) {
  Harness h;

  EXPECT_FALSE(h.runtime->AddAssetSourceNode(AudioAssetId{}).ok());
}

TEST(AudioRuntimeAssetGraphTest, ReleasingAStagedPlanReleasesTheAssetPin) {
  Harness h;
  const AudioAssetId asset = h.Load("mono_pcm16.wav");
  StatusOr<GraphPlanHandle> plan = h.StageMonoAssetGraph(asset);

  ASSERT_TRUE(plan.ok());
  ASSERT_TRUE(h.runtime->ReleaseAudioAsset(asset).ok());
  EXPECT_EQ(h.runtime->stats().retiring_asset_count, 1u);

  ASSERT_TRUE(h.runtime->ReleaseStagedGraphPlan(plan.value()).ok());

  h.Load("stereo_pcm16.wav");

  EXPECT_EQ(h.runtime->stats().retiring_asset_count, 0u);
}

TEST(AudioRuntimeAssetGraphTest, ShutdownWithAStagedAssetPlanIsClean) {
  Harness h;
  StatusOr<GraphPlanHandle> plan =
      h.StageMonoAssetGraph(h.Load("mono_pcm16.wav"));

  ASSERT_TRUE(plan.ok());
  EXPECT_TRUE(h.runtime->Shutdown().ok());
}

}  // namespace
}  // namespace lavanda
