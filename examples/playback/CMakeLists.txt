add_executable(playback main.cc)
target_link_libraries(playback PRIVATE lavanda::lavanda)
lavanda_set_project_warnings(playback ${LAVANDA_WARNINGS_AS_ERRORS})
