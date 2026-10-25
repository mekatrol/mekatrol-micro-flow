cmake_minimum_required(VERSION 3.25)

if(MICROFLOW_BOARD STREQUAL "raspberry-pi-pico")
    if(NOT DEFINED PICO_SDK_PATH AND DEFINED ENV{PICO_SDK_PATH})
        set(PICO_SDK_PATH "$ENV{PICO_SDK_PATH}")
    endif()

    if(NOT PICO_SDK_PATH)
        set(_microflow_pico_sdk_candidates)
        if(DEFINED ENV{HOME})
            list(APPEND _microflow_pico_sdk_candidates
                "$ENV{HOME}/.pico-sdk/sdk/2.3.1"
                "$ENV{HOME}/sdk/pico-sdk-2.3.1")
        endif()
        if(DEFINED ENV{USERPROFILE})
            list(APPEND _microflow_pico_sdk_candidates
                "$ENV{USERPROFILE}/.pico-sdk/sdk/2.3.1"
                "$ENV{USERPROFILE}/sdk/pico-sdk-2.3.1")
        endif()

        foreach(_microflow_pico_sdk_candidate IN LISTS _microflow_pico_sdk_candidates)
            if(EXISTS "${_microflow_pico_sdk_candidate}/external/pico_sdk_import.cmake")
                set(PICO_SDK_PATH "${_microflow_pico_sdk_candidate}")
                break()
            endif()
        endforeach()
        unset(_microflow_pico_sdk_candidate)
        unset(_microflow_pico_sdk_candidates)
    endif()

    if(NOT PICO_SDK_PATH OR
       NOT EXISTS "${PICO_SDK_PATH}/external/pico_sdk_import.cmake")
        message(FATAL_ERROR
            "Raspberry Pi Pico requires Pico SDK 2.3.1. "
            "Set PICO_SDK_PATH; see docs/PROJECT_SETUP.md.")
    endif()
    set(PICO_BOARD pico CACHE STRING "Pico SDK board definition" FORCE)
    set(PICO_NO_PICOTOOL 1 CACHE BOOL "Do not fetch picotool" FORCE)
    include("${PICO_SDK_PATH}/external/pico_sdk_import.cmake")
endif()

project(
    microflow
    VERSION 0.0.1
    LANGUAGES C
)

if(MICROFLOW_BOARD STREQUAL "raspberry-pi-pico")
    enable_language(CXX ASM)
    pico_sdk_init()
    if(NOT PICO_SDK_VERSION_STRING VERSION_EQUAL "2.3.1")
        message(FATAL_ERROR
            "Raspberry Pi Pico requires Pico SDK 2.3.1; found ${PICO_SDK_VERSION_STRING}")
    endif()
endif()

option(MICROFLOW_BUILD_TESTS "Build host tests" ON)
option(MICROFLOW_ENABLE_SANITIZERS "Enable supported host sanitizers" OFF)
set(MICROFLOW_BOARD "" CACHE STRING "Standalone firmware board")

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(MicroflowSanitizers)
include(MicroflowWarnings)

add_subdirectory(src/lib/core)
add_subdirectory(src/lib/blocks)
add_subdirectory(src/lib/runtime)

add_library(microflow INTERFACE)
add_library(Microflow::microflow ALIAS microflow)
target_link_libraries(
    microflow
    INTERFACE
        Microflow::core
        Microflow::blocks
        Microflow::runtime
)

if(MICROFLOW_BOARD)
    include("${PROJECT_SOURCE_DIR}/cmake/boards/${MICROFLOW_BOARD}.cmake")
elseif(MICROFLOW_BUILD_TESTS)
    include(CTest)
    add_subdirectory(src/lib/host)
    add_subdirectory(tests)
endif()
