#include "lavanda/graph/graph_executor.h"

#include "lavanda/runtime/mixer_math.h"

namespace lavanda {

void GraphExecutor::Render(GraphExecutionPlan& plan, AudioBufferView output,
                           std::uint32_t frame_count, double sample_rate_hz,
                           std::uint64_t clock_frame) noexcept {
  if (frame_count > plan.max_frames_per_block()) {
    return;
  }

  for (const GraphExecutionPlan::Step& step : plan.steps()) {
    NodeProcessContext context;

    for (std::uint32_t i = 0; i < step.input_count; ++i) {
      context.inputs[i] =
          plan.buffers()[step.input_buffer_indices[i]].View(frame_count);
    }

    context.input_count = step.input_count;
    context.output = plan.buffers()[step.output_buffer_index].View(frame_count);
    context.frame_count = frame_count;
    context.sample_rate_hz = sample_rate_hz;
    context.clock_frame = clock_frame;

    step.node->Process(context);
  }

  if (plan.output_channel_count() != output.channel_count()) {
    return;
  }

  AudioBufferView graph_output =
      plan.buffers()[plan.output_buffer_index()].View(frame_count);

  AccumulateInto(output, graph_output, 1.0f);
}

}  // namespace lavanda
