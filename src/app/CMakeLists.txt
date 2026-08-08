## src/app — Application entry point and main window
## Depends on: engine (and transitively all modules)

target_sources(jarvis PRIVATE
    main.cpp
    mainwindow.h
    mainwindow.cpp
    visual_insights_widget.h
    visual_insights_widget.cpp
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
