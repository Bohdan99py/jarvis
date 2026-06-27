## src/intelligence — Brain, learning, prediction, indexing, search, OCR
## Depends on: common (context_snapshot.h in brain.h, user_profile.h)
## Private: db, llm (used in .cpp files only)

add_library(jarvis_intelligence OBJECT
    brain.h
    brain.cpp
    action_predictor.h
    action_predictor.cpp
    semantic_mapper.h
    semantic_mapper.cpp
    search_router.h
    search_router.cpp
    content_search.h
    content_search.cpp
    ocr_extractor.h
    ocr_extractor.cpp
    user_profile.h
    user_profile.cpp
    user_profile_manager.h
    activity_tracker.h
    activity_tracker.cpp
    background_learner.h
    background_learner.cpp
    local_trainer.h
    local_trainer.cpp
    training_processing_worker.h
    training_processing_worker.cpp
    screenshot_learner.h
    screenshot_learner.cpp
    learned_commands.h
    learned_commands.cpp
    curiosity_engine.h
    curiosity_engine.cpp
    project_indexer.h
    project_indexer.cpp
    code_actions.h
    code_actions.cpp
)

target_include_directories(jarvis_intelligence PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(jarvis_intelligence
    PUBLIC  jarvis_common Qt6::Core Qt6::Concurrent
    PRIVATE jarvis_db jarvis_llm
)
