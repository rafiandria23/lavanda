add_executable(mixing main.cc)
target_link_libraries(mixing PRIVATE lavanda::lavanda)
lavanda_set_project_warnings(mixing ${LAVANDA_WARNINGS_AS_ERRORS})
