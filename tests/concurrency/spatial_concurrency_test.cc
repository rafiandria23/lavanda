#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "lavanda/core/audio_buffer.h"
#include "lavanda/core/time.h"
#include "lavanda/resources/audio_asset_id.h"
#include "lavanda/runtime/audio_runtime.h"
#include "lavanda/runtime/command.h"
#include "lavanda/runtime/mixing.h"
#include "lavanda/runtime/runtime_config.h"
#include "lavanda/runtime/runtime_stats.h"
#include "lavanda/spatial/quaternion.h"
#include "lavanda/spatial/spatial_state.h"
#include "test_support/fake_audio_device.h"

namespace lavanda {
namespace {

using test_support::FakeAudioDevice;

// ---------------------------------------------------------------------------
// Definitions
// ---------------------------------------------------------------------------

constexpr std::chrono::seconds kWaitTimeout{20};
constexpr float kSampleBound = 4.0f;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

std::string FixturePath(const char* name) {
  return std::string(LAVANDA_FIXTURE_DIR) + "/" + name;
}

std::uint64_t Now(const AudioRuntime& runtime) {
  return ToInteger(runtime.frame_position());
}

bool IsBackpressure(ErrorCode code) {
  return code == ErrorCode::kQueueFull || code == ErrorCode::kResourceExhausted;
}

template <typename Fn>
Status RetryBounded(Fn&& fn) {
  const auto deadline = std::chrono::steady_clock::now() + kWaitTimeout;
  Status status;

  do {
    status = fn();

    if (!IsBackpressure(status.code())) {
      return status;
    }

    std::this_thread::yield();
  } while (std::chrono::steady_clock::now() < deadline);

  return status;
}

template <typename Fn>
StatusOr<Voice> CreateRetry(Fn&& fn) {
  const auto deadline = std::chrono::steady_clock::now() + kWaitTimeout;
  StatusOr<Voice> voice = fn();

  while (!voice.ok() && IsBackpressure(voice.status().code()) &&
         std::chrono::steady_clock::now() < deadline) {
    std::this_thread::yield();
    voice = fn();
  }

  return voice;
}

Command ListenerAt(const Vec3& position) {
  Command command;
  command.type = CommandType::kSetListenerState;
  command.spatial.position = position;

  return command;
}

Command EmitterAt(VoiceId voice, const Vec3& position) {
  Command command;

  command.type = CommandType::kSetVoiceEmitterState;
  command.voice_id = voice;
  command.spatial.position = position;

  return command;
}

Command SpatializationFlag(VoiceId voice, float flag) {
  Command command;

  command.type = CommandType::kSetVoiceSpatialization;
  command.voice_id = voice;
  command.value = flag;

  return command;
}

struct Outcomes {
  std::atomic<std::uint64_t> accepted{0};
  std::atomic<std::uint64_t> queue_full{0};
  std::atomic<std::uint64_t> exhausted{0};
  std::atomic<std::uint64_t> unexpected{0};

  void Record(const Status& status) noexcept {
    if (status.ok()) {
      accepted.fetch_add(1, std::memory_order_relaxed);
    } else if (status.code() == ErrorCode::kQueueFull) {
      queue_full.fetch_add(1, std::memory_order_relaxed);
    } else if (status.code() == ErrorCode::kResourceExhausted) {
      exhausted.fetch_add(1, std::memory_order_relaxed);
    } else {
      unexpected.fetch_add(1, std::memory_order_relaxed);
    }
  }

  std::uint64_t total() const noexcept {
    return accepted.load() + queue_full.load() + exhausted.load() +
           unexpected.load();
  }
};

class AudioThread {
 public:
  explicit AudioThread(FakeAudioDevice* device)
      : thread_([this, device]() { Run(device); }) {}

  ~AudioThread() { Stop(); }

  AudioThread(const AudioThread&) = delete;
  AudioThread& operator=(const AudioThread&) = delete;

  void Stop() {
    running_.store(false, std::memory_order_relaxed);

    if (thread_.joinable()) {
      thread_.join();
    }
  }

  std::uint64_t blocks() const noexcept {
    return blocks_.load(std::memory_order_relaxed);
  }

  std::uint64_t bad_samples() const noexcept {
    return bad_samples_.load(std::memory_order_relaxed);
  }

 private:
  void Run(FakeAudioDevice* device) {
    constexpr std::uint32_t kSizes[] = {64, 16, 37, 64, 5};
    constexpr std::size_t kSizeCount = sizeof(kSizes) / sizeof(kSizes[0]);

    AudioBuffer buffer(64, 2);
    std::size_t size_index = 0;

    while (running_.load(std::memory_order_relaxed)) {
      const std::uint32_t frames = kSizes[size_index];

      size_index = (size_index + 1) % kSizeCount;

      AudioBufferView view = buffer.View().SubView(0, frames);

      view.Clear();

      if (device->PumpRender(view)) {
        blocks_.fetch_add(1, std::memory_order_relaxed);

        std::uint64_t bad = 0;

        for (std::uint32_t frame = 0; frame < frames; ++frame) {
          for (std::uint32_t channel = 0; channel < 2; ++channel) {
            const float sample = view(frame, channel);

            if (!std::isfinite(sample) || std::fabs(sample) > kSampleBound) {
              ++bad;
            }
          }
        }

        if (bad > 0) {
          bad_samples_.fetch_add(bad, std::memory_order_relaxed);
        }
      }

      std::this_thread::yield();
    }
  }

  std::atomic<bool> running_{true};
  std::atomic<std::uint64_t> blocks_{0};
  std::atomic<std::uint64_t> bad_samples_{0};
  std::thread thread_;  // last, so it starts after the atomics exist
};

void ExpectAccountingIdentity(const RuntimeStats& stats) {
  EXPECT_EQ(stats.scheduled_events_accepted,
            stats.scheduled_events_executed + stats.scheduled_events_discarded +
                stats.scheduled_events_dropped +
                stats.scheduled_events_pending);
}

class SpatialConcurrencyTest : public ::testing::Test {
 protected:
  void Init(const RuntimeConfig& config) {
    auto owned = std::make_unique<FakeAudioDevice>();

    device_ = owned.get();
    runtime_ = std::make_unique<AudioRuntime>(std::move(owned), config);

    ASSERT_TRUE(runtime_->Start().ok());
  }

  bool Flush() {
    AudioBuffer buffer(64, 2);

    for (int i = 0; i < 1000; ++i) {
      if (runtime_->stats().scheduled_events_pending == 0) {
        return true;
      }

      if (!device_->PumpRender(buffer.View())) {
        return false;
      }
    }

    return runtime_->stats().scheduled_events_pending == 0;
  }

  FakeAudioDevice* device_ = nullptr;
  std::unique_ptr<AudioRuntime> runtime_;
};

// ---------------------------------------------------------------------------
// 1. Producers racing the audio thread
// ---------------------------------------------------------------------------

TEST_F(SpatialConcurrencyTest,
       ControlTrafficRacingTheAudioThreadKeepsAccountingExact) {
  constexpr std::uint64_t kProducers = 3;
  constexpr std::uint64_t kAttemptsPerProducer = 4000;

  RuntimeConfig config;

  config.max_voices = 8;
  config.max_scheduled_events = 64;
  config.command_queue_capacity = 32;

  ASSERT_NO_FATAL_FAILURE(Init(config));

  std::vector<Voice> tones;

  for (int i = 0; i < 3; ++i) {
    StatusOr<Voice> created = runtime_->CreateVoice();

    ASSERT_TRUE(created.ok());

    Voice voice = created.value();

    ASSERT_TRUE(
        voice.SetFrequency(220.0f + 110.0f * static_cast<float>(i)).ok());
    ASSERT_TRUE(voice.SetGain(0.3f).ok());
    ASSERT_TRUE(voice.SetSpatialized(true).ok());
    ASSERT_TRUE(voice.Start().ok());

    tones.push_back(voice);
  }

  StatusOr<AudioAssetId> asset =
      runtime_->LoadAudioAsset(FixturePath("mono_pcm16.wav"));

  ASSERT_TRUE(asset.ok());

  StatusOr<Voice> clip_or = runtime_->CreateVoice(asset.value());

  ASSERT_TRUE(clip_or.ok());

  Voice clip = clip_or.value();

  ASSERT_TRUE(clip.SetGain(0.3f).ok());
  ASSERT_TRUE(clip.SetSpatialized(true).ok());
  ASSERT_TRUE(clip.Start().ok());

  const RuntimeStats before = runtime_->stats();

  Outcomes immediate;
  Outcomes scheduled;
  std::mutex control_mutex;
  AudioThread audio(device_);

  std::vector<std::thread> producers;

  for (std::uint64_t p = 0; p < kProducers; ++p) {
    producers.emplace_back([&, p]() {
      for (std::uint64_t i = 0; i < kAttemptsPerProducer; ++i) {
        const float t = static_cast<float>(i % 1000);
        const std::size_t v = static_cast<std::size_t>((i + p) % tones.size());
        const bool on = (i / 7) % 2 == 0;

        std::lock_guard<std::mutex> lock(control_mutex);

        switch ((i + p) % 7) {
          case 0: {
            ListenerState listener;
            listener.position = Vec3{20.0f * std::cos(0.01f * t), 0.0f,
                                     20.0f * std::sin(0.01f * t)};
            listener.orientation =
                QuaternionFromAxisAngle(Vec3{0.0f, 1.0f, 0.0f}, 0.02f * t);

            immediate.Record(runtime_->SetListenerState(listener));

            break;
          }
          case 1: {
            EmitterState emitter;
            emitter.position = Vec3{6.0f * std::sin(0.02f * t), 1.0f, -8.0f};
            emitter.velocity = Vec3{2.0f, 0.0f, 0.0f};

            immediate.Record(tones[v].SetEmitterState(emitter));

            break;
          }
          case 2:
            immediate.Record(tones[v].SetSpatialized(on));
            break;
          case 3:
            scheduled.Record(runtime_->ScheduleAtFrame(
                ListenerAt(Vec3{t, 0.0f, 0.0f}),
                FramePosition{Now(*runtime_) + (i * 7 + p) % 300}));
            break;
          case 4:
            scheduled.Record(runtime_->ScheduleAtFrame(
                EmitterAt(tones[v].id(), Vec3{-t, 0.0f, -4.0f}),
                FramePosition{Now(*runtime_) + 5000}));
            break;
          case 5:
            scheduled.Record(runtime_->ScheduleAtFrame(
                SpatializationFlag(tones[v].id(), on ? 1.0f : 0.0f),
                FramePosition{Now(*runtime_) + (i * 7 + p) % 300}));
            break;
          default:
            immediate.Record(clip.Start());
            break;
        }
      }
    });
  }

  for (std::thread& producer : producers) {
    producer.join();
  }

  audio.Stop();

  ASSERT_TRUE(Flush());

  const RuntimeStats after = runtime_->stats();

  EXPECT_EQ(immediate.unexpected.load(), 0u);
  EXPECT_EQ(scheduled.unexpected.load(), 0u);
  EXPECT_EQ(immediate.exhausted.load(), 0u);
  EXPECT_EQ(immediate.total() + scheduled.total(),
            kProducers * kAttemptsPerProducer);
  ASSERT_GT(immediate.accepted.load(), 0u);
  ASSERT_GT(scheduled.accepted.load(), 0u);

  EXPECT_EQ(after.scheduled_events_accepted - before.scheduled_events_accepted,
            scheduled.accepted.load());
  EXPECT_EQ(after.scheduled_events_executed - before.scheduled_events_executed,
            scheduled.accepted.load())
      << "an accepted event was lost";
  EXPECT_EQ(after.scheduled_events_capacity_failures -
                before.scheduled_events_capacity_failures,
            scheduled.exhausted.load());
  EXPECT_EQ(after.command_failures - before.command_failures,
            immediate.queue_full.load() + scheduled.queue_full.load());
  EXPECT_EQ(after.scheduled_events_dropped, 0u);
  EXPECT_EQ(after.scheduled_events_discarded, 0u);
  EXPECT_EQ(after.scheduled_events_pending, 0u);

  EXPECT_GT(audio.blocks(), 0u);
  EXPECT_EQ(audio.bad_samples(), 0u)
      << "NaN, Inf or an out-of-range sample reached the output";

  ExpectAccountingIdentity(after);

  EXPECT_TRUE(runtime_->Shutdown().ok());
}

// ---------------------------------------------------------------------------
// 2. Voice churn with live spatial state and slot reuse
// ---------------------------------------------------------------------------

TEST_F(SpatialConcurrencyTest, SpatializedVoiceChurnWithSlotReuseStaysClean) {
  constexpr int kIterations = 1500;

  RuntimeConfig config;
  config.max_voices = 4;
  config.command_queue_capacity = 16;

  ASSERT_NO_FATAL_FAILURE(Init(config));

  StatusOr<AudioAssetId> asset =
      runtime_->LoadAudioAsset(FixturePath("mono_pcm16.wav"));

  ASSERT_TRUE(asset.ok());

  AudioThread audio(device_);

  for (int i = 0; i < kIterations; ++i) {
    SCOPED_TRACE(::testing::Message() << "iteration=" << i);

    StatusOr<Voice> created = CreateRetry([&]() {
      return i % 2 == 0 ? runtime_->CreateVoice()
                        : runtime_->CreateVoice(asset.value());
    });

    ASSERT_TRUE(created.ok());

    Voice voice = created.value();
    const float t = static_cast<float>(i % 400);

    EmitterState emitter;
    emitter.position = Vec3{8.0f * std::cos(0.05f * t), 0.0f, -5.0f};
    emitter.velocity = Vec3{0.0f, 0.0f, 3.0f};

    ASSERT_TRUE(
        RetryBounded([&]() { return voice.SetEmitterState(emitter); }).ok());
    ASSERT_TRUE(
        RetryBounded([&]() { return voice.SetSpatialized(true); }).ok());
    ASSERT_TRUE(RetryBounded([&]() { return voice.Start(); }).ok());

    Voice stale = voice;

    ASSERT_TRUE(RetryBounded([&]() { return voice.Destroy(); }).ok());

    EXPECT_EQ(stale.SetSpatialized(true).code(), ErrorCode::kInvalidArgument);

    static_cast<void>(stale.SetEmitterState(emitter));
  }

  audio.Stop();

  EXPECT_GT(audio.blocks(), 0u);
  EXPECT_EQ(audio.bad_samples(), 0u);
  EXPECT_GT(runtime_->stats().render_count, 0u);

  EXPECT_TRUE(runtime_->Shutdown().ok());
}

}  // namespace
}  // namespace lavanda
