set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR riscv32)
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

find_program(CMAKE_C_COMPILER riscv-none-elf-gcc REQUIRED)
find_program(CMAKE_ASM_COMPILER riscv-none-elf-gcc REQUIRED)
find_program(CMAKE_OBJCOPY riscv-none-elf-objcopy REQUIRED)
find_program(CMAKE_OBJDUMP riscv-none-elf-objdump REQUIRED)
find_program(CMAKE_SIZE riscv-none-elf-size REQUIRED)

execute_process(COMMAND ${CMAKE_C_COMPILER} --version
  OUTPUT_VARIABLE _compiler_version ERROR_VARIABLE _compiler_error
  RESULT_VARIABLE _compiler_result)
if(NOT _compiler_result EQUAL 0)
  message(FATAL_ERROR "Cannot execute pinned cross compiler: ${_compiler_error}")
endif()
if(NOT _compiler_version MATCHES "14\\.3\\.0" OR NOT _compiler_version MATCHES "xPack")
  message(FATAL_ERROR
    "CH32V003 builds require xPack GNU RISC-V Embedded GCC 14.3.0-1. Found:\n${_compiler_version}")
endif()

set(_arch_flags "-march=rv32ec_zicsr -mabi=ilp32e")
set(CMAKE_C_FLAGS_INIT "${_arch_flags}")
set(CMAKE_ASM_FLAGS_INIT "${_arch_flags}")
set(CMAKE_EXE_LINKER_FLAGS_INIT "${_arch_flags}")
