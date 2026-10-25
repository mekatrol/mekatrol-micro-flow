add_executable(microflow-version-test version_test.c)
target_link_libraries(microflow-version-test PRIVATE Microflow::microflow)

microflow_set_warnings(microflow-version-test)
microflow_enable_sanitizers(microflow-version-test)

add_test(NAME microflow.version COMMAND microflow-version-test)

add_executable(microflow-flow-test flow_test.c)
target_link_libraries(microflow-flow-test PRIVATE Microflow::runtime)
microflow_set_warnings(microflow-flow-test)
microflow_enable_sanitizers(microflow-flow-test)
add_test(NAME microflow.flow COMMAND microflow-flow-test)

add_executable(microflow-scheduler-test scheduler_test.c)
target_link_libraries(microflow-scheduler-test PRIVATE Microflow::runtime)

microflow_set_warnings(microflow-scheduler-test)
microflow_enable_sanitizers(microflow-scheduler-test)

add_test(NAME microflow.scheduler COMMAND microflow-scheduler-test)

set(pico_board_dir
    ${PROJECT_SOURCE_DIR}/src/boards/rpi/raspberry-pi-pico)

add_executable(microflow-supervisor-test supervisor_test.c)
target_link_libraries(microflow-supervisor-test PRIVATE Microflow::runtime)
microflow_set_warnings(microflow-supervisor-test)
microflow_enable_sanitizers(microflow-supervisor-test)
add_test(NAME microflow.supervisor COMMAND microflow-supervisor-test)

add_executable(microflow-upload-transport-test upload_transport_test.c)
target_link_libraries(microflow-upload-transport-test PRIVATE Microflow::runtime)
microflow_set_warnings(microflow-upload-transport-test)
microflow_enable_sanitizers(microflow-upload-transport-test)
add_test(NAME microflow.upload-transport COMMAND microflow-upload-transport-test)

add_executable(microflow-console-test console_test.c)
target_link_libraries(microflow-console-test PRIVATE Microflow::runtime)
microflow_set_warnings(microflow-console-test)
microflow_enable_sanitizers(microflow-console-test)
add_test(NAME microflow.console COMMAND microflow-console-test)

add_executable(microflow-board-gpio-test
    board_gpio_test.c ${pico_board_dir}/board_gpio.c)
target_include_directories(microflow-board-gpio-test PRIVATE ${pico_board_dir})
microflow_set_warnings(microflow-board-gpio-test)
microflow_enable_sanitizers(microflow-board-gpio-test)
add_test(NAME microflow.board-gpio COMMAND microflow-board-gpio-test)

if(MICROFLOW_ENABLE_SANITIZERS AND CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    set_tests_properties(
        microflow.version microflow.flow microflow.scheduler microflow.supervisor
        microflow.upload-transport microflow.console microflow.board-gpio
        PROPERTIES ENVIRONMENT "ASAN_OPTIONS=detect_leaks=0"
    )
endif()
