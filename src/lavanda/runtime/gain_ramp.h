#ifndef LAVANDA_RUNTIME_GAIN_RAMP_H_
#define LAVANDA_RUNTIME_GAIN_RAMP_H_

#include <cstdint>
#include <limits>

namespace lavanda {

class GainRamp {
 public:
  constexpr GainRamp() noexcept = default;
  explicit constexpr GainRamp(float gain) noexcept
      : start_value_(gain), target_(gain) {}

  void Set(float gain) noexcept {
    start_value_ = gain;
    target_ = gain;
    start_frame_ = 0;
    end_frame_ = 0;
  }

  void Start(std::uint64_t now, float target, std::uint32_t duration) noexcept {
    if (duration == 0) {
      Set(target);
      return;
    }

    constexpr std::uint64_t kMax = std::numeric_limits<std::uint64_t>::max();

    start_value_ = ValueAt(now);
    target_ = target;
    start_frame_ = now;
    end_frame_ = (kMax - now < duration) ? kMax : now + duration;
  }

  float target() const noexcept { return target_; }

  bool IsConstantFrom(std::uint64_t frame) const noexcept {
    return frame >= end_frame_;
  }

  float ValueAt(std::uint64_t frame) const noexcept {
    if (frame >= end_frame_) {
      return target_;
    }

    if (frame <= start_frame_) {
      return start_value_;
    }

    const double progress = static_cast<double>(frame - start_frame_) /
                            static_cast<double>(end_frame_ - start_frame_);
    const double from = static_cast<double>(start_value_);
    const double to = static_cast<double>(target_);

    return static_cast<float>(from + (to - from) * progress);
  }

 private:
  float start_value_ = 1.0f;
  float target_ = 1.0f;
  std::uint64_t start_frame_ = 0;
  std::uint64_t end_frame_ = 0;
};

}  // namespace lavanda

#endif
