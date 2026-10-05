#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>
#include <thread>

#include "lavanda/lavanda.h"

namespace {

void PrintUsage(const char* program) {
  std::fprintf(stderr, "usage: %s <file.wav> [--gain G] [--pan P]\n", program);
  std::fprintf(stderr, "  --gain G   voice gain, default 1.0\n");
  std::fprintf(stderr,
               "  --pan P    -1.0 (left) .. 1.0 (right), default 0.0\n");
}

bool ParseFloat(const char* text, float& out) {
  char* end = nullptr;
  const float value = std::strtof(text, &end);

  if (end == text || *end != '\0') {
    return false;
  }

  out = value;

  return true;
}

}  // namespace

int main(int argc, char** argv) {
  if (argc < 2) {
    PrintUsage(argv[0]);
    return 2;
  }

  const std::string path = argv[1];
  float gain = 1.0f;
  float pan = 0.0f;

  for (int i = 2; i < argc; ++i) {
    const std::string option = argv[i];

    if ((option == "--gain" || option == "--pan") && i + 1 < argc) {
      float& target = (option == "--gain") ? gain : pan;

      if (!ParseFloat(argv[++i], target)) {
        std::fprintf(stderr, "playback: invalid value for %s\n",
                     option.c_str());
        return 2;
      }
    } else {
      PrintUsage(argv[0]);
      return 2;
    }
  }

  lavanda::StatusOr<std::unique_ptr<lavanda::AudioDevice>> device_or =
      lavanda::CreateDefaultOutputDevice();

  if (!device_or.ok()) {
    std::fprintf(stderr, "playback: no output device available: %s\n",
                 device_or.status().message().c_str());
    return 1;
  }

  lavanda::AudioRuntime runtime(std::move(device_or.value()));
  lavanda::Status start_status = runtime.Start();

  if (!start_status.ok()) {
    std::fprintf(stderr, "playback: start() failed: %s\n",
                 start_status.message().c_str());
    return 1;
  }

  lavanda::StatusOr<lavanda::AudioAssetId> asset_or =
      runtime.LoadAudioAsset(path);

  if (!asset_or.ok()) {
    std::fprintf(stderr, "playback: could not load '%s': %s\n", path.c_str(),
                 asset_or.status().message().c_str());
    return 1;
  }

  const lavanda::AudioAssetId asset = asset_or.value();

  std::printf(
      "playback: loaded '%s' (%llu bytes resident).\n", path.c_str(),
      static_cast<unsigned long long>(runtime.stats().resident_asset_bytes));

  lavanda::StatusOr<lavanda::Voice> voice_or = runtime.CreateVoice(asset);

  if (!voice_or.ok()) {
    std::fprintf(stderr, "playback: could not create voice: %s\n",
                 voice_or.status().message().c_str());
    return 1;
  }

  lavanda::Voice voice = voice_or.value();

  lavanda::Status gain_status = voice.SetGain(gain);
  lavanda::Status pan_status = voice.SetPan(pan);

  if (!gain_status.ok() || !pan_status.ok()) {
    std::fprintf(
        stderr, "playback: invalid gain or pan: %s\n",
        (gain_status.ok() ? pan_status : gain_status).message().c_str());
    return 2;
  }

  std::printf("playback: playing (gain %.2f, pan %.2f)...\n",
              static_cast<double>(gain), static_cast<double>(pan));

  static_cast<void>(voice.Start());

  std::this_thread::sleep_for(std::chrono::milliseconds(100));

  const auto deadline =
      std::chrono::steady_clock::now() + std::chrono::seconds(130);

  while (voice.IsPlaying() && std::chrono::steady_clock::now() < deadline) {
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
  }

  const lavanda::RuntimeStats stats = runtime.stats();

  std::printf("playback: finished. renders=%llu missed=%llu\n",
              static_cast<unsigned long long>(stats.render_count),
              static_cast<unsigned long long>(stats.missed_deadline_count));

  static_cast<void>(voice.Destroy());

  std::this_thread::sleep_for(std::chrono::milliseconds(100));

  static_cast<void>(runtime.ReleaseAudioAsset(asset));

  lavanda::Status shutdown_status = runtime.Shutdown();

  if (!shutdown_status.ok()) {
    std::fprintf(stderr, "playback: shutdown() failed: %s\n",
                 shutdown_status.message().c_str());
    return 1;
  }

  std::printf("playback: shut down cleanly.\n");

  return 0;
}
