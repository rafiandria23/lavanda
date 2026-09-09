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

TEST(AudioRuntimeIntegrationTest, FullLifecycleAgainstRealHardware) {
  if (!HardwareTestsRequested()) {
    GTEST_SKIP() << "Set LAVANDA_RUN_HARDWARE_TESTS=1 to run this against "
                    "real hardware.";
  }

  StatusOr<std::unique_ptr<AudioDevice>> device_or =
      CreateDefaultOutputDevice();

  ASSERT_TRUE(device_or.ok()) << device_or.status().message();

  AudioRuntime runtime(std::move(device_or.value()));

  ASSERT_TRUE(runtime.Start().ok());
  EXPECT_TRUE(runtime.is_running());

  ASSERT_TRUE(
      runtime.Submit({.type = CommandType::kSetFrequency, .value = 440.0f})
          .ok());
  ASSERT_TRUE(
      runtime.Submit({.type = CommandType::kSetGain, .value = 0.2f}).ok());
  ASSERT_TRUE(runtime.Submit({.type = CommandType::kStartTone}).ok());

  std::this_thread::sleep_for(std::chrono::milliseconds(300));

  RuntimeStats stats = runtime.stats();

  EXPECT_GT(stats.render_count, 0u)
      << "expected at least one real render callback to have fired";

  ASSERT_TRUE(runtime.Submit({.type = CommandType::kStopTone}).ok());

  std::this_thread::sleep_for(std::chrono::milliseconds(50));

  EXPECT_TRUE(runtime.Stop().ok());
  EXPECT_FALSE(runtime.is_running());
  EXPECT_TRUE(runtime.Start().ok());
  EXPECT_TRUE(runtime.Stop().ok());

  EXPECT_TRUE(runtime.Shutdown().ok());
}

TEST(AudioRuntimeIntegrationTest, StartFailsCleanlyWhenNoBackendAvailable) {
  StatusOr<std::unique_ptr<AudioDevice>> device_or =
      CreateDefaultOutputDevice();

#if !defined(__APPLE__)
  ASSERT_FALSE(device_or.ok());

  AudioRuntime runtime(nullptr);
  Status status = runtime.Start();

  EXPECT_FALSE(status.ok());
  EXPECT_EQ(status.code(), ErrorCode::kDeviceUnavailable);
#else
  if (!device_or.ok()) {
    GTEST_SKIP() << "Core Audio backend unavailable here: "
                 << device_or.status().message();
  }

  SUCCEED();
#endif
}

}  // namespace
}  // namespace lavanda
