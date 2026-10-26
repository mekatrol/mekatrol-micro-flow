include(MicroflowFirmware)

function(microflow_add_rp2350_firmware target architecture)
    set(soc_dir "${PROJECT_SOURCE_DIR}/src/boards/rpi/rp2350")
    set(board_dir "${PROJECT_SOURCE_DIR}/src/boards/rpi/raspberry-pi-pico-2")

    if(architecture STREQUAL "arm")
        set(startup "${soc_dir}/startup_arm.S")
        set(linker_script "${soc_dir}/rp2350_arm.ld")
        set(architecture_options -mcpu=cortex-m33 -mthumb)
    elseif(architecture STREQUAL "riscv")
        set(startup "${soc_dir}/startup_riscv.S")
        set(linker_script "${soc_dir}/rp2350_riscv.ld")
        set(architecture_options -march=rv32imac_zicsr_zifencei -mabi=ilp32)
    else()
        message(FATAL_ERROR "Unknown RP2350 architecture: ${architecture}")
    endif()

    microflow_add_firmware(
        ${target}
        "${board_dir}/main.c"
        "${startup}"
    )
    target_compile_options(
        ${target}
        PRIVATE
            ${architecture_options}
            -O2
            -ffreestanding
            -ffunction-sections
            -fdata-sections
    )
    target_compile_options(
        microflow-core
        PRIVATE
            ${architecture_options}
            -O2
            -ffreestanding
            -ffunction-sections
            -fdata-sections
    )
    target_link_options(
        ${target}
        PRIVATE
            ${architecture_options}
            -nostdlib
            -Wl,--gc-sections
            -Wl,--build-id=none
            -Wl,-Map=${CMAKE_CURRENT_BINARY_DIR}/${target}.map
            -T${linker_script}
    )

    set(binary "${CMAKE_CURRENT_BINARY_DIR}/${target}.bin")
    add_custom_command(
        TARGET ${target}
        POST_BUILD
        COMMAND "${CMAKE_OBJCOPY}" -O binary "$<TARGET_FILE:${target}>" "${binary}"
        COMMAND "${CMAKE_SIZE}" "$<TARGET_FILE:${target}>"
        VERBATIM
    )
endfunction()
