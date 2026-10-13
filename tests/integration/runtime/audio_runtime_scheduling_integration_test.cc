#include <gtest/gtest.h>

#include <chrono>
#include <cstdint>
#include <memory>
#include <thread>

#include "lavanda/lavanda.h"
#include "test_support/hardware_gate.h"

namespace lavanda {
namespace {

// ---------------------------------------------------------------------------
// Definitions
// ---------------------------------------------------------------------------

constexpr std::uint64_t kLeadFrames = 24000;
constexpr std::uint64_t kFarFuture = 1'000'000'000;

constexpr std::chrono::seconds kWaitTimeout{15};

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

std::uint64_t Now(const AudioRuntime& runtime) {
  return ToInteger(runtime.frame_position());
}

template <typename Predicate>
bool WaitUntil(Predicate&& predicate) {
  const auto deadline = std::chrono::steady_clock::now() + kWaitTimeout;

  while (!predicate()) {
    if (std::chrono::steady_clock::now() > deadline) {
      return false;
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(2));
  }

  return true;
}

bool WaitForFrame(const AudioRuntime& runtime, std::uint64_t frame) {
  return WaitUntil([&runtime, frame]() { return Now(runtime) >= frame; });
}

bool WaitForNoPending(const AudioRuntime& runtime) {
  return WaitUntil(
      [&runtime]() { return runtime.stats().scheduled_events_pending == 0; });
}

Status ScheduleVoice(AudioRuntime& runtime, CommandType type, VoiceId voice_id,
                     float value, std::uint64_t frame,
                     std::uint32_t duration_frames = 0) {
  Command command;

  command.type = type;
  command.value = value;
  command.voice_id = voice_id;
  command.duration_frames = duration_frames;

  return runtime.ScheduleAtFrame(command, FramePosition{frame});
}

// ---------------------------------------------------------------------------
// A scheduled start/ramp/stop flows through real device callbacks
// ---------------------------------------------------------------------------

TEST(AudioRuntimeSchedulingIntegrationTest,
     ScheduledEventsFlowThroughRealCallbacksAgainstRealHardware) {
  if (!test_support::HardwareTestsRequested()) {
    GTEST_SKIP() << test_support::kHardwareSkipMessage;
  }

  StatusOr<std::unique_ptr<AudioDevice>> device_or =
      CreateDefaultOutputDevice();

  ASSERT_TRUE(device_or.ok()) << device_or.status().message();

  AudioRuntime runtime(std::move(device_or.value()));

  ASSERT_TRUE(runtime.Start().ok());

  const std::uint64_t first_position = Now(runtime);

  ASSERT_TRUE(WaitForFrame(runtime, first_position + 4800))
      << "the frame clock did not advance: no real callbacks are arriving";
  EXPECT_GT(runtime.stats().last_callback_frame_count, 0u);

  StatusOr<Voice> voice_or = runtime.CreateVoice();

  ASSERT_TRUE(voice_or.ok());

  Voice voice = voice_or.value();

  ASSERT_TRUE(voice.SetFrequency(440.0f).ok());
  ASSERT_TRUE(voice.SetGain(0.05f).ok());  // quiet: this plays through speakers

  const RuntimeStats before = runtime.stats();
  const std::uint64_t start_at = Now(runtime) + kLeadFrames;
  const std::uint64_t stop_at = start_at + 48000;

  ASSERT_TRUE(ScheduleVoice(runtime, CommandType::kStartVoice, voice.id(), 0.0f,
                            start_at)
                  .ok());
  ASSERT_TRUE(ScheduleVoice(runtime, CommandType::kRampVoiceGain, voice.id(),
                            0.2f, start_at, 24000)
                  .ok());
  ASSERT_TRUE(
      ScheduleVoice(runtime, CommandType::kStopVoice, voice.id(), 0.0f, stop_at)
          .ok());

  EXPECT_FALSE(voice.IsPlaying()) << "the start is half a second away";

  ASSERT_TRUE(WaitForFrame(runtime, start_at + 2400));
  EXPECT_TRUE(voice.IsPlaying());

  ASSERT_TRUE(WaitForFrame(runtime, stop_at + 4096));
  ASSERT_TRUE(WaitForNoPending(runtime));
  EXPECT_FALSE(voice.IsPlaying());

  const RuntimeStats after = runtime.stats();

  EXPECT_EQ(after.scheduled_events_accepted - before.scheduled_events_accepted,
            3u);
  EXPECT_EQ(after.scheduled_events_executed - before.scheduled_events_executed,
            3u);
  EXPECT_EQ(after.scheduled_events_late - before.scheduled_events_late, 0u)
      << "max lateness was " << after.scheduled_events_max_lateness_frames
      << " frames";
  EXPECT_EQ(after.scheduled_events_discarded, 0u);
  EXPECT_EQ(after.scheduled_events_dropped, 0u);
  EXPECT_EQ(after.scheduled_events_pending, 0u);
  EXPECT_EQ(after.scheduled_events_capacity_failures -
                before.scheduled_events_capacity_failures,
            0u);
  EXPECT_EQ(after.command_failures - before.command_failures, 0u);

  EXPECT_TRUE(voice.Destroy().ok());
  EXPECT_TRUE(runtime.Shutdown().ok());
  EXPECT_FALSE(runtime.is_running());
}

// ---------------------------------------------------------------------------
// Stop/Start retain events; Shutdown/restart discards them and returns capacity
// ---------------------------------------------------------------------------

TEST(AudioRuntimeSchedulingIntegrationTest,
     StopStartAndShutdownRestartAgainstRealHardware) {
  if (!test_support::HardwareTestsRequested()) {
    GTEST_SKIP() << test_support::kHardwareSkipMessage;
  }

  StatusOr<std::unique_ptr<AudioDevice>> device_or =
      CreateDefaultOutputDevice();

  ASSERT_TRUE(device_or.ok()) << device_or.status().message();

  AudioRuntime runtime(std::move(device_or.value()));

  ASSERT_TRUE(runtime.Start().ok());
  ASSERT_TRUE(WaitForFrame(runtime, Now(runtime) + 4800));

  StatusOr<Voice> voice_or = runtime.CreateVoice();

  ASSERT_TRUE(voice_or.ok());

  Voice voice = voice_or.value();
  const VoiceId voice_id = voice.id();

  // --- Stop() freezes the clock and retains pending events -----------------

  const RuntimeStats before_stop = runtime.stats();
  const std::uint64_t target = Now(runtime) + kLeadFrames;

  for (int i = 0; i < 4; ++i) {
    ASSERT_TRUE(ScheduleVoice(runtime, CommandType::kSetVoiceGain, voice_id,
                              0.05f, target)
                    .ok());
  }

  ASSERT_TRUE(runtime.Stop().ok());
  EXPECT_FALSE(runtime.is_running());

  const std::uint64_t stopped_at = Now(runtime);

  std::this_thread::sleep_for(std::chrono::milliseconds(100));

  EXPECT_EQ(Now(runtime), stopped_at)
      << "the clock advanced after Stop() returned";

  for (int i = 0; i < 2; ++i) {
    ASSERT_TRUE(ScheduleVoice(runtime, CommandType::kSetVoiceGain, voice_id,
                              0.05f, target + 1000)
                    .ok());
  }

  ASSERT_TRUE(runtime.Start().ok());
  EXPECT_TRUE(runtime.is_running());
  ASSERT_TRUE(WaitForFrame(runtime, target + 1000 + 4096));
  ASSERT_TRUE(WaitForNoPending(runtime));
  EXPECT_GE(Now(runtime), stopped_at) << "the clock must never go backwards";

  RuntimeStats stats = runtime.stats();

  EXPECT_EQ(
      stats.scheduled_events_executed - before_stop.scheduled_events_executed,
      6u);
  EXPECT_EQ(stats.scheduled_events_discarded, 0u);
  EXPECT_EQ(stats.scheduled_events_dropped, 0u);

  // --- Shutdown()/Start() discards pending events, returns capacity --------

  const RuntimeStats before_shutdown = runtime.stats();
  constexpr std::uint64_t kPendingAtShutdown = 8;

  for (std::uint64_t i = 0; i < kPendingAtShutdown; ++i) {
    ASSERT_TRUE(ScheduleVoice(runtime, CommandType::kSetVoiceGain, voice_id,
                              0.05f, Now(runtime) + kFarFuture)
                    .ok());
  }

  EXPECT_EQ(runtime.stats().scheduled_events_pending, kPendingAtShutdown);

  ASSERT_TRUE(runtime.Shutdown().ok());
  EXPECT_FALSE(runtime.is_running());
  EXPECT_EQ(runtime.stats().scheduled_events_pending, kPendingAtShutdown)
      << "Shutdown() itself must not release capacity; the audio thread does "
         "it when the next session starts";

  const std::uint64_t position_at_shutdown = Now(runtime);

  ASSERT_TRUE(runtime.Start().ok());  // reopens the real device
  ASSERT_TRUE(WaitForNoPending(runtime))
      << "pending events were not discarded after the restart";
  ASSERT_TRUE(WaitForFrame(runtime, position_at_shutdown + 4800))
      << "the clock did not continue past its pre-Shutdown position";

  stats = runtime.stats();

  EXPECT_EQ(stats.scheduled_events_discarded -
                before_shutdown.scheduled_events_discarded,
            kPendingAtShutdown);
  EXPECT_EQ(stats.scheduled_events_executed,
            before_shutdown.scheduled_events_executed);
  EXPECT_EQ(stats.scheduled_events_pending, 0u);
  EXPECT_EQ(stats.scheduled_events_accepted,
            stats.scheduled_events_executed + stats.scheduled_events_discarded +
                stats.scheduled_events_dropped +
                stats.scheduled_events_pending);

  for (std::uint64_t i = 0; i < kPendingAtShutdown; ++i) {
    EXPECT_TRUE(ScheduleVoice(runtime, CommandType::kSetVoiceGain, voice_id,
                              0.05f, Now(runtime) + kFarFuture)
                    .ok());
  }

  EXPECT_TRUE(runtime.Shutdown().ok());
}

}  // namespace
}  // namespace lavanda
