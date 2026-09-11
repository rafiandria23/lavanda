#include <gtest/gtest.h>

#include <chrono>
#include <cstdlib>
#include <memory>
#include <string>
#include <thread>

#include "lavanda/lavanda.h"

namespace lavanda {
namespace {

bool HardwareTestsRequested() {
  const char* value = std::getenv("LAVANDA_RUN_HARDWARE_TESTS");
  return value != nullptr && std::string(value) == "1";
}

TEST(AudioRuntimeMixingIntegrationTest, MultiVoiceMixingAgainstRealHardware) {
  if (!HardwareTestsRequested()) {
    GTEST_SKIP() << "Set LAVANDA_RUN_HARDWARE_TESTS=1 to run this against "
                    "real hardware.";
  }

  StatusOr<std::unique_ptr<AudioDevice>> device_or =
      CreateDefaultOutputDevice();

  ASSERT_TRUE(device_or.ok()) << device_or.status().message();

  RuntimeConfig config;

  config.max_voices = 8;
  config.max_user_buses = 2;

  AudioRuntime runtime(std::move(device_or.value()), config);

  ASSERT_TRUE(runtime.Start().ok());

  StatusOr<Voice> voice_a_or = runtime.CreateVoice();
  StatusOr<Voice> voice_b_or = runtime.CreateVoice();
  StatusOr<Voice> voice_c_or = runtime.CreateVoice();

  ASSERT_TRUE(voice_a_or.ok());
  ASSERT_TRUE(voice_b_or.ok());
  ASSERT_TRUE(voice_c_or.ok());

  Voice voice_a = voice_a_or.value();
  Voice voice_b = voice_b_or.value();
  Voice voice_c = voice_c_or.value();

  ASSERT_TRUE(voice_a.SetFrequency(330.0f).ok());
  ASSERT_TRUE(voice_a.SetGain(0.15f).ok());
  ASSERT_TRUE(voice_b.SetFrequency(440.0f).ok());
  ASSERT_TRUE(voice_b.SetGain(0.15f).ok());
  ASSERT_TRUE(voice_c.SetFrequency(550.0f).ok());
  ASSERT_TRUE(voice_c.SetGain(0.15f).ok());
  ASSERT_TRUE(voice_a.Start().ok());
  ASSERT_TRUE(voice_b.Start().ok());
  ASSERT_TRUE(voice_c.Start().ok());

  std::this_thread::sleep_for(std::chrono::milliseconds(800));

  ASSERT_TRUE(voice_a.SetGain(0.35f).ok());

  std::this_thread::sleep_for(std::chrono::milliseconds(500));

  ASSERT_TRUE(voice_b.SetPan(-0.8f).ok());

  std::this_thread::sleep_for(std::chrono::milliseconds(500));

  StatusOr<Bus> bus_or = runtime.CreateBus();

  ASSERT_TRUE(bus_or.ok());

  Bus bus = bus_or.value();

  ASSERT_TRUE(bus.SetGain(0.7f).ok());
  ASSERT_TRUE(voice_c.SetBus(bus).ok());

  std::this_thread::sleep_for(std::chrono::milliseconds(500));

  RuntimeStats mid_stats = runtime.stats();

  EXPECT_GE(mid_stats.active_voice_count, 3u);
  EXPECT_GE(mid_stats.active_bus_count, 2u);

  ASSERT_TRUE(voice_a.Stop().ok());

  std::this_thread::sleep_for(std::chrono::milliseconds(300));

  VoiceId stale_id = voice_b.id();

  ASSERT_TRUE(voice_b.Destroy().ok());
  EXPECT_FALSE(voice_b.is_valid());

  std::this_thread::sleep_for(std::chrono::milliseconds(200));

  StatusOr<Voice> voice_d_or = runtime.CreateVoice();

  ASSERT_TRUE(voice_d_or.ok());

  Voice voice_d = voice_d_or.value();

  ASSERT_TRUE(voice_d.SetFrequency(660.0f).ok());
  ASSERT_TRUE(voice_d.SetGain(0.2f).ok());
  ASSERT_TRUE(voice_d.Start().ok());

  EXPECT_TRUE(runtime
                  .Submit({.type = CommandType::kSetVoiceGain,
                           .value = 0.0f,
                           .voice_id = stale_id})
                  .ok());

  std::this_thread::sleep_for(std::chrono::milliseconds(500));

  RuntimeStats final_stats = runtime.stats();

  EXPECT_GT(final_stats.render_count, 0u);

  EXPECT_TRUE(voice_c.Stop().ok());
  EXPECT_TRUE(voice_d.Stop().ok());
  EXPECT_TRUE(runtime.Shutdown().ok());
  EXPECT_FALSE(runtime.is_running());
}

}  // namespace
}  // namespace lavanda
