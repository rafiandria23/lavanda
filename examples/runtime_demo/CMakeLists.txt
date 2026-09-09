add_executable(runtime_demo main.cc)
target_link_libraries(runtime_demo PRIVATE lavanda::lavanda)
lavanda_set_project_warnings(runtime_demo ${LAVANDA_WARNINGS_AS_ERRORS})
