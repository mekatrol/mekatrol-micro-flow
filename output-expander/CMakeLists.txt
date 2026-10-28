cmake_minimum_required(VERSION 3.25)
project(output_expander C ASM)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

include(cmake/warnings.cmake)

if(CMAKE_CROSSCOMPILING)
  add_executable(output-expander
    src/main.c
    src/hal.c
    src/rs485.c
    src/irq_rx.c
    src/usart.c
    src/usart_irq.c
    src/startup.S)
  target_include_directories(output-expander PRIVATE src)
  output_expander_warnings(output-expander)
  target_compile_options(output-expander PRIVATE
    -ffreestanding -fno-builtin -ffunction-sections -fdata-sections
    -fstack-usage -msmall-data-limit=0)
  target_link_options(output-expander PRIVATE
    -nostdlib -nostartfiles
    -T${CMAKE_CURRENT_SOURCE_DIR}/src/ch32v003.ld
    -Wl,--gc-sections
    -Wl,-Map,$<TARGET_FILE_DIR:output-expander>/output-expander.map
    -Wl,--print-memory-usage)
  set_target_properties(output-expander PROPERTIES SUFFIX ".elf")

  add_custom_command(TARGET output-expander POST_BUILD
    COMMAND ${CMAKE_OBJCOPY} -O binary $<TARGET_FILE:output-expander>
            $<TARGET_FILE_DIR:output-expander>/output-expander.bin
    COMMAND ${CMAKE_OBJDUMP} -d -S $<TARGET_FILE:output-expander>
            > $<TARGET_FILE_DIR:output-expander>/output-expander.dis
    COMMAND ${CMAKE_SIZE} $<TARGET_FILE:output-expander>
    COMMAND ${CMAKE_COMMAND}
            -DELF=$<TARGET_FILE:output-expander>
            -DSIZE_TOOL=${CMAKE_SIZE}
            -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/check_size.cmake
    VERBATIM)
else()
  include(CTest)
  include(cmake/sanitizers.cmake)
  add_library(output_expander_core
    src/smoke.c
    src/irq_rx.c
    src/crc16.c
    src/cobs.c)
  target_include_directories(output_expander_core PUBLIC src)
  output_expander_warnings(output_expander_core)
  output_expander_sanitizers(output_expander_core)

  add_executable(output_expander_smoke_test tests/unit/test_smoke.c)
  target_link_libraries(output_expander_smoke_test PRIVATE output_expander_core)
  output_expander_warnings(output_expander_smoke_test)
  output_expander_sanitizers(output_expander_smoke_test)
  add_test(NAME portable_smoke COMMAND output_expander_smoke_test)
  set_tests_properties(portable_smoke PROPERTIES
    ENVIRONMENT "ASAN_OPTIONS=detect_leaks=0")

  # Verify interrupt-to-foreground queue behavior on
  # the host without requiring CH32V003 hardware.
  add_executable(output_expander_irq_rx_test
    tests/unit/test_irq_rx.c)

  target_link_libraries(output_expander_irq_rx_test
    PRIVATE output_expander_core)

  output_expander_warnings(output_expander_irq_rx_test)
  output_expander_sanitizers(output_expander_irq_rx_test)

  add_test(
    NAME irq_rx
    COMMAND output_expander_irq_rx_test)

  set_tests_properties(irq_rx PROPERTIES
    ENVIRONMENT "ASAN_OPTIONS=detect_leaks=0")
  
  # Test the portable CRC implementation on the host.
  #
  # The same algorithm will later be compiled into
  # CH32V003 firmware, without any hardware dependencies.
  add_executable(output_expander_crc16_test
    tests/unit/test_crc16.c)

  target_link_libraries(output_expander_crc16_test
    PRIVATE output_expander_core)

  output_expander_warnings(output_expander_crc16_test)
  output_expander_sanitizers(output_expander_crc16_test)

  add_test(
    NAME crc16
    COMMAND output_expander_crc16_test)

  set_tests_properties(crc16 PROPERTIES
    ENVIRONMENT "ASAN_OPTIONS=detect_leaks=0")

  # Verify COBS framing independently of the MCU.
  #
  # Both the firmware and host use the same
  # protocol-defined maximum packet capacities.
  add_executable(output_expander_cobs_test
    tests/unit/test_cobs.c)

  target_link_libraries(output_expander_cobs_test
    PRIVATE output_expander_core)

  output_expander_warnings(output_expander_cobs_test)
  output_expander_sanitizers(output_expander_cobs_test)

  add_test(
    NAME cobs
    COMMAND output_expander_cobs_test)

  set_tests_properties(cobs PROPERTIES
    ENVIRONMENT "ASAN_OPTIONS=detect_leaks=0")
endif()
