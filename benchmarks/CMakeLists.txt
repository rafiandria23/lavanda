if(NOT CMAKE_BUILD_TYPE STREQUAL "Release")
  message(WARNING "lavanda: benchmarks are only meaningful in a Release "
                  "build (CMAKE_BUILD_TYPE is '${CMAKE_BUILD_TYPE}').")
endif()

if(LAVANDA_ENABLE_ASAN OR LAVANDA_ENABLE_TSAN)
  message(WARNING "lavanda: sanitizers are enabled; benchmark timings "
                  "will be heavily distorted.")
endif()

add_executable(graph_benchmark graph_benchmark.cc)
target_link_libraries(graph_benchmark PRIVATE lavanda::lavanda)
target_include_directories(graph_benchmark PRIVATE ${PROJECT_SOURCE_DIR}/src)
lavanda_set_project_warnings(graph_benchmark ${LAVANDA_WARNINGS_AS_ERRORS})

add_test(NAME GraphBenchmarkSmoke COMMAND graph_benchmark 64 50)
