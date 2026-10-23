include(MicroflowFirmware)

function(microflow_add_esp_firmware target chip flash_size board linker_script)
    find_program(ESPTOOL_EXECUTABLE NAMES esptool esptool.py REQUIRED)

    set(board_dir "${PROJECT_SOURCE_DIR}/src/boards/esp32/${board}")

    microflow_add_firmware(
        ${target}
        "${board_dir}/main.c"
        "${board_dir}/startup.S"
        "${PROJECT_SOURCE_DIR}/src/boards/esp32/common/esp32.c"
        "${board_dir}/board.c"
    )
    target_include_directories(
        ${target}
        PRIVATE
            "${PROJECT_SOURCE_DIR}/src/boards/esp32/common"
            "${board_dir}"
    )
    target_compile_options(
        ${target}
        PRIVATE
            -O2
            -ffreestanding
            -ffunction-sections
            -fdata-sections
    )
    target_link_options(
        ${target}
        PRIVATE
            -nostdlib
            -Wl,--gc-sections
            -Wl,--build-id=none
            -Wl,-Map=${CMAKE_CURRENT_BINARY_DIR}/${target}.map
            -T${board_dir}/${linker_script}
    )

    set(image "${CMAKE_CURRENT_BINARY_DIR}/${target}.bin")
    add_custom_command(
        TARGET ${target}
        POST_BUILD
        COMMAND "${ESPTOOL_EXECUTABLE}" --chip ${chip} elf2image
                --flash-mode dio --flash-freq 80m --flash-size ${flash_size}
                --output "${image}" "$<TARGET_FILE:${target}>"
        COMMAND "${CMAKE_SIZE}" "$<TARGET_FILE:${target}>"
        VERBATIM
    )
endfunction()
