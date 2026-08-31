## src/db — Database, session memory, LLM response cache
## Depends on: common

add_library(jarvis_db OBJECT
    database_manager.h
    database_manager.cpp
    session_memory.h
    session_memory.cpp
    llm_cache_manager.h
    llm_cache_manager.cpp
    memory_consolidation.h
    memory_consolidation.cpp
    memory_manager.h
    memory_manager.cpp
    synapse_graph.h
    synapse_graph.cpp
    sentence_composer.h
    sentence_composer.cpp
    artifact_registry.h
    artifact_registry.cpp
)

target_include_directories(jarvis_db PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(jarvis_db
    PUBLIC  jarvis_common Qt6::Core Qt6::Sql
)
