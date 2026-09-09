#ifndef LAVANDA_RUNTIME_AUDIO_CLOCK_H_
#define LAVANDA_RUNTIME_AUDIO_CLOCK_H_

#include <atomic>
#include <cstdint>

namespace lavanda {

class AudioClock {
 public:
  AudioClock() noexcept = default;
  explicit AudioClock(double sample_rate_hz) noexcept;

  AudioClock(const AudioClock&) = delete;
  AudioClock& operator=(const AudioClock&) = delete;

  void Advance(std::uint32_t frame_count) noexcept;

  std::uint64_t current_frame() const noexcept;

  double sample_rate_hz() const noexcept { return sample_rate_hz_; }

  void SetSampleRate(double sample_rate_hz) noexcept {
    sample_rate_hz_ = sample_rate_hz;
  }

  double ElapsedSeconds() const noexcept;

 private:
  double sample_rate_hz_ = 0.0;
  std::atomic<std::uint64_t> current_frame_{0};
};

}  // namespace lavanda

#endif
