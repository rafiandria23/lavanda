#include "lavanda/graph/graph_plan_store.h"

#include <gtest/gtest.h>

#include <memory>

#include "lavanda/graph/nodes/oscillator_node.h"
#include "lavanda/graph/nodes/output_node.h"

namespace lavanda {
namespace {

AudioGraph MakeSimpleGraph() {
  AudioGraph graph;
  auto osc = graph.AddNode(std::make_unique<OscillatorNode>());
  auto output = graph.AddNode(std::make_unique<OutputNode>(1));

  graph.Connect(osc.value(), output.value(), 0);
  graph.SetOutput(output.value());

  return graph;
}

TEST(GraphPlanStoreTest, ReportsConstructedCapacity) {
  GraphPlanStore store(3);

  EXPECT_EQ(store.capacity(), 3u);
}

TEST(GraphPlanStoreTest, ZeroCapacityIsClampedToAtLeastOne) {
  GraphPlanStore store(0);

  EXPECT_GE(store.capacity(), 1u);
}

TEST(GraphPlanStoreTest, ActivePlanIsNullBeforeAnyActivation) {
  GraphPlanStore store(2);

  EXPECT_EQ(store.ActivePlan(), nullptr);
}

TEST(GraphPlanStoreTest, BuildAndStageSucceedsForValidGraph) {
  GraphPlanStore store(2);
  AudioGraph graph = MakeSimpleGraph();
  StatusOr<GraphPlanHandle> handle = store.BuildAndStage(graph, 512);

  ASSERT_TRUE(handle.ok());
  EXPECT_TRUE(handle.value().is_valid());
}

TEST(GraphPlanStoreTest,
     BuildAndStageFailsForInvalidGraphWithoutConsumingCapacity) {
  GraphPlanStore store(1);
  AudioGraph empty_graph;
  StatusOr<GraphPlanHandle> failed = store.BuildAndStage(empty_graph, 512);

  EXPECT_FALSE(failed.ok());

  AudioGraph valid_graph = MakeSimpleGraph();
  StatusOr<GraphPlanHandle> succeeded = store.BuildAndStage(valid_graph, 512);

  EXPECT_TRUE(succeeded.ok());
}

TEST(GraphPlanStoreTest, BuildAndStageFailsWhenAtCapacity) {
  GraphPlanStore store(1);
  AudioGraph graph_a = MakeSimpleGraph();

  ASSERT_TRUE(store.BuildAndStage(graph_a, 512).ok());

  AudioGraph graph_b = MakeSimpleGraph();
  StatusOr<GraphPlanHandle> second = store.BuildAndStage(graph_b, 512);

  EXPECT_FALSE(second.ok());
  EXPECT_EQ(second.status().code(), ErrorCode::kResourceExhausted);
}

TEST(GraphPlanStoreTest, TryActivateMakesPlanActive) {
  GraphPlanStore store(2);
  AudioGraph graph = MakeSimpleGraph();
  StatusOr<GraphPlanHandle> handle = store.BuildAndStage(graph, 512);

  ASSERT_TRUE(handle.ok());

  store.TryActivate(handle.value());

  EXPECT_NE(store.ActivePlan(), nullptr);
}

TEST(GraphPlanStoreTest, TryActivateIgnoresStaleGeneration) {
  GraphPlanStore store(1);
  AudioGraph graph = MakeSimpleGraph();
  StatusOr<GraphPlanHandle> handle = store.BuildAndStage(graph, 512);

  ASSERT_TRUE(handle.ok());

  GraphPlanHandle stale = handle.value();

  stale.generation += 1;
  store.TryActivate(stale);

  EXPECT_EQ(store.ActivePlan(), nullptr)
      << "a generation mismatch must be silently ignored";
}

TEST(GraphPlanStoreTest, TryActivateIgnoresOutOfRangeSlotIndex) {
  GraphPlanStore store(1);
  GraphPlanHandle bogus{99, 0};

  store.TryActivate(bogus);

  EXPECT_EQ(store.ActivePlan(), nullptr);
}

TEST(GraphPlanStoreTest, RepeatedActivationFreesThePreviousSlotForReuse) {
  GraphPlanStore store(2);
  AudioGraph graph_a = MakeSimpleGraph();
  StatusOr<GraphPlanHandle> handle_a = store.BuildAndStage(graph_a, 512);

  ASSERT_TRUE(handle_a.ok());

  store.TryActivate(handle_a.value());

  AudioGraph graph_b = MakeSimpleGraph();
  StatusOr<GraphPlanHandle> handle_b = store.BuildAndStage(graph_b, 512);

  ASSERT_TRUE(handle_b.ok());
  EXPECT_NE(handle_b.value().slot_index, handle_a.value().slot_index)
      << "slot A is still Active -- must not be reused while B is only staged";

  store.TryActivate(handle_b.value());

  AudioGraph graph_c = MakeSimpleGraph();
  StatusOr<GraphPlanHandle> handle_c = store.BuildAndStage(graph_c, 512);

  ASSERT_TRUE(handle_c.ok());
  EXPECT_EQ(handle_c.value().slot_index, handle_a.value().slot_index);
  EXPECT_NE(handle_c.value().generation, handle_a.value().generation);
}

TEST(GraphPlanStoreTest, ActivatingSecondPlanChangesActivePlanIdentity) {
  GraphPlanStore store(2);
  AudioGraph graph_a = MakeSimpleGraph();
  StatusOr<GraphPlanHandle> handle_a = store.BuildAndStage(graph_a, 512);

  ASSERT_TRUE(handle_a.ok());

  store.TryActivate(handle_a.value());

  GraphExecutionPlan* first_active = store.ActivePlan();

  ASSERT_NE(first_active, nullptr);

  AudioGraph graph_b = MakeSimpleGraph();
  StatusOr<GraphPlanHandle> handle_b = store.BuildAndStage(graph_b, 512);

  ASSERT_TRUE(handle_b.ok());

  store.TryActivate(handle_b.value());

  GraphExecutionPlan* second_active = store.ActivePlan();

  ASSERT_NE(second_active, nullptr);

  EXPECT_NE(first_active, second_active);
}

TEST(GraphPlanStoreTest, ReleaseStagedPlanFreesAPendingSlot) {
  GraphPlanStore store(1);
  AudioGraph graph = MakeSimpleGraph();
  StatusOr<GraphPlanHandle> handle = store.BuildAndStage(graph, 512);

  ASSERT_TRUE(handle.ok());

  store.ReleaseStagedPlan(handle.value());

  AudioGraph another = MakeSimpleGraph();
  StatusOr<GraphPlanHandle> reused = store.BuildAndStage(another, 512);

  ASSERT_TRUE(reused.ok());
  EXPECT_EQ(reused.value().slot_index, handle.value().slot_index);
}

TEST(GraphPlanStoreTest, ReleaseStagedPlanIsSafeNoOpOnAlreadyActiveSlot) {
  GraphPlanStore store(1);
  AudioGraph graph = MakeSimpleGraph();
  StatusOr<GraphPlanHandle> handle = store.BuildAndStage(graph, 512);

  ASSERT_TRUE(handle.ok());

  store.TryActivate(handle.value());
  store.ReleaseStagedPlan(handle.value());

  EXPECT_NE(store.ActivePlan(), nullptr);
}

TEST(GraphPlanStoreTest, ReleaseStagedPlanIsSafeNoOpOnStaleHandle) {
  GraphPlanStore store(1);
  GraphPlanHandle bogus{0, 999};

  store.ReleaseStagedPlan(bogus);

  SUCCEED();
}

}  // namespace
}  // namespace lavanda
