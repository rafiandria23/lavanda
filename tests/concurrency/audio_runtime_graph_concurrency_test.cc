#include <gtest/gtest.h>

#include <atomic>
#include <cstdint>
#include <memory>
#include <set>
#include <thread>

#include "lavanda/graph/nodes/gain_node.h"
#include "lavanda/graph/nodes/one_pole_low_pass_node.h"
#include "lavanda/graph/nodes/oscillator_node.h"
#include "lavanda/graph/nodes/output_node.h"
#include "lavanda/graph/nodes/pan_node.h"
#include "lavanda/runtime/audio_runtime.h"
#include "test_support/fake_audio_device.h"

namespace lavanda {
namespace {

constexpr int kMaxRetryAttempts = 10'000'000;

template <typename NodeT>
NodeT* NodeAs(AudioGraph& graph, NodeId id) {
  return static_cast<NodeT*>(graph.GetNode(id));
}

struct GraphIds {
  NodeId osc, gain, filter, pan, output;
};

GraphIds BuildStereoChain(AudioGraph& graph) {
  GraphIds ids;

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

bool HasNonSilence(const AudioBuffer& buffer) {
  for (std::uint32_t f = 0; f < buffer.frame_count(); ++f) {
    for (std::uint32_t c = 0; c < buffer.channel_count(); ++c) {
      if (buffer(f, c) != 0.0f) return true;
    }
  }
  return false;
}

template <typename Fn>

bool RetryUntilOk(Fn&& fn) {
  for (int attempt = 0; attempt < kMaxRetryAttempts; ++attempt) {
    if (fn().ok()) return true;
    std::this_thread::yield();
  }
  return false;
}

GraphPlanHandle CompileWithRetry(AudioRuntime& runtime) {
  for (int attempt = 0; attempt < kMaxRetryAttempts; ++attempt) {
    StatusOr<GraphPlanHandle> handle = runtime.CompileAndStageGraph();

    if (handle.ok()) {
      return handle.value();
    }

    std::this_thread::yield();
  }

  return GraphPlanHandle();
}

class RenderLoop {
 public:
  explicit RenderLoop(test_support::FakeAudioDevice* device) {
    thread_ = std::thread([this, device]() {
      AudioBuffer buffer(64, 2);

      while (keep_running_.load(std::memory_order_relaxed)) {
        device->PumpRender(buffer.View());
        std::this_thread::yield();
      }
    });
  }

  ~RenderLoop() { Stop(); }

  RenderLoop(const RenderLoop&) = delete;
  RenderLoop& operator=(const RenderLoop&) = delete;

  void Stop() {
    keep_running_.store(false, std::memory_order_relaxed);

    if (thread_.joinable()) {
      thread_.join();
    }
  }

 private:
  std::atomic<bool> keep_running_{true};
  std::thread thread_;
};

TEST(AudioRuntimeGraphConcurrencyTest,
     RepeatedRecompileAndActivateUnderConcurrentRendering) {
  auto owned_device = std::make_unique<test_support::FakeAudioDevice>();
  test_support::FakeAudioDevice* device = owned_device.get();

  RuntimeConfig config;
  config.max_graph_plans = 2;
  config.command_queue_capacity = 8;

  AudioRuntime runtime(std::move(owned_device), config);

  ASSERT_TRUE(runtime.Start().ok());

  GraphIds ids = BuildStereoChain(runtime.graph());

  std::set<std::uint32_t> slots_used;

  constexpr int kIterations = 300;

  {
    RenderLoop render_loop(device);

    for (int i = 0; i < kIterations; ++i) {
      NodeAs<OscillatorNode>(runtime.graph(), ids.osc)
          ->SetFrequency(200.0f + static_cast<float>(i % 600));

      GraphPlanHandle handle = CompileWithRetry(runtime);

      ASSERT_TRUE(handle.is_valid());

      slots_used.insert(handle.slot_index);

      ASSERT_TRUE(
          RetryUntilOk([&]() { return runtime.ActivateGraphPlan(handle); }));
    }

    render_loop.Stop();
  }

  EXPECT_EQ(slots_used.size(), 2u);
  EXPECT_GT(runtime.stats().render_count, 0u);

  AudioBuffer final_buffer(64, 2);

  ASSERT_TRUE(device->PumpRender(final_buffer.View()));
  EXPECT_TRUE(HasNonSilence(final_buffer));

  EXPECT_TRUE(runtime.Shutdown().ok());
}

TEST(AudioRuntimeGraphConcurrencyTest,
     NodeParameterCommandStormAgainstActivePlan) {
  auto owned_device = std::make_unique<test_support::FakeAudioDevice>();
  test_support::FakeAudioDevice* device = owned_device.get();
  AudioRuntime runtime(std::move(owned_device));

  ASSERT_TRUE(runtime.Start().ok());

  GraphIds ids = BuildStereoChain(runtime.graph());
  GraphPlanHandle plan = CompileWithRetry(runtime);

  ASSERT_TRUE(plan.is_valid());
  ASSERT_TRUE(RetryUntilOk([&]() { return runtime.ActivateGraphPlan(plan); }));

  GraphNodeHandle osc = runtime.GetGraphNode(ids.osc);
  GraphNodeHandle gain = runtime.GetGraphNode(ids.gain);
  GraphNodeHandle filter = runtime.GetGraphNode(ids.filter);
  GraphNodeHandle pan = runtime.GetGraphNode(ids.pan);

  {
    RenderLoop render_loop(device);

    constexpr int kIterations = 10000;

    for (int i = 0; i < kIterations; ++i) {
      switch (i % 4) {
        case 0:
          static_cast<void>(
              osc.SetFrequency(200.0f + static_cast<float>(i % 400)));
          break;
        case 1:
          static_cast<void>(
              gain.SetGain(0.1f + 0.05f * static_cast<float>(i % 18)));
          break;
        case 2:
          static_cast<void>(
              filter.SetCutoffHz(500.0f + 100.0f * static_cast<float>(i % 40)));
          break;
        default:
          static_cast<void>(
              pan.SetPan(-1.0f + 0.1f * static_cast<float>(i % 20)));
          break;
      }
    }

    ASSERT_TRUE(RetryUntilOk([&]() { return osc.SetFrequency(440.0f); }));
    ASSERT_TRUE(RetryUntilOk([&]() { return gain.SetGain(0.5f); }));
    ASSERT_TRUE(RetryUntilOk([&]() { return filter.SetCutoffHz(5000.0f); }));
    ASSERT_TRUE(RetryUntilOk([&]() { return pan.SetPan(0.0f); }));

    render_loop.Stop();
  }

  AudioBuffer final_buffer(64, 2);

  ASSERT_TRUE(device->PumpRender(final_buffer.View()));
  EXPECT_TRUE(HasNonSilence(final_buffer));
  EXPECT_GT(runtime.stats().render_count, 0u);

  EXPECT_TRUE(runtime.Shutdown().ok());
}

TEST(AudioRuntimeGraphConcurrencyTest,
     ShutdownWithStagedAndPendingPlansAfterConcurrentActivity) {
  constexpr int kCycles = 20;

  for (int cycle = 0; cycle < kCycles; ++cycle) {
    auto owned_device = std::make_unique<test_support::FakeAudioDevice>();
    test_support::FakeAudioDevice* device = owned_device.get();

    RuntimeConfig config;
    config.max_graph_plans = 3;

    AudioRuntime runtime(std::move(owned_device), config);

    ASSERT_TRUE(runtime.Start().ok());

    BuildStereoChain(runtime.graph());

    {
      RenderLoop render_loop(device);

      for (int k = 0; k < 10; ++k) {
        GraphPlanHandle handle = CompileWithRetry(runtime);

        ASSERT_TRUE(handle.is_valid());
        ASSERT_TRUE(
            RetryUntilOk([&]() { return runtime.ActivateGraphPlan(handle); }));
      }
    }

    StatusOr<GraphPlanHandle> leftover = runtime.CompileAndStageGraph();

    if (leftover.ok()) {
      static_cast<void>(runtime.ActivateGraphPlan(leftover.value()));
    }

    EXPECT_TRUE(runtime.Shutdown().ok());
  }
}

}  // namespace
}  // namespace lavanda
