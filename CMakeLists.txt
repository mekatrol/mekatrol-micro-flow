cmake_minimum_required(VERSION 3.25)

project(
    microflow
    VERSION 0.1.0
    LANGUAGES C
)

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
