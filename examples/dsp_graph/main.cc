#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <thread>
#include <utility>

#include "lavanda/lavanda.h"

namespace {

[[noreturn]] void Die(const char* what, const lavanda::Status& status) {
  std::fprintf(stderr, "dsp_graph: %s: %s\n", what, status.message().c_str());
  std::exit(1);
}

void Report(const char* label, const lavanda::Status& status) {
  if (!status.ok()) {
    std::fprintf(stderr, "dsp_graph: %s rejected: %s\n", label,
                 status.message().c_str());
  }
}

template <typename NodeT>

lavanda::NodeId AddNode(lavanda::AudioGraph& graph, std::unique_ptr<NodeT> node,
                        const char* what) {
  lavanda::StatusOr<lavanda::NodeId> id = graph.AddNode(std::move(node));

  if (!id.ok()) {
    Die(what, id.status());
  }

  return id.value();
}

void Connect(lavanda::AudioGraph& graph, lavanda::NodeId source,
             lavanda::NodeId destination, std::uint32_t input_index) {
  lavanda::Status status = graph.Connect(source, destination, input_index);

  if (!status.ok()) {
    Die("connect", status);
  }
}

lavanda::GraphPlanHandle CompileAndActivate(lavanda::AudioRuntime& runtime) {
  lavanda::StatusOr<lavanda::GraphPlanHandle> plan =
      runtime.CompileAndStageGraph();

  if (!plan.ok()) {
    Die("compile", plan.status());
  }

  lavanda::Status status = runtime.ActivateGraphPlan(plan.value());

  if (!status.ok()) {
    Die("activate", status);
  }

  return plan.value();
}

void Sleep(int milliseconds) {
  std::this_thread::sleep_for(std::chrono::milliseconds(milliseconds));
}

void PrintStats(const lavanda::AudioRuntime& runtime, const char* label) {
  const lavanda::RuntimeStats stats = runtime.stats();

  std::printf(
      "dsp_graph: [%s] graph_nodes=%u activations=%llu compile_failures=%llu "
      "renders=%llu missed=%llu\n",
      label, stats.active_graph_node_count,
      static_cast<unsigned long long>(stats.graph_activation_count),
      static_cast<unsigned long long>(stats.graph_compilation_failures),
      static_cast<unsigned long long>(stats.render_count),
      static_cast<unsigned long long>(stats.missed_deadline_count));
}

}  // namespace

int main() {
  lavanda::StatusOr<std::unique_ptr<lavanda::AudioDevice>> device_or =
      lavanda::CreateDefaultOutputDevice();

  if (!device_or.ok()) {
    Die("no output device", device_or.status());
  }

  lavanda::AudioRuntime runtime(std::move(device_or.value()));
  lavanda::Status start_status = runtime.Start();

  if (!start_status.ok()) {
    Die("start", start_status);
  }

  std::printf("dsp_graph: runtime started.\n");

  // --- Build the graph (control side; ordinary C++, may allocate) ------------

  lavanda::AudioGraph& graph = runtime.graph();

  auto osc_a_node = std::make_unique<lavanda::OscillatorNode>();
  osc_a_node->SetFrequency(220.0f);

  auto osc_b_node = std::make_unique<lavanda::OscillatorNode>();
  osc_b_node->SetFrequency(275.0f);

  auto gain_node = std::make_unique<lavanda::GainNode>(1);
  gain_node->SetGain(0.15f);

  auto filter_node = std::make_unique<lavanda::OnePoleLowPassNode>(1);
  filter_node->SetCutoffHz(3000.0f);

  lavanda::NodeId osc_a = AddNode(graph, std::move(osc_a_node), "oscillator A");
  lavanda::NodeId osc_b = AddNode(graph, std::move(osc_b_node), "oscillator B");
  lavanda::NodeId mixer =
      AddNode(graph, std::make_unique<lavanda::MixerNode>(1, 2), "mixer");
  lavanda::NodeId gain = AddNode(graph, std::move(gain_node), "gain");
  lavanda::NodeId filter = AddNode(graph, std::move(filter_node), "low-pass");
  lavanda::NodeId pan =
      AddNode(graph, std::make_unique<lavanda::PanNode>(), "pan");
  lavanda::NodeId output =
      AddNode(graph, std::make_unique<lavanda::OutputNode>(2), "output");

  Connect(graph, osc_a, mixer, 0);
  Connect(graph, osc_b, mixer, 1);
  Connect(graph, mixer, gain, 0);
  Connect(graph, gain, filter, 0);
  Connect(graph, filter, pan, 0);
  Connect(graph, pan, output, 0);

  lavanda::Status output_status = graph.SetOutput(output);

  if (!output_status.ok()) {
    Die("set output", output_status);
  }

  // --- Compile into an immutable plan and activate it ------------------------

  CompileAndActivate(runtime);
  std::printf(
      "dsp_graph: compiled and activated; playing 220 Hz + 275 Hz...\n");
  Sleep(1500);
  PrintStats(runtime, "playing");

  // --- Live parameter changes: commands, applied on the audio thread ---------

  std::printf("dsp_graph: oscillator B -> 330 Hz...\n");
  Report("SetFrequency", runtime.GetGraphNode(osc_b).SetFrequency(330.0f));
  Sleep(1200);

  std::printf("dsp_graph: low-pass cutoff -> 600 Hz (darker)...\n");
  Report("SetCutoffHz", runtime.GetGraphNode(filter).SetCutoffHz(600.0f));
  Sleep(1200);

  std::printf("dsp_graph: panning left to right...\n");

  for (float position = -1.0f; position <= 1.001f; position += 0.25f) {
    Report("SetPan", runtime.GetGraphNode(pan).SetPan(position));
    Sleep(200);
  }

  // --- A failed compile leaves the playing plan untouched --------------------

  std::printf(
      "dsp_graph: unwiring the mixer's second input, then compiling...\n");

  graph.Disconnect(mixer, 1);

  lavanda::StatusOr<lavanda::GraphPlanHandle> broken =
      runtime.CompileAndStageGraph();

  if (broken.ok()) {
    std::fprintf(stderr, "dsp_graph: expected that compile to fail\n");
    return 1;
  }

  std::printf("dsp_graph: compile rejected as expected: %s\n",
              broken.status().message().c_str());
  std::printf("dsp_graph: the active plan is unaffected; still playing...\n");
  Sleep(1000);
  PrintStats(runtime, "after rejected compile");

  // --- Replace the graph: insert a delay into oscillator B's branch ----------

  std::printf(
      "dsp_graph: inserting a 5 ms delay on branch B and replacing the "
      "plan...\n");

  auto delay_node = std::make_unique<lavanda::DelayNode>(1, 4800);
  delay_node->SetDelayFrames(240);

  lavanda::NodeId delay = AddNode(graph, std::move(delay_node), "delay");

  Connect(graph, osc_b, delay, 0);
  Connect(graph, delay, mixer, 1);

  CompileAndActivate(runtime);
  Sleep(2000);
  PrintStats(runtime, "after replacement");

  lavanda::Status shutdown_status = runtime.Shutdown();

  if (!shutdown_status.ok()) {
    Die("shutdown", shutdown_status);
  }

  std::printf("dsp_graph: shut down cleanly.\n");

  return 0;
}
