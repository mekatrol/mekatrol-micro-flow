add_library(microflow-runtime INTERFACE)
add_library(Microflow::runtime ALIAS microflow-runtime)

target_compile_features(microflow-runtime INTERFACE c_std_11)
target_link_libraries(microflow-runtime INTERFACE Microflow::core)
