add_executable(scheduling main.cc)
target_link_libraries(scheduling PRIVATE lavanda::lavanda)
lavanda_set_project_warnings(scheduling ${LAVANDA_WARNINGS_AS_ERRORS})
