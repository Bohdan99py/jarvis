## src/agent — Слой действий: инструменты, разрешения, агентный цикл
## Depends on: common, llm (ClaudeApi), platform (PcController)
##
## Это мост между "модель что-то ответила" и "на машине что-то
## произошло". Всё исполнение делегируется platform/pc_controller.

add_library(jarvis_agent OBJECT
    tool_registry.h
    tool_registry.cpp
    permission_gate.h
    permission_gate.cpp
    agent_loop.h
    agent_loop.cpp
    context_tracker.h
    context_tracker.cpp
    context_advisor.h
    context_advisor.cpp
    workflow_manager.h
    workflow_manager.cpp
    action_log.h
    action_log.cpp
    git_tools.h
    git_tools.cpp
    journal_tools.h
    journal_tools.cpp
    clipboard_tools.h
    clipboard_tools.cpp
    project_registry.h
    project_registry.cpp
    trigger_engine.h
    trigger_engine.cpp
    global_search.h
    global_search.cpp
    event_feed.h
    event_feed.cpp
    system_watcher.h
    system_watcher.cpp
    device_hub.h
    device_hub.cpp
    system_tools.h
    system_tools.cpp
)

target_include_directories(jarvis_agent PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(jarvis_agent
    PUBLIC  jarvis_common jarvis_llm jarvis_platform
            Qt6::Core Qt6::Gui Qt6::Network
    PRIVATE psapi user32
)
