## src/media — Media preview, routing, analyzer, attachments
## Depends on: common

add_library(jarvis_media OBJECT
    media_preview_widget.h
    media_preview_widget.cpp
    media_routing_manager.h
    media_routing_manager.cpp
    media_analyzer_module.h
    media_analyzer_module.cpp
    attachments_manager.h
    attachments_manager.cpp
)

target_include_directories(jarvis_media PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(jarvis_media
    PUBLIC  jarvis_common Qt6::Core Qt6::Widgets Qt6::Multimedia Qt6::MultimediaWidgets
)
