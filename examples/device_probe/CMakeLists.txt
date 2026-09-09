add_executable(device_probe main.cc)
target_link_libraries(device_probe PRIVATE lavanda::lavanda)
lavanda_set_project_warnings(device_probe ${LAVANDA_WARNINGS_AS_ERRORS})
