#include <gtest/gtest.h>

#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

#include "lavanda/core/time.h"
#include "lavanda/runtime/audio_runtime.h"
#include "lavanda/runtime/runtime_config.h"
#include "test_support/fake_audio_device.h"
#include "test_support/render_loop.h"

namespace lavanda {
namespace {

using test_support::BlockRecord;
using test_support::FakeAudioDevice;
using test_support::RenderLoop;
using test_support::RenderLoopOptions;

// ---------------------------------------------------------------------------
// Definitions
// ---------------------------------------------------------------------------

constexpr int kMaxRetryAttempts = 10'000'000;
constexpr std::chrono::seconds kWaitTimeout{20};

constexpr std::uint64_t kMaxBlockFrames = 64;

constexpr std::uint64_t kFarFuture = 1'000'000'000;

constexpr std::uint64_t kLeadFrames = 4096;
constexpr std::uint64_t kObserveFrames = 128;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

std::uint64_t Now(const AudioRuntime& runtime) {
  return ToInteger(runtime.frame_position());
}

template <typename Predicate>
bool WaitUntil(Predicate&& predicate) {
  const auto deadline = std::chrono::steady_clock::now() + kWaitTimeout;

  while (!predicate()) {
    if (std::chrono::steady_clock::now() > deadline) {
      return false;
    }

    std::this_thread::yield();
  }

  return true;
}

bool WaitForFrame(const AudioRuntime& runtime, std::uint64_t frame) {
  return WaitUntil([&runtime, frame]() { return Now(runtime) >= frame; });
}

template <typename Fn>
Status RetryOnBackpressure(std::mutex& control_mutex, Fn&& fn) {
  Status status;

  for (int attempt = 0; attempt < kMaxRetryAttempts; ++attempt) {
    {
      std::lock_guard<std::mutex> lock(control_mutex);

      status = fn();
    }

    if (status.code() != ErrorCode::kQueueFull &&
        status.code() != ErrorCode::kResourceExhausted) {
      return status;
    }

    std::this_thread::yield();
  }

  return status;
}

StatusOr<Voice> CreateVoiceLocked(AudioRuntime& runtime,
                                  std::mutex& control_mutex) {
  for (int attempt = 0; attempt < kMaxRetryAttempts; ++attempt) {
    {
      std::lock_guard<std::mutex> lock(control_mutex);
      StatusOr<Voice> voice_or = runtime.CreateVoice();

      if (voice_or.ok()) {
        return voice_or;
      }
    }

    std::this_thread::yield();
  }

  return Status(ErrorCode::kResourceExhausted, "could not create a voice");
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

class Background {
 public:
  template <typename Fn>
  explicit Background(Fn fn)
      : thread_([this, fn]() mutable {
          while (running_.load(std::memory_order_relaxed)) {
            fn();
          }
        }) {}

  ~Background() { Stop(); }

  Background(const Background&) = delete;
  Background& operator=(const Background&) = delete;

  void Stop() {
    running_.store(false, std::memory_order_relaxed);

    if (thread_.joinable()) {
      thread_.join();
    }
  }

 private:
  std::atomic<bool> running_{true};
  std::thread thread_;
};

void ExpectAccountingIdentity(const RuntimeStats& stats) {
  EXPECT_EQ(stats.scheduled_events_accepted,
            stats.scheduled_events_executed + stats.scheduled_events_discarded +
                stats.scheduled_events_dropped +
                stats.scheduled_events_pending);
}

class ScheduledConcurrencyTest : public ::testing::Test {
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

TEST_F(ScheduledConcurrencyTest,
       SerializedProducersRacingTheAudioThreadKeepAccountingExact) {
  constexpr std::uint64_t kProducers = 3;
  constexpr std::uint64_t kAttemptsPerProducer = 10000;

  RuntimeConfig config;
  config.max_scheduled_events = 64;
  config.command_queue_capacity = 32;

  ASSERT_NO_FATAL_FAILURE(Init(config));

  std::mutex control_mutex;
  StatusOr<Voice> voice_or = CreateVoiceLocked(*runtime_, control_mutex);

  ASSERT_TRUE(voice_or.ok());
  ASSERT_TRUE(voice_or.value().Start().ok());

  const VoiceId voice_id = voice_or.value().id();
  const RuntimeStats before = runtime_->stats();

  Outcomes outcomes;
  std::atomic<std::uint64_t> monitor_violations{0};
  std::uint64_t last_position = 0;      // monitor thread only
  std::uint64_t last_render_count = 0;  // monitor thread only

  RenderLoop render_loop(device_, runtime_.get());

  Background monitor([&]() {
    const std::uint64_t position = Now(*runtime_);
    const RuntimeStats snapshot = runtime_->stats();

    if (position < last_position || snapshot.render_count < last_render_count ||
        snapshot.scheduled_events_pending > config.max_scheduled_events) {
      monitor_violations.fetch_add(1, std::memory_order_relaxed);
    }

    last_position = position;
    last_render_count = snapshot.render_count;

    std::this_thread::yield();
  });

  std::vector<std::thread> producers;

  for (std::uint64_t p = 0; p < kProducers; ++p) {
    producers.emplace_back([&, p]() {
      for (std::uint64_t i = 0; i < kAttemptsPerProducer; ++i) {
        Status status;

        {
          std::lock_guard<std::mutex> lock(control_mutex);

          const std::uint64_t lead =
              (i % 4 == 0) ? std::uint64_t{5000} : (i * 7 + p) % 300;

          status = runtime_->ScheduleAtFrame(
              {.type = CommandType::kSetVoiceGain,
               .value = 0.1f + 0.05f * static_cast<float>(i % 18),
               .voice_id = voice_id},
              FramePosition{Now(*runtime_) + lead});
        }

        outcomes.Record(status);

        if (!status.ok()) {
          std::this_thread::yield();
        }
      }
    });
  }

  for (std::thread& producer : producers) {
    producer.join();
  }

  monitor.Stop();
  render_loop.Stop();

  EXPECT_EQ(outcomes.unexpected.load(), 0u);
  EXPECT_EQ(outcomes.total(), kProducers * kAttemptsPerProducer);
  EXPECT_EQ(monitor_violations.load(), 0u)
      << "clock/render_count went backwards or pending exceeded capacity";
  ASSERT_GT(outcomes.accepted.load(), 0u);

  ASSERT_TRUE(Flush());

  const RuntimeStats after = runtime_->stats();

  EXPECT_EQ(after.scheduled_events_accepted - before.scheduled_events_accepted,
            outcomes.accepted.load());
  EXPECT_EQ(after.scheduled_events_executed - before.scheduled_events_executed,
            outcomes.accepted.load())
      << "an accepted event was lost";
  EXPECT_EQ(after.scheduled_events_capacity_failures -
                before.scheduled_events_capacity_failures,
            outcomes.exhausted.load());
  EXPECT_EQ(after.command_failures - before.command_failures,
            outcomes.queue_full.load());
  EXPECT_EQ(after.scheduled_events_dropped, 0u);
  EXPECT_EQ(after.scheduled_events_discarded, 0u);
  EXPECT_EQ(after.scheduled_events_pending, 0u);
  EXPECT_LE(after.scheduled_events_late, after.scheduled_events_executed);

  ExpectAccountingIdentity(after);

  EXPECT_TRUE(runtime_->Shutdown().ok());
}

// ---------------------------------------------------------------------------
// 2. Capacity exhaustion is exact
// ---------------------------------------------------------------------------

TEST_F(ScheduledConcurrencyTest, CapacityExhaustionIsExactUnderContention) {
  constexpr std::uint64_t kCapacity = 16;
  constexpr std::uint64_t kProducers = 3;
  constexpr std::uint64_t kAttemptsPerProducer = 100;

  RuntimeConfig config;
  config.max_scheduled_events = kCapacity;
  config.command_queue_capacity = 64;

  ASSERT_NO_FATAL_FAILURE(Init(config));

  std::mutex control_mutex;
  StatusOr<Voice> voice_or = CreateVoiceLocked(*runtime_, control_mutex);

  ASSERT_TRUE(voice_or.ok());

  const VoiceId voice_id = voice_or.value().id();
  const RuntimeStats before = runtime_->stats();
  Outcomes outcomes;

  {
    RenderLoop render_loop(device_, runtime_.get());
    std::vector<std::thread> producers;

    for (std::uint64_t p = 0; p < kProducers; ++p) {
      producers.emplace_back([&]() {
        for (std::uint64_t i = 0; i < kAttemptsPerProducer; ++i) {
          std::lock_guard<std::mutex> lock(control_mutex);

          outcomes.Record(runtime_->ScheduleAtFrame(
              {.type = CommandType::kSetVoiceGain,
               .value = 0.5f,
               .voice_id = voice_id},
              FramePosition{Now(*runtime_) + kFarFuture}));
        }
      });
    }

    for (std::thread& producer : producers) {
      producer.join();
    }
  }

  const std::uint64_t attempts = kProducers * kAttemptsPerProducer;
  const RuntimeStats after = runtime_->stats();

  EXPECT_EQ(outcomes.unexpected.load(), 0u);
  EXPECT_EQ(outcomes.queue_full.load(), 0u);
  EXPECT_EQ(outcomes.accepted.load(), kCapacity);
  EXPECT_EQ(outcomes.exhausted.load(), attempts - kCapacity);

  EXPECT_EQ(after.scheduled_events_pending, kCapacity);
  EXPECT_EQ(after.scheduled_events_accepted - before.scheduled_events_accepted,
            kCapacity);
  EXPECT_EQ(after.scheduled_events_capacity_failures -
                before.scheduled_events_capacity_failures,
            attempts - kCapacity);

  ExpectAccountingIdentity(after);

  EXPECT_TRUE(runtime_->Shutdown().ok());
}

// ---------------------------------------------------------------------------
// 3. Events land on the exact frame while callbacks and traffic race
// ---------------------------------------------------------------------------

enum class TrialKind {
  kStart,
  kStartThenStopSameFrame,
  kStopThenStartSameFrame,
};

struct Trial {
  TrialKind kind = TrialKind::kStart;
  std::uint64_t window_begin = 0;
  std::uint64_t target = 0;
  std::uint64_t window_end = 0;
  bool exact = false;
};

TEST_F(ScheduledConcurrencyTest,
       EventsLandOnTheExactFrameWhileCallbacksAndTrafficRace) {
  constexpr std::size_t kTrials = 90;

  ASSERT_NO_FATAL_FAILURE(Init(RuntimeConfig()));

  std::mutex control_mutex;
  StatusOr<Voice> noise_or = CreateVoiceLocked(*runtime_, control_mutex);

  ASSERT_TRUE(noise_or.ok());

  const VoiceId noise_id = noise_or.value().id();

  RenderLoopOptions options;
  options.record_blocks = true;
  options.pause = std::chrono::microseconds(10);

  RenderLoop render_loop(device_, runtime_.get(), options);

  std::uint64_t noise_counter = 0;  // noise thread only

  Background noise([&]() {
    {
      std::lock_guard<std::mutex> lock(control_mutex);

      static_cast<void>(runtime_->ScheduleAtFrame(
          {.type = CommandType::kSetVoiceGain,
           .value = 0.5f,
           .voice_id = noise_id},
          FramePosition{Now(*runtime_) + noise_counter % 200}));
    }

    ++noise_counter;
    std::this_thread::yield();
  });

  ASSERT_TRUE(WaitForFrame(*runtime_, Now(*runtime_) + 3 * kMaxBlockFrames));

  std::uint64_t window_begin = Now(*runtime_);
  std::vector<Trial> trials;

  trials.reserve(kTrials);

  for (std::size_t i = 0; i < kTrials; ++i) {
    Trial trial;
    trial.kind = static_cast<TrialKind>(i % 3);
    trial.window_begin = window_begin;

    StatusOr<Voice> voice_or = CreateVoiceLocked(*runtime_, control_mutex);

    ASSERT_TRUE(voice_or.ok());

    Voice voice = voice_or.value();

    ASSERT_TRUE(RetryOnBackpressure(control_mutex, [&]() {
                  return voice.SetFrequency(440.0f);
                }).ok());

    const Command start{.type = CommandType::kStartVoice,
                        .voice_id = voice.id()};
    const Command stop{.type = CommandType::kStopVoice, .voice_id = voice.id()};
    std::array<Command, 2> sequence{start, start};
    std::size_t count = 1;

    switch (trial.kind) {
      case TrialKind::kStart:
        break;
      case TrialKind::kStartThenStopSameFrame:
        sequence = {start, stop};
        count = 2;
        break;
      case TrialKind::kStopThenStartSameFrame:
        sequence = {stop, start};
        count = 2;
        break;
    }

    trial.target = Now(*runtime_) + kLeadFrames;

    for (std::size_t k = 0; k < count; ++k) {
      ASSERT_TRUE(RetryOnBackpressure(control_mutex, [&]() {
                    return runtime_->ScheduleAtFrame(
                        sequence[k], FramePosition{trial.target});
                  }).ok());
    }

    trial.exact = Now(*runtime_) + kMaxBlockFrames <= trial.target;
    trial.window_end = trial.target + kObserveFrames;

    ASSERT_TRUE(WaitForFrame(*runtime_, trial.window_end + kMaxBlockFrames));
    ASSERT_TRUE(RetryOnBackpressure(control_mutex, [&]() {
                  return voice.Destroy();
                }).ok());

    const std::uint64_t destroyed_at = Now(*runtime_);

    ASSERT_TRUE(WaitForFrame(*runtime_, destroyed_at + 3 * kMaxBlockFrames));

    window_begin = Now(*runtime_);
    trials.push_back(trial);
  }

  noise.Stop();
  render_loop.Stop();

  ASSERT_FALSE(render_loop.overflowed());

  const std::vector<BlockRecord>& records = render_loop.records();

  ASSERT_GT(records.size(), 0u);

  std::size_t exact_trials = 0;

  for (const Trial& trial : trials) {
    SCOPED_TRACE(::testing::Message() << "kind=" << static_cast<int>(trial.kind)
                                      << " target=" << trial.target);

    bool found = false;
    std::uint64_t first_frame = 0;

    for (const BlockRecord& block : records) {
      if (block.start_frame < trial.window_begin) {
        continue;
      }

      if (block.start_frame >= trial.window_end) {
        break;
      }

      if (block.first_nonzero_index >= 0) {
        found = true;
        first_frame = block.start_frame +
                      static_cast<std::uint64_t>(block.first_nonzero_index);
        break;
      }
    }

    if (trial.exact) {
      ++exact_trials;
    }

    if (trial.kind == TrialKind::kStartThenStopSameFrame) {
      EXPECT_FALSE(found)
          << "Start then Stop at the same frame must never be audible; first "
             "audible frame was "
          << first_frame;
      continue;
    }

    ASSERT_TRUE(found) << "the scheduled start never became audible";
    EXPECT_GE(first_frame, trial.target + 1)
        << "event took effect before its frame";

    if (trial.exact) {
      EXPECT_EQ(first_frame, trial.target + 1);
    }
  }

  EXPECT_GE(exact_trials * 2, kTrials)
      << "too few trials were scheduled early enough for the exact check";

  EXPECT_TRUE(runtime_->Shutdown().ok());
}

// ---------------------------------------------------------------------------
// 4. Stop/Start cycles while scheduling
// ---------------------------------------------------------------------------

TEST_F(ScheduledConcurrencyTest,
       StopStartCyclesWhileSchedulingKeepAccountingExact) {
  constexpr int kCycles = 300;

  RuntimeConfig config;
  config.max_scheduled_events = 64;
  config.command_queue_capacity = 32;

  ASSERT_NO_FATAL_FAILURE(Init(config));

  std::mutex control_mutex;
  StatusOr<Voice> voice_or = CreateVoiceLocked(*runtime_, control_mutex);

  ASSERT_TRUE(voice_or.ok());
  ASSERT_TRUE(voice_or.value().Start().ok());

  const VoiceId voice_id = voice_or.value().id();
  const RuntimeStats before = runtime_->stats();
  Outcomes outcomes;

  const auto schedule = [&](std::uint64_t lead) {
    outcomes.Record(
        runtime_->ScheduleAtFrame({.type = CommandType::kSetVoiceGain,
                                   .value = 0.5f,
                                   .voice_id = voice_id},
                                  FramePosition{Now(*runtime_) + lead}));
  };

  {
    RenderLoop render_loop(device_, runtime_.get());
    std::uint64_t last_position = Now(*runtime_);

    for (int cycle = 0; cycle < kCycles; ++cycle) {
      for (std::uint64_t k = 0; k < 5; ++k) {
        schedule(50 + 40 * k);
      }

      ASSERT_TRUE(runtime_->Stop().ok());
      EXPECT_FALSE(runtime_->is_running());

      schedule(100);
      schedule(200);

      ASSERT_TRUE(runtime_->Start().ok());
      EXPECT_TRUE(runtime_->is_running());

      const std::uint64_t position = Now(*runtime_);

      EXPECT_GE(position, last_position) << "the clock must never go backwards";

      last_position = position;
    }
  }

  ASSERT_TRUE(runtime_->is_running());
  ASSERT_TRUE(Flush());

  const RuntimeStats after = runtime_->stats();

  EXPECT_EQ(outcomes.unexpected.load(), 0u);
  ASSERT_GT(outcomes.accepted.load(), 0u);
  EXPECT_EQ(after.scheduled_events_accepted - before.scheduled_events_accepted,
            outcomes.accepted.load());
  EXPECT_EQ(after.scheduled_events_executed - before.scheduled_events_executed,
            outcomes.accepted.load())
      << "Stop() must retain pending events, not lose them";
  EXPECT_EQ(after.scheduled_events_discarded, 0u);
  EXPECT_EQ(after.scheduled_events_dropped, 0u);
  EXPECT_EQ(after.scheduled_events_pending, 0u);

  ExpectAccountingIdentity(after);

  EXPECT_TRUE(runtime_->Shutdown().ok());
}

// ---------------------------------------------------------------------------
// 5. Shutdown/restart discards pending events and returns capacity
// ---------------------------------------------------------------------------

TEST_F(ScheduledConcurrencyTest,
       ShutdownRestartCyclesDiscardPendingEventsAndReturnCapacity) {
  constexpr std::uint64_t kCapacity = 16;
  constexpr std::uint64_t kCycles = 8;

  RuntimeConfig config;
  config.max_scheduled_events = kCapacity;
  config.command_queue_capacity = 32;

  ASSERT_NO_FATAL_FAILURE(Init(config));

  std::mutex control_mutex;
  StatusOr<Voice> voice_or = CreateVoiceLocked(*runtime_, control_mutex);

  ASSERT_TRUE(voice_or.ok());

  const VoiceId voice_id = voice_or.value().id();

  const auto schedule_far = [&]() {
    return runtime_->ScheduleAtFrame(
        {.type = CommandType::kSetVoiceGain,
         .value = 0.5f,
         .voice_id = voice_id},
        FramePosition{Now(*runtime_) + kFarFuture});
  };

  std::uint64_t discarded_so_far = runtime_->stats().scheduled_events_discarded;

  for (std::uint64_t cycle = 0; cycle < kCycles; ++cycle) {
    SCOPED_TRACE(::testing::Message() << "cycle=" << cycle);

    {
      RenderLoop render_loop(device_, runtime_.get());

      for (std::uint64_t k = 0; k < kCapacity; ++k) {
        Status status = schedule_far();

        while (status.code() == ErrorCode::kQueueFull) {
          std::this_thread::yield();
          status = schedule_far();
        }

        ASSERT_TRUE(status.ok())
            << "capacity from the previous session was not returned";
      }

      EXPECT_EQ(schedule_far().code(), ErrorCode::kResourceExhausted);
    }

    ASSERT_TRUE(runtime_->Shutdown().ok());

    const RuntimeStats after_shutdown = runtime_->stats();

    EXPECT_EQ(after_shutdown.scheduled_events_pending, kCapacity)
        << "Shutdown() itself must not release capacity; the audio thread "
           "does it at the next session start";
    EXPECT_EQ(after_shutdown.scheduled_events_discarded, discarded_so_far);

    ASSERT_TRUE(runtime_->Start().ok());

    {
      RenderLoop render_loop(device_, runtime_.get());

      ASSERT_TRUE(WaitUntil(
          [&]() { return runtime_->stats().scheduled_events_pending == 0; }));
    }

    const RuntimeStats restarted = runtime_->stats();

    EXPECT_EQ(restarted.scheduled_events_discarded - discarded_so_far,
              kCapacity);

    discarded_so_far = restarted.scheduled_events_discarded;
  }

  const RuntimeStats final_stats = runtime_->stats();

  EXPECT_EQ(final_stats.scheduled_events_executed, 0u);
  EXPECT_EQ(final_stats.scheduled_events_dropped, 0u);
  EXPECT_EQ(final_stats.scheduled_events_discarded, kCycles * kCapacity);

  ExpectAccountingIdentity(final_stats);

  EXPECT_TRUE(runtime_->Shutdown().ok());
}

}  // namespace
}  // namespace lavanda
