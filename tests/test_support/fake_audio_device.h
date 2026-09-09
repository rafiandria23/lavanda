#ifndef LAVANDA_TESTS_TEST_SUPPORT_FAKE_AUDIO_DEVICE_H_
#define LAVANDA_TESTS_TEST_SUPPORT_FAKE_AUDIO_DEVICE_H_

#include <utility>

#include "lavanda/device/audio_device.h"

namespace lavanda::test_support {

class FakeAudioDevice : public AudioDevice {
 public:
  explicit FakeAudioDevice(bool fail_open = false) : fail_open_(fail_open) {}

  Status Open(const DeviceConfig& config,
              RenderCallback render_callback) override {
    if (is_open_) {
      return Status(ErrorCode::kAlreadyOpen, "fake device already open");
    }

    if (fail_open_) {
      return Status(ErrorCode::kPlatformError,
                    "fake device configured to fail Open()");
    }

    render_callback_ = std::move(render_callback);
    format_ = AudioFormat(config.sample_rate_hz, config.channel_count,
                          SampleFormat::kFloat32);
    buffer_size_frames_ = config.buffer_size_frames;
    is_open_ = true;

    return Status::Ok();
  }

  Status Start() override {
    if (!is_open_) {
      return Status(ErrorCode::kNotOpen, "fake device not open");
    }

    is_running_ = true;

    return Status::Ok();
  }

  Status Stop() override {
    is_running_ = false;

    return Status::Ok();
  }

  Status Close() override {
    is_running_ = false;
    is_open_ = false;
    render_callback_ = nullptr;

    return Status::Ok();
  }

  bool is_open() const noexcept override { return is_open_; }
  bool is_running() const noexcept override { return is_running_; }
  const AudioFormat& format() const noexcept override { return format_; }
  std::uint32_t buffer_size_frames() const noexcept override {
    return buffer_size_frames_;
  }

  bool PumpRender(AudioBufferView output) {
    if (!is_running_ || !render_callback_) {
      return false;
    }

    render_callback_(output);

    return true;
  }

 private:
  bool fail_open_;
  bool is_open_ = false;
  bool is_running_ = false;
  RenderCallback render_callback_;
  AudioFormat format_;
  std::uint32_t buffer_size_frames_ = 0;
};

}  // namespace lavanda::test_support

#endif
