## src/llm — LLM API clients (Claude, Ollama)
## Depends on: common
## Private: db (session_memory used in .cpp only)

add_library(jarvis_llm OBJECT
    claude_api.h
    claude_api.cpp
    ollama_api.h
)

target_include_directories(jarvis_llm PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(jarvis_llm
    PUBLIC  jarvis_common Qt6::Core Qt6::Network
    PRIVATE jarvis_db
)
