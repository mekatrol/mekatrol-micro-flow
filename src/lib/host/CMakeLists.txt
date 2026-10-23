add_library(microflow-host INTERFACE)
add_library(Microflow::host ALIAS microflow-host)

target_link_libraries(microflow-host INTERFACE Microflow::runtime)
