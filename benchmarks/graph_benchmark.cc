#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <utility>
#include <vector>

#include "lavanda/core/audio_buffer.h"
#include "lavanda/core/status.h"
#include "lavanda/graph/execution_plan.h"
#include "lavanda/graph/graph.h"
#include "lavanda/graph/graph_compiler.h"
#include "lavanda/graph/graph_executor.h"
#include "lavanda/graph/nodes/delay_node.h"
#include "lavanda/graph/nodes/gain_node.h"
#include "lavanda/graph/nodes/mixer_node.h"
#include "lavanda/graph/nodes/one_pole_high_pass_node.h"
#include "lavanda/graph/nodes/one_pole_low_pass_node.h"
#include "lavanda/graph/nodes/oscillator_node.h"
#include "lavanda/graph/nodes/output_node.h"
#include "lavanda/graph/nodes/pan_node.h"

namespace lavanda {
namespace {

using Clock = std::chrono::steady_clock;

constexpr double kSampleRateHz = 48000.0;
constexpr std::uint32_t kDefaultFramesPerBlock = 512;
constexpr std::uint32_t kDefaultBlockCount = 20000;
constexpr std::uint32_t kWarmupBlocks = 200;
constexpr std::size_t kCompileRuns = 200;

#ifdef NDEBUG
constexpr const char* kBuildNote = "optimized (NDEBUG)";
#else
constexpr const char* kBuildNote =
    "NOT optimized -- numbers are not representative";
#endif

volatile double g_sink = 0.0;

double Micros(Clock::duration duration) {
  return std::chrono::duration<double, std::micro>(duration).count();
}

[[noreturn]] void Die(const char* message) {
  std::fprintf(stderr, "graph_benchmark: %s\n", message);
  std::exit(1);
}

// --- Graph-building helpers (control-side; not timed) ---------------------

NodeId Add(AudioGraph& graph, std::unique_ptr<AudioNode> node) {
  StatusOr<NodeId> id = graph.AddNode(std::move(node));

  if (!id.ok()) {
    Die(id.status().message().c_str());
  }

  return id.value();
}

void Link(AudioGraph& graph, NodeId source, NodeId destination,
          std::uint32_t input_index = 0) {
  Status status = graph.Connect(source, destination, input_index);

  if (!status.ok()) {
    Die(status.message().c_str());
  }
}

NodeId AddOscillator(AudioGraph& graph, float frequency_hz) {
  auto node = std::make_unique<OscillatorNode>();
  node->SetFrequency(frequency_hz);
  node->SetGain(0.2f);

  return Add(graph, std::move(node));
}

NodeId AddGain(AudioGraph& graph, float gain) {
  auto node = std::make_unique<GainNode>(1);
  node->SetGain(gain);

  return Add(graph, std::move(node));
}

NodeId AddLowPass(AudioGraph& graph, float cutoff_hz) {
  auto node = std::make_unique<OnePoleLowPassNode>(1);
  node->SetCutoffHz(cutoff_hz);

  return Add(graph, std::move(node));
}

NodeId AddHighPass(AudioGraph& graph, float cutoff_hz) {
  auto node = std::make_unique<OnePoleHighPassNode>(1);
  node->SetCutoffHz(cutoff_hz);

  return Add(graph, std::move(node));
}

NodeId AddDelay(AudioGraph& graph, std::uint32_t delay_frames) {
  auto node = std::make_unique<DelayNode>(1, 4800);
  node->SetDelayFrames(delay_frames);

  return Add(graph, std::move(node));
}

void FinishWithPanAndOutput(AudioGraph& graph, NodeId mono_source) {
  NodeId pan = Add(graph, std::make_unique<PanNode>());
  NodeId output = Add(graph, std::make_unique<OutputNode>(2));

  Link(graph, mono_source, pan);
  Link(graph, pan, output);

  Status status = graph.SetOutput(output);

  if (!status.ok()) {
    Die(status.message().c_str());
  }
}

// --- Scenarios --------------------------------------------------------------

void BuildLinear(AudioGraph& graph) {
  NodeId osc = AddOscillator(graph, 440.0f);
  NodeId gain = AddGain(graph, 0.5f);
  NodeId filter = AddLowPass(graph, 5000.0f);

  Link(graph, osc, gain);
  Link(graph, gain, filter);

  FinishWithPanAndOutput(graph, filter);
}

void BuildLinearLong(AudioGraph& graph) {
  constexpr int kChainLength = 32;

  NodeId previous = AddOscillator(graph, 440.0f);

  for (int i = 0; i < kChainLength; ++i) {
    NodeId gain = AddGain(graph, 0.99f);

    Link(graph, previous, gain);

    previous = gain;
  }

  FinishWithPanAndOutput(graph, previous);
}

void BuildBranching(AudioGraph& graph) {
  NodeId osc = AddOscillator(graph, 220.0f);
  NodeId gain = AddGain(graph, 0.5f);
  NodeId low = AddLowPass(graph, 2000.0f);
  NodeId high = AddHighPass(graph, 200.0f);
  NodeId mixer = Add(graph, std::make_unique<MixerNode>(1, 3));

  Link(graph, osc, gain);
  Link(graph, osc, low);
  Link(graph, osc, high);
  Link(graph, gain, mixer, 0);
  Link(graph, low, mixer, 1);
  Link(graph, high, mixer, 2);

  FinishWithPanAndOutput(graph, mixer);
}

void BuildConverging(AudioGraph& graph) {
  NodeId osc_a = AddOscillator(graph, 220.0f);
  NodeId gain_a = AddGain(graph, 0.5f);
  NodeId low_a = AddLowPass(graph, 3000.0f);

  Link(graph, osc_a, gain_a);
  Link(graph, gain_a, low_a);

  NodeId osc_b = AddOscillator(graph, 330.0f);
  NodeId high_b = AddHighPass(graph, 150.0f);

  Link(graph, osc_b, high_b);

  NodeId osc_c = AddOscillator(graph, 440.0f);
  NodeId delay_c = AddDelay(graph, 480);

  Link(graph, osc_c, delay_c);

  NodeId mixer = Add(graph, std::make_unique<MixerNode>(1, 3));

  Link(graph, low_a, mixer, 0);
  Link(graph, high_b, mixer, 1);
  Link(graph, delay_c, mixer, 2);

  FinishWithPanAndOutput(graph, mixer);
}

void BuildMultiSource(AudioGraph& graph) {
  constexpr std::uint32_t kSources = 8;

  NodeId mixer = Add(graph, std::make_unique<MixerNode>(1, kSources));

  for (std::uint32_t i = 0; i < kSources; ++i) {
    NodeId osc = AddOscillator(graph, 110.0f * static_cast<float>(i + 1));
    Link(graph, osc, mixer, i);
  }

  NodeId filter = AddLowPass(graph, 4000.0f);
  Link(graph, mixer, filter);

  FinishWithPanAndOutput(graph, filter);
}

struct Scenario {
  const char* name;
  void (*build)(AudioGraph&);
};

// --- Measurement ---------------------------------------------------------

struct Summary {
  double median = 0.0;
  double p99 = 0.0;
  double max = 0.0;
};

Summary Summarize(std::vector<double>& samples) {
  Summary summary;

  if (samples.empty()) {
    return summary;
  }

  std::sort(samples.begin(), samples.end());

  summary.median = samples[samples.size() / 2];
  summary.p99 = samples[static_cast<std::size_t>(
      0.99 * static_cast<double>(samples.size() - 1))];
  summary.max = samples.back();

  return summary;
}

bool RunScenario(const Scenario& scenario, std::uint32_t frames,
                 std::uint32_t block_count) {
  AudioGraph graph;
  scenario.build(graph);

  // --- Compilation time ---------------------------------------------------

  std::vector<double> compile_us;
  compile_us.reserve(kCompileRuns);

  for (std::size_t run = 0; run < kCompileRuns; ++run) {
    const auto start = Clock::now();
    StatusOr<GraphExecutionPlan> result = GraphCompiler::Compile(graph, frames);
    const auto stop = Clock::now();

    if (!result.ok()) {
      std::fprintf(stderr, "graph_benchmark: %s failed to compile: %s\n",
                   scenario.name, result.status().message().c_str());
      return false;
    }

    g_sink = static_cast<double>(result.value().step_count());
    compile_us.push_back(Micros(stop - start));
  }

  const Summary compile = Summarize(compile_us);

  // --- Render cost ----------------------------------------------------------

  StatusOr<GraphExecutionPlan> plan_or = GraphCompiler::Compile(graph, frames);

  if (!plan_or.ok()) {
    return false;
  }

  GraphExecutionPlan& plan = plan_or.value();

  std::size_t buffer_bytes = 0;

  for (const AudioBuffer& buffer : plan.buffers()) {
    buffer_bytes += static_cast<std::size_t>(buffer.frame_count()) *
                    buffer.channel_count() * sizeof(float);
  }

  AudioBuffer destination(frames, 2);
  std::uint64_t clock_frame = 0;

  for (std::uint32_t block = 0; block < kWarmupBlocks; ++block) {
    destination.Clear();
    GraphExecutor::Render(plan, destination.View(), frames, kSampleRateHz,
                          clock_frame);

    clock_frame += frames;
  }

  std::vector<double> render_us;
  render_us.reserve(block_count);

  double checksum = 0.0;

  for (std::uint32_t block = 0; block < block_count; ++block) {
    destination.Clear();

    const auto start = Clock::now();
    GraphExecutor::Render(plan, destination.View(), frames, kSampleRateHz,
                          clock_frame);
    const auto stop = Clock::now();

    render_us.push_back(Micros(stop - start));

    checksum += static_cast<double>(destination(0, 0));
    clock_frame += frames;
  }

  g_sink = checksum;

  bool produced_audio = false;

  for (std::uint32_t f = 0; f < frames && !produced_audio; ++f) {
    produced_audio = destination(f, 0) != 0.0f || destination(f, 1) != 0.0f;
  }

  if (!produced_audio) {
    std::fprintf(stderr,
                 "graph_benchmark: %s produced silence -- the measurement "
                 "would be vacuous\n",
                 scenario.name);
    return false;
  }

  const Summary render = Summarize(render_us);
  const double budget_us = static_cast<double>(frames) / kSampleRateHz * 1e6;

  std::printf("%-24s %5u %5u %8.1f %11.1f %10.2f %9.2f %9.2f %8.3f%%\n",
              scenario.name, plan.step_count(), plan.buffer_count(),
              static_cast<double>(buffer_bytes) / 1024.0, compile.median,
              render.median, render.p99, render.max,
              render.median / budget_us * 100.0);

  return true;
}

std::uint32_t ParseOrDefault(int argc, char** argv, int index,
                             std::uint32_t fallback) {
  if (index >= argc) {
    return fallback;
  }

  char* end = nullptr;
  const unsigned long value = std::strtoul(argv[index], &end, 10);

  if (end == argv[index] || value == 0) {
    return fallback;
  }

  return static_cast<std::uint32_t>(value);
}

}  // namespace

int RunBenchmarks(int argc, char** argv) {
  const std::uint32_t frames =
      ParseOrDefault(argc, argv, 1, kDefaultFramesPerBlock);
  const std::uint32_t blocks =
      ParseOrDefault(argc, argv, 2, kDefaultBlockCount);
  const double budget_us = static_cast<double>(frames) / kSampleRateHz * 1e6;

  std::printf(
      "graph_benchmark: %u frames/block @ %.0f Hz (deadline budget %.1f "
      "us/block), %u blocks per scenario\n",
      frames, kSampleRateHz, budget_us, blocks);
  std::printf("build: %s\n\n", kBuildNote);
  std::printf("%-24s %5s %5s %8s %11s %10s %9s %9s %9s\n", "scenario", "steps",
              "bufs", "plan_KiB", "compile_us", "render_us", "p99_us", "max_us",
              "of_budget");
  std::printf("%-24s %5s %5s %8s %11s %10s %9s %9s %9s\n", "", "", "", "",
              "(median)", "(median)", "", "", "(median)");

  const Scenario scenarios[] = {
      {"linear (5 nodes)", &BuildLinear},
      {"linear-long (32 gains)", &BuildLinearLong},
      {"branching", &BuildBranching},
      {"converging", &BuildConverging},
      {"multi-source (8 osc)", &BuildMultiSource},
  };

  bool all_ok = true;

  for (const Scenario& scenario : scenarios) {
    all_ok = RunScenario(scenario, frames, blocks) && all_ok;
  }

  return all_ok ? 0 : 1;
}

}  // namespace lavanda

int main(int argc, char** argv) { return lavanda::RunBenchmarks(argc, argv); }
