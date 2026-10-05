#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "lavanda/lavanda.h"

namespace lavanda {
namespace {

namespace fs = std::filesystem;

constexpr double kPi = 3.14159265358979323846;

bool HardwareTestsRequested() {
  const char* value = std::getenv("LAVANDA_RUN_HARDWARE_TESTS");
  return value != nullptr && std::string(value) == "1";
}

void SleepMs(int milliseconds) {
  std::this_thread::sleep_for(std::chrono::milliseconds(milliseconds));
}

void AppendLe(std::string& out, std::uint32_t value, int byte_count) {
  for (int i = 0; i < byte_count; ++i) {
    out.push_back(static_cast<char>((value >> (8 * i)) & 0xFFu));
  }
}

bool WriteSineWav(const fs::path& path, std::uint32_t sample_rate_hz,
                  double seconds, const std::vector<double>& frequencies_hz) {
  const auto channels = static_cast<std::uint32_t>(frequencies_hz.size());
  const auto frames =
      static_cast<std::uint32_t>(seconds * static_cast<double>(sample_rate_hz));
  const std::uint32_t data_bytes = frames * channels * 2u;
  const double fade_frames = 0.01 * static_cast<double>(sample_rate_hz);

  std::string out;

  out.append("RIFF", 4);

  AppendLe(out, 36u + data_bytes, 4);

  out.append("WAVE", 4);
  out.append("fmt ", 4);

  AppendLe(out, 16u, 4);
  AppendLe(out, 1u, 2);  // PCM
  AppendLe(out, channels, 2);
  AppendLe(out, sample_rate_hz, 4);
  AppendLe(out, sample_rate_hz * channels * 2u, 4);
  AppendLe(out, channels * 2u, 2);
  AppendLe(out, 16u, 2);

  out.append("data", 4);

  AppendLe(out, data_bytes, 4);

  for (std::uint32_t frame = 0; frame < frames; ++frame) {
    const double t =
        static_cast<double>(frame) / static_cast<double>(sample_rate_hz);
    const double edge =
        std::min({1.0, static_cast<double>(frame) / fade_frames,
                  static_cast<double>(frames - 1u - frame) / fade_frames});

    for (double frequency_hz : frequencies_hz) {
      const double amplitude =
          0.3 * edge * std::sin(2.0 * kPi * frequency_hz * t);
      const auto sample =
          static_cast<std::int16_t>(std::lround(amplitude * 32767.0));

      AppendLe(out, static_cast<std::uint16_t>(sample), 2);
    }
  }

  std::ofstream file(path, std::ios::binary);

  file.write(out.data(), static_cast<std::streamsize>(out.size()));

  return file.good();
}

class TempFile {
 public:
  explicit TempFile(const std::string& name)
      : path_(fs::temp_directory_path() / name) {}

  ~TempFile() {
    std::error_code ignored;
    fs::remove(path_, ignored);
  }

  TempFile(const TempFile&) = delete;
  TempFile& operator=(const TempFile&) = delete;

  const fs::path& path() const { return path_; }

 private:
  fs::path path_;
};

void ClearGraph(AudioGraph& graph) {
  for (NodeId id : graph.AllNodeIds()) {
    graph.RemoveNode(id);
  }
}

void BuildMonoTail(AudioGraph& graph, NodeId source) {
  auto gain = std::make_unique<GainNode>(1);
  gain->SetGain(0.5f);

  NodeId gain_id = graph.AddNode(std::move(gain)).value();
  NodeId pan_id = graph.AddNode(std::make_unique<PanNode>()).value();
  NodeId out_id = graph.AddNode(std::make_unique<OutputNode>(2)).value();

  graph.Connect(source, gain_id, 0);
  graph.Connect(gain_id, pan_id, 0);
  graph.Connect(pan_id, out_id, 0);
  graph.SetOutput(out_id);
}

void CompileAndActivate(AudioRuntime& runtime) {
  StatusOr<GraphPlanHandle> plan = runtime.CompileAndStageGraph();

  ASSERT_TRUE(plan.ok()) << plan.status().message();
  ASSERT_TRUE(runtime.ActivateGraphPlan(plan.value()).ok());
}

TEST(AudioRuntimeAssetIntegrationTest, AssetPlaybackAgainstRealHardware) {
  if (!HardwareTestsRequested()) {
    GTEST_SKIP() << "Set LAVANDA_RUN_HARDWARE_TESTS=1 to run this against "
                    "real hardware.";
  }

  TempFile mono_file("lavanda_hw_mono.wav");
  TempFile stereo_file("lavanda_hw_stereo.wav");

  // 44.1 kHz sources force real resampling against the device rate.
  ASSERT_TRUE(WriteSineWav(mono_file.path(), 44100, 1.0, {440.0}));
  ASSERT_TRUE(WriteSineWav(stereo_file.path(), 44100, 1.0, {440.0, 660.0}));

  StatusOr<std::unique_ptr<AudioDevice>> device_or =
      CreateDefaultOutputDevice();

  ASSERT_TRUE(device_or.ok()) << device_or.status().message();

  AudioRuntime runtime(std::move(device_or.value()));

  ASSERT_TRUE(runtime.Start().ok());

  StatusOr<AudioAssetId> mono_or =
      runtime.LoadAudioAsset(mono_file.path().string());
  StatusOr<AudioAssetId> stereo_or =
      runtime.LoadAudioAsset(stereo_file.path().string());

  ASSERT_TRUE(mono_or.ok()) << mono_or.status().message();
  ASSERT_TRUE(stereo_or.ok()) << stereo_or.status().message();

  AudioAssetId mono = mono_or.value();
  AudioAssetId stereo = stereo_or.value();

  EXPECT_EQ(runtime.stats().resident_asset_count, 2u);

  // --- Stage A: mono asset voice plays to the end --------------------------

  StatusOr<Voice> mono_voice_or = runtime.CreateVoice(mono);

  ASSERT_TRUE(mono_voice_or.ok());

  Voice mono_voice = mono_voice_or.value();

  ASSERT_TRUE(mono_voice.SetGain(0.6f).ok());
  ASSERT_TRUE(mono_voice.Start().ok());

  SleepMs(300);

  EXPECT_TRUE(mono_voice.IsPlaying());

  SleepMs(1200);

  EXPECT_FALSE(mono_voice.IsPlaying())
      << "the 1 s asset should have ended by now";
  EXPECT_GT(runtime.stats().render_count, 0u);

  ASSERT_TRUE(mono_voice.Destroy().ok());

  // --- Stage B: stereo asset, live balance changes, then a replay ---------

  StatusOr<Voice> stereo_voice_or = runtime.CreateVoice(stereo);

  ASSERT_TRUE(stereo_voice_or.ok());

  Voice stereo_voice = stereo_voice_or.value();

  ASSERT_TRUE(stereo_voice.SetGain(0.6f).ok());
  ASSERT_TRUE(stereo_voice.Start().ok());

  SleepMs(300);

  ASSERT_TRUE(stereo_voice.SetPan(-0.8f).ok());  // 660 Hz (right) fades

  SleepMs(300);

  ASSERT_TRUE(stereo_voice.SetPan(0.8f).ok());  // 440 Hz (left) fades

  SleepMs(900);

  EXPECT_FALSE(stereo_voice.IsPlaying());

  ASSERT_TRUE(stereo_voice.SetPan(0.0f).ok());
  ASSERT_TRUE(stereo_voice.Start().ok());  // replay after the end

  SleepMs(300);

  EXPECT_TRUE(stereo_voice.IsPlaying());

  SleepMs(1000);

  EXPECT_FALSE(stereo_voice.IsPlaying());

  ASSERT_TRUE(stereo_voice.Destroy().ok());

  SleepMs(200);  // let the destroy commands apply

  // --- Stage C: asset node in a graph, release while playing --------------

  AudioGraph& graph = runtime.graph();
  StatusOr<NodeId> source_or = runtime.AddAssetSourceNode(mono);

  ASSERT_TRUE(source_or.ok());

  BuildMonoTail(graph, source_or.value());
  CompileAndActivate(runtime);
  SleepMs(300);

  RuntimeStats stats = runtime.stats();

  EXPECT_EQ(stats.graph_activation_count, 1u);
  EXPECT_EQ(stats.active_graph_node_count, 4u);  // source + gain + pan + out

  ASSERT_TRUE(runtime.ReleaseAudioAsset(mono).ok());
  EXPECT_EQ(runtime.stats().retiring_asset_count, 1u);

  SleepMs(900);

  ClearGraph(graph);

  auto oscillator = std::make_unique<OscillatorNode>();
  oscillator->SetFrequency(330.0f);

  BuildMonoTail(graph, graph.AddNode(std::move(oscillator)).value());
  CompileAndActivate(runtime);
  SleepMs(500);

  stats = runtime.stats();

  EXPECT_EQ(stats.graph_activation_count, 2u);

  runtime.ReclaimAssets();

  EXPECT_EQ(runtime.stats().retiring_asset_count, 0u)
      << "the retired plan should have released the asset";

  ASSERT_TRUE(runtime.ReleaseAudioAsset(stereo).ok());

  stats = runtime.stats();

  EXPECT_EQ(stats.resident_asset_count, 0u);
  EXPECT_EQ(stats.retiring_asset_count, 0u);

  EXPECT_TRUE(runtime.Shutdown().ok());
  EXPECT_FALSE(runtime.is_running());
}

}  // namespace
}  // namespace lavanda
