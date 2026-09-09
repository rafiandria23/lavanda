#include "lavanda/runtime/audio_clock.h"

#include "lavanda/core/time.h"

namespace lavanda {

AudioClock::AudioClock(double sample_rate_hz) noexcept
    : sample_rate_hz_(sample_rate_hz) {}

void AudioClock::Advance(std::uint32_t frame_count) noexcept {
  current_frame_.fetch_add(frame_count, std::memory_order_relaxed);
}

std::uint64_t AudioClock::current_frame() const noexcept {
  return current_frame_.load(std::memory_order_relaxed);
}

double AudioClock::ElapsedSeconds() const noexcept {
  return FramesToSeconds(FrameCount{current_frame()}, sample_rate_hz_);
}

}  // namespace lavanda
