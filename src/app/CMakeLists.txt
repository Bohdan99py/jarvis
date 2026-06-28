## src/app — Application entry point and main window
## Depends on: engine (and transitively all modules)

target_sources(jarvis PRIVATE
    main.cpp
    mainwindow.h
    mainwindow.cpp
    visual_insights_widget.h
    visual_insights_widget.cpp
)

target_include_directories(jarvis PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)
