#include <gtest/gtest.h>

#include "lavanda/graph/graph.h"
#include "lavanda/graph/nodes/gain_node.h"
#include "lavanda/graph/nodes/oscillator_node.h"
#include "lavanda/graph/nodes/output_node.h"

namespace lavanda {
namespace {

TEST(AudioGraphTest, AddNodeSucceedsAndReturnsValidId) {
  AudioGraph graph;
  StatusOr<NodeId> id = graph.AddNode(std::make_unique<OscillatorNode>());

  ASSERT_TRUE(id.ok());
  EXPECT_TRUE(graph.IsValidNode(id.value()));
}

TEST(AudioGraphTest, AddNodeRejectsNullNode) {
  AudioGraph graph;
  StatusOr<NodeId> id = graph.AddNode(nullptr);

  EXPECT_FALSE(id.ok());
  EXPECT_EQ(id.status().code(), ErrorCode::kInvalidArgument);
}

TEST(AudioGraphTest, AddNodeFailsWhenAtCapacity) {
  GraphConfig config;
  config.max_nodes = 1;

  AudioGraph graph(config);

  ASSERT_TRUE(graph.AddNode(std::make_unique<OscillatorNode>()).ok());

  StatusOr<NodeId> second = graph.AddNode(std::make_unique<OscillatorNode>());

  EXPECT_FALSE(second.ok());
  EXPECT_EQ(second.status().code(), ErrorCode::kResourceExhausted);
}

TEST(AudioGraphTest, RemoveNodeFreesSlotForReuse) {
  AudioGraph graph;
  StatusOr<NodeId> first = graph.AddNode(std::make_unique<OscillatorNode>());

  ASSERT_TRUE(first.ok());
  ASSERT_TRUE(graph.RemoveNode(first.value()).ok());

  StatusOr<NodeId> second = graph.AddNode(std::make_unique<OscillatorNode>());

  ASSERT_TRUE(second.ok());
  EXPECT_EQ(second.value().index, first.value().index);
  EXPECT_NE(second.value().generation, first.value().generation);
}

TEST(AudioGraphTest, StaleHandleCannotControlReusedNode) {
  GraphConfig config;
  config.max_nodes = 1;

  AudioGraph graph(config);

  StatusOr<NodeId> node_a = graph.AddNode(std::make_unique<OscillatorNode>());

  ASSERT_TRUE(node_a.ok());
  ASSERT_TRUE(graph.RemoveNode(node_a.value()).ok());

  StatusOr<NodeId> node_b = graph.AddNode(std::make_unique<GainNode>(1));

  ASSERT_TRUE(node_b.ok());
  EXPECT_EQ(node_b.value().index, node_a.value().index);
  EXPECT_NE(node_b.value().generation, node_a.value().generation);

  EXPECT_FALSE(graph.IsValidNode(node_a.value()));
  EXPECT_EQ(graph.GetNode(node_a.value()), nullptr);
  EXPECT_TRUE(graph.IsValidNode(node_b.value()));
  EXPECT_NE(graph.GetNode(node_b.value()), nullptr);
}

TEST(AudioGraphTest, RemoveNodeRemovesConnectionsAsSourceAndDestination) {
  AudioGraph graph;
  StatusOr<NodeId> osc = graph.AddNode(std::make_unique<OscillatorNode>());
  StatusOr<NodeId> gain = graph.AddNode(std::make_unique<GainNode>(1));

  ASSERT_TRUE(osc.ok());
  ASSERT_TRUE(gain.ok());
  ASSERT_TRUE(graph.Connect(osc.value(), gain.value(), 0).ok());
  ASSERT_EQ(graph.connections().size(), 1u);

  ASSERT_TRUE(graph.RemoveNode(osc.value()).ok());
  EXPECT_EQ(graph.connections().size(), 0u);
}

TEST(AudioGraphTest, RemoveNodeClearsOutputDesignationIfItWasTheOutput) {
  AudioGraph graph;
  StatusOr<NodeId> node = graph.AddNode(std::make_unique<OutputNode>(1));

  ASSERT_TRUE(node.ok());
  ASSERT_TRUE(graph.SetOutput(node.value()).ok());
  ASSERT_EQ(graph.output(), node.value());

  ASSERT_TRUE(graph.RemoveNode(node.value()).ok());
  EXPECT_FALSE(graph.output().is_valid());
}

TEST(AudioGraphTest, ConnectSucceedsForCompatibleChannelCounts) {
  AudioGraph graph;
  StatusOr<NodeId> osc = graph.AddNode(std::make_unique<OscillatorNode>());
  StatusOr<NodeId> gain = graph.AddNode(std::make_unique<GainNode>(1));
  ASSERT_TRUE(osc.ok());
  ASSERT_TRUE(gain.ok());
  EXPECT_TRUE(graph.Connect(osc.value(), gain.value(), 0).ok());
}

TEST(AudioGraphTest, ConnectRejectsIncompatibleChannelCounts) {
  AudioGraph graph;
  StatusOr<NodeId> osc = graph.AddNode(std::make_unique<OscillatorNode>());
  StatusOr<NodeId> gain = graph.AddNode(std::make_unique<GainNode>(2));

  ASSERT_TRUE(osc.ok());
  ASSERT_TRUE(gain.ok());

  Status status = graph.Connect(osc.value(), gain.value(), 0);

  EXPECT_FALSE(status.ok());
  EXPECT_EQ(status.code(), ErrorCode::kInvalidArgument);
}

TEST(AudioGraphTest, ConnectRejectsSelfConnection) {
  AudioGraph graph;
  StatusOr<NodeId> gain = graph.AddNode(std::make_unique<GainNode>(1));

  ASSERT_TRUE(gain.ok());

  Status status = graph.Connect(gain.value(), gain.value(), 0);

  EXPECT_FALSE(status.ok());
}

TEST(AudioGraphTest, ConnectRejectsInvalidNodeIds) {
  AudioGraph graph;
  StatusOr<NodeId> gain = graph.AddNode(std::make_unique<GainNode>(1));

  ASSERT_TRUE(gain.ok());

  NodeId invalid{99, 0};

  EXPECT_FALSE(graph.Connect(invalid, gain.value(), 0).ok());
  EXPECT_FALSE(graph.Connect(gain.value(), invalid, 0).ok());
}

TEST(AudioGraphTest, ConnectRejectsOutOfRangeInputIndex) {
  AudioGraph graph;
  StatusOr<NodeId> osc = graph.AddNode(std::make_unique<OscillatorNode>());
  StatusOr<NodeId> gain = graph.AddNode(std::make_unique<GainNode>(1));

  ASSERT_TRUE(osc.ok());
  ASSERT_TRUE(gain.ok());

  Status status = graph.Connect(osc.value(), gain.value(), 5);

  EXPECT_FALSE(status.ok());
}

TEST(AudioGraphTest, ConnectRejectsDuplicateConnectionToSameInputSlot) {
  AudioGraph graph;
  StatusOr<NodeId> osc_a = graph.AddNode(std::make_unique<OscillatorNode>());
  StatusOr<NodeId> osc_b = graph.AddNode(std::make_unique<OscillatorNode>());
  StatusOr<NodeId> gain = graph.AddNode(std::make_unique<GainNode>(1));

  ASSERT_TRUE(osc_a.ok());
  ASSERT_TRUE(osc_b.ok());
  ASSERT_TRUE(gain.ok());
  ASSERT_TRUE(graph.Connect(osc_a.value(), gain.value(), 0).ok());

  Status status = graph.Connect(osc_b.value(), gain.value(), 0);

  EXPECT_FALSE(status.ok());
}

TEST(AudioGraphTest, DisconnectAllowsReconnectingSameInputSlot) {
  AudioGraph graph;
  StatusOr<NodeId> osc_a = graph.AddNode(std::make_unique<OscillatorNode>());
  StatusOr<NodeId> osc_b = graph.AddNode(std::make_unique<OscillatorNode>());
  StatusOr<NodeId> gain = graph.AddNode(std::make_unique<GainNode>(1));

  ASSERT_TRUE(osc_a.ok());
  ASSERT_TRUE(osc_b.ok());
  ASSERT_TRUE(gain.ok());
  ASSERT_TRUE(graph.Connect(osc_a.value(), gain.value(), 0).ok());

  ASSERT_TRUE(graph.Disconnect(gain.value(), 0).ok());
  EXPECT_TRUE(graph.Connect(osc_b.value(), gain.value(), 0).ok());
}

TEST(AudioGraphTest, ConnectFailsWhenAtConnectionCapacity) {
  GraphConfig config;
  config.max_connections = 1;

  AudioGraph graph(config);

  StatusOr<NodeId> osc_a = graph.AddNode(std::make_unique<OscillatorNode>());
  StatusOr<NodeId> osc_b = graph.AddNode(std::make_unique<OscillatorNode>());
  StatusOr<NodeId> gain_a = graph.AddNode(std::make_unique<GainNode>(1));
  StatusOr<NodeId> gain_b = graph.AddNode(std::make_unique<GainNode>(1));

  ASSERT_TRUE(osc_a.ok() && osc_b.ok() && gain_a.ok() && gain_b.ok());
  ASSERT_TRUE(graph.Connect(osc_a.value(), gain_a.value(), 0).ok());

  Status status = graph.Connect(osc_b.value(), gain_b.value(), 0);

  EXPECT_FALSE(status.ok());
  EXPECT_EQ(status.code(), ErrorCode::kResourceExhausted);
}

TEST(AudioGraphTest, SetOutputSucceedsForValidNode) {
  AudioGraph graph;
  StatusOr<NodeId> node = graph.AddNode(std::make_unique<OutputNode>(1));

  ASSERT_TRUE(node.ok());
  EXPECT_TRUE(graph.SetOutput(node.value()).ok());
  EXPECT_EQ(graph.output(), node.value());
}

TEST(AudioGraphTest, SetOutputRejectsInvalidNode) {
  AudioGraph graph;
  NodeId invalid{5, 0};

  EXPECT_FALSE(graph.SetOutput(invalid).ok());
}

TEST(AudioGraphTest, DefaultOutputIsInvalid) {
  AudioGraph graph;

  EXPECT_FALSE(graph.output().is_valid());
}

TEST(AudioGraphTest, AllNodeIdsReturnsOnlyCurrentlyValidNodes) {
  AudioGraph graph;
  StatusOr<NodeId> a = graph.AddNode(std::make_unique<OscillatorNode>());
  StatusOr<NodeId> b = graph.AddNode(std::make_unique<OscillatorNode>());

  ASSERT_TRUE(a.ok() && b.ok());
  ASSERT_TRUE(graph.RemoveNode(a.value()).ok());

  std::vector<NodeId> ids = graph.AllNodeIds();

  ASSERT_EQ(ids.size(), 1u);
  EXPECT_EQ(ids[0], b.value());
}

}  // namespace
}  // namespace lavanda
