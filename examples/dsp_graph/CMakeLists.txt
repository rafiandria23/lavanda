add_executable(dsp_graph main.cc)
target_link_libraries(dsp_graph PRIVATE lavanda::lavanda)
lavanda_set_project_warnings(dsp_graph ${LAVANDA_WARNINGS_AS_ERRORS})
