include(MicroflowFirmware)
find_package(Python3 REQUIRED COMPONENTS Interpreter)

set(boot2_object "${CMAKE_CURRENT_BINARY_DIR}/boot2.o")
set(boot2_elf "${CMAKE_CURRENT_BINARY_DIR}/boot2.elf")
set(boot2_binary "${CMAKE_CURRENT_BINARY_DIR}/boot2.bin")
set(boot2_padded "${CMAKE_CURRENT_BINARY_DIR}/boot2_padded.S")
set(board_dir "${PROJECT_SOURCE_DIR}/src/boards/rpi/waveshare-rp2040-zero")

add_custom_command(
    OUTPUT "${boot2_padded}"
    COMMAND "${CMAKE_ASM_COMPILER}" -mcpu=cortex-m0plus -mthumb -c
            "${board_dir}/boot2_generic_03h.S" -o "${boot2_object}"
    COMMAND "${CMAKE_C_COMPILER}" -nostdlib -Wl,-Ttext=0 -Wl,-e,_stage2_boot -Wl,--build-id=none
            "${boot2_object}" -o "${boot2_elf}"
    COMMAND "${CMAKE_OBJCOPY}" -O binary "${boot2_elf}" "${boot2_binary}"
    COMMAND "${Python3_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/tools/pad_boot2.py"
            "${boot2_binary}" "${boot2_padded}"
    DEPENDS
        "${board_dir}/boot2_generic_03h.S"
        "${PROJECT_SOURCE_DIR}/tools/pad_boot2.py"
    VERBATIM
)

microflow_add_firmware(
    microflow-waveshare-rp2040-zero
    "${board_dir}/main.c"
    "${board_dir}/startup.S"
    "${board_dir}/rp2040.c"
    "${board_dir}/board.c"
    "${boot2_padded}"
)
target_include_directories(
    microflow-waveshare-rp2040-zero
    PRIVATE "${board_dir}"
)
target_compile_options(
    microflow-waveshare-rp2040-zero
    PRIVATE
        -mcpu=cortex-m0plus
        -mthumb
        -O2
        -ffreestanding
        -ffunction-sections
        -fdata-sections
)
target_compile_options(
    microflow-core
    PRIVATE
        -mcpu=cortex-m0plus
        -mthumb
        -O2
        -ffreestanding
        -ffunction-sections
        -fdata-sections
)
target_link_options(
    microflow-waveshare-rp2040-zero
    PRIVATE
        -mcpu=cortex-m0plus
        -mthumb
        -nostdlib
        -Wl,--gc-sections
        -Wl,--build-id=none
        -Wl,-Map=${CMAKE_CURRENT_BINARY_DIR}/microflow-waveshare-rp2040-zero.map
        -T${board_dir}/rp2040.ld
)

set(rp2040_binary "${CMAKE_CURRENT_BINARY_DIR}/microflow-waveshare-rp2040-zero.bin")
set(rp2040_uf2 "${CMAKE_CURRENT_BINARY_DIR}/microflow-waveshare-rp2040-zero.uf2")
add_custom_command(
    TARGET microflow-waveshare-rp2040-zero
    POST_BUILD
    COMMAND "${CMAKE_OBJCOPY}" -O binary "$<TARGET_FILE:microflow-waveshare-rp2040-zero>" "${rp2040_binary}"
    COMMAND "${Python3_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/tools/bin_to_uf2.py"
            "${rp2040_binary}" "${rp2040_uf2}"
    COMMAND "${CMAKE_SIZE}" "$<TARGET_FILE:microflow-waveshare-rp2040-zero>"
    VERBATIM
)
