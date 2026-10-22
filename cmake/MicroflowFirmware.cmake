enable_language(ASM)

function(microflow_add_firmware target)
    add_executable(${target} ${ARGN})
    target_compile_features(${target} PRIVATE c_std_11)
    target_include_directories(
        ${target}
        PRIVATE
            "${PROJECT_SOURCE_DIR}/src/runtime/include"
    )
    target_link_libraries(${target} PRIVATE Microflow::microflow)
    microflow_set_warnings(${target})
    set_target_properties(${target} PROPERTIES SUFFIX ".elf")
endfunction()
