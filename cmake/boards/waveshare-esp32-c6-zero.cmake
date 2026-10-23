include(MicroflowEspFirmware)

microflow_add_esp_firmware(
    microflow-waveshare-esp32-c6-zero
    esp32c6
    8MB
    esp32-c6
    waveshare-esp32-c6-zero
    esp32-c6.ld
)
target_compile_options(microflow-waveshare-esp32-c6-zero PRIVATE -march=rv32imac_zicsr_zifencei -mabi=ilp32)
target_link_options(microflow-waveshare-esp32-c6-zero PRIVATE -march=rv32imac_zicsr_zifencei -mabi=ilp32)
target_compile_options(
    microflow-core
    PRIVATE
        -march=rv32imac_zicsr_zifencei
        -mabi=ilp32
        -O2
        -ffreestanding
        -ffunction-sections
        -fdata-sections
)
