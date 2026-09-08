#include "lavanda/device/device_config.h"

#include <gtest/gtest.h>

namespace lavanda {
namespace {

TEST(DeviceConfigTest, DefaultsAreValid) {
  DeviceConfig config;

  EXPECT_TRUE(config.IsValid());
  EXPECT_TRUE(config.device.is_default());
}

TEST(DeviceConfigTest, ZeroSampleRateIsInvalid) {
  DeviceConfig config;
  config.sample_rate_hz = 0.0;

  EXPECT_FALSE(config.IsValid());
}

TEST(DeviceConfigTest, ZeroChannelsIsInvalid) {
  DeviceConfig config;
  config.channel_count = 0;

  EXPECT_FALSE(config.IsValid());
}

TEST(DeviceConfigTest, ZeroBufferSizeIsInvalid) {
  DeviceConfig config;
  config.buffer_size_frames = 0;

  EXPECT_FALSE(config.IsValid());
}

TEST(DeviceSelectorTest, ByIdSelectorCarriesOpaqueId) {
  DeviceSelector selector = DeviceSelector::ById("some-backend-uid");

  EXPECT_FALSE(selector.is_default());
  ASSERT_TRUE(selector.backend_id().has_value());
  EXPECT_EQ(*selector.backend_id(), "some-backend-uid");
}

}  // namespace
}  // namespace lavanda
