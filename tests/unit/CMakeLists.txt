add_executable(microflow-version-test version_test.c)
target_link_libraries(microflow-version-test PRIVATE Microflow::microflow)

microflow_set_warnings(microflow-version-test)
microflow_enable_sanitizers(microflow-version-test)

add_test(NAME microflow.version COMMAND microflow-version-test)

if(MICROFLOW_ENABLE_SANITIZERS AND CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    set_tests_properties(
        microflow.version
        PROPERTIES ENVIRONMENT "ASAN_OPTIONS=detect_leaks=0"
    )
endif()
