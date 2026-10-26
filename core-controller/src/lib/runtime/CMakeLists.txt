add_library(microflow-runtime STATIC
    console.c
    flow_store.c
    upload_transport.c
    byte_ring.c)
add_library(Microflow::runtime ALIAS microflow-runtime)

target_compile_features(microflow-runtime PUBLIC c_std_11)
target_include_directories(
    microflow-runtime
    PUBLIC
        $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/src/lib/include>
        $<INSTALL_INTERFACE:include>
)
target_link_libraries(microflow-runtime PUBLIC Microflow::core)

microflow_set_warnings(microflow-runtime)
microflow_enable_sanitizers(microflow-runtime)
