add_library(microflow-blocks INTERFACE)
add_library(Microflow::blocks ALIAS microflow-blocks)

target_compile_features(microflow-blocks INTERFACE c_std_11)
target_link_libraries(microflow-blocks INTERFACE Microflow::core)
