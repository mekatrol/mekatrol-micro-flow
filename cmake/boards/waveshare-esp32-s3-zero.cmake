include(MicroflowEspFirmware)

microflow_add_esp_firmware(
    microflow-waveshare-esp32-s3-zero
    esp32s3
    4MB
    esp32-s3
    xtensa/startup_esp32s3.S
    waveshare-esp32-s3-zero
    soc/esp32-s3/esp32-s3.ld
)
target_compile_options(microflow-waveshare-esp32-s3-zero PRIVATE -mabi=call0 -mtext-section-literals)
target_link_options(microflow-waveshare-esp32-s3-zero PRIVATE -mabi=call0)
target_compile_options(
    microflow-core
    PRIVATE
        -mabi=call0
        -mtext-section-literals
        -O2
        -ffreestanding
        -ffunction-sections
        -fdata-sections
)
