function(lavanda_set_project_warnings target treat_as_errors)
  set(msvc_warnings /W4)
  set(clang_warnings
      -Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wsign-conversion
      -Wnon-virtual-dtor -Woverloaded-virtual -Wnull-dereference
      -Wdouble-promotion -Wformat=2 -Wold-style-cast
  )
  set(gcc_warnings
      ${clang_warnings}
      -Wmisleading-indentation -Wduplicated-cond -Wduplicated-branches
      -Wlogical-op -Wuseless-cast
  )

  if(treat_as_errors)
    list(APPEND clang_warnings -Werror)
    list(APPEND gcc_warnings -Werror)
    list(APPEND msvc_warnings /WX)
  endif()

  if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    target_compile_options(${target} PRIVATE ${clang_warnings})
  elseif(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    target_compile_options(${target} PRIVATE ${gcc_warnings})
  elseif(CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
    target_compile_options(${target} PRIVATE ${msvc_warnings})
  endif()
endfunction()
