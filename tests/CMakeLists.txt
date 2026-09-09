lavanda_provide_googletest()

include_directories(${CMAKE_SOURCE_DIR}/src)

find_package(Threads REQUIRED)

add_library(lavanda_test_support STATIC
  test_support/deterministic_signal.cc
)
target_include_directories(lavanda_test_support PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(lavanda_test_support PUBLIC lavanda::lavanda)
lavanda_set_project_warnings(lavanda_test_support ${LAVANDA_WARNINGS_AS_ERRORS})

add_executable(lavanda_unit_tests
  unit/core/audio_format_test.cc
  unit/core/audio_buffer_test.cc
  unit/device/device_config_test.cc
  unit/runtime/audio_clock_test.cc
  unit/runtime/command_queue_test.cc
  unit/runtime/render_state_test.cc
  unit/runtime/render_diagnostics_test.cc
)
target_link_libraries(lavanda_unit_tests PRIVATE
  lavanda::lavanda
  lavanda_test_support
  GTest::gtest_main
)
lavanda_set_project_warnings(lavanda_unit_tests ${LAVANDA_WARNINGS_AS_ERRORS})

add_executable(lavanda_integration_tests
  integration/device/default_output_device_test.cc
)
target_link_libraries(lavanda_integration_tests PRIVATE
  lavanda::lavanda
  GTest::gtest_main
)
lavanda_set_project_warnings(lavanda_integration_tests ${LAVANDA_WARNINGS_AS_ERRORS})

add_executable(lavanda_concurrency_tests
  concurrency/command_queue_concurrency_test.cc
)
target_link_libraries(lavanda_concurrency_tests PRIVATE
  lavanda::lavanda
  GTest::gtest_main
  Threads::Threads
)
lavanda_set_project_warnings(lavanda_concurrency_tests ${LAVANDA_WARNINGS_AS_ERRORS})

include(GoogleTest)
gtest_discover_tests(lavanda_unit_tests)
gtest_discover_tests(lavanda_integration_tests)
gtest_discover_tests(lavanda_concurrency_tests)
