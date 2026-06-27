## src/platform — PC control: mouse, keyboard, macros, screen agent, clipboard
## Depends on: common
## Private: engine (forward-declared Jarvis* in clipboard_watcher.cpp)

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
)

target_include_directories(jarvis_platform PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(jarvis_platform
    PUBLIC  jarvis_common Qt6::Core Qt6::Gui Qt6::Widgets
    PRIVATE user32 ole32 oleaut32 shell32
)
