#include <gtest/gtest.h>

#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <thread>

#include "lavanda/graph/nodes/output_node.h"
#include "lavanda/graph/nodes/pan_node.h"
#include "lavanda/runtime/audio_runtime.h"
#include "lavanda/runtime/runtime_config.h"
#include "test_support/fake_audio_device.h"

namespace lavanda {
namespace {

constexpr int kMaxRetryAttempts = 10'000'000;
constexpr int kMaxLoadAttempts = 10'000;

bool HasNonSilence(const AudioBuffer& buffer) {
  for (std::uint32_t f = 0; f < buffer.frame_count(); ++f) {
    for (std::uint32_t c = 0; c < buffer.channel_count(); ++c) {
      if (buffer(f, c) != 0.0f) return true;
    }
  }
  return false;
}

std::string FixturePath(const char* name) {
  return std::string(LAVANDA_FIXTURE_DIR) + "/" + name;
}

Status StatusOf(const Status& status) { return status; }

template <typename T>
Status StatusOf(const StatusOr<T>& result) {
  return result.status();
}

template <typename Fn>

auto Retry(Fn&& fn, int max_attempts = kMaxRetryAttempts) -> decltype(fn()) {
  auto result = fn();

  for (int attempt = 1; attempt < max_attempts && !StatusOf(result).ok();
       ++attempt) {
    std::this_thread::yield();
    result = fn();
  }

  return result;
}

GraphPlanHandle CompileWithRetry(AudioRuntime& runtime) {
  StatusOr<GraphPlanHandle> handle =
      Retry([&]() { return runtime.CompileAndStageGraph(); });

  return handle.ok() ? handle.value() : GraphPlanHandle();
}

class RenderLoop {
 public:
  explicit RenderLoop(test_support::FakeAudioDevice* device) {
    thread_ = std::thread([this, device]() {
      AudioBuffer buffer(64, 2);

      while (keep_running_.load(std::memory_order_relaxed)) {
        device->PumpRender(buffer.View());
        std::this_thread::yield();
      }
    });
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

 private:
  std::atomic<bool> keep_running_{true};
  std::thread thread_;
};

class AssetConcurrencyTest : public ::testing::Test {
 protected:
  void Init(const RuntimeConfig& config) {
    auto owned = std::make_unique<test_support::FakeAudioDevice>();

    device_ = owned.get();
    runtime_ = std::make_unique<AudioRuntime>(std::move(owned), config);

    ASSERT_TRUE(runtime_->Start().ok());
  }

  AudioAssetId Load(const char* fixture) {
    StatusOr<AudioAssetId> id = runtime_->LoadAudioAsset(FixturePath(fixture));

    EXPECT_TRUE(id.ok());

    return id.ok() ? id.value() : AudioAssetId();
  }

  void Drain() {
    AudioBuffer buffer(64, 2);

    ASSERT_TRUE(device_->PumpRender(buffer.View()));
    ASSERT_TRUE(device_->PumpRender(buffer.View()));
  }

  test_support::FakeAudioDevice* device_ = nullptr;
  std::unique_ptr<AudioRuntime> runtime_;
};

TEST_F(AssetConcurrencyTest, AssetVoiceChurnUnderConcurrentRendering) {
  RuntimeConfig config;

  config.max_voices = 4;
  config.command_queue_capacity = 16;

  Init(config);

  AudioAssetId mono = Load("mono_pcm16.wav");
  AudioAssetId stereo = Load("stereo_pcm16.wav");

  ASSERT_EQ(runtime_->stats().resident_asset_count, 2u);

  constexpr int kIterations = 3000;
  auto last_render_count = runtime_->stats().render_count;

  {
    RenderLoop render_loop(device_);

    for (int i = 0; i < kIterations; ++i) {
      AudioAssetId asset = (i % 2 == 0) ? mono : stereo;
      StatusOr<Voice> voice_or =
          Retry([&]() { return runtime_->CreateVoice(asset); });

      ASSERT_TRUE(voice_or.ok());

      Voice voice = voice_or.value();

      ASSERT_TRUE(Retry([&]() { return voice.SetGain(0.5f); }).ok());
      ASSERT_TRUE(Retry([&]() { return voice.Start(); }).ok());
      ASSERT_TRUE(Retry([&]() { return voice.Destroy(); }).ok());

      if (i % 16 == 0) {
        const RuntimeStats stats = runtime_->stats();

        EXPECT_GE(stats.render_count, last_render_count);
        EXPECT_EQ(stats.resident_asset_count, 2u);
        EXPECT_EQ(stats.retiring_asset_count, 0u);

        last_render_count = stats.render_count;
      }
    }

    render_loop.Stop();
  }

  EXPECT_GT(runtime_->stats().render_count, 0u);

  Drain();

  StatusOr<Voice> voice_or = runtime_->CreateVoice(mono);

  ASSERT_TRUE(voice_or.ok());

  Voice voice = voice_or.value();
  AudioBuffer buffer(64, 2);

  ASSERT_TRUE(voice.Start().ok());
  ASSERT_TRUE(device_->PumpRender(buffer.View()));
  EXPECT_TRUE(HasNonSilence(buffer));

  ASSERT_TRUE(voice.Destroy().ok());
  Drain();

  ASSERT_TRUE(runtime_->ReleaseAudioAsset(mono).ok());
  ASSERT_TRUE(runtime_->ReleaseAudioAsset(stereo).ok());

  const RuntimeStats final_stats = runtime_->stats();

  EXPECT_EQ(final_stats.resident_asset_count, 0u);
  EXPECT_EQ(final_stats.retiring_asset_count, 0u);

  EXPECT_TRUE(runtime_->Shutdown().ok());
}

TEST_F(AssetConcurrencyTest, ReleaseAndReloadWhilePlayingUnderRendering) {
  RuntimeConfig config;

  config.max_voices = 4;
  config.command_queue_capacity = 16;
  config.resource_config.max_assets = 4;

  Init(config);

  const std::string path = FixturePath("mono_pcm16.wav");
  constexpr int kIterations = 300;

  {
    RenderLoop render_loop(device_);

    for (int i = 0; i < kIterations; ++i) {
      StatusOr<AudioAssetId> asset_or = Retry(
          [&]() { return runtime_->LoadAudioAsset(path); }, kMaxLoadAttempts);

      ASSERT_TRUE(asset_or.ok());

      AudioAssetId asset = asset_or.value();
      StatusOr<Voice> voice_or =
          Retry([&]() { return runtime_->CreateVoice(asset); });

      ASSERT_TRUE(voice_or.ok());

      Voice voice = voice_or.value();

      ASSERT_TRUE(Retry([&]() { return voice.Start(); }).ok());

      ASSERT_TRUE(runtime_->ReleaseAudioAsset(asset).ok());
      ASSERT_TRUE(Retry([&]() { return voice.Destroy(); }).ok());
    }

    render_loop.Stop();
  }

  Drain();
  runtime_->ReclaimAssets();

  const RuntimeStats stats = runtime_->stats();

  EXPECT_EQ(stats.resident_asset_count, 0u);
  EXPECT_EQ(stats.retiring_asset_count, 0u);

  EXPECT_TRUE(runtime_->Shutdown().ok());
}

TEST_F(AssetConcurrencyTest, AssetSourceNodePlanSwapsUnderConcurrentRendering) {
  RuntimeConfig config;
  config.max_graph_plans = 2;
  config.command_queue_capacity = 8;

  Init(config);

  AudioAssetId asset = Load("mono_pcm16.wav");
  AudioGraph& graph = runtime_->graph();

  StatusOr<NodeId> source = runtime_->AddAssetSourceNode(asset);

  ASSERT_TRUE(source.ok());

  NodeId pan = graph.AddNode(std::make_unique<PanNode>()).value();
  NodeId output = graph.AddNode(std::make_unique<OutputNode>(2)).value();

  graph.Connect(source.value(), pan, 0);
  graph.Connect(pan, output, 0);
  graph.SetOutput(output);

  constexpr int kIterations = 300;

  {
    RenderLoop render_loop(device_);

    for (int i = 0; i < kIterations; ++i) {
      GraphPlanHandle handle = CompileWithRetry(*runtime_);

      ASSERT_TRUE(handle.is_valid());
      ASSERT_TRUE(
          Retry([&]() { return runtime_->ActivateGraphPlan(handle); }).ok());

      if (i % 16 == 0) {
        EXPECT_EQ(runtime_->stats().resident_asset_count, 1u);
      }
    }

    render_loop.Stop();
  }

  Drain();

  GraphPlanHandle last = CompileWithRetry(*runtime_);

  ASSERT_TRUE(last.is_valid());
  ASSERT_TRUE(runtime_->ActivateGraphPlan(last).ok());

  AudioBuffer buffer(64, 2);

  ASSERT_TRUE(device_->PumpRender(buffer.View()));
  EXPECT_TRUE(HasNonSilence(buffer));

  ASSERT_TRUE(runtime_->ReleaseAudioAsset(asset).ok());
  runtime_->ReclaimAssets();

  EXPECT_EQ(runtime_->stats().retiring_asset_count, 1u)
      << "exactly the active plan should still pin the released asset";

  EXPECT_TRUE(runtime_->Shutdown().ok());
}

}  // namespace
}  // namespace lavanda
