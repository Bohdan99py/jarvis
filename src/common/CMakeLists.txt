## src/common — Zero-dependency utilities, data types, constants
## No project dependencies. Everything else depends on this.

add_library(jarvis_common OBJECT
    jarvis_core_export.h
    jarvis_paths.h
    # Журнал правок лежит здесь, а не в intelligence: в него пишут все
    # три слоя — чат (code_actions), советник и реестр инструментов.
    edit_journal.h
    edit_journal.cpp
    jarvis_response.h
    # Фаза («что JARVIS делает прямо сейчас») лежит в common по той же
    # причине, что и журнал правок: её выставляет агент, а читают QML,
    # лента и уведомления — то есть слои по обе стороны от неё.
    jarvis_state.h
    jarvis_state.cpp
    lang.h
    theme.h
    theme_manager.h
    jarvis_theme.h
    jarvis_theme.cpp
    context_snapshot.h
    embedded_key.h
    fileviewer.h
    applauncher.h
    languagedetector.h
    systemcontroller.h
    layout_fixer.h
    layout_fixer.cpp
    dialog_cues.h
    dialog_cues.cpp
    mermaid_renderer.h
    mermaid_renderer.cpp
    kicad_symbol_cache.h
    kicad_symbol_cache.cpp
    kicad_schematic_builder.h
    kicad_schematic_builder.cpp
    notification_manager.h
    notification_manager.cpp
)
# notification_toast.qrc is intentionally NOT listed here — see the
# top-level CMakeLists.txt comment above where it's attached to the
# "jarvis" executable target instead (Visual Studio generator + AUTORCC +
# OBJECT library + $<TARGET_OBJECTS:> does not reliably link).

target_include_directories(jarvis_common PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(jarvis_common PUBLIC
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::Concurrent
    Qt6::Qml
    Qt6::Quick
    Qt6::QuickWidgets
    Qt6::QuickEffects
    Qt6::QuickControls2
    Qt6::QuickShapes
)
