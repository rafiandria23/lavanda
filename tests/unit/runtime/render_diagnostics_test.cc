#include "lavanda/runtime/render_diagnostics.h"

#include <gtest/gtest.h>

namespace lavanda {
namespace {

TEST(RenderDiagnosticsTest, DefaultSnapshotIsAllZero) {
  RenderDiagnostics diagnostics;
  RuntimeStats stats = diagnostics.Snapshot();

  EXPECT_EQ(stats.render_count, 0u);
  EXPECT_EQ(stats.missed_deadline_count, 0u);
  EXPECT_DOUBLE_EQ(stats.last_render_duration_seconds, 0.0);
  EXPECT_DOUBLE_EQ(stats.max_render_duration_seconds, 0.0);
  EXPECT_EQ(stats.last_callback_frame_count, 0u);
}

TEST(RenderDiagnosticsTest, RecordRenderIncrementsCount) {
  RenderDiagnostics diagnostics;

  diagnostics.RecordRender(0.001, 512, 48000.0);
  diagnostics.RecordRender(0.001, 512, 48000.0);

  EXPECT_EQ(diagnostics.Snapshot().render_count, 2u);
}

TEST(RenderDiagnosticsTest, LastDurationReflectsMostRecentCall) {
  RenderDiagnostics diagnostics;

  diagnostics.RecordRender(0.001, 512, 48000.0);
  diagnostics.RecordRender(0.002, 512, 48000.0);

  EXPECT_DOUBLE_EQ(diagnostics.Snapshot().last_render_duration_seconds, 0.002);
}

TEST(RenderDiagnosticsTest, MaxDurationTracksLargestSeenNotMostRecent) {
  RenderDiagnostics diagnostics;

  diagnostics.RecordRender(0.005, 512, 48000.0);
  diagnostics.RecordRender(0.001, 512, 48000.0);

  EXPECT_DOUBLE_EQ(diagnostics.Snapshot().max_render_duration_seconds, 0.005);
}

TEST(RenderDiagnosticsTest, LastFrameCountReflectsMostRecentCall) {
  RenderDiagnostics diagnostics;

  diagnostics.RecordRender(0.001, 256, 48000.0);
  diagnostics.RecordRender(0.001, 512, 48000.0);

  EXPECT_EQ(diagnostics.Snapshot().last_callback_frame_count, 512u);
}

TEST(RenderDiagnosticsTest, WithinBudgetIsNotCountedAsMissed) {
  RenderDiagnostics diagnostics;
  diagnostics.RecordRender(0.001, 512, 48000.0);

  EXPECT_EQ(diagnostics.Snapshot().missed_deadline_count, 0u);
}

TEST(RenderDiagnosticsTest, ExceedingBudgetIsCountedAsMissed) {
  RenderDiagnostics diagnostics;
  diagnostics.RecordRender(0.050, 512, 48000.0);

  EXPECT_EQ(diagnostics.Snapshot().missed_deadline_count, 1u);
}

TEST(RenderDiagnosticsTest, ZeroSampleRateDoesNotCountAsMissed) {
  RenderDiagnostics diagnostics;
  diagnostics.RecordRender(0.050, 512, 0.0);

  EXPECT_EQ(diagnostics.Snapshot().missed_deadline_count, 0u);
}

}  // namespace
}  // namespace lavanda
