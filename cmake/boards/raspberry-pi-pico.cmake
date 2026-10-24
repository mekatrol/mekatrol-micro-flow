set(board_dir "${PROJECT_SOURCE_DIR}/src/boards/rpi/raspberry-pi-pico")
set(soc_dir "${PROJECT_SOURCE_DIR}/src/boards/rpi/rp2040")
find_package(Python3 REQUIRED COMPONENTS Interpreter)
find_program(MICROFLOW_SIZE_TOOL arm-none-eabi-size REQUIRED)

add_executable(microflow-raspberry-pi-pico
    "${board_dir}/main.c"
    "${board_dir}/board.c"
    "${board_dir}/board_gpio.c"
    "${board_dir}/supervisor.c"
    "${board_dir}/upload_transport.c"
    "${board_dir}/console.c"
    "${board_dir}/usb_descriptors.c"
    "${soc_dir}/rp2040_sdk.c"
    "${soc_dir}/rp2040_usb_cdc_sdk.c")
target_compile_features(microflow-raspberry-pi-pico PRIVATE c_std_11)
target_include_directories(microflow-raspberry-pi-pico PRIVATE
    "${board_dir}" "${soc_dir}"
    "${PROJECT_SOURCE_DIR}/src/lib/runtime/include")
target_link_libraries(microflow-raspberry-pi-pico PRIVATE
    Microflow::microflow pico_stdlib hardware_gpio hardware_irq hardware_sync
    hardware_timer hardware_uart hardware_watchdog tinyusb_device tinyusb_board)
target_compile_definitions(microflow-raspberry-pi-pico PRIVATE
    PICO_STDIO_USB=0 PICO_STDIO_UART=0)
target_link_options(microflow-raspberry-pi-pico PRIVATE
    "-Wl,-Map=${CMAKE_CURRENT_BINARY_DIR}/microflow-raspberry-pi-pico.map")
target_compile_options(microflow-raspberry-pi-pico PRIVATE -Wall -Wextra)
pico_enable_stdio_usb(microflow-raspberry-pi-pico 0)
pico_enable_stdio_uart(microflow-raspberry-pi-pico 0)

set(firmware_binary "${CMAKE_CURRENT_BINARY_DIR}/microflow-raspberry-pi-pico.bin")
set(firmware_uf2 "${CMAKE_CURRENT_BINARY_DIR}/microflow-raspberry-pi-pico.uf2")
add_custom_command(TARGET microflow-raspberry-pi-pico POST_BUILD
    COMMAND "${CMAKE_OBJCOPY}" -O binary
            "$<TARGET_FILE:microflow-raspberry-pi-pico>" "${firmware_binary}"
    COMMAND "${Python3_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/tools/bin_to_uf2.py"
            "${firmware_binary}" "${firmware_uf2}"
    COMMAND "${MICROFLOW_SIZE_TOOL}" "$<TARGET_FILE:microflow-raspberry-pi-pico>"
    COMMAND "${Python3_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/tools/check_elf_size.py"
            --size-tool "${MICROFLOW_SIZE_TOOL}"
            --elf "$<TARGET_FILE:microflow-raspberry-pi-pico>"
            --flash-limit 2097152 --ram-limit 270336
    VERBATIM)
