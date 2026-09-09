#include <gtest/gtest.h>

#include <cstdlib>
#include <memory>
#include <string>

#include "lavanda/lavanda.h"

namespace lavanda {
namespace {

bool HardwareTestsRequested() {
  const char* value = std::getenv("LAVANDA_RUN_HARDWARE_TESTS");

  return value != nullptr && std::string(value) == "1";
}

TEST(DefaultOutputDeviceTest, BackendAvailabilityIsReportedExplicitly) {
  StatusOr<std::unique_ptr<AudioDevice>> device_or =
      CreateDefaultOutputDevice();

#if defined(__APPLE__)
  if (!device_or.ok()) {
    GTEST_SKIP() << "Core Audio backend unavailable here: "
                 << device_or.status().message();
  }

  SUCCEED();
#else
  ASSERT_FALSE(device_or.ok());
  EXPECT_EQ(device_or.status().code(), ErrorCode::kDeviceUnavailable);
#endif
}

TEST(DefaultOutputDeviceTest, FullLifecycleAgainstRealHardware) {
  if (!HardwareTestsRequested()) {
    GTEST_SKIP() << "Set LAVANDA_RUN_HARDWARE_TESTS=1 to run this against "
                    "real hardware.";
  }

  StatusOr<std::unique_ptr<AudioDevice>> device_or =
      CreateDefaultOutputDevice();

  ASSERT_TRUE(device_or.ok()) << device_or.status().message();

  std::unique_ptr<AudioDevice> device = std::move(device_or.value());

  DeviceConfig config;
  Status open_status =
      device->Open(config, [](AudioBufferView output) { output.Clear(); });

  ASSERT_TRUE(open_status.ok()) << open_status.message();
  EXPECT_TRUE(device->is_open());
  EXPECT_TRUE(device->format().IsValid());
  EXPECT_GT(device->buffer_size_frames(), 0u);

  EXPECT_TRUE(device->Start().ok());
  EXPECT_TRUE(device->is_running());
  EXPECT_TRUE(device->Stop().ok());
  EXPECT_FALSE(device->is_running());

  EXPECT_TRUE(device->Start().ok());
  EXPECT_TRUE(device->Stop().ok());

  EXPECT_TRUE(device->Close().ok());
  EXPECT_FALSE(device->is_open());
}

TEST(DefaultOutputDeviceTest, InvalidConfigIsRejectedCleanly) {
  StatusOr<std::unique_ptr<AudioDevice>> device_or =
      CreateDefaultOutputDevice();

  if (!device_or.ok()) {
    GTEST_SKIP() << "No backend available here: "
                 << device_or.status().message();
  }

  std::unique_ptr<AudioDevice> device = std::move(device_or.value());

  DeviceConfig invalid_config;
  invalid_config.sample_rate_hz = 0.0;

  Status status = device->Open(invalid_config,
                               [](AudioBufferView output) { output.Clear(); });

  EXPECT_FALSE(status.ok());
  EXPECT_EQ(status.code(), ErrorCode::kInvalidArgument);
  EXPECT_FALSE(device->is_open());
}

}  // namespace
}  // namespace lavanda
