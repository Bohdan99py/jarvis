## -------------------------------------------------------
## core/CMakeLists.txt — Ядро J.A.R.V.I.S. (Shared Library)
## -------------------------------------------------------

add_library(JarvisCore SHARED
    # Экспорт
    jarvis_core_export.h

    # Ядро
    jarvis.h
    jarvis.cpp
    command_registry.h
    command_registry.cpp
    session_memory.h
    session_memory.cpp
    theme.h
    virtual_keyboard.h
    virtual_keyboard.cpp

    # Встроенные модули
    claude_api.h
    claude_api.cpp
    embedded_key.h
    action_predictor.h
    action_predictor.cpp

    # Плагинная система
    plugin_interface.h
    plugin_host.h
    plugin_manager.h
    plugin_manager.cpp

    # Автообновление
    updater.h
    updater.cpp
)

target_compile_definitions(JarvisCore PRIVATE JARVIS_CORE_LIBRARY UNICODE _UNICODE)

target_include_directories(JarvisCore PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(JarvisCore PUBLIC
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::Network
)

target_link_libraries(JarvisCore PRIVATE
    ole32
    oleaut32
    shell32
)

if(MSVC)
    target_compile_options(JarvisCore PRIVATE /W4)
    target_compile_options(JarvisCore PRIVATE $<$<CONFIG:Release>:/O2 /GL>)
    target_link_options(JarvisCore PRIVATE $<$<CONFIG:Release>:/LTCG>)
endif()
