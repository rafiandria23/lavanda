#include <chrono>
#include <cstdio>
#include <memory>
#include <thread>

#include "lavanda/lavanda.h"

namespace {

void PrintStats(const lavanda::RuntimeStats& stats, const char* label) {
  std::printf(
      "runtime_demo: [%s] renders=%llu missed=%llu last=%.3fms max=%.3fms\n",
      label, static_cast<unsigned long long>(stats.render_count),
      static_cast<unsigned long long>(stats.missed_deadline_count),
      stats.last_render_duration_seconds * 1000.0,
      stats.max_render_duration_seconds * 1000.0);
}

}  // namespace

int main() {
  lavanda::StatusOr<std::unique_ptr<lavanda::AudioDevice>> device_or =
      lavanda::CreateDefaultOutputDevice();

  if (!device_or.ok()) {
    std::fprintf(stderr, "runtime_demo: no output device available: %s\n",
                 device_or.status().message().c_str());
    return 1;
  }

  lavanda::AudioRuntime runtime(std::move(device_or.value()));
  lavanda::Status start_status = runtime.Start();

  if (!start_status.ok()) {
    std::fprintf(stderr, "runtime_demo: start() failed: %s\n",
                 start_status.message().c_str());
    return 1;
  }

  std::printf("runtime_demo: runtime started.\n");

  auto submit_or_report = [&runtime](const lavanda::Command& command,
                                     const char* label) {
    lavanda::Status status = runtime.Submit(command);

    if (!status.ok()) {
      std::fprintf(stderr, "runtime_demo: %s rejected: %s\n", label,
                   status.message().c_str());
    } else {
      std::printf("runtime_demo: submitted %s\n", label);
    }
  };

  using lavanda::Command;
  using lavanda::CommandType;

  submit_or_report({.type = CommandType::kSetGain, .value = 0.2f},
                   "SetGain(0.2)");
  submit_or_report({.type = CommandType::kSetFrequency, .value = 440.0f},
                   "SetFrequency(440)");
  submit_or_report({.type = CommandType::kStartTone}, "StartTone");

  std::printf("runtime_demo: playing 440 Hz for 1.5s...\n");
  std::this_thread::sleep_for(std::chrono::milliseconds(1500));

  PrintStats(runtime.stats(), "440Hz");

  submit_or_report({.type = CommandType::kSetFrequency, .value = 880.0f},
                   "SetFrequency(880)");

  std::printf("runtime_demo: playing 880 Hz for 1.5s...\n");
  std::this_thread::sleep_for(std::chrono::milliseconds(1500));

  PrintStats(runtime.stats(), "880Hz");

  submit_or_report({.type = CommandType::kSetGain, .value = 0.05f},
                   "SetGain(0.05)");

  std::printf("runtime_demo: quiet for 1s...\n");
  std::this_thread::sleep_for(std::chrono::milliseconds(1000));

  submit_or_report({.type = CommandType::kStopTone}, "StopTone");

  std::this_thread::sleep_for(std::chrono::milliseconds(100));
  PrintStats(runtime.stats(), "stopped");

  lavanda::Status shutdown_status = runtime.Shutdown();

  if (!shutdown_status.ok()) {
    std::fprintf(stderr, "runtime_demo: shutdown() failed: %s\n",
                 shutdown_status.message().c_str());
    return 1;
  }

  std::printf("runtime_demo: shut down cleanly.\n");

  return 0;
}
