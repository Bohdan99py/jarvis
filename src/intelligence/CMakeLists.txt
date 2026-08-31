## src/intelligence — Brain, learning, prediction, indexing, search, OCR
## Depends on: common (context_snapshot.h in brain.h, user_profile.h)
## Private: db, llm (used in .cpp files only)
##          network (j2j_telegram_gateway.h in curiosity_engine.cpp)
##          Qt6::Widgets (QApplication::clipboard() in brain.cpp)
##          Qt6::Sql (QSqlQuery in curiosity_engine.cpp)

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
    social_presence.h
    social_presence.cpp
    reflection_engine.h
    reflection_engine.cpp
    personality_engine.h
    personality_engine.cpp
    pdf_distiller.h
    pdf_distiller.cpp
    self_journal.h
    self_journal.cpp
    user_profile_extended.h
    user_profile_extended.cpp
    project_indexer.h
    project_indexer.cpp
    project_profile.h
    project_profile.cpp
    dev_advisor.h
    dev_advisor.cpp
    code_actions.h
    code_actions.cpp
    file_organizer.h
    file_organizer.cpp
    synonym_learner.h
    synonym_learner.cpp
    case_distiller.h
    case_distiller.cpp
)

target_include_directories(jarvis_intelligence PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_include_directories(jarvis_intelligence PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../network
    ${CMAKE_CURRENT_SOURCE_DIR}/../platform
)

target_link_libraries(jarvis_intelligence
    PUBLIC  jarvis_common Qt6::Core Qt6::Concurrent
    PRIVATE jarvis_db jarvis_llm Qt6::Widgets Qt6::Gui Qt6::Sql
)

# cv::ml (ActionPredictor's local experience classifier) — same OpenCV
# distribution already used by src/platform, see that CMakeLists.txt for
# why OpenCV_FOUND must not be checked here.
if(JARVIS_HAS_OPENCV)
    target_include_directories(jarvis_intelligence PRIVATE ${OpenCV_INCLUDE_DIRS})
    target_compile_definitions(jarvis_intelligence PRIVATE JARVIS_HAS_OPENCV)
endif()
