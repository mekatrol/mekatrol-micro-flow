function(output_expander_warnings target)
  if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(${target} PRIVATE
      -Wall -Wextra -Wpedantic -Werror -Wconversion -Wshadow
      -Wstrict-prototypes -Wmissing-prototypes)
  elseif(MSVC)
    target_compile_options(${target} PRIVATE /W4 /WX)
  endif()
endfunction()
