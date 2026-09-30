#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

#include "lavanda/graph/nodes/delay_node.h"
#include "lavanda/graph/nodes/gain_node.h"
#include "lavanda/graph/nodes/mixer_node.h"
#include "lavanda/graph/nodes/one_pole_high_pass_node.h"
#include "lavanda/graph/nodes/one_pole_low_pass_node.h"
#include "lavanda/graph/nodes/oscillator_node.h"
#include "lavanda/graph/nodes/output_node.h"
#include "lavanda/graph/nodes/pan_node.h"
#include "lavanda/runtime/audio_runtime.h"
#include "test_support/allocation_guard.h"
#include "test_support/fake_audio_device.h"

namespace lavanda {
namespace {

template <typename NodeT>
NodeT* NodeAs(AudioGraph& graph, NodeId id) {
  return static_cast<NodeT*>(graph.GetNode(id));
}

struct LinearIds {
  NodeId osc, gain, filter, pan, output;
};

LinearIds BuildLinearGraph(AudioGraph& graph) {
  LinearIds ids;

  ids.osc = graph.AddNode(std::make_unique<OscillatorNode>()).value();
  ids.gain = graph.AddNode(std::make_unique<GainNode>(1)).value();
  ids.filter = graph.AddNode(std::make_unique<OnePoleLowPassNode>(1)).value();
  ids.pan = graph.AddNode(std::make_unique<PanNode>()).value();
  ids.output = graph.AddNode(std::make_unique<OutputNode>(2)).value();

  graph.Connect(ids.osc, ids.gain, 0);
  graph.Connect(ids.gain, ids.filter, 0);
  graph.Connect(ids.filter, ids.pan, 0);
  graph.Connect(ids.pan, ids.output, 0);
  graph.SetOutput(ids.output);

  NodeAs<OscillatorNode>(graph, ids.osc)->SetFrequency(440.0f);
  NodeAs<GainNode>(graph, ids.gain)->SetGain(0.5f);
  NodeAs<OnePoleLowPassNode>(graph, ids.filter)->SetCutoffHz(5000.0f);

  return ids;
}

GraphPlanHandle CompileAndActivate(AudioRuntime& runtime) {
  StatusOr<GraphPlanHandle> handle = runtime.CompileAndStageGraph();

  EXPECT_TRUE(handle.ok());

  if (!handle.ok()) {
    return GraphPlanHandle();
  }

  EXPECT_TRUE(runtime.ActivateGraphPlan(handle.value()).ok());

  return handle.value();
}

bool HasNonSilence(const AudioBuffer& buffer) {
  for (std::uint32_t f = 0; f < buffer.frame_count(); ++f) {
    for (std::uint32_t c = 0; c < buffer.channel_count(); ++c) {
      if (buffer(f, c) != 0.0f) {
        return true;
      }
    }
  }

  return false;
}

TEST(RealtimeSafetyTest, RenderPathPerformsNoHeapAllocation) {
  auto owned_device = std::make_unique<test_support::FakeAudioDevice>();
  test_support::FakeAudioDevice* device = owned_device.get();
  AudioRuntime runtime(std::move(owned_device));

  ASSERT_TRUE(runtime.Start().ok());
  ASSERT_TRUE(runtime.Submit({.type = CommandType::kStartTone}).ok());
  ASSERT_TRUE(
      runtime.Submit({.type = CommandType::kSetFrequency, .value = 440.0f})
          .ok());
  ASSERT_TRUE(
      runtime.Submit({.type = CommandType::kSetGain, .value = 0.5f}).ok());

  AudioBuffer buffer(512, 2);

  {
    test_support::AllocationGuard guard;

    ASSERT_TRUE(device->PumpRender(buffer.View()));
    EXPECT_EQ(guard.allocation_count(), 0u);
  }
}

TEST(RealtimeSafetyTest, DrainingManyQueuedCommandsPerformsNoHeapAllocation) {
  auto owned_device = std::make_unique<test_support::FakeAudioDevice>();
  test_support::FakeAudioDevice* device = owned_device.get();
  AudioRuntime runtime(std::move(owned_device), /*command_queue_capacity=*/32);

  ASSERT_TRUE(runtime.Start().ok());

  for (int i = 0; i < 20; ++i) {
    ASSERT_TRUE(runtime
                    .Submit({.type = CommandType::kSetFrequency,
                             .value = static_cast<float>(i)})
                    .ok());
  }

  AudioBuffer buffer(64, 1);

  {
    test_support::AllocationGuard guard;

    ASSERT_TRUE(device->PumpRender(buffer.View()));
    EXPECT_EQ(guard.allocation_count(), 0u);
  }
}

TEST(RealtimeSafetyTest, PopulatedMixerRenderPerformsNoHeapAllocation) {
  auto owned_device = std::make_unique<test_support::FakeAudioDevice>();
  test_support::FakeAudioDevice* device = owned_device.get();
  AudioRuntime runtime(std::move(owned_device));

  ASSERT_TRUE(runtime.Start().ok());

  StatusOr<Bus> bus_or = runtime.CreateBus();

  ASSERT_TRUE(bus_or.ok());

  Bus bus = bus_or.value();
  std::vector<Voice> voices;

  for (int i = 0; i < 4; ++i) {
    StatusOr<Voice> voice_or = runtime.CreateVoice();

    ASSERT_TRUE(voice_or.ok());

    Voice voice = voice_or.value();

    ASSERT_TRUE(
        voice.SetFrequency(220.0f + static_cast<float>(i) * 110.0f).ok());
    ASSERT_TRUE(voice.SetGain(0.5f).ok());
    ASSERT_TRUE(voice.SetPan(-0.5f + static_cast<float>(i) * 0.3f).ok());

    if (i % 2 == 0) {
      ASSERT_TRUE(voice.SetBus(bus).ok());
    }

    ASSERT_TRUE(voice.Start().ok());

    voices.push_back(voice);
  }

  AudioBuffer buffer(512, 2);

  {
    test_support::AllocationGuard guard;

    ASSERT_TRUE(device->PumpRender(buffer.View()));
    EXPECT_EQ(guard.allocation_count(), 0u)
        << "populated mixer render allocated " << guard.allocation_count()
        << " time(s) -- see VoicePool::RenderVoiceSource, "
           "AudioRuntime::Impl::RenderVoicesAndBuses, mixer_math.cc";
  }
}

TEST(RealtimeSafetyTest, DrainingVoiceAndBusCommandsPerformsNoHeapAllocation) {
  auto owned_device = std::make_unique<test_support::FakeAudioDevice>();
  test_support::FakeAudioDevice* device = owned_device.get();
  AudioRuntime runtime(std::move(owned_device));

  ASSERT_TRUE(runtime.Start().ok());

  StatusOr<Bus> bus_or = runtime.CreateBus();

  ASSERT_TRUE(bus_or.ok());

  Bus bus = bus_or.value();

  StatusOr<Voice> voice_or = runtime.CreateVoice();

  ASSERT_TRUE(voice_or.ok());

  Voice voice = voice_or.value();

  ASSERT_TRUE(voice.SetFrequency(440.0f).ok());
  ASSERT_TRUE(voice.SetGain(0.8f).ok());
  ASSERT_TRUE(voice.SetPan(0.25f).ok());
  ASSERT_TRUE(voice.SetBus(bus).ok());
  ASSERT_TRUE(voice.Start().ok());
  ASSERT_TRUE(bus.SetGain(0.6f).ok());

  AudioBuffer buffer(64, 2);

  {
    test_support::AllocationGuard guard;

    ASSERT_TRUE(device->PumpRender(buffer.View()));
    EXPECT_EQ(guard.allocation_count(), 0u);
  }
}

TEST(RealtimeSafetyTest,
     RepeatedRenderBlocksWithActiveVoicesStayAllocationFree) {
  auto owned_device = std::make_unique<test_support::FakeAudioDevice>();
  test_support::FakeAudioDevice* device = owned_device.get();
  AudioRuntime runtime(std::move(owned_device));

  ASSERT_TRUE(runtime.Start().ok());

  StatusOr<Voice> voice_or = runtime.CreateVoice();

  ASSERT_TRUE(voice_or.ok());

  Voice voice = voice_or.value();

  ASSERT_TRUE(voice.SetFrequency(440.0f).ok());
  ASSERT_TRUE(voice.SetGain(1.0f).ok());
  ASSERT_TRUE(voice.Start().ok());

  AudioBuffer buffer(128, 2);

  {
    test_support::AllocationGuard guard;

    for (int block = 0; block < 50; ++block) {
      ASSERT_TRUE(device->PumpRender(buffer.View()));
    }

    EXPECT_EQ(guard.allocation_count(), 0u);
  }
}

TEST(RealtimeSafetyTest,
     LinearGraphActivationAndRenderPerformNoHeapAllocation) {
  auto owned_device = std::make_unique<test_support::FakeAudioDevice>();
  test_support::FakeAudioDevice* device = owned_device.get();
  AudioRuntime runtime(std::move(owned_device));

  ASSERT_TRUE(runtime.Start().ok());

  BuildLinearGraph(runtime.graph());
  CompileAndActivate(runtime);

  AudioBuffer buffer(512, 2);

  {
    test_support::AllocationGuard guard;

    for (int block = 0; block < 50; ++block) {
      ASSERT_TRUE(device->PumpRender(buffer.View()));
    }

    EXPECT_EQ(guard.allocation_count(), 0u)
        << "graph activation/render allocated " << guard.allocation_count()
        << " time(s) -- see GraphPlanStore::TryActivate, "
           "GraphExecutor::Render, and each node's Process()";
  }

  EXPECT_TRUE(HasNonSilence(buffer))
      << "graph produced silence -- the test may be passing vacuously";
}

TEST(RealtimeSafetyTest, MultiSourceMixerGraphPerformsNoHeapAllocation) {
  auto owned_device = std::make_unique<test_support::FakeAudioDevice>();
  test_support::FakeAudioDevice* device = owned_device.get();
  AudioRuntime runtime(std::move(owned_device));

  ASSERT_TRUE(runtime.Start().ok());

  AudioGraph& graph = runtime.graph();

  NodeId osc_a = graph.AddNode(std::make_unique<OscillatorNode>()).value();
  NodeId osc_b = graph.AddNode(std::make_unique<OscillatorNode>()).value();
  NodeId osc_c = graph.AddNode(std::make_unique<OscillatorNode>()).value();
  NodeId mixer = graph.AddNode(std::make_unique<MixerNode>(1, 3)).value();
  NodeId filter =
      graph.AddNode(std::make_unique<OnePoleLowPassNode>(1)).value();
  NodeId pan = graph.AddNode(std::make_unique<PanNode>()).value();
  NodeId output = graph.AddNode(std::make_unique<OutputNode>(2)).value();

  NodeAs<OscillatorNode>(graph, osc_a)->SetFrequency(220.0f);
  NodeAs<OscillatorNode>(graph, osc_b)->SetFrequency(330.0f);
  NodeAs<OscillatorNode>(graph, osc_c)->SetFrequency(440.0f);
  NodeAs<OscillatorNode>(graph, osc_a)->SetGain(0.3f);
  NodeAs<OscillatorNode>(graph, osc_b)->SetGain(0.3f);
  NodeAs<OscillatorNode>(graph, osc_c)->SetGain(0.3f);

  graph.Connect(osc_a, mixer, 0);
  graph.Connect(osc_b, mixer, 1);
  graph.Connect(osc_c, mixer, 2);
  graph.Connect(mixer, filter, 0);
  graph.Connect(filter, pan, 0);
  graph.Connect(pan, output, 0);
  graph.SetOutput(output);

  CompileAndActivate(runtime);

  AudioBuffer buffer(512, 2);

  {
    test_support::AllocationGuard guard;

    for (int block = 0; block < 50; ++block) {
      ASSERT_TRUE(device->PumpRender(buffer.View()));
    }

    EXPECT_EQ(guard.allocation_count(), 0u);
  }

  EXPECT_TRUE(HasNonSilence(buffer));
}

TEST(RealtimeSafetyTest,
     BranchingGraphWithBufferReusePerformsNoHeapAllocation) {
  auto owned_device = std::make_unique<test_support::FakeAudioDevice>();
  test_support::FakeAudioDevice* device = owned_device.get();
  AudioRuntime runtime(std::move(owned_device));

  ASSERT_TRUE(runtime.Start().ok());

  AudioGraph& graph = runtime.graph();

  NodeId osc = graph.AddNode(std::make_unique<OscillatorNode>()).value();
  NodeId gain_a = graph.AddNode(std::make_unique<GainNode>(1)).value();
  NodeId gain_b = graph.AddNode(std::make_unique<GainNode>(1)).value();
  NodeId mixer = graph.AddNode(std::make_unique<MixerNode>(1, 2)).value();
  NodeId pan = graph.AddNode(std::make_unique<PanNode>()).value();
  NodeId output = graph.AddNode(std::make_unique<OutputNode>(2)).value();

  NodeAs<GainNode>(graph, gain_a)->SetGain(0.4f);
  NodeAs<GainNode>(graph, gain_b)->SetGain(0.2f);

  graph.Connect(osc, gain_a, 0);
  graph.Connect(osc, gain_b, 0);
  graph.Connect(gain_a, mixer, 0);
  graph.Connect(gain_b, mixer, 1);
  graph.Connect(mixer, pan, 0);
  graph.Connect(pan, output, 0);
  graph.SetOutput(output);

  CompileAndActivate(runtime);

  AudioBuffer buffer(512, 2);

  {
    test_support::AllocationGuard guard;

    for (int block = 0; block < 50; ++block) {
      ASSERT_TRUE(device->PumpRender(buffer.View()));
    }

    EXPECT_EQ(guard.allocation_count(), 0u);
  }

  EXPECT_TRUE(HasNonSilence(buffer));
}

TEST(RealtimeSafetyTest,
     StatefulDelayAndHighPassGraphPerformsNoHeapAllocation) {
  auto owned_device = std::make_unique<test_support::FakeAudioDevice>();
  test_support::FakeAudioDevice* device = owned_device.get();
  AudioRuntime runtime(std::move(owned_device));

  ASSERT_TRUE(runtime.Start().ok());

  AudioGraph& graph = runtime.graph();

  NodeId osc = graph.AddNode(std::make_unique<OscillatorNode>()).value();
  NodeId delay = graph.AddNode(std::make_unique<DelayNode>(1, 4800)).value();
  NodeId high_pass =
      graph.AddNode(std::make_unique<OnePoleHighPassNode>(1)).value();
  NodeId pan = graph.AddNode(std::make_unique<PanNode>()).value();
  NodeId output = graph.AddNode(std::make_unique<OutputNode>(2)).value();

  NodeAs<DelayNode>(graph, delay)->SetDelayFrames(480);
  NodeAs<OnePoleHighPassNode>(graph, high_pass)->SetCutoffHz(100.0f);

  graph.Connect(osc, delay, 0);
  graph.Connect(delay, high_pass, 0);
  graph.Connect(high_pass, pan, 0);
  graph.Connect(pan, output, 0);
  graph.SetOutput(output);

  CompileAndActivate(runtime);

  ASSERT_TRUE(runtime.GetGraphNode(delay).SetDelayFrames(240.0f).ok());

  AudioBuffer buffer(512, 2);

  {
    test_support::AllocationGuard guard;

    for (int block = 0; block < 50; ++block) {
      ASSERT_TRUE(device->PumpRender(buffer.View()));
    }

    EXPECT_EQ(guard.allocation_count(), 0u);
  }

  EXPECT_TRUE(HasNonSilence(buffer));
}

TEST(RealtimeSafetyTest, NodeParameterCommandsPerformNoHeapAllocation) {
  auto owned_device = std::make_unique<test_support::FakeAudioDevice>();
  test_support::FakeAudioDevice* device = owned_device.get();
  AudioRuntime runtime(std::move(owned_device));

  ASSERT_TRUE(runtime.Start().ok());

  LinearIds ids = BuildLinearGraph(runtime.graph());

  CompileAndActivate(runtime);

  ASSERT_TRUE(runtime.GetGraphNode(ids.osc).SetFrequency(880.0f).ok());
  ASSERT_TRUE(runtime.GetGraphNode(ids.gain).SetGain(0.8f).ok());
  ASSERT_TRUE(runtime.GetGraphNode(ids.filter).SetCutoffHz(2000.0f).ok());
  ASSERT_TRUE(runtime.GetGraphNode(ids.pan).SetPan(-0.5f).ok());

  AudioBuffer buffer(512, 2);

  {
    test_support::AllocationGuard guard;

    ASSERT_TRUE(device->PumpRender(buffer.View()));
    EXPECT_EQ(guard.allocation_count(), 0u);
  }

  EXPECT_TRUE(HasNonSilence(buffer));
}

TEST(RealtimeSafetyTest,
     PlanReplacementPerformsNoHeapAllocationAndDefersDestruction) {
  auto owned_device = std::make_unique<test_support::FakeAudioDevice>();
  test_support::FakeAudioDevice* device = owned_device.get();
  AudioRuntime runtime(std::move(owned_device));

  ASSERT_TRUE(runtime.Start().ok());

  LinearIds ids = BuildLinearGraph(runtime.graph());
  GraphPlanHandle plan_a = CompileAndActivate(runtime);
  AudioBuffer buffer(512, 2);

  ASSERT_TRUE(device->PumpRender(buffer.View()));

  NodeAs<GainNode>(runtime.graph(), ids.gain)->SetGain(0.25f);

  GraphPlanHandle plan_b = CompileAndActivate(runtime);

  ASSERT_NE(plan_b.slot_index, plan_a.slot_index);

  {
    test_support::AllocationGuard guard;

    ASSERT_TRUE(device->PumpRender(buffer.View()));
    EXPECT_EQ(guard.allocation_count(), 0u);
  }

  EXPECT_TRUE(HasNonSilence(buffer));

  GraphPlanHandle plan_c = CompileAndActivate(runtime);

  EXPECT_EQ(plan_c.slot_index, plan_a.slot_index)
      << "the retired slot should be reclaimed by the next staging call";
}

TEST(RealtimeSafetyTest,
     RenderingWithNoActiveOrOnlyStagedPlanPerformsNoHeapAllocation) {
  auto owned_device = std::make_unique<test_support::FakeAudioDevice>();
  test_support::FakeAudioDevice* device = owned_device.get();
  AudioRuntime runtime(std::move(owned_device));

  ASSERT_TRUE(runtime.Start().ok());

  AudioBuffer buffer(512, 2);

  {
    test_support::AllocationGuard guard;

    for (int block = 0; block < 10; ++block) {
      ASSERT_TRUE(device->PumpRender(buffer.View()));
    }

    EXPECT_EQ(guard.allocation_count(), 0u);
  }

  BuildLinearGraph(runtime.graph());

  ASSERT_TRUE(runtime.CompileAndStageGraph().ok());

  {
    test_support::AllocationGuard guard;

    for (int block = 0; block < 10; ++block) {
      ASSERT_TRUE(device->PumpRender(buffer.View()));
    }

    EXPECT_EQ(guard.allocation_count(), 0u);
  }
}

TEST(RealtimeSafetyTest, GraphCompilationAllocatesOnTheControlSide) {
  AudioRuntime runtime(std::make_unique<test_support::FakeAudioDevice>());

  ASSERT_TRUE(runtime.Start().ok());

  BuildLinearGraph(runtime.graph());

  std::size_t control_side_allocations = 0;

  {
    test_support::AllocationGuard guard;

    StatusOr<GraphPlanHandle> handle = runtime.CompileAndStageGraph();
    control_side_allocations = guard.allocation_count();

    ASSERT_TRUE(handle.ok());
  }

  EXPECT_GT(control_side_allocations, 0u);
}

}  // namespace
}  // namespace lavanda
