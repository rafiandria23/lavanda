#include <chrono>
#include <cstdio>
#include <memory>
#include <thread>

#include "lavanda/lavanda.h"

namespace {

void PrintStats(const lavanda::RuntimeStats& stats, const char* label) {
  std::printf("mixing: [%s] voices=%u buses=%u renders=%llu missed=%llu\n",
              label, stats.active_voice_count, stats.active_bus_count,
              static_cast<unsigned long long>(stats.render_count),
              static_cast<unsigned long long>(stats.missed_deadline_count));
}

}  // namespace

int main() {
  lavanda::StatusOr<std::unique_ptr<lavanda::AudioDevice>> device_or =
      lavanda::CreateDefaultOutputDevice();

  if (!device_or.ok()) {
    std::fprintf(stderr, "mixing: no output device available: %s\n",
                 device_or.status().message().c_str());
    return 1;
  }

  lavanda::AudioRuntime runtime(std::move(device_or.value()));
  lavanda::Status start_status = runtime.Start();

  if (!start_status.ok()) {
    std::fprintf(stderr, "mixing: start() failed: %s\n",
                 start_status.message().c_str());
    return 1;
  }

  std::printf("mixing: runtime started.\n");

  lavanda::StatusOr<lavanda::Bus> sfx_bus_or = runtime.CreateBus();
  lavanda::StatusOr<lavanda::Bus> music_bus_or = runtime.CreateBus();

  if (!sfx_bus_or.ok() || !music_bus_or.ok()) {
    std::fprintf(stderr, "mixing: failed to create buses\n");
    return 1;
  }

  lavanda::Bus sfx_bus = sfx_bus_or.value();
  lavanda::Bus music_bus = music_bus_or.value();

  static_cast<void>(sfx_bus.SetGain(0.6f));
  static_cast<void>(music_bus.SetGain(0.8f));

  lavanda::StatusOr<lavanda::Voice> voice_a_or = runtime.CreateVoice();
  lavanda::StatusOr<lavanda::Voice> voice_b_or = runtime.CreateVoice();
  lavanda::StatusOr<lavanda::Voice> voice_c_or = runtime.CreateVoice();

  if (!voice_a_or.ok() || !voice_b_or.ok() || !voice_c_or.ok()) {
    std::fprintf(stderr, "mixing: failed to create voices\n");
    return 1;
  }

  lavanda::Voice voice_a = voice_a_or.value();  // SFX bus, panned left
  lavanda::Voice voice_b = voice_b_or.value();  // SFX bus, panned right
  lavanda::Voice voice_c = voice_c_or.value();  // Music bus, centered

  static_cast<void>(voice_a.SetFrequency(660.0f));
  static_cast<void>(voice_a.SetGain(0.3f));
  static_cast<void>(voice_a.SetPan(-0.6f));
  static_cast<void>(voice_a.SetBus(sfx_bus));

  static_cast<void>(voice_b.SetFrequency(880.0f));
  static_cast<void>(voice_b.SetGain(0.3f));
  static_cast<void>(voice_b.SetPan(0.6f));
  static_cast<void>(voice_b.SetBus(sfx_bus));

  static_cast<void>(voice_c.SetFrequency(220.0f));
  static_cast<void>(voice_c.SetGain(0.25f));
  static_cast<void>(voice_c.SetPan(0.0f));
  static_cast<void>(voice_c.SetBus(music_bus));

  std::printf(
      "mixing: starting voice A (660Hz, SFX bus, left) and B (880Hz, SFX "
      "bus, right)...\n");

  static_cast<void>(voice_a.Start());
  static_cast<void>(voice_b.Start());

  std::this_thread::sleep_for(std::chrono::milliseconds(1500));

  PrintStats(runtime.stats(), "A+B on SFX");

  std::printf("mixing: starting voice C (220Hz, Music bus, center)...\n");

  static_cast<void>(voice_c.Start());

  std::this_thread::sleep_for(std::chrono::milliseconds(1500));

  PrintStats(runtime.stats(), "A+B+C, all buses");

  std::printf(
      "mixing: lowering SFX bus gain (A+B get quieter, C unaffected)...\n");

  static_cast<void>(sfx_bus.SetGain(0.1f));

  std::this_thread::sleep_for(std::chrono::milliseconds(1200));

  std::printf("mixing: stopping A, B; destroying C...\n");

  static_cast<void>(voice_a.Stop());
  static_cast<void>(voice_b.Stop());
  static_cast<void>(voice_c.Destroy());

  std::this_thread::sleep_for(std::chrono::milliseconds(300));

  PrintStats(runtime.stats(), "stopped");

  lavanda::Status shutdown_status = runtime.Shutdown();

  if (!shutdown_status.ok()) {
    std::fprintf(stderr, "mixing: shutdown() failed: %s\n",
                 shutdown_status.message().c_str());
    return 1;
  }

  std::printf("mixing: shut down cleanly.\n");

  return 0;
}
