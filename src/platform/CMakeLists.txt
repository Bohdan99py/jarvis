## src/platform — PC control: mouse, keyboard, macros, screen agent, clipboard
## Depends on: common
## Private .cpp deps: engine (jarvis.h, command_registry.h),
##                    network (screen_agent.cpp uses QNetworkAccessManager)

add_library(jarvis_platform OBJECT
    pc_controller.h
    pc_controller.cpp
    pc_command_registry.h
    pc_command_registry.cpp
    macro_recorder.h
    macro_recorder.cpp
    virtual_keyboard.h
    virtual_keyboard.cpp
    screen_agent.h
    screen_agent.cpp
    clipboard_watcher.h
    clipboard_watcher.cpp
    camera_agent.h
    camera_agent.cpp
    security_camera.h
    security_camera.cpp
    face_registry.h
    face_registry.cpp
)

target_include_directories(jarvis_platform PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_include_directories(jarvis_platform PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../engine
)

target_link_libraries(jarvis_platform
    PUBLIC  jarvis_common Qt6::Core Qt6::Gui Qt6::Widgets
    PRIVATE Qt6::Network Qt6::Multimedia Qt6::Concurrent Qt6::Sql user32 ole32 oleaut32 shell32
)
# JARVIS_HAS_OPENCV is set in the root CMakeLists.txt for BOTH system
# OpenCV (find_package) AND the redist/opencv/build/ fallback.
# Do NOT check OpenCV_FOUND here — it's only set by find_package
# and is FALSE when using the bundled redist copy.
if(JARVIS_HAS_OPENCV)
    target_include_directories(jarvis_platform PRIVATE ${OpenCV_INCLUDE_DIRS})
    target_compile_definitions(jarvis_platform PRIVATE JARVIS_HAS_OPENCV)
endif()