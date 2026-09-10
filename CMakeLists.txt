cmake_minimum_required(VERSION 3.24)

project(lavanda
  VERSION 0.1.0
  DESCRIPTION "Lavanda real-time audio engine"
  LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE "Debug" CACHE STRING "Build type" FORCE)
endif()

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

include(CompilerWarnings)
include(Sanitizers)
include(Dependencies)
include(GNUInstallDirs)
include(CTest)

option(LAVANDA_BUILD_TESTS "Build Lavanda's unit and integration tests" ${PROJECT_IS_TOP_LEVEL})
option(LAVANDA_BUILD_EXAMPLES "Build Lavanda's examples" ${PROJECT_IS_TOP_LEVEL})
option(LAVANDA_WARNINGS_AS_ERRORS "Treat compiler warnings as errors for project-owned targets" ON)

add_library(lavanda STATIC
  src/lavanda/lavanda.cc
  src/lavanda/core/audio_format.cc
  src/lavanda/core/audio_buffer.cc
  src/lavanda/device/audio_device.cc
  src/lavanda/runtime/audio_clock.cc
  src/lavanda/runtime/render_state.cc
  src/lavanda/runtime/command_queue.cc
  src/lavanda/runtime/render_diagnostics.cc
  src/lavanda/runtime/mixer_math.cc
  src/lavanda/runtime/voice_pool.cc
  src/lavanda/runtime/audio_runtime.cc
)
add_library(lavanda::lavanda ALIAS lavanda)

target_compile_features(lavanda PUBLIC cxx_std_20)

target_include_directories(lavanda
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

lavanda_set_project_warnings(lavanda ${LAVANDA_WARNINGS_AS_ERRORS})
lavanda_enable_sanitizers(lavanda)

option(LAVANDA_BUILD_COREAUDIO_BACKEND "Build the Core Audio backend (macOS only)" OFF)

if(APPLE AND LAVANDA_BUILD_COREAUDIO_BACKEND)
  target_sources(lavanda PRIVATE
    src/lavanda/platform/coreaudio/coreaudio_device.cc
  )
  find_library(LAVANDA_AUDIOTOOLBOX_FRAMEWORK AudioToolbox REQUIRED)
  find_library(LAVANDA_COREAUDIO_FRAMEWORK CoreAudio REQUIRED)
  find_library(LAVANDA_COREFOUNDATION_FRAMEWORK CoreFoundation REQUIRED)
  target_link_libraries(lavanda PRIVATE
    ${LAVANDA_AUDIOTOOLBOX_FRAMEWORK}
    ${LAVANDA_COREAUDIO_FRAMEWORK}
    ${LAVANDA_COREFOUNDATION_FRAMEWORK}
  )
else()
  message(STATUS "lavanda: Core Audio backend not built "
                  "(CreateDefaultOutputDevice() will return kDeviceUnavailable)")
endif()

if(LAVANDA_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()

if(LAVANDA_BUILD_EXAMPLES)
  if(APPLE)
    add_subdirectory(examples/device_probe)
    add_subdirectory(examples/runtime_demo)
  else()
    message(STATUS "lavanda: skipping examples/device_probe (macOS only in Phase 1)")
  endif()
endif()

# --- Installation / package config -----------------------------------------
include(CMakePackageConfigHelpers)

install(TARGETS lavanda
  EXPORT lavandaTargets
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
  LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
install(DIRECTORY include/lavanda DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(EXPORT lavandaTargets
  FILE lavandaTargets.cmake
  NAMESPACE lavanda::
  DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/lavanda
)

configure_package_config_file(
  ${CMAKE_CURRENT_SOURCE_DIR}/cmake/lavandaConfig.cmake.in
  ${CMAKE_CURRENT_BINARY_DIR}/lavandaConfig.cmake
  INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/lavanda
)
write_basic_package_version_file(
  ${CMAKE_CURRENT_BINARY_DIR}/lavandaConfigVersion.cmake
  VERSION ${PROJECT_VERSION}
  COMPATIBILITY SameMajorVersion
)
install(FILES
  ${CMAKE_CURRENT_BINARY_DIR}/lavandaConfig.cmake
  ${CMAKE_CURRENT_BINARY_DIR}/lavandaConfigVersion.cmake
  DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/lavanda
)
