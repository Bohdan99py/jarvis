## src/common — Zero-dependency utilities, data types, constants
## No project dependencies. Everything else depends on this.

add_library(jarvis_common OBJECT
    jarvis_core_export.h
    jarvis_paths.h
    jarvis_response.h
    lang.h
    theme.h
    theme_manager.h
    context_snapshot.h
    embedded_key.h
    fileviewer.h
    applauncher.h
    languagedetector.h
    systemcontroller.h
    layout_fixer.h
    layout_fixer.cpp
    mermaid_renderer.h
    mermaid_renderer.cpp
)

target_include_directories(jarvis_common PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(jarvis_common PUBLIC
    Qt6::Core
    Qt6::Gui
    Qt6::Concurrent
)
