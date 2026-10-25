
set(board_dir "${PROJECT_SOURCE_DIR}/src/boards/rpi/raspberry-pi-pico")
set(soc_dir "${PROJECT_SOURCE_DIR}/src/boards/rpi/rp2040")

find_package(Python3 REQUIRED COMPONENTS Interpreter)
find_program(MICROFLOW_SIZE_TOOL arm-none-eabi-size REQUIRED)

add_executable(microflow-raspberry-pi-pico
    "${board_dir}/main.c"
    "${board_dir}/freertos_hooks.c"
    "${board_dir}/firmware.c"
    "${board_dir}/firmware_console.c"
    "${board_dir}/firmware_services.c"
    "${board_dir}/firmware_usb.c"
    "${board_dir}/board.c"
    "${board_dir}/board_gpio.c"
    "${board_dir}/board_gpio_rp2040.c"
    "${board_dir}/usb_descriptors.c"
    "${soc_dir}/rp2040_sdk.c"
    "${soc_dir}/rp2040_usb_cdc_sdk.c"
)

target_compile_features(microflow-raspberry-pi-pico
    PRIVATE
        c_std_11
)

# Firmware-specific include directories.
# FreeRTOSConfig.h is located in ${board_dir}.
target_include_directories(microflow-raspberry-pi-pico
    PRIVATE
        "${board_dir}"
        "${soc_dir}"
        "${PROJECT_SOURCE_DIR}/src/lib/runtime/include"
)

# Link Pico SDK and FreeRTOS libraries.
# FreeRTOS remains private to the firmware executable.
target_link_libraries(microflow-raspberry-pi-pico
    PRIVATE
        Microflow::microflow

        # Pico SDK
        pico_stdlib
        hardware_gpio
        hardware_irq
        hardware_sync
        hardware_timer
        hardware_uart
        hardware_watchdog

        # TinyUSB
        tinyusb_device
        tinyusb_board

        # FreeRTOS RP2040 SMP
        FreeRTOS-Kernel-Static
        pico_async_context_freertos
)

target_compile_definitions(microflow-raspberry-pi-pico
    PRIVATE
        PICO_STDIO_USB=0
        PICO_STDIO_UART=0
)

if(MICROFLOW_WAVESHARE_RP2040_ZERO)
    target_compile_definitions(microflow-raspberry-pi-pico
        PRIVATE
            MICROFLOW_WAVESHARE_RP2040_ZERO=1
    )
endif()

target_link_options(microflow-raspberry-pi-pico
    PRIVATE
        "-Wl,-Map=${CMAKE_CURRENT_BINARY_DIR}/microflow-raspberry-pi-pico.map"
)

target_compile_options(microflow-raspberry-pi-pico
    PRIVATE
        -Wall
        -Wextra
        -Werror
)

# Pico SDK 2.3.1 compatibility:
# Suppress the unused-parameter warning only in this upstream source.
set_source_files_properties(
    "${PICO_SDK_PATH}/src/rp2_common/pico_async_context/async_context_freertos.c"
    PROPERTIES
        COMPILE_OPTIONS "-Wno-unused-parameter"
)

pico_enable_stdio_usb(microflow-raspberry-pi-pico 0)
pico_enable_stdio_uart(microflow-raspberry-pi-pico 0)

# Firmware output paths.
set(firmware_binary
    "${CMAKE_CURRENT_BINARY_DIR}/microflow-raspberry-pi-pico.bin"
)

set(firmware_uf2
    "${CMAKE_CURRENT_BINARY_DIR}/microflow-raspberry-pi-pico.uf2"
)

# Generate firmware images and validate memory usage.
add_custom_command(TARGET microflow-raspberry-pi-pico POST_BUILD
    COMMAND "${CMAKE_OBJCOPY}" -O binary
            "$<TARGET_FILE:microflow-raspberry-pi-pico>"
            "${firmware_binary}"

    COMMAND "${Python3_EXECUTABLE}"
            "${PROJECT_SOURCE_DIR}/tools/bin_to_uf2.py"
            "${firmware_binary}"
            "${firmware_uf2}"

    COMMAND "${MICROFLOW_SIZE_TOOL}"
            "$<TARGET_FILE:microflow-raspberry-pi-pico>"

    COMMAND "${Python3_EXECUTABLE}"
            "${PROJECT_SOURCE_DIR}/tools/check_elf_size.py"
            --size-tool "${MICROFLOW_SIZE_TOOL}"
            --elf "$<TARGET_FILE:microflow-raspberry-pi-pico>"
            --flash-limit 2097152
            --ram-limit 270336

    VERBATIM
)
