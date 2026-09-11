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
  EXPECT_EQ(stats.active_voice_count, 0u);
  EXPECT_EQ(stats.active_bus_count, 0u);
  EXPECT_EQ(stats.voice_creation_failures, 0u);
  EXPECT_EQ(stats.bus_creation_failures, 0u);
  EXPECT_EQ(stats.command_failures, 0u);
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

TEST(RenderDiagnosticsTest, SetActiveCountsUpdatesBothFields) {
  RenderDiagnostics diagnostics;
  diagnostics.SetActiveCounts(3, 2);

  RuntimeStats stats = diagnostics.Snapshot();

  EXPECT_EQ(stats.active_voice_count, 3u);
  EXPECT_EQ(stats.active_bus_count, 2u);
}

TEST(RenderDiagnosticsTest, SetActiveCountsReflectsMostRecentCall) {
  RenderDiagnostics diagnostics;

  diagnostics.SetActiveCounts(5, 1);
  diagnostics.SetActiveCounts(1, 1);

  EXPECT_EQ(diagnostics.Snapshot().active_voice_count, 1u);
}

TEST(RenderDiagnosticsTest, RecordVoiceCreationFailureIncrementsCounter) {
  RenderDiagnostics diagnostics;

  diagnostics.RecordVoiceCreationFailure();
  diagnostics.RecordVoiceCreationFailure();

  EXPECT_EQ(diagnostics.Snapshot().voice_creation_failures, 2u);
}

TEST(RenderDiagnosticsTest, RecordBusCreationFailureIncrementsCounter) {
  RenderDiagnostics diagnostics;
  diagnostics.RecordBusCreationFailure();

  EXPECT_EQ(diagnostics.Snapshot().bus_creation_failures, 1u);
}

TEST(RenderDiagnosticsTest, RecordCommandFailureIncrementsCounter) {
  RenderDiagnostics diagnostics;

  diagnostics.RecordCommandFailure();
  diagnostics.RecordCommandFailure();
  diagnostics.RecordCommandFailure();

  EXPECT_EQ(diagnostics.Snapshot().command_failures, 3u);
}

TEST(RenderDiagnosticsTest, FailureCountersAreIndependentOfEachOther) {
  RenderDiagnostics diagnostics;
  diagnostics.RecordVoiceCreationFailure();

  RuntimeStats stats = diagnostics.Snapshot();

  EXPECT_EQ(stats.voice_creation_failures, 1u);
  EXPECT_EQ(stats.bus_creation_failures, 0u);
  EXPECT_EQ(stats.command_failures, 0u);
}

}  // namespace
}  // namespace lavanda
