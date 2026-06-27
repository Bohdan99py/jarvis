## src/modules — Lifestyle & domain modules (gourmet, reminders)
## Depends on: common

add_library(jarvis_modules OBJECT
    gourmet_module.h
    gourmet_module.cpp
    proactive_reminder_manager.h
    proactive_reminder_manager.cpp
)

target_include_directories(jarvis_modules PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(jarvis_modules
    PUBLIC  jarvis_common Qt6::Core
)
