## src/voice — Voice input (Vosk ASR), TTS (SAPI/Piper), passive recording
## Depends on: common
## Private: db (database_manager used in .cpp only)

add_library(jarvis_voice OBJECT
    voice_input.h
    voice_input.cpp
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
