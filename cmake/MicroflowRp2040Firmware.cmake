include(MicroflowFirmware)
find_package(Python3 REQUIRED COMPONENTS Interpreter)

function(microflow_add_rp2040_firmware target board_name)
    set(common_dir "${PROJECT_SOURCE_DIR}/src/boards/rpi/common")
    set(board_dir "${PROJECT_SOURCE_DIR}/src/boards/rpi/${board_name}")
    set(boot2_object "${CMAKE_CURRENT_BINARY_DIR}/${target}-boot2.o")
    set(boot2_elf "${CMAKE_CURRENT_BINARY_DIR}/${target}-boot2.elf")
    set(boot2_binary "${CMAKE_CURRENT_BINARY_DIR}/${target}-boot2.bin")
    set(boot2_padded "${CMAKE_CURRENT_BINARY_DIR}/${target}-boot2-padded.S")

    add_custom_command(
        OUTPUT "${boot2_padded}"
        COMMAND "${CMAKE_ASM_COMPILER}" -mcpu=cortex-m0plus -mthumb -c
                "${common_dir}/boot2_generic_03h.S" -o "${boot2_object}"
        COMMAND "${CMAKE_C_COMPILER}" -nostdlib -Wl,-Ttext=0 -Wl,-e,_stage2_boot -Wl,--build-id=none
                "${boot2_object}" -o "${boot2_elf}"
        COMMAND "${CMAKE_OBJCOPY}" -O binary "${boot2_elf}" "${boot2_binary}"
        COMMAND "${Python3_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/tools/pad_boot2.py"
                "${boot2_binary}" "${boot2_padded}"
        DEPENDS "${common_dir}/boot2_generic_03h.S" "${PROJECT_SOURCE_DIR}/tools/pad_boot2.py"
        VERBATIM
    )

    microflow_add_firmware(
        ${target}
        "${board_dir}/main.c"
        "${board_dir}/board.c"
        "${common_dir}/rp2040_startup.S"
        "${common_dir}/rp2040.c"
        "${boot2_padded}"
    )
    target_include_directories(${target} PRIVATE "${common_dir}")
    target_compile_options(
        ${target} PRIVATE -mcpu=cortex-m0plus -mthumb -O2 -ffreestanding
        -ffunction-sections -fdata-sections
    )
    target_compile_options(
        microflow-core PRIVATE -mcpu=cortex-m0plus -mthumb -O2 -ffreestanding
        -ffunction-sections -fdata-sections
    )
    target_link_options(
        ${target}
        PRIVATE
            -mcpu=cortex-m0plus
            -mthumb
            -nostdlib
            -Wl,--gc-sections
            -Wl,--build-id=none
            -Wl,-Map=${CMAKE_CURRENT_BINARY_DIR}/${target}.map
            -T${common_dir}/rp2040.ld
    )

    set(binary "${CMAKE_CURRENT_BINARY_DIR}/${target}.bin")
    set(uf2 "${CMAKE_CURRENT_BINARY_DIR}/${target}.uf2")
    add_custom_command(
        TARGET ${target}
        POST_BUILD
        COMMAND "${CMAKE_OBJCOPY}" -O binary "$<TARGET_FILE:${target}>" "${binary}"
        COMMAND "${Python3_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/tools/bin_to_uf2.py"
                "${binary}" "${uf2}"
        COMMAND "${CMAKE_SIZE}" "$<TARGET_FILE:${target}>"
        VERBATIM
    )
endfunction()
