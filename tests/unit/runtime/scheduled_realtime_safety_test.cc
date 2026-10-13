#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>

#include "lavanda/core/audio_buffer.h"
#include "lavanda/core/time.h"
#include "lavanda/graph/node_id.h"
#include "lavanda/graph/nodes/delay_node.h"
#include "lavanda/graph/nodes/gain_node.h"
#include "lavanda/graph/nodes/one_pole_low_pass_node.h"
#include "lavanda/graph/nodes/oscillator_node.h"
#include "lavanda/graph/nodes/output_node.h"
#include "lavanda/graph/nodes/pan_node.h"
#include "lavanda/runtime/audio_runtime.h"
#include "lavanda/runtime/command.h"
#include "lavanda/runtime/mixing.h"
#include "lavanda/runtime/runtime_stats.h"
#include "test_support/allocation_guard.h"
#include "test_support/fake_audio_device.h"

namespace lavanda {
namespace {

// ---- definitions -----------------------------------------------------------

constexpr std::uint32_t kBlockFrames = 64;
constexpr std::uint32_t kTinyBlockFrames = 2;
constexpr std::uint32_t kMaxDelayFrames = 64;
constexpr int kBurstEvents = 60;
constexpr int kWaves = 4;
constexpr std::uint64_t kFirstFarFrame = 1000;
constexpr std::uint64_t kFarSpread = 240;  // distinct targets, coprime stride
constexpr std::uint64_t kStride = 37;

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

Command VoiceCmd(CommandType type, VoiceId id, float value,
                 std::uint32_t duration = 0) {
  return {.type = type,
          .value = value,
          .voice_id = id,
          .duration_frames = duration};
}

Command BusCmd(CommandType type, BusId id, float value,
               std::uint32_t duration = 0) {
  return {
      .type = type, .value = value, .bus_id = id, .duration_frames = duration};
}

Command NodeCmd(CommandType type, NodeId id, float value) {
  return {.type = type, .value = value, .node_id = id};
}

struct ChainIds {
  NodeId source;
  NodeId low_pass;
  NodeId delay;
  NodeId gain;
  NodeId pan;
  NodeId output;
};

class ScheduledRealtimeSafetyTest : public ::testing::Test {
 protected:
  void SetUp() override {
    auto owned = std::make_unique<test_support::FakeAudioDevice>();

    device_ = owned.get();
    runtime_ = std::make_unique<AudioRuntime>(std::move(owned));

    ASSERT_TRUE(runtime_->Start().ok());

    voice_ = MakeOscillator(440.0f);

    StatusOr<Bus> bus = runtime_->CreateBus();

    ASSERT_TRUE(bus.ok());

    bus_ = bus.value();
    bus_voice_ = MakeOscillator(330.0f);

    ASSERT_TRUE(bus_voice_.SetBus(bus_).ok());

    std::size_t setup_allocations = 0;
    bool heard = false;

    ASSERT_TRUE(RenderGuarded(1, kBlockFrames, setup_allocations, heard));
    ASSERT_TRUE(heard);
  }

  Voice MakeOscillator(float hz) {
    StatusOr<Voice> created = runtime_->CreateVoice();

    EXPECT_TRUE(created.ok());

    Voice voice = created.value();

    EXPECT_TRUE(voice.SetFrequency(hz).ok());
    EXPECT_TRUE(voice.SetGain(0.5f).ok());
    EXPECT_TRUE(voice.Start().ok());

    return voice;
  }

  AudioAssetId LoadFixture(const char* name) {
    StatusOr<AudioAssetId> id = runtime_->LoadAudioAsset(FixturePath(name));

    EXPECT_TRUE(id.ok());

    return id.ok() ? id.value() : AudioAssetId();
  }

  Voice MakeAssetVoice(AudioAssetId asset) {
    StatusOr<Voice> created = runtime_->CreateVoice(asset);

    EXPECT_TRUE(created.ok());

    return created.ok() ? created.value() : Voice();
  }

  Status Schedule(const Command& command, std::uint64_t frame) {
    return runtime_->ScheduleAtFrame(command, FramePosition{frame});
  }

  std::uint64_t Now() const { return ToInteger(runtime_->frame_position()); }

  bool RenderGuarded(int blocks, std::uint32_t frames, std::size_t& allocations,
                     bool& heard) {
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

  ChainIds BuildChain() {
    AudioGraph& graph = runtime_->graph();
    ChainIds ids;

    ids.source = graph.AddNode(std::make_unique<OscillatorNode>()).value();
    ids.low_pass =
        graph.AddNode(std::make_unique<OnePoleLowPassNode>(1)).value();
    ids.delay =
        graph.AddNode(std::make_unique<DelayNode>(1, kMaxDelayFrames)).value();
    ids.gain = graph.AddNode(std::make_unique<GainNode>(1)).value();
    ids.pan = graph.AddNode(std::make_unique<PanNode>()).value();
    ids.output = graph.AddNode(std::make_unique<OutputNode>(2)).value();

    EXPECT_TRUE(graph.Connect(ids.source, ids.low_pass, 0).ok());
    EXPECT_TRUE(graph.Connect(ids.low_pass, ids.delay, 0).ok());
    EXPECT_TRUE(graph.Connect(ids.delay, ids.gain, 0).ok());
    EXPECT_TRUE(graph.Connect(ids.gain, ids.pan, 0).ok());
    EXPECT_TRUE(graph.Connect(ids.pan, ids.output, 0).ok());
    EXPECT_TRUE(graph.SetOutput(ids.output).ok());

    return ids;
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

  test_support::FakeAudioDevice* device_ = nullptr;
  std::unique_ptr<AudioRuntime> runtime_;
  Voice voice_;
  Voice bus_voice_;
  Bus bus_;
};

// ---- tests -----------------------------------------------------------------

TEST_F(ScheduledRealtimeSafetyTest,
       BurstOfMixedEventsExecutesWithoutAllocating) {
  const std::uint64_t base = Now();

  for (int i = 0; i < kBurstEvents; ++i) {
    const std::uint64_t frame = base + 3 + 3 * static_cast<std::uint64_t>(i);
    const float step = static_cast<float>(i);
    Command command;

    switch (i % 5) {
      case 0:
        command = VoiceCmd(CommandType::kSetVoiceGain, voice_.id(),
                           0.3f + 0.005f * step);
        break;
      case 1:
        command = VoiceCmd(CommandType::kSetVoicePan, voice_.id(),
                           -0.9f + 0.03f * step);
        break;
      case 2:
        command = VoiceCmd(CommandType::kSetVoiceFrequency, voice_.id(),
                           300.0f + 5.0f * step);
        break;
      case 3:
        command =
            BusCmd(CommandType::kSetBusGain, bus_.id(), 0.2f + 0.01f * step);
        break;
      default:
        command = VoiceCmd(CommandType::kRampVoiceGain, voice_.id(), 0.5f, 20);
        break;
    }

    ASSERT_TRUE(Schedule(command, frame).ok());
  }

  std::size_t allocations = 0;
  bool heard = false;

  ASSERT_TRUE(RenderGuarded(4, kBlockFrames, allocations, heard));

  EXPECT_EQ(allocations, 0u);
  EXPECT_TRUE(heard);

  const RuntimeStats stats = runtime_->stats();

  EXPECT_EQ(stats.scheduled_events_executed,
            static_cast<std::uint64_t>(kBurstEvents));
  EXPECT_EQ(stats.scheduled_events_pending, 0u);
  EXPECT_EQ(stats.scheduled_events_late, 0u);
  EXPECT_EQ(stats.scheduled_events_dropped, 0u);
}

TEST_F(ScheduledRealtimeSafetyTest, FillingAndDrainingTheHeapDoesNotAllocate) {
  std::size_t allocations = 0;
  bool heard = false;
  int scheduled = 0;

  for (int wave = 0; wave < kWaves; ++wave) {
    for (int i = 0; i < kBurstEvents; ++i) {
      const std::uint64_t k = static_cast<std::uint64_t>(scheduled);
      const std::uint64_t frame = kFirstFarFrame + (k * kStride) % kFarSpread;
      const float step = static_cast<float>(scheduled);
      Command command;

      switch (scheduled % 4) {
        case 0:
          command = VoiceCmd(CommandType::kSetVoiceGain, voice_.id(),
                             0.2f + 0.002f * step);
          break;
        case 1:
          command = VoiceCmd(CommandType::kSetVoicePan, voice_.id(),
                             -0.8f + 0.006f * step);
          break;
        case 2:
          command = VoiceCmd(CommandType::kSetVoiceFrequency, voice_.id(),
                             200.0f + step);
          break;
        default:
          command =
              BusCmd(CommandType::kSetBusGain, bus_.id(), 0.1f + 0.003f * step);
          break;
      }

      ASSERT_TRUE(Schedule(command, frame).ok());

      ++scheduled;
    }

    ASSERT_TRUE(RenderGuarded(1, kBlockFrames, allocations, heard));
  }

  EXPECT_EQ(runtime_->stats().scheduled_events_pending,
            static_cast<std::uint64_t>(kWaves * kBurstEvents));

  ASSERT_TRUE(RenderGuarded(20, kBlockFrames, allocations, heard));

  EXPECT_EQ(allocations, 0u);
  EXPECT_TRUE(heard);

  const RuntimeStats stats = runtime_->stats();

  EXPECT_EQ(stats.scheduled_events_executed,
            static_cast<std::uint64_t>(kWaves * kBurstEvents));
  EXPECT_EQ(stats.scheduled_events_pending, 0u);
  EXPECT_EQ(stats.scheduled_events_dropped, 0u);
}

TEST_F(ScheduledRealtimeSafetyTest, GainRampsAndTheirReplacementDoNotAllocate) {
  const std::uint64_t base = Now();

  ASSERT_TRUE(
      Schedule(VoiceCmd(CommandType::kRampVoiceGain, voice_.id(), 0.2f, 100),
               base + 6)
          .ok());
  ASSERT_TRUE(
      Schedule(VoiceCmd(CommandType::kRampVoiceGain, voice_.id(), 1.0f, 40),
               base + 26)
          .ok());
  ASSERT_TRUE(Schedule(BusCmd(CommandType::kRampBusGain, bus_.id(), 0.0f, 64),
                       base + 56)
                  .ok());
  ASSERT_TRUE(
      Schedule(BusCmd(CommandType::kRampBusGain, kMasterBusId, 0.5f, 128),
               base + 86)
          .ok());
  ASSERT_TRUE(Schedule(VoiceCmd(CommandType::kSetVoiceGain, voice_.id(), 0.6f),
                       base + 116)
                  .ok());
  ASSERT_TRUE(
      Schedule(VoiceCmd(CommandType::kRampVoiceGain, voice_.id(), 0.0f, 0),
               base + 136)
          .ok());

  std::size_t allocations = 0;
  bool heard = false;

  ASSERT_TRUE(RenderGuarded(8, kBlockFrames, allocations, heard));

  EXPECT_EQ(allocations, 0u);
  EXPECT_TRUE(heard);
  EXPECT_EQ(runtime_->stats().scheduled_events_executed, 6u);
}

TEST_F(ScheduledRealtimeSafetyTest,
       RampsOnMonoAndStereoAssetVoicesDoNotAllocate) {
  ASSERT_TRUE(voice_.Stop().ok());
  ASSERT_TRUE(bus_voice_.Stop().ok());

  Voice mono = MakeAssetVoice(LoadFixture("mono_pcm16.wav"));
  Voice stereo = MakeAssetVoice(LoadFixture("stereo_pcm16.wav"));

  std::size_t allocations = 0;
  bool heard = false;

  ASSERT_TRUE(RenderGuarded(1, kBlockFrames, allocations, heard));

  const std::uint64_t base = Now();

  ASSERT_TRUE(
      Schedule(VoiceCmd(CommandType::kStartVoice, mono.id(), 0.0f), base + 1)
          .ok());
  ASSERT_TRUE(
      Schedule(VoiceCmd(CommandType::kStartVoice, stereo.id(), 0.0f), base + 1)
          .ok());
  ASSERT_TRUE(
      Schedule(VoiceCmd(CommandType::kRampVoiceGain, mono.id(), 0.0f, 6),
               base + 1)
          .ok());
  ASSERT_TRUE(
      Schedule(VoiceCmd(CommandType::kRampVoiceGain, stereo.id(), 0.0f, 6),
               base + 1)
          .ok());
  ASSERT_TRUE(
      Schedule(VoiceCmd(CommandType::kSetVoicePan, stereo.id(), 0.5f), base + 3)
          .ok());
  ASSERT_TRUE(
      Schedule(VoiceCmd(CommandType::kSetVoicePan, mono.id(), -0.5f), base + 3)
          .ok());

  allocations = 0;
  heard = false;

  ASSERT_TRUE(RenderGuarded(12, kTinyBlockFrames, allocations, heard));

  EXPECT_EQ(allocations, 0u);
  EXPECT_TRUE(heard) << "asset voices produced silence -- vacuous pass?";
  EXPECT_EQ(runtime_->stats().scheduled_events_executed, 6u);
  EXPECT_FALSE(mono.IsPlaying());
  EXPECT_FALSE(stereo.IsPlaying());
}

TEST_F(ScheduledRealtimeSafetyTest,
       GraphNodeEventsAcrossAPlanSwapDoNotAllocate) {
  const ChainIds ids = BuildChain();

  CompileAndActivate();

  std::size_t allocations = 0;
  bool heard = false;

  ASSERT_TRUE(RenderGuarded(1, kBlockFrames, allocations, heard));

  const std::uint64_t base = Now();

  ASSERT_TRUE(
      Schedule(NodeCmd(CommandType::kSetNodeGain, ids.gain, 0.4f), base + 10)
          .ok());
  ASSERT_TRUE(
      Schedule(NodeCmd(CommandType::kSetNodeFrequency, ids.source, 660.0f),
               base + 20)
          .ok());
  ASSERT_TRUE(
      Schedule(NodeCmd(CommandType::kSetNodeCutoff, ids.low_pass, 300.0f),
               base + 30)
          .ok());
  ASSERT_TRUE(
      Schedule(NodeCmd(CommandType::kSetNodePan, ids.pan, 0.7f), base + 40)
          .ok());
  ASSERT_TRUE(
      Schedule(NodeCmd(CommandType::kSetNodeDelayFrames, ids.delay, 20.0f),
               base + 50)
          .ok());
  ASSERT_TRUE(
      Schedule(NodeCmd(CommandType::kSetNodeDelayFrames, ids.delay, 1e20f),
               base + 60)
          .ok());

  CompileAndActivate();

  ASSERT_TRUE(
      Schedule(NodeCmd(CommandType::kSetNodeGain, ids.gain, 0.2f), base + 130)
          .ok());
  ASSERT_TRUE(
      Schedule(NodeCmd(CommandType::kSetNodePan, ids.pan, -0.7f), base + 150)
          .ok());

  allocations = 0;
  heard = false;

  ASSERT_TRUE(RenderGuarded(5, kBlockFrames, allocations, heard));

  EXPECT_EQ(allocations, 0u);
  EXPECT_TRUE(heard);
  EXPECT_EQ(runtime_->stats().scheduled_events_executed, 8u);
  EXPECT_EQ(runtime_->stats().graph_activation_count, 2u);

  runtime_->ReclaimAssets();
}

TEST_F(ScheduledRealtimeSafetyTest,
       ShutdownDiscardOfPendingEventsDoesNotAllocate) {
  for (std::uint64_t i = 0; i < 5; ++i) {
    ASSERT_TRUE(
        Schedule(VoiceCmd(CommandType::kSetVoiceGain, voice_.id(), 0.1f),
                 100000 + i)
            .ok());
  }

  std::size_t allocations = 0;
  bool heard = false;

  ASSERT_TRUE(RenderGuarded(1, kBlockFrames, allocations, heard));

  EXPECT_EQ(runtime_->stats().scheduled_events_pending, 5u);

  ASSERT_TRUE(runtime_->Shutdown().ok());
  ASSERT_TRUE(runtime_->Start().ok());

  allocations = 0;

  ASSERT_TRUE(RenderGuarded(1, kBlockFrames, allocations, heard));

  EXPECT_EQ(allocations, 0u);

  const RuntimeStats stats = runtime_->stats();

  EXPECT_EQ(stats.scheduled_events_discarded, 5u);
  EXPECT_EQ(stats.scheduled_events_pending, 0u);
  EXPECT_EQ(stats.scheduled_events_executed, 0u);
}

TEST_F(ScheduledRealtimeSafetyTest,
       LateStaleAndUnknownTargetEventsDoNotAllocate) {
  std::size_t allocations = 0;
  bool heard = false;

  ASSERT_TRUE(RenderGuarded(2, kBlockFrames, allocations, heard));

  const VoiceId stale = voice_.id();

  ASSERT_TRUE(voice_.Destroy().ok());

  const std::uint64_t now = Now();
  const NodeId unknown_node{.index = 7, .generation = 1};

  ASSERT_TRUE(
      Schedule(BusCmd(CommandType::kSetBusGain, bus_.id(), 0.4f), 1).ok());
  ASSERT_TRUE(
      Schedule(VoiceCmd(CommandType::kSetVoiceGain, stale, 0.0f), now + 10)
          .ok());
  ASSERT_TRUE(
      Schedule(VoiceCmd(CommandType::kRampVoiceGain, stale, 0.0f, 30), now + 20)
          .ok());
  ASSERT_TRUE(
      Schedule(VoiceCmd(CommandType::kStartVoice, stale, 0.0f), now + 30).ok());
  ASSERT_TRUE(
      Schedule(NodeCmd(CommandType::kSetNodeGain, unknown_node, 0.5f), now + 40)
          .ok());

  allocations = 0;
  heard = false;

  ASSERT_TRUE(RenderGuarded(2, kBlockFrames, allocations, heard));

  EXPECT_EQ(allocations, 0u);
  EXPECT_TRUE(heard) << "the bus voice keeps playing";

  const RuntimeStats stats = runtime_->stats();

  EXPECT_EQ(stats.scheduled_events_executed, 5u);
  EXPECT_EQ(stats.scheduled_events_late, 1u);
  EXPECT_EQ(stats.scheduled_events_max_lateness_frames, now - 1);
  EXPECT_EQ(stats.scheduled_events_pending, 0u);
}

}  // namespace
}  // namespace lavanda
