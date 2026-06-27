## src/modules — Lifestyle & domain modules (gourmet, reminders)
## Depends on: common
## Private .cpp deps: engine (jarvis.h), network (j2j_telegram_gateway.h),
##                    voice (voice_synthesis_manager.h), db (database_manager.h)

add_library(jarvis_modules OBJECT
    gourmet_module.h
    gourmet_module.cpp
    proactive_reminder_manager.h
    proactive_reminder_manager.cpp
)

target_include_directories(jarvis_modules PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_include_directories(jarvis_modules PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../engine
    ${CMAKE_CURRENT_SOURCE_DIR}/../network
    ${CMAKE_CURRENT_SOURCE_DIR}/../voice
)

target_link_libraries(jarvis_modules
    PUBLIC  jarvis_common Qt6::Core
    PRIVATE jarvis_db Qt6::Sql
)
