#include <gtest/gtest.h>

#include <memory>

#include "lavanda/lavanda.h"

namespace lavanda {
namespace {

TEST(DefaultOutputDeviceTest, ReportsUnavailableBeforeBackendIsWiredUp) {
  StatusOr<std::unique_ptr<AudioDevice>> device_or =
      CreateDefaultOutputDevice();

  ASSERT_FALSE(device_or.ok());

  EXPECT_EQ(device_or.status().code(), ErrorCode::kDeviceUnavailable);
}

}  // namespace
}  // namespace lavanda