include(FetchContent)

function(lavanda_provide_googletest)
  find_package(GTest QUIET)
  if(GTest_FOUND)
    return()
  endif()
  set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
  set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
  FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG v1.18.0
  )
  FetchContent_MakeAvailable(googletest)
endfunction()
