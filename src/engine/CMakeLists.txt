## src/engine — Core orchestrator (Jarvis), command registry, updater, translation
## Depends on: common, db, intelligence (jarvis.h includes command_registry.h,
##             user_profile.h, activity_tracker.h; task_manager_dialog.h includes
##             database_manager.h; system_manifest.h includes database_manager.h)
## Private: llm, voice, platform, network, media, modules, plugins (used in jarvis.cpp)

add_library(jarvis_engine OBJECT
    jarvis.h
    jarvis.cpp
    command_registry.h
    command_registry.cpp
    skill_manager.h
    skill_manager.cpp
    mode_manager.h
    mode_manager.cpp
    profile_tools.h
    profile_tools.cpp
    health_center.h
    health_center.cpp
    plugin_bridge.h
    plugin_bridge.cpp
    system_manifest.h
    task_manager_dialog.h
    task_manager_dialog.cpp
    task_board_model.h
    task_board_model.cpp
    training_center_dialog.h
    training_center_dialog.cpp
    organize_plan_dialog.h
    organize_plan_dialog.cpp
    chat_history_dialog.h
    chat_history_dialog.cpp
    translation_engine.h
    translation_engine.cpp
    auto_updater.h
    auto_updater.cpp
    bug_reporter.h
    bug_reporter.cpp
    self_update_reflector.h
    self_update_reflector.cpp
    vision_center_dialog.h
    vision_center_dialog.cpp
    camera_view_dialog.h
    camera_view_dialog.cpp
    artifacts_dialog.h
    artifacts_dialog.cpp
)
# task_board.qrc / training_center.qrc are intentionally NOT listed here —
# see the top-level CMakeLists.txt comment where they're attached to the
# "jarvis" executable target instead (Visual Studio generator + AUTORCC +
# OBJECT library + $<TARGET_OBJECTS:> does not reliably link).

target_include_directories(jarvis_engine PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(jarvis_engine
    PUBLIC  jarvis_common jarvis_db jarvis_intelligence
            Qt6::Core Qt6::Network Qt6::Widgets Qt6::Sql
            Qt6::Quick Qt6::QuickWidgets Qt6::Qml
    PRIVATE jarvis_llm jarvis_voice jarvis_platform jarvis_agent jarvis_network
            jarvis_media jarvis_modules jarvis_plugins
            advapi32 psapi PowrProf
)
