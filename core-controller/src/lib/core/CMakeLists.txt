add_library(microflow-core STATIC flow.c version.c)
add_library(Microflow::core ALIAS microflow-core)

target_compile_features(microflow-core PUBLIC c_std_11)
target_include_directories(
    microflow-core
    PUBLIC
        $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/src/lib/include>
        $<INSTALL_INTERFACE:include>
)

microflow_set_warnings(microflow-core)
microflow_enable_sanitizers(microflow-core)
