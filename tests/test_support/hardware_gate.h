#ifndef LAVANDA_TESTS_TEST_SUPPORT_HARDWARE_GATE_H_
#define LAVANDA_TESTS_TEST_SUPPORT_HARDWARE_GATE_H_

#include <cstdlib>
#include <string>

namespace lavanda::test_support {

inline bool HardwareTestsRequested() {
  const char* value = std::getenv("LAVANDA_RUN_HARDWARE_TESTS");

  return value != nullptr && std::string(value) == "1";
}

inline constexpr const char* kHardwareSkipMessage =
    "Set LAVANDA_RUN_HARDWARE_TESTS=1 to run this against real hardware.";

}  // namespace lavanda::test_support

#endif
