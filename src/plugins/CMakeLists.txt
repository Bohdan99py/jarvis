## src/plugins — Plugin interface, host, and manager
## Depends on: common

add_library(jarvis_plugins OBJECT
    plugin_interface.h
    plugin_host.h
    plugin_manager.h
    plugin_manager.cpp
)

target_include_directories(jarvis_plugins PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(jarvis_plugins
    PUBLIC  jarvis_common Qt6::Core
)
