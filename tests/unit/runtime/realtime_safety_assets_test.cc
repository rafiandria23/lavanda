#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>

#include "lavanda/graph/nodes/output_node.h"
#include "lavanda/graph/nodes/pan_node.h"
#include "lavanda/runtime/audio_runtime.h"
#include "test_support/allocation_guard.h"
#include "test_support/fake_audio_device.h"

namespace lavanda {
namespace {

constexpr std::uint32_t kTinyBlockFrames = 2;
constexpr int kBlocks = 12;

bool HasNonSilence(const AudioBuffer& buffer) {
  for (std::uint32_t f = 0; f < buffer.frame_count(); ++f) {
    for (std::uint32_t c = 0; c < buffer.channel_count(); ++c) {
      if (buffer(f, c) != 0.0f) {
        return true;
      }
    }
  }

  return false;
}

std::string FixturePath(const char* name) {
  return std::string(LAVANDA_FIXTURE_DIR) + "/" + name;
}

class AssetRealtimeSafetyTest : public ::testing::Test {
 protected:
  void SetUp() override {
    auto owned = std::make_unique<test_support::FakeAudioDevice>();

    device_ = owned.get();
    runtime_ = std::make_unique<AudioRuntime>(std::move(owned));

    ASSERT_TRUE(runtime_->Start().ok());
  }

  AudioAssetId LoadPath(const std::string& path) {
    StatusOr<AudioAssetId> id = runtime_->LoadAudioAsset(path);

    EXPECT_TRUE(id.ok());

    return id.ok() ? id.value() : AudioAssetId();
  }

  AudioAssetId Load(const char* fixture) {
    return LoadPath(FixturePath(fixture));
  }

  Voice MakeAssetVoice(AudioAssetId asset) {
    StatusOr<Voice> voice = runtime_->CreateVoice(asset);

    EXPECT_TRUE(voice.ok());

    return voice.ok() ? voice.value() : Voice();
  }

  GraphPlanHandle CompileAndActivate() {
    StatusOr<GraphPlanHandle> handle = runtime_->CompileAndStageGraph();

    EXPECT_TRUE(handle.ok());

    if (!handle.ok()) {
      return GraphPlanHandle();
    }

    EXPECT_TRUE(runtime_->ActivateGraphPlan(handle.value()).ok());

    return handle.value();
  }

  bool RenderGuarded(int blocks, std::size_t& allocations, bool& heard,
                     std::uint32_t frames = kTinyBlockFrames) {
    AudioBuffer buffer(frames, 2);

    for (int i = 0; i < blocks; ++i) {
      bool ok = false;

      {
        test_support::AllocationGuard guard;

        ok = device_->PumpRender(buffer.View());
        allocations += guard.allocation_count();
      }

      if (!ok) {
        return false;
      }

      heard = heard || HasNonSilence(buffer);
    }

    return true;
  }

  test_support::FakeAudioDevice* device_ = nullptr;
  std::unique_ptr<AudioRuntime> runtime_;
};

TEST_F(AssetRealtimeSafetyTest, MonoAssetVoicePlaysToEndWithoutAllocating) {
  Voice voice = MakeAssetVoice(Load("mono_pcm16.wav"));

  ASSERT_TRUE(voice.Start().ok());

  std::size_t allocations = 0;
  bool heard = false;

  ASSERT_TRUE(RenderGuarded(kBlocks, allocations, heard));

  EXPECT_EQ(allocations, 0u);
  EXPECT_TRUE(heard) << "asset voice produced silence -- vacuous pass?";
  EXPECT_FALSE(voice.IsPlaying())
      << "the end-of-asset transition should have run under the guard";
}

TEST_F(AssetRealtimeSafetyTest,
       StereoAssetVoiceWithBalancePlaysToEndWithoutAllocating) {
  Voice voice = MakeAssetVoice(Load("stereo_pcm16.wav"));

  ASSERT_TRUE(voice.SetGain(0.8f).ok());
  ASSERT_TRUE(voice.SetPan(-0.5f).ok());
  ASSERT_TRUE(voice.Start().ok());

  std::size_t allocations = 0;
  bool heard = false;

  ASSERT_TRUE(RenderGuarded(kBlocks, allocations, heard));

  EXPECT_EQ(allocations, 0u);
  EXPECT_TRUE(heard);
  EXPECT_FALSE(voice.IsPlaying());
}

TEST_F(AssetRealtimeSafetyTest, RestartingAnEndedAssetVoiceDoesNotAllocate) {
  Voice voice = MakeAssetVoice(Load("mono_pcm16.wav"));
  std::size_t allocations = 0;

  for (int round = 0; round < 3; ++round) {
    ASSERT_TRUE(voice.Start().ok());

    bool heard = false;

    ASSERT_TRUE(RenderGuarded(kBlocks, allocations, heard));

    EXPECT_TRUE(heard) << "round " << round << " was silent";
    EXPECT_FALSE(voice.IsPlaying()) << "round " << round;
  }

  EXPECT_EQ(allocations, 0u);
}

TEST_F(AssetRealtimeSafetyTest, MonoAndStereoAssetVoicesOnABusDoNotAllocate) {
  StatusOr<Bus> bus_or = runtime_->CreateBus();

  ASSERT_TRUE(bus_or.ok());

  Bus bus = bus_or.value();
  Voice mono = MakeAssetVoice(Load("mono_pcm16.wav"));
  Voice stereo = MakeAssetVoice(Load("stereo_pcm16.wav"));

  ASSERT_TRUE(bus.SetGain(0.5f).ok());
  ASSERT_TRUE(mono.SetBus(bus).ok());
  ASSERT_TRUE(mono.SetPan(0.3f).ok());
  ASSERT_TRUE(stereo.SetBus(bus).ok());
  ASSERT_TRUE(mono.Start().ok());
  ASSERT_TRUE(stereo.Start().ok());

  std::size_t allocations = 0;
  bool heard = false;

  ASSERT_TRUE(RenderGuarded(kBlocks, allocations, heard));

  EXPECT_EQ(allocations, 0u);
  EXPECT_TRUE(heard);
}

TEST_F(AssetRealtimeSafetyTest,
       DestroyingAnAssetVoiceUnpinsOnTheAudioThreadWithoutAllocating) {
  AudioAssetId asset = Load("mono_pcm16.wav");
  Voice voice = MakeAssetVoice(asset);

  ASSERT_TRUE(voice.Start().ok());

  std::size_t allocations = 0;
  bool heard = false;

  ASSERT_TRUE(RenderGuarded(kBlocks, allocations, heard));
  ASSERT_TRUE(voice.Destroy().ok());
  ASSERT_TRUE(runtime_->ReleaseAudioAsset(asset).ok());

  EXPECT_EQ(runtime_->stats().retiring_asset_count, 1u);

  ASSERT_TRUE(RenderGuarded(1, allocations, heard));

  EXPECT_EQ(allocations, 0u);

  runtime_->ReclaimAssets();

  const RuntimeStats stats = runtime_->stats();

  EXPECT_EQ(stats.retiring_asset_count, 0u);
  EXPECT_EQ(stats.resident_asset_count, 0u);
}

TEST_F(AssetRealtimeSafetyTest,
       AssetSourceGraphPlaysAndSwapsPlansWithoutAllocating) {
  AudioAssetId asset = Load("mono_pcm16.wav");
  AudioGraph& graph = runtime_->graph();

  StatusOr<NodeId> source = runtime_->AddAssetSourceNode(asset);

  ASSERT_TRUE(source.ok());

  NodeId pan = graph.AddNode(std::make_unique<PanNode>()).value();
  NodeId output = graph.AddNode(std::make_unique<OutputNode>(2)).value();

  graph.Connect(source.value(), pan, 0);
  graph.Connect(pan, output, 0);
  graph.SetOutput(output);

  GraphPlanHandle plan_a = CompileAndActivate();

  std::size_t allocations = 0;
  bool heard = false;

  ASSERT_TRUE(RenderGuarded(kBlocks, allocations, heard));

  EXPECT_TRUE(heard);

  GraphPlanHandle plan_b = CompileAndActivate();

  ASSERT_NE(plan_b.slot_index, plan_a.slot_index);

  heard = false;

  ASSERT_TRUE(RenderGuarded(kBlocks, allocations, heard));

  EXPECT_EQ(allocations, 0u);
  EXPECT_TRUE(heard) << "plan B should replay the asset from frame 0";

  runtime_->ReclaimAssets();
}

TEST_F(AssetRealtimeSafetyTest, PlaybackDoesNotDependOnTheFileAfterLoading) {
  namespace fs = std::filesystem;

  const fs::path copy =
      fs::temp_directory_path() /
      (std::string("lavanda_") +
       ::testing::UnitTest::GetInstance()->current_test_info()->name() +
       ".wav");

  fs::copy_file(FixturePath("mono_pcm16.wav"), copy,
                fs::copy_options::overwrite_existing);

  AudioAssetId asset = LoadPath(copy.string());

  fs::remove(copy);

  ASSERT_FALSE(fs::exists(copy));

  Voice voice = MakeAssetVoice(asset);

  ASSERT_TRUE(voice.Start().ok());

  std::size_t allocations = 0;
  bool heard = false;

  ASSERT_TRUE(RenderGuarded(kBlocks, allocations, heard));

  EXPECT_EQ(allocations, 0u);
  EXPECT_TRUE(heard) << "playback must come from the resident asset only";
}

TEST_F(AssetRealtimeSafetyTest, LoadingAnAssetAllocatesOnTheControlSide) {
  std::size_t control_side_allocations = 0;

  {
    test_support::AllocationGuard guard;

    StatusOr<AudioAssetId> id =
        runtime_->LoadAudioAsset(FixturePath("mono_pcm16.wav"));

    control_side_allocations = guard.allocation_count();

    ASSERT_TRUE(id.ok());
  }

  EXPECT_GT(control_side_allocations, 0u);
}

}  // namespace
}  // namespace lavanda
