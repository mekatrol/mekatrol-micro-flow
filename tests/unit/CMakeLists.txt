add_executable(microflow-version-test version_test.c)
target_link_libraries(microflow-version-test PRIVATE Microflow::microflow)

microflow_set_warnings(microflow-version-test)
microflow_enable_sanitizers(microflow-version-test)

add_test(NAME microflow.version COMMAND microflow-version-test)

add_executable(microflow-scheduler-test scheduler_test.c)
target_link_libraries(microflow-scheduler-test PRIVATE Microflow::runtime)

microflow_set_warnings(microflow-scheduler-test)
microflow_enable_sanitizers(microflow-scheduler-test)

add_test(NAME microflow.scheduler COMMAND microflow-scheduler-test)

add_executable(microflow-supervisor-test
    supervisor_test.c
    ${PROJECT_SOURCE_DIR}/src/boards/rpi/waveshare-rp2040-zero/supervisor.c)
target_include_directories(microflow-supervisor-test PRIVATE
    ${PROJECT_SOURCE_DIR}/src/boards/rpi/waveshare-rp2040-zero)
microflow_set_warnings(microflow-supervisor-test)
microflow_enable_sanitizers(microflow-supervisor-test)
add_test(NAME microflow.supervisor COMMAND microflow-supervisor-test)

if(MICROFLOW_ENABLE_SANITIZERS AND CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    set_tests_properties(
        microflow.version microflow.scheduler microflow.supervisor
        PROPERTIES ENVIRONMENT "ASAN_OPTIONS=detect_leaks=0"
    )
endif()
