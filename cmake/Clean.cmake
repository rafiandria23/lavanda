get_filename_component(LAVANDA_SOURCE_DIR "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
set(LAVANDA_BUILD_DIR "${LAVANDA_SOURCE_DIR}/build")

if(EXISTS "${LAVANDA_BUILD_DIR}")
  message(STATUS "lavanda: removing ${LAVANDA_BUILD_DIR}")
  file(REMOVE_RECURSE "${LAVANDA_BUILD_DIR}")
else()
  message(STATUS "lavanda: ${LAVANDA_BUILD_DIR} does not exist, nothing to clean")
endif()
