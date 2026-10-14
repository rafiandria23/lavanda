#include <chrono>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <thread>

#include "lavanda/lavanda.h"

namespace {

constexpr double kSampleRateHz = 48000.0;
constexpr int kNotes = 8;
constexpr double kNoteStepSeconds = 0.25;
constexpr double kAttackSeconds = 0.005;
constexpr double kDecaySeconds = 0.150;
constexpr double kDecayOffsetSeconds = 0.010;
constexpr double kLeadSeconds = 0.25;
constexpr float kNoteFrequenciesHz[kNotes] = {220.0f, 275.0f, 330.0f, 440.0f,
                                              330.0f, 275.0f, 220.0f, 165.0f};

std::uint64_t Frames(double seconds) {
  return static_cast<std::uint64_t>(seconds * kSampleRateHz + 0.5);
}

lavanda::FramePosition At(std::uint64_t frame) {
  return lavanda::FramePosition{frame};
}

lavanda::Command VoiceCommand(lavanda::CommandType type,
                              const lavanda::Voice& voice, float value = 0.0f,
                              std::uint32_t duration_frames = 0) {
  lavanda::Command command;

  command.type = type;
  command.value = value;
  command.voice_id = voice.id();
  command.duration_frames = duration_frames;

  return command;
}

lavanda::Command BusCommand(lavanda::CommandType type, const lavanda::Bus& bus,
                            float value, std::uint32_t duration_frames) {
  lavanda::Command command;

  command.type = type;
  command.value = value;
  command.bus_id = bus.id();
  command.duration_frames = duration_frames;

  return command;
}

void Schedule(lavanda::AudioRuntime& runtime, const lavanda::Command& command,
              std::uint64_t frame, int* failures) {
  const lavanda::Status status = runtime.ScheduleAtFrame(command, At(frame));

  if (!status.ok()) {
    std::fprintf(stderr, "scheduling: ScheduleAtFrame(frame=%llu) failed: %s\n",
                 static_cast<unsigned long long>(frame),
                 status.message().c_str());
    ++*failures;
  }
}

void PrintStats(const lavanda::RuntimeStats& stats, const char* label) {
  std::printf(
      "scheduling: [%s] accepted=%llu executed=%llu late=%llu discarded=%llu "
      "dropped=%llu pending=%llu max_lateness=%llu renders=%llu missed=%llu\n",
      label, static_cast<unsigned long long>(stats.scheduled_events_accepted),
      static_cast<unsigned long long>(stats.scheduled_events_executed),
      static_cast<unsigned long long>(stats.scheduled_events_late),
      static_cast<unsigned long long>(stats.scheduled_events_discarded),
      static_cast<unsigned long long>(stats.scheduled_events_dropped),
      static_cast<unsigned long long>(stats.scheduled_events_pending),
      static_cast<unsigned long long>(
          stats.scheduled_events_max_lateness_frames),
      static_cast<unsigned long long>(stats.render_count),
      static_cast<unsigned long long>(stats.missed_deadline_count));
}

}  // namespace

int main() {
  lavanda::StatusOr<std::unique_ptr<lavanda::AudioDevice>> device_or =
      lavanda::CreateDefaultOutputDevice();

  if (!device_or.ok()) {
    std::fprintf(stderr, "scheduling: no output device available: %s\n",
                 device_or.status().message().c_str());
    return 1;
  }

  lavanda::AudioRuntime runtime(std::move(device_or.value()));

  lavanda::DeviceConfig device_config;
  device_config.sample_rate_hz = kSampleRateHz;

  const lavanda::Status start_status = runtime.Start(device_config);

  if (!start_status.ok()) {
    std::fprintf(stderr, "scheduling: start() failed: %s\n",
                 start_status.message().c_str());
    return 1;
  }

  std::printf("scheduling: runtime started.\n");

  lavanda::StatusOr<lavanda::Bus> bus_or = runtime.CreateBus();
  lavanda::StatusOr<lavanda::Voice> voice_or = runtime.CreateVoice();

  if (!bus_or.ok() || !voice_or.ok()) {
    std::fprintf(stderr, "scheduling: failed to create bus or voice\n");
    static_cast<void>(runtime.Shutdown());
    return 1;
  }

  lavanda::Bus bus = bus_or.value();
  lavanda::Voice voice = voice_or.value();

  static_cast<void>(bus.SetGain(0.3f));
  static_cast<void>(voice.SetFrequency(kNoteFrequenciesHz[0]));
  static_cast<void>(voice.SetGain(0.0f));
  static_cast<void>(voice.SetBus(bus));

  const auto startup_deadline =
      std::chrono::steady_clock::now() + std::chrono::seconds(2);

  while (lavanda::ToInteger(runtime.frame_position()) == 0) {
    if (std::chrono::steady_clock::now() >= startup_deadline) {
      std::fprintf(stderr, "scheduling: the audio clock never started\n");
      static_cast<void>(runtime.Shutdown());
      return 1;
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }

  const std::uint64_t now = lavanda::ToInteger(runtime.frame_position());
  const std::uint64_t t0 = now + Frames(kLeadSeconds);
  const std::uint64_t step = Frames(kNoteStepSeconds);
  const std::uint64_t end = t0 + static_cast<std::uint64_t>(kNotes) * step;

  const std::uint32_t attack_frames =
      static_cast<std::uint32_t>(Frames(kAttackSeconds));
  const std::uint32_t decay_frames =
      static_cast<std::uint32_t>(Frames(kDecaySeconds));
  const std::uint32_t swell_frames = static_cast<std::uint32_t>(end - t0);

  std::printf(
      "scheduling: now=%llu t0=%llu end=%llu (%d notes, %.0f ms apart)\n",
      static_cast<unsigned long long>(now), static_cast<unsigned long long>(t0),
      static_cast<unsigned long long>(end), kNotes, kNoteStepSeconds * 1000.0);

  int failures = 0;

  // 1. Start the voice and swell the bus over the whole phrase.
  Schedule(runtime, VoiceCommand(lavanda::CommandType::kStartVoice, voice), t0,
           &failures);
  Schedule(
      runtime,
      BusCommand(lavanda::CommandType::kRampBusGain, bus, 1.0f, swell_frames),
      t0, &failures);

  // 2. Per note: set pitch, fast attack, then a decay 10 ms later.
  for (int i = 0; i < kNotes; ++i) {
    const std::uint64_t note_frame = t0 + static_cast<std::uint64_t>(i) * step;

    Schedule(runtime,
             VoiceCommand(lavanda::CommandType::kSetVoiceFrequency, voice,
                          kNoteFrequenciesHz[i]),
             note_frame, &failures);
    Schedule(runtime,
             VoiceCommand(lavanda::CommandType::kRampVoiceGain, voice, 0.3f,
                          attack_frames),
             note_frame, &failures);
    Schedule(runtime,
             VoiceCommand(lavanda::CommandType::kRampVoiceGain, voice, 0.0f,
                          decay_frames),
             note_frame + Frames(kDecayOffsetSeconds), &failures);
  }

  // 3. Stop the voice a quarter second after the last note.
  Schedule(runtime, VoiceCommand(lavanda::CommandType::kStopVoice, voice),
           end + Frames(kLeadSeconds), &failures);

  // 4. A deliberately past-dated event, one frame behind the clock. It is
  //    accepted, applied at the next callback start, and counted as late
  //    (late=1 in the final stats).
  Schedule(runtime,
           VoiceCommand(lavanda::CommandType::kSetVoicePan, voice, 0.0f),
           now - 1, &failures);

  if (failures != 0) {
    std::fprintf(stderr, "scheduling: %d event(s) failed to schedule\n",
                 failures);
    static_cast<void>(runtime.Shutdown());
    return 1;
  }

  PrintStats(runtime.stats(), "scheduled");

  const std::uint64_t finish = end + Frames(kLeadSeconds) + 4096;
  const auto deadline =
      std::chrono::steady_clock::now() + std::chrono::seconds(10);

  while (lavanda::ToInteger(runtime.frame_position()) < finish) {
    if (std::chrono::steady_clock::now() >= deadline) {
      std::fprintf(stderr, "scheduling: timed out waiting for the clock\n");
      static_cast<void>(runtime.Shutdown());
      return 1;
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }

  PrintStats(runtime.stats(), "finished");

  static_cast<void>(voice.Destroy());
  static_cast<void>(bus.Destroy());

  std::this_thread::sleep_for(std::chrono::milliseconds(100));

  const lavanda::Status shutdown_status = runtime.Shutdown();

  if (!shutdown_status.ok()) {
    std::fprintf(stderr, "scheduling: shutdown() failed: %s\n",
                 shutdown_status.message().c_str());
    return 1;
  }

  std::printf("scheduling: shut down cleanly.\n");

  return 0;
}
