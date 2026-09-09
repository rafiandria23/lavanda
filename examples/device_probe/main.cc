#include <chrono>
#include <cmath>
#include <cstdio>
#include <memory>
#include <numbers>
#include <thread>

#include "lavanda/lavanda.h"

namespace {

constexpr double kTestToneHz = 440.0;
constexpr float kTestToneAmplitude = 0.2f;
constexpr auto kRunDuration = std::chrono::seconds(2);

class SineGenerator {
 public:
  explicit SineGenerator(double sample_rate_hz)
      : phase_increment_(2.0 * std::numbers::pi * kTestToneHz /
                         sample_rate_hz) {}

  void Render(lavanda::AudioBufferView output) noexcept {
    for (std::uint32_t frame = 0; frame < output.frame_count(); ++frame) {
      const float sample =
          static_cast<float>(std::sin(phase_)) * kTestToneAmplitude;
      for (std::uint32_t channel = 0; channel < output.channel_count();
           ++channel) {
        output(frame, channel) = sample;
      }
      phase_ += phase_increment_;
    }
  }

 private:
  double phase_ = 0.0;
  double phase_increment_;
};

}  // namespace

int main() {
  lavanda::StatusOr<std::unique_ptr<lavanda::AudioDevice>> device_or =
      lavanda::CreateDefaultOutputDevice();

  if (!device_or.ok()) {
    std::fprintf(stderr, "device_probe: no output device available: %s\n",
                 device_or.status().message().c_str());
    return 1;
  }

  std::unique_ptr<lavanda::AudioDevice> device = std::move(device_or.value());
  lavanda::DeviceConfig config;
  SineGenerator* generator = nullptr;
  lavanda::Status open_status =
      device->Open(config, [&generator](lavanda::AudioBufferView output) {
        generator->Render(output);
      });

  if (!open_status.ok()) {
    std::fprintf(stderr, "device_probe: open() failed: %s\n",
                 open_status.message().c_str());
    return 1;
  }

  SineGenerator sine(device->format().sample_rate_hz());
  generator = &sine;

  std::printf("device_probe: negotiated format: %s, buffer %u frames\n",
              device->format().ToString().c_str(),
              device->buffer_size_frames());

  lavanda::Status start_status = device->Start();

  if (!start_status.ok()) {
    std::fprintf(stderr, "device_probe: start() failed: %s\n",
                 start_status.message().c_str());
    device->Close();
    return 1;
  }

  std::printf("device_probe: playing a %.0f Hz test tone for %lld s...\n",
              kTestToneHz, static_cast<long long>(kRunDuration.count()));

  std::this_thread::sleep_for(kRunDuration);

  device->Stop();

  if (!device->Close().ok()) {
    std::fprintf(stderr, "device_probe: close() failed\n");
    return 1;
  }

  std::printf("device_probe: stopped and closed cleanly.\n");

  return 0;
}
