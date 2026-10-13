#ifndef LAVANDA_TESTS_TEST_SUPPORT_RENDER_LOOP_H_
#define LAVANDA_TESTS_TEST_SUPPORT_RENDER_LOOP_H_

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <thread>
#include <vector>

#include "lavanda/core/time.h"
#include "lavanda/runtime/audio_runtime.h"
#include "test_support/fake_audio_device.h"

namespace lavanda::test_support {

struct BlockRecord {
  std::uint64_t start_frame = 0;
  std::uint32_t frame_count = 0;
  std::int32_t first_nonzero_index = -1;
};

struct RenderLoopOptions {
  bool record_blocks = false;
  std::chrono::microseconds pause{0};
  std::size_t max_records = std::size_t{1} << 18;
};

class RenderLoop {
 public:
  RenderLoop(FakeAudioDevice* device, const AudioRuntime* runtime,
             RenderLoopOptions options = RenderLoopOptions())
      : options_(options) {
    if (options_.record_blocks) {
      records_.reserve(options.max_records);
    }

    thread_ = std::thread([this, device, runtime]() { Run(device, runtime); });
  }

  ~RenderLoop() { Stop(); }

  RenderLoop(const RenderLoop&) = delete;
  RenderLoop& operator=(const RenderLoop&) = delete;

  void Stop() {
    keep_running_.store(false, std::memory_order_relaxed);

    if (thread_.joinable()) {
      thread_.join();
    }
  }

  const std::vector<BlockRecord>& records() const noexcept { return records_; }
  bool overflowed() const noexcept {
    return overflowed_.load(std::memory_order_relaxed);
  }

 private:
  void Run(FakeAudioDevice* device, const AudioRuntime* runtime) {
    constexpr std::uint32_t kSizes[] = {64, 16, 37, 64, 5};
    constexpr std::size_t kSizeCount = sizeof(kSizes) / sizeof(kSizes[0]);

    AudioBuffer buffer(64, 2);
    std::size_t size_index = 0;

    while (keep_running_.load(std::memory_order_relaxed)) {
      const std::uint32_t frames = kSizes[size_index];

      size_index = (size_index + 1) % kSizeCount;

      const std::uint64_t start = ToInteger(runtime->frame_position());
      AudioBufferView view = buffer.View().SubView(0, frames);

      view.Clear();

      if (device->PumpRender(view) && options_.record_blocks) {
        Record(buffer, start, frames);
      }

      if (options_.pause.count() > 0) {
        std::this_thread::sleep_for(options_.pause);
      } else {
        std::this_thread::yield();
      }
    }
  }

  void Record(const AudioBuffer& buffer, std::uint64_t start,
              std::uint32_t frames) {
    if (records_.size() >= options_.max_records) {
      overflowed_.store(true, std::memory_order_relaxed);
      return;
    }

    BlockRecord record;
    record.start_frame = start;
    record.frame_count = frames;

    for (std::uint32_t f = 0; f < frames && record.first_nonzero_index < 0;
         ++f) {
      for (std::uint32_t c = 0; c < buffer.channel_count(); ++c) {
        if (buffer(f, c) != 0.0f) {
          record.first_nonzero_index = static_cast<std::int32_t>(f);
          break;
        }
      }
    }

    records_.push_back(record);
  }

  RenderLoopOptions options_;
  std::vector<BlockRecord> records_;
  std::atomic<bool> keep_running_{true};
  std::atomic<bool> overflowed_{false};
  std::thread thread_;
};

}  // namespace lavanda::test_support

#endif
