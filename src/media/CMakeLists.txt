## src/media — Media preview, routing, analyzer, attachments
## Depends on: common
## Private .cpp deps: network (j2j_telegram_gateway.h in media_routing_manager.cpp)

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

target_include_directories(jarvis_media PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../network
    ${CMAKE_CURRENT_SOURCE_DIR}/../engine
)

target_link_libraries(jarvis_media
    PUBLIC  jarvis_common Qt6::Core Qt6::Widgets Qt6::Multimedia Qt6::MultimediaWidgets
)
