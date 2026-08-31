## src/voice — Voice input (Vosk ASR), TTS (SAPI/Piper), passive recording
## Depends on: common
## Private: db (database_manager used in .cpp only)

add_library(jarvis_voice OBJECT
    voice_input.h
    voice_input.cpp
    speech_request.h
    speech_phrases.h
    voice_policy.h
    voice_policy.cpp
    speech_aggregator.h
    speech_aggregator.cpp
    voice_provider.h
    piper_provider.h
    piper_provider.cpp
    elevenlabs_provider.h
    elevenlabs_provider.cpp
    speech_cache.h
    speech_cache.cpp
    voice_synthesis_manager.h
    voice_synthesis_manager.cpp
    passive_listener.h
    passive_listener.cpp
    VoskSetupDialog.h
    VoskSetupDialog.cpp
    audio_manager.h
)

target_include_directories(jarvis_voice PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(jarvis_voice
    PUBLIC  jarvis_common Qt6::Core Qt6::Multimedia Qt6::Widgets
    PRIVATE jarvis_db Qt6::Concurrent Qt6::Network
)

# Vosk offline ASR — linked conditionally by master CMakeLists.txt
