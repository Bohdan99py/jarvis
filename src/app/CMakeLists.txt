## src/app — Application entry point and main window
## Depends on: engine (and transitively all modules)

target_sources(jarvis PRIVATE
    main.cpp
    mainwindow.h
    mainwindow.cpp
    command_palette.h
    command_palette.cpp
    tray_presence.h
    tray_presence.cpp
    push_to_talk.h
    push_to_talk.cpp
    system_monitor_dialog.h
    system_monitor_dialog.cpp
    notifications_dialog.h
    notifications_dialog.cpp
    device_hub_dialog.h
    device_hub_dialog.cpp
    dashboard_dialog.h
    dashboard_dialog.cpp
    action_registry.h
    action_registry.cpp
    sparkline.h
    sparkline.cpp
    chat_model.h
    chat_model.cpp
    chat_controller.h
    chat_controller.cpp
    notice_controller.h
    notice_controller.cpp
    attachment_model.h
    attachment_model.cpp
    welcome_controller.h
    welcome_controller.cpp
    visual_insights_controller.h
    visual_insights_controller.cpp
    dependency_manager_dialog.h
    dependency_manager_dialog.cpp
    profile_setup_dialog.h
    profile_setup_dialog.cpp
    skills_dialog.h
    skills_dialog.cpp
    modes_dialog.h
    modes_dialog.cpp
    user_center_dialog.h
    user_center_dialog.cpp
    user_center.qrc
    modes.qrc
)

target_include_directories(jarvis PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)
