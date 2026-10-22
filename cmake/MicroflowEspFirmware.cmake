include(MicroflowFirmware)

function(microflow_add_esp_firmware target chip flash_size soc architecture board linker_script)
    find_program(ESPTOOL_EXECUTABLE NAMES esptool esptool.py REQUIRED)

    microflow_add_firmware(
        ${target}
        "${PROJECT_SOURCE_DIR}/examples/status_blink.c"
        "${PROJECT_SOURCE_DIR}/ports/${architecture}"
        "${PROJECT_SOURCE_DIR}/soc/esp32-common/esp32.c"
        "${PROJECT_SOURCE_DIR}/boards/${board}/board.c"
    )
    target_include_directories(
        ${target}
        PRIVATE
            "${PROJECT_SOURCE_DIR}/soc/esp32-common"
            "${PROJECT_SOURCE_DIR}/soc/${soc}"
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
            -T${PROJECT_SOURCE_DIR}/${linker_script}
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
