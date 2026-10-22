function(microflow_enable_sanitizers target)
    if(NOT MICROFLOW_ENABLE_SANITIZERS)
        return()
    endif()

    if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(
            ${target}
            PRIVATE
                -fsanitize=address,undefined
                -fno-omit-frame-pointer
        )
        target_link_options(
            ${target}
            PUBLIC
                -fsanitize=address,undefined
        )
    endif()
endfunction()
