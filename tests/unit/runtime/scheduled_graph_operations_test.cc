#include <gtest/gtest.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "lavanda/core/audio_buffer.h"
#include "lavanda/core/status.h"
#include "lavanda/core/time.h"
#include "lavanda/graph/node_id.h"
#include "lavanda/graph/nodes/delay_node.h"
#include "lavanda/graph/nodes/gain_node.h"
#include "lavanda/graph/nodes/one_pole_low_pass_node.h"
#include "lavanda/graph/nodes/oscillator_node.h"
#include "lavanda/graph/nodes/output_node.h"
#include "lavanda/graph/nodes/pan_node.h"
#include "lavanda/runtime/audio_runtime.h"
#include "lavanda/runtime/command.h"
#include "lavanda/runtime/runtime_stats.h"
#include "test_support/fake_audio_device.h"

namespace lavanda {
namespace {

// ---- definitions -----------------------------------------------------------

constexpr std::uint32_t kChannels = 2;
constexpr std::uint32_t kTotalFrames = 192;
constexpr std::uint32_t kPlanAFrames = 64;
constexpr std::uint64_t kEventFrame = 100;
constexpr std::uint32_t kMaxDelayFrames = 64;
constexpr float kOscillatorGain = 0.5f;
constexpr float kPlanBOscillatorGain = 1.0f;
constexpr std::size_t kNoMismatch = std::numeric_limits<std::size_t>::max();
constexpr std::uint32_t kCallbackSizes[] = {192, 64, 7};
constexpr std::uint64_t kTargets[] = {0, 1, 17, 63, 64, 65, 128, 191, 192, 500};
constexpr const char* kStereoAsset = "stereo_pcm16.wav";
constexpr float kNaN = std::numeric_limits<float>::quiet_NaN();
constexpr float kInf = std::numeric_limits<float>::infinity();

enum class Topology {
  kOscillatorChain,
  kStereoAsset,
};

struct GraphIds {
  NodeId source;
  NodeId low_pass;
  NodeId delay;
  NodeId gain;
  NodeId pan;
  NodeId output;
};

struct Step {
  std::uint64_t target;
  Command command;
};

class GraphSession {
 public:
  explicit GraphSession(Topology topology = Topology::kOscillatorChain,
                        bool activate = true) {
    auto device = std::make_unique<test_support::FakeAudioDevice>();

    device_ = device.get();
    runtime_ = std::make_unique<AudioRuntime>(std::move(device));

    EXPECT_TRUE(runtime_->Start().ok());

    if (topology == Topology::kOscillatorChain) {
      BuildOscillatorChain();
    } else {
      BuildStereoAsset();
    }

    if (activate) {
      CompileAndActivate();
    }
  }

  AudioRuntime& runtime() { return *runtime_; }
  const GraphIds& ids() const { return ids_; }
  RuntimeStats Stats() const { return runtime_->stats(); }

  Status Schedule(const Command& command, std::uint64_t frame) {
    return runtime_->ScheduleAtFrame(command, FramePosition{frame});
  }

  Status Submit(const Command& command) { return runtime_->Submit(command); }

  void SetControlOscillatorGain(float gain) {
    static_cast<OscillatorNode*>(runtime_->graph().GetNode(ids_.source))
        ->SetGain(gain);
  }

  StatusOr<GraphPlanHandle> CompileAndStage() {
    StatusOr<GraphPlanHandle> plan = runtime_->CompileAndStageGraph();

    EXPECT_TRUE(plan.ok());

    return plan;
  }

  Status Activate(GraphPlanHandle plan) {
    return runtime_->ActivateGraphPlan(plan);
  }

  void CompileAndActivate() {
    StatusOr<GraphPlanHandle> plan = CompileAndStage();

    if (plan.ok()) {
      EXPECT_TRUE(Activate(plan.value()).ok());
    }
  }

  std::vector<float> Render(std::uint32_t frames,
                            std::uint32_t callback_frames) {
    std::vector<float> samples;

    samples.reserve(static_cast<std::size_t>(frames) * kChannels);

    for (std::uint32_t done = 0; done < frames;) {
      const std::uint32_t chunk = std::min(callback_frames, frames - done);
      AudioBuffer buffer(chunk, kChannels);

      EXPECT_TRUE(device_->PumpRender(buffer.View()));

      for (std::uint32_t f = 0; f < chunk; ++f) {
        for (std::uint32_t c = 0; c < kChannels; ++c) {
          samples.push_back(buffer(f, c));
        }
      }

      done += chunk;
    }

    return samples;
  }

 private:
  NodeId Add(std::unique_ptr<AudioNode> node) {
    StatusOr<NodeId> id = runtime_->graph().AddNode(std::move(node));

    EXPECT_TRUE(id.ok());

    return id.ok() ? id.value() : NodeId{};
  }

  void Link(NodeId from, NodeId to) {
    EXPECT_TRUE(runtime_->graph().Connect(from, to, 0).ok());
  }

  void BuildOscillatorChain() {
    ids_.source = Add(std::make_unique<OscillatorNode>());
    ids_.low_pass = Add(std::make_unique<OnePoleLowPassNode>(1));
    ids_.delay = Add(std::make_unique<DelayNode>(1, kMaxDelayFrames));
    ids_.gain = Add(std::make_unique<GainNode>(1));
    ids_.pan = Add(std::make_unique<PanNode>());
    ids_.output = Add(std::make_unique<OutputNode>(2));

    SetControlOscillatorGain(kOscillatorGain);

    Link(ids_.source, ids_.low_pass);
    Link(ids_.low_pass, ids_.delay);
    Link(ids_.delay, ids_.gain);
    Link(ids_.gain, ids_.pan);
    Link(ids_.pan, ids_.output);

    EXPECT_TRUE(runtime_->graph().SetOutput(ids_.output).ok());
  }

  void BuildStereoAsset() {
    StatusOr<AudioAssetId> asset = runtime_->LoadAudioAsset(
        std::string(LAVANDA_FIXTURE_DIR) + "/" + kStereoAsset);

    EXPECT_TRUE(asset.ok());

    StatusOr<NodeId> source = runtime_->AddAssetSourceNode(asset.value());

    EXPECT_TRUE(source.ok());

    ids_.source = source.value();
    ids_.gain = Add(std::make_unique<GainNode>(2));
    ids_.output = Add(std::make_unique<OutputNode>(2));

    Link(ids_.source, ids_.gain);
    Link(ids_.gain, ids_.output);

    EXPECT_TRUE(runtime_->graph().SetOutput(ids_.output).ok());
  }

  test_support::FakeAudioDevice* device_ = nullptr;
  std::unique_ptr<AudioRuntime> runtime_;
  GraphIds ids_;
};

Command CmdNode(CommandType type, NodeId id, float value) {
  return {.type = type, .value = value, .node_id = id};
}

struct Scenario {
  const char* name;
  Command (*make)(const GraphIds&);
};

const Scenario kScenarios[] = {
    {"GainNodeGain",
     [](const GraphIds& ids) {
       return CmdNode(CommandType::kSetNodeGain, ids.gain, 0.1f);
     }},
    {"OscillatorGain",
     [](const GraphIds& ids) {
       return CmdNode(CommandType::kSetNodeGain, ids.source, 0.1f);
     }},
    {"OscillatorFrequency",
     [](const GraphIds& ids) {
       return CmdNode(CommandType::kSetNodeFrequency, ids.source, 880.0f);
     }},
    {"LowPassCutoff",
     [](const GraphIds& ids) {
       return CmdNode(CommandType::kSetNodeCutoff, ids.low_pass, 300.0f);
     }},
    {"PanNodePan",
     [](const GraphIds& ids) {
       return CmdNode(CommandType::kSetNodePan, ids.pan, 0.8f);
     }},
    {"DelayNodeFrames", [](const GraphIds& ids) {
       return CmdNode(CommandType::kSetNodeDelayFrames, ids.delay, 20.0f);
     }}};

std::vector<float> RenderWithImmediates(GraphSession& session,
                                        const std::vector<Step>& steps) {
  std::vector<float> samples;
  std::uint64_t cursor = 0;

  auto append = [&samples](const std::vector<float>& part) {
    samples.insert(samples.end(), part.begin(), part.end());
  };

  for (const Step& step : steps) {
    const std::uint64_t at = std::min<std::uint64_t>(step.target, kTotalFrames);

    if (at > cursor) {
      append(session.Render(static_cast<std::uint32_t>(at - cursor),
                            kTotalFrames));
      cursor = at;
    }

    EXPECT_TRUE(session.Submit(step.command).ok());
  }

  append(session.Render(static_cast<std::uint32_t>(kTotalFrames - cursor),
                        kTotalFrames));

  return samples;
}

std::size_t FirstMismatchFrame(const std::vector<float>& actual,
                               const std::vector<float>& expected) {
  for (std::size_t i = 0; i < actual.size(); ++i) {
    if (actual[i] != expected[i]) {
      return i / kChannels;
    }
  }

  return kNoMismatch;
}

void ExpectIdentical(const std::vector<float>& actual,
                     const std::vector<float>& expected) {
  ASSERT_EQ(actual.size(), expected.size());
  EXPECT_EQ(FirstMismatchFrame(actual, expected), kNoMismatch);
}

bool AllZero(const std::vector<float>& samples) {
  for (float sample : samples) {
    if (sample != 0.0f) {
      return false;
    }
  }

  return true;
}

bool AllFinite(const std::vector<float>& samples) {
  for (float sample : samples) {
    if (!(sample == sample) || sample > 1e9f || sample < -1e9f) {
      return false;
    }
  }

  return true;
}

void ExpectEventCounts(const GraphSession& session, std::uint64_t executed,
                       std::uint64_t pending) {
  const RuntimeStats stats = session.Stats();

  EXPECT_EQ(stats.scheduled_events_executed, executed);
  EXPECT_EQ(stats.scheduled_events_pending, pending);
  EXPECT_EQ(stats.scheduled_events_late, 0u);
  EXPECT_EQ(stats.scheduled_events_dropped, 0u);
  EXPECT_EQ(stats.scheduled_events_accepted, executed + pending);
}

// ---- every graph parameter, exact frame ------------------------------------

TEST(ScheduledGraphTest, EveryNodeParameterLandsExactlyOnTheTargetFrame) {
  GraphSession baseline_session;
  const std::vector<float> baseline =
      baseline_session.Render(kTotalFrames, kTotalFrames);

  for (const Scenario& scenario : kScenarios) {
    for (std::uint64_t target : kTargets) {
      GraphSession reference;
      const std::vector<float> expected = RenderWithImmediates(
          reference, {{target, scenario.make(reference.ids())}});
      const std::size_t effective =
          std::min<std::uint64_t>(target, kTotalFrames);
      const bool executes = target < kTotalFrames;

      for (std::uint32_t callback_frames : kCallbackSizes) {
        SCOPED_TRACE(testing::Message()
                     << scenario.name << ", target frame " << target
                     << ", callback size " << callback_frames);

        GraphSession session;

        ASSERT_TRUE(
            session.Schedule(scenario.make(session.ids()), target).ok());

        const std::vector<float> actual =
            session.Render(kTotalFrames, callback_frames);

        ExpectIdentical(actual, expected);

        EXPECT_GE(FirstMismatchFrame(actual, baseline), effective)
            << "the parameter must have no effect before its target frame";

        ExpectEventCounts(session, executes ? 1u : 0u, executes ? 0u : 1u);
      }
    }
  }
}

TEST(ScheduledGraphTest, EveryScenarioIsObservableAfterItsTarget) {
  constexpr std::uint64_t kTarget = 40;

  GraphSession baseline_session;
  const std::vector<float> baseline =
      baseline_session.Render(kTotalFrames, kTotalFrames);

  for (const Scenario& scenario : kScenarios) {
    SCOPED_TRACE(scenario.name);

    GraphSession reference;
    const std::vector<float> changed = RenderWithImmediates(
        reference, {{kTarget, scenario.make(reference.ids())}});

    const std::size_t first = FirstMismatchFrame(baseline, changed);

    ASSERT_NE(first, kNoMismatch) << "the scenario changes nothing audible";
    EXPECT_GE(first, kTarget);
  }
}

// ---- unknown, stale and mismatched targets ----------------------------------

TEST(ScheduledGraphTest, EventsForUnknownStaleOrMismatchedNodesAreHarmless) {
  GraphSession baseline_session;
  const std::vector<float> baseline =
      baseline_session.Render(kTotalFrames, kTotalFrames);

  GraphSession session;

  const GraphIds ids = session.ids();
  NodeId stale_gain = ids.gain;

  stale_gain.generation += 1;

  const NodeId unknown{.index = 999, .generation = 1};
  const Step steps[] = {
      {10, CmdNode(CommandType::kSetNodeGain, stale_gain, 0.0f)},
      {20, CmdNode(CommandType::kSetNodeGain, unknown, 0.0f)},
      {30, CmdNode(CommandType::kSetNodeCutoff, ids.source, 100.0f)},
      {40, CmdNode(CommandType::kSetNodePan, ids.gain, 1.0f)},
      {50, CmdNode(CommandType::kSetNodeDelayFrames, ids.source, 30.0f)},
      {60, CmdNode(CommandType::kSetNodeFrequency, ids.gain, 880.0f)}};

  for (const Step& step : steps) {
    ASSERT_TRUE(session.Schedule(step.command, step.target).ok());
  }

  ExpectIdentical(session.Render(kTotalFrames, kTotalFrames), baseline);
  ExpectEventCounts(session, 6, 0);
}

TEST(ScheduledGraphTest, EventWithoutAnActivePlanIsConsumedNotRemembered) {
  GraphSession reference(Topology::kOscillatorChain, /*activate=*/false);
  reference.Render(kPlanAFrames, kPlanAFrames);
  reference.CompileAndActivate();

  const std::vector<float> expected =
      reference.Render(kPlanAFrames, kPlanAFrames);

  ASSERT_FALSE(AllZero(expected));

  GraphSession session(Topology::kOscillatorChain, /*activate=*/false);

  ASSERT_TRUE(
      session
          .Schedule(
              CmdNode(CommandType::kSetNodeGain, session.ids().gain, 0.0f), 20)
          .ok());

  EXPECT_TRUE(AllZero(session.Render(kPlanAFrames, kPlanAFrames)));

  ExpectEventCounts(session, 1, 0);

  session.CompileAndActivate();

  ExpectIdentical(session.Render(kPlanAFrames, kPlanAFrames), expected);
}

// ---- plan swaps -------------------------------------------------------------

enum class SwapMode { kScheduled, kImmediateReplay, kNoEvent };

std::vector<float> RunPlanSwap(SwapMode mode, std::uint32_t callback_frames) {
  GraphSession session;
  std::vector<float> samples = session.Render(kPlanAFrames, callback_frames);

  EXPECT_EQ(ToInteger(session.runtime().frame_position()),
            std::uint64_t{kPlanAFrames});

  session.SetControlOscillatorGain(kPlanBOscillatorGain);

  StatusOr<GraphPlanHandle> plan_b = session.CompileAndStage();

  if (!plan_b.ok()) {
    return samples;
  }

  const Command event =
      CmdNode(CommandType::kSetNodeGain, session.ids().gain, 0.1f);
  const std::uint32_t before_event =
      static_cast<std::uint32_t>(kEventFrame - kPlanAFrames);
  const std::uint32_t after_event =
      static_cast<std::uint32_t>(kTotalFrames - kEventFrame);

  auto append = [&samples](const std::vector<float>& part) {
    samples.insert(samples.end(), part.begin(), part.end());
  };

  switch (mode) {
    case SwapMode::kScheduled:
      EXPECT_TRUE(session.Schedule(event, kEventFrame).ok());
      EXPECT_TRUE(session.Activate(plan_b.value()).ok());

      append(session.Render(before_event + after_event, callback_frames));

      break;
    case SwapMode::kImmediateReplay:
      EXPECT_TRUE(session.Activate(plan_b.value()).ok());

      append(session.Render(before_event, kTotalFrames));

      EXPECT_TRUE(session.Submit(event).ok());

      append(session.Render(after_event, kTotalFrames));

      break;
    case SwapMode::kNoEvent:
      EXPECT_TRUE(session.Activate(plan_b.value()).ok());

      append(session.Render(before_event + after_event, callback_frames));

      break;
  }

  EXPECT_EQ(session.Stats().active_graph_plan_generation,
            plan_b.value().generation);

  return samples;
}

TEST(ScheduledGraphTest, ParameterEventsApplyToThePlanActiveAtTheTargetFrame) {
  const std::vector<float> no_event = RunPlanSwap(SwapMode::kNoEvent, 64);
  const std::vector<float> expected =
      RunPlanSwap(SwapMode::kImmediateReplay, 64);

  const std::size_t first = FirstMismatchFrame(no_event, expected);

  ASSERT_NE(first, kNoMismatch) << "the event must be audible on plan B";
  EXPECT_GE(first, kEventFrame);

  for (std::uint32_t callback_frames : kCallbackSizes) {
    SCOPED_TRACE(testing::Message() << "callback size " << callback_frames);

    ExpectIdentical(RunPlanSwap(SwapMode::kScheduled, callback_frames),
                    expected);
  }
}

// ---- asset-source graphs ----------------------------------------------------

TEST(ScheduledGraphTest, GainEventAfterAnAssetSourceIsFrameExact) {
  GraphSession baseline_session(Topology::kStereoAsset);
  const std::vector<float> baseline =
      baseline_session.Render(kTotalFrames, kTotalFrames);

  ASSERT_FALSE(AllZero(baseline));

  bool observable = false;

  for (std::uint64_t target = 1; target <= 3; ++target) {
    GraphSession reference(Topology::kStereoAsset);
    const std::vector<float> expected = RenderWithImmediates(
        reference, {{target, CmdNode(CommandType::kSetNodeGain,
                                     reference.ids().gain, 0.1f)}});

    if (FirstMismatchFrame(baseline, expected) != kNoMismatch) {
      observable = true;
    }

    for (std::uint32_t callback_frames : kCallbackSizes) {
      SCOPED_TRACE(testing::Message() << "target frame " << target
                                      << ", callback size " << callback_frames);

      GraphSession session(Topology::kStereoAsset);

      ASSERT_TRUE(session
                      .Schedule(CmdNode(CommandType::kSetNodeGain,
                                        session.ids().gain, 0.1f),
                                target)
                      .ok());

      ExpectIdentical(session.Render(kTotalFrames, callback_frames), expected);
      ExpectEventCounts(session, 1, 0);
    }
  }

  EXPECT_TRUE(observable)
      << "no target changed the asset's output, so the test proves nothing";
}

// ---- out-of-range delay values through the runtime --------------------------

TEST(ScheduledGraphTest, OutOfRangeDelayValuesAreAcceptedAndRenderCleanly) {
  const float huge[] = {4294967296.0f, 1e20f,
                        std::numeric_limits<float>::max()};

  for (float value : huge) {
    SCOPED_TRACE(value);

    GraphSession session;

    ASSERT_TRUE(session
                    .Schedule(CmdNode(CommandType::kSetNodeDelayFrames,
                                      session.ids().delay, value),
                              30)
                    .ok());

    const std::vector<float> actual = session.Render(kTotalFrames, 64);

    EXPECT_TRUE(AllFinite(actual));
    EXPECT_FALSE(AllZero(actual));

    ExpectEventCounts(session, 1, 0);
  }
}

TEST(ScheduledGraphTest, NonFiniteAndNegativeDelaysAreRejectedAtScheduleTime) {
  const float bad[] = {kNaN, kInf, -kInf, -1.0f};

  for (float value : bad) {
    SCOPED_TRACE(value);

    GraphSession session;

    const Status status = session.Schedule(
        CmdNode(CommandType::kSetNodeDelayFrames, session.ids().delay, value),
        30);

    EXPECT_FALSE(status.ok());
    EXPECT_EQ(status.code(), ErrorCode::kInvalidArgument);

    const RuntimeStats stats = session.Stats();

    EXPECT_EQ(stats.scheduled_events_accepted, 0u);
    EXPECT_EQ(stats.scheduled_events_capacity_failures, 0u);
  }
}

TEST(ScheduledGraphTest, NaNAndNegativeDelaysSubmittedRawLeaveTheNodeAtZero) {
  GraphSession baseline_session;
  const std::vector<float> baseline =
      baseline_session.Render(kTotalFrames, kTotalFrames);

  const float bad[] = {kNaN, -5.0f, -kInf};

  for (float value : bad) {
    SCOPED_TRACE(value);

    GraphSession session;

    ASSERT_TRUE(session
                    .Submit(CmdNode(CommandType::kSetNodeDelayFrames,
                                    session.ids().delay, value))
                    .ok());

    ExpectIdentical(session.Render(kTotalFrames, kTotalFrames), baseline);
  }
}

}  // namespace
}  // namespace lavanda
