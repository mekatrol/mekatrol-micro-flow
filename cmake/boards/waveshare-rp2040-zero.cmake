set(board_dir "${PROJECT_SOURCE_DIR}/src/boards/rpi/waveshare-rp2040-zero")
set(soc_dir "${PROJECT_SOURCE_DIR}/src/boards/rpi/rp2040")
find_package(Python3 REQUIRED COMPONENTS Interpreter)
find_program(MICROFLOW_SIZE_TOOL arm-none-eabi-size REQUIRED)

add_executable(
    microflow-waveshare-rp2040-zero
    "${board_dir}/main.c"
    "${board_dir}/board_sdk.c"
    "${board_dir}/board_gpio.c"
    "${board_dir}/supervisor.c"
    "${board_dir}/upload_transport.c"
    "${board_dir}/console.c"
    "${board_dir}/usb_descriptors.c"
    "${soc_dir}/rp2040_sdk.c"
    "${soc_dir}/rp2040_usb_cdc_sdk.c"
)
target_compile_features(microflow-waveshare-rp2040-zero PRIVATE c_std_11)
target_include_directories(
    microflow-waveshare-rp2040-zero
    PRIVATE
        "${board_dir}"
        "${soc_dir}"
        "${PROJECT_SOURCE_DIR}/src/lib/runtime/include"
)
target_link_libraries(
    microflow-waveshare-rp2040-zero
    PRIVATE
        Microflow::microflow
        pico_stdlib
        hardware_gpio
        hardware_irq
        hardware_sync
        hardware_timer
        hardware_uart
        hardware_watchdog
        tinyusb_device
        tinyusb_board
)
target_compile_definitions(
    microflow-waveshare-rp2040-zero PRIVATE
    PICO_STDIO_USB=0
    PICO_STDIO_UART=0
)
target_link_options(
    microflow-waveshare-rp2040-zero PRIVATE
    "-Wl,-Map=${CMAKE_CURRENT_BINARY_DIR}/microflow-waveshare-rp2040-zero.map"
)
# SDK sources are compiled as part of this target and intentionally use some
# extensions rejected by the project's portable-code warning policy.
target_compile_options(microflow-waveshare-rp2040-zero PRIVATE -Wall -Wextra)
pico_enable_stdio_usb(microflow-waveshare-rp2040-zero 0)
pico_enable_stdio_uart(microflow-waveshare-rp2040-zero 0)
set(firmware_binary "${CMAKE_CURRENT_BINARY_DIR}/microflow-waveshare-rp2040-zero.bin")
set(firmware_uf2 "${CMAKE_CURRENT_BINARY_DIR}/microflow-waveshare-rp2040-zero.uf2")
add_custom_command(
    TARGET microflow-waveshare-rp2040-zero POST_BUILD
    COMMAND "${CMAKE_OBJCOPY}" -O binary
            "$<TARGET_FILE:microflow-waveshare-rp2040-zero>" "${firmware_binary}"
    COMMAND "${Python3_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/tools/bin_to_uf2.py"
            "${firmware_binary}" "${firmware_uf2}"
    COMMAND "${MICROFLOW_SIZE_TOOL}" "$<TARGET_FILE:microflow-waveshare-rp2040-zero>"
    COMMAND "${Python3_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/tools/check_elf_size.py"
            --size-tool "${MICROFLOW_SIZE_TOOL}"
            --elf "$<TARGET_FILE:microflow-waveshare-rp2040-zero>"
            --flash-limit 2097152
            --ram-limit 270336
    VERBATIM
)
