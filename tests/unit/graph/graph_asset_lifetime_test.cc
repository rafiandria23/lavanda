#include <gtest/gtest.h>

#include <cstdint>
#include <memory>
#include <string>
#include <utility>

#include "lavanda/graph/graph.h"
#include "lavanda/graph/graph_compile_context.h"
#include "lavanda/graph/graph_compiler.h"
#include "lavanda/graph/graph_plan_store.h"
#include "lavanda/graph/nodes/audio_asset_source_node.h"
#include "lavanda/graph/nodes/oscillator_node.h"
#include "lavanda/graph/nodes/output_node.h"
#include "lavanda/graph/nodes/pan_node.h"
#include "lavanda/resources/resource_config.h"
#include "lavanda/resources/resource_loader.h"
#include "lavanda/resources/resource_store.h"

namespace lavanda {
namespace {

// ---- definitions -----------------------------------------------------------

constexpr std::uint32_t kRate = 48000;
constexpr std::uint32_t kOtherRate = 44100;
constexpr std::uint32_t kBlock = 64;
constexpr std::size_t kPlanSlots = 4;

struct Fixture {
  ResourceConfig config;
  ResourceStore store{config};
  ResourceLoader loader{store, config};
  GraphCompileContext context{&store, kRate};

  AudioAssetId Load() {
    StatusOr<AudioAssetId> id = loader.Load(
        std::string(LAVANDA_FIXTURE_DIR) + "/mono_pcm16.wav", kRate);

    if (!id.ok()) {
      ADD_FAILURE() << "fixture load failed";
      return AudioAssetId{};
    }

    return id.value();
  }

  void HoldBaselinePin(AudioAssetId id) {
    ASSERT_TRUE(store.Pin(id) != nullptr);
  }
};

NodeId BuildAssetGraph(AudioGraph& graph, AudioAssetId asset,
                       std::uint32_t declared_channels) {
  StatusOr<NodeId> source = graph.AddNode(
      std::make_unique<AudioAssetSourceNode>(asset, declared_channels));
  StatusOr<NodeId> output = graph.AddNode(std::make_unique<OutputNode>(2));

  if (declared_channels == 1) {
    StatusOr<NodeId> pan = graph.AddNode(std::make_unique<PanNode>());

    EXPECT_TRUE(graph.Connect(source.value(), pan.value(), 0).ok());
    EXPECT_TRUE(graph.Connect(pan.value(), output.value(), 0).ok());
  } else {
    EXPECT_TRUE(graph.Connect(source.value(), output.value(), 0).ok());
  }

  EXPECT_TRUE(graph.SetOutput(output.value()).ok());

  return source.value();
}

void BuildOscillatorGraph(AudioGraph& graph) {
  StatusOr<NodeId> osc = graph.AddNode(std::make_unique<OscillatorNode>());
  StatusOr<NodeId> pan = graph.AddNode(std::make_unique<PanNode>());
  StatusOr<NodeId> output = graph.AddNode(std::make_unique<OutputNode>(2));

  EXPECT_TRUE(graph.Connect(osc.value(), pan.value(), 0).ok());
  EXPECT_TRUE(graph.Connect(pan.value(), output.value(), 0).ok());
  EXPECT_TRUE(graph.SetOutput(output.value()).ok());
}

// ---- compiler --------------------------------------------------------------

TEST(GraphAssetLifetimeTest,
     CompilePinsTheAssetAndDestroyingThePlanReleasesIt) {
  Fixture f;
  const AudioAssetId asset = f.Load();

  f.HoldBaselinePin(asset);

  AudioGraph graph;
  BuildAssetGraph(graph, asset, 1);

  {
    StatusOr<GraphExecutionPlan> plan =
        GraphCompiler::Compile(graph, kBlock, f.context);

    ASSERT_TRUE(plan.ok());
    EXPECT_EQ(plan.value().pinned_asset_count(), 1u);
    EXPECT_EQ(f.store.pin_count(asset), 2u);
  }

  EXPECT_EQ(f.store.pin_count(asset), 1u);
}

TEST(GraphAssetLifetimeTest, UnreachableAssetNodeIsNotPinned) {
  Fixture f;
  const AudioAssetId asset = f.Load();

  f.HoldBaselinePin(asset);

  AudioGraph graph;
  BuildAssetGraph(graph, asset, 1);

  EXPECT_TRUE(
      graph.AddNode(std::make_unique<AudioAssetSourceNode>(asset, 1)).ok());

  StatusOr<GraphExecutionPlan> plan =
      GraphCompiler::Compile(graph, kBlock, f.context);

  ASSERT_TRUE(plan.ok());
  EXPECT_EQ(plan.value().pinned_asset_count(), 1u);
  EXPECT_EQ(f.store.pin_count(asset), 2u);
}

TEST(GraphAssetLifetimeTest, MovedPlansTransferPinsExactlyOnce) {
  Fixture f;
  const AudioAssetId asset = f.Load();

  f.HoldBaselinePin(asset);

  AudioGraph asset_graph;
  BuildAssetGraph(asset_graph, asset, 1);

  AudioGraph oscillator_graph;
  BuildOscillatorGraph(oscillator_graph);

  {
    StatusOr<GraphExecutionPlan> compiled =
        GraphCompiler::Compile(asset_graph, kBlock, f.context);

    ASSERT_TRUE(compiled.ok());

    GraphExecutionPlan moved(std::move(compiled.value()));

    EXPECT_EQ(f.store.pin_count(asset), 2u);

    StatusOr<GraphExecutionPlan> other =
        GraphCompiler::Compile(oscillator_graph, kBlock);

    ASSERT_TRUE(other.ok());

    moved = std::move(other.value());

    EXPECT_EQ(f.store.pin_count(asset), 1u);
  }

  EXPECT_EQ(f.store.pin_count(asset), 1u);
}

TEST(GraphAssetLifetimeTest, ChannelMismatchFailsAndReleasesItsPin) {
  Fixture f;
  const AudioAssetId asset = f.Load();

  f.HoldBaselinePin(asset);

  AudioGraph graph;
  BuildAssetGraph(graph, asset, 2);

  StatusOr<GraphExecutionPlan> plan =
      GraphCompiler::Compile(graph, kBlock, f.context);

  EXPECT_FALSE(plan.ok());
  EXPECT_EQ(f.store.pin_count(asset), 1u);
}

TEST(GraphAssetLifetimeTest, RateMismatchFailsAndReleasesItsPin) {
  Fixture f;
  const AudioAssetId asset = f.Load();

  f.HoldBaselinePin(asset);

  AudioGraph graph;
  BuildAssetGraph(graph, asset, 1);

  GraphCompileContext other_rate{&f.store, kOtherRate};
  StatusOr<GraphExecutionPlan> plan =
      GraphCompiler::Compile(graph, kBlock, other_rate);

  EXPECT_FALSE(plan.ok());
  EXPECT_EQ(f.store.pin_count(asset), 1u);
}

TEST(GraphAssetLifetimeTest, ReleasedAssetFailsToCompile) {
  Fixture f;
  const AudioAssetId asset = f.Load();

  AudioGraph graph;
  BuildAssetGraph(graph, asset, 1);

  ASSERT_TRUE(f.store.Release(asset).ok());

  EXPECT_FALSE(GraphCompiler::Compile(graph, kBlock, f.context).ok());
}

TEST(GraphAssetLifetimeTest, AssetNodesNeedAStoreAndAnOpenDevice) {
  Fixture f;
  const AudioAssetId asset = f.Load();

  AudioGraph graph;
  BuildAssetGraph(graph, asset, 1);

  EXPECT_FALSE(GraphCompiler::Compile(graph, kBlock).ok());

  GraphCompileContext closed{&f.store, 0};

  EXPECT_FALSE(GraphCompiler::Compile(graph, kBlock, closed).ok());
}

TEST(GraphAssetLifetimeTest, GraphsWithoutAssetNodesNeedNoContext) {
  AudioGraph graph;
  BuildOscillatorGraph(graph);

  StatusOr<GraphExecutionPlan> plan = GraphCompiler::Compile(graph, kBlock);

  ASSERT_TRUE(plan.ok());
  EXPECT_EQ(plan.value().pinned_asset_count(), 0u);
}

// ---- plan store ------------------------------------------------------------

TEST(GraphAssetLifetimeTest, ReleasingAStagedPlanReleasesItsPin) {
  Fixture f;
  const AudioAssetId asset = f.Load();

  f.HoldBaselinePin(asset);

  AudioGraph graph;
  BuildAssetGraph(graph, asset, 1);

  GraphPlanStore plans(kPlanSlots);
  StatusOr<GraphPlanHandle> handle =
      plans.BuildAndStage(graph, kBlock, f.context);

  ASSERT_TRUE(handle.ok());
  EXPECT_EQ(f.store.pin_count(asset), 2u);

  EXPECT_TRUE(plans.ReleaseStagedPlan(handle.value()));
  EXPECT_EQ(f.store.pin_count(asset), 1u);
}

TEST(GraphAssetLifetimeTest, RetiredPlanKeepsItsPinUntilItsSlotIsReused) {
  Fixture f;
  const AudioAssetId asset = f.Load();

  f.HoldBaselinePin(asset);

  AudioGraph asset_graph;
  BuildAssetGraph(asset_graph, asset, 1);

  AudioGraph oscillator_graph;
  BuildOscillatorGraph(oscillator_graph);

  GraphPlanStore plans(kPlanSlots);

  StatusOr<GraphPlanHandle> a =
      plans.BuildAndStage(asset_graph, kBlock, f.context);

  ASSERT_TRUE(a.ok());
  ASSERT_TRUE(plans.TryActivate(a.value()));
  EXPECT_EQ(f.store.pin_count(asset), 2u);

  StatusOr<GraphPlanHandle> b = plans.BuildAndStage(oscillator_graph, kBlock);

  ASSERT_TRUE(b.ok());
  ASSERT_TRUE(plans.TryActivate(b.value()));

  EXPECT_EQ(f.store.pin_count(asset), 2u);

  StatusOr<GraphPlanHandle> c = plans.BuildAndStage(oscillator_graph, kBlock);
  ASSERT_TRUE(c.ok());

  EXPECT_EQ(f.store.pin_count(asset), 1u);
}

}  // namespace
}  // namespace lavanda
