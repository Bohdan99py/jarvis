## ============================================================
## CMakeLists.txt — J.A.R.V.I.S. Modular Build System
##
## Architecture:
##   src/common/       — Zero-dep utilities, data types, constants
##   src/db/           — SQLite persistence, session memory, LLM cache
##   src/llm/          — LLM API clients (Claude, Gemini, Ollama)
##   src/voice/        — Voice I/O (Vosk ASR, SAPI/Piper TTS)
##   src/platform/     — PC control (mouse, keyboard, macros, screen)
##   src/media/        — Media preview, routing, attachments
##   src/intelligence/  — Brain, learning, prediction, indexing, OCR
##   src/network/      — Telegram gateway, mesh connector, mobile sync
##   src/modules/      — Domain modules (gourmet, reminders)
##   src/plugins/      — Plugin system (DLL interface)
##   src/engine/       — Core orchestrator, command registry, updater
##   src/app/          — Executable entry point + MainWindow
##
## Each module is an OBJECT library with explicit dependencies.
## All objects combine into JarvisCore (STATIC), linked by jarvis.exe.
## ============================================================

cmake_minimum_required(VERSION 3.16)
project(jarvis VERSION 3.6.3 LANGUAGES CXX)

# --- C++17, no extensions ---
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# --- Qt MOC/RCC/UIC ---
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

# --- All artifacts → bin/ ---
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

# ============================================================
# Qt 6 discovery
# ============================================================

if(NOT DEFINED CMAKE_PREFIX_PATH OR CMAKE_PREFIX_PATH STREQUAL "")
    if(DEFINED ENV{Qt6_DIR} AND EXISTS "$ENV{Qt6_DIR}")
        list(APPEND CMAKE_PREFIX_PATH "$ENV{Qt6_DIR}")
        message(STATUS "Qt: using Qt6_DIR env -> $ENV{Qt6_DIR}")
    elseif(DEFINED ENV{QT_DIR} AND EXISTS "$ENV{QT_DIR}")
        list(APPEND CMAKE_PREFIX_PATH "$ENV{QT_DIR}")
        message(STATUS "Qt: using QT_DIR env -> $ENV{QT_DIR}")
    else()
        foreach(_qt_try
                "C:/Qt/6.11.1/msvc2022_64"
                "C:/Qt/6.11.0/msvc2022_64"
                "C:/Qt/6.10.0/msvc2022_64"
                "C:/Qt/6.9.3/msvc2022_64"
                "C:/Qt/6.9.0/msvc2022_64"
                "C:/Qt/6.8.3/msvc2022_64"
        )
            if(EXISTS "${_qt_try}/lib/cmake/Qt6/Qt6Config.cmake")
                list(APPEND CMAKE_PREFIX_PATH "${_qt_try}")
                message(STATUS "Qt: found -> ${_qt_try}")
                break()
            endif()
        endforeach()

        if(NOT CMAKE_PREFIX_PATH)
            message(FATAL_ERROR
                    "Qt 6 not found. Pass -DCMAKE_PREFIX_PATH=C:/Qt/6.x.x/msvc2022_64 "
                    "or set environment variable Qt6_DIR.")
        endif()
    endif()
endif()

find_package(Qt6 REQUIRED COMPONENTS
    Core Gui Widgets Network Sql Concurrent Multimedia MultimediaWidgets
    Svg SvgWidgets
)

# WebEngine is optional — CI environments may not have it installed
find_package(Qt6 QUIET COMPONENTS WebEngineWidgets)
if(Qt6WebEngineWidgets_FOUND)
    set(JARVIS_HAS_WEBENGINE TRUE)
    message(STATUS "Qt WebEngine: FOUND — interactive diagram panel enabled")
else()
    set(JARVIS_HAS_WEBENGINE FALSE)
    message(STATUS "Qt WebEngine: NOT FOUND — falling back to SVG diagram panel")
endif()

message(STATUS "Qt version: ${Qt6_VERSION}")

# ============================================================
# Vosk — offline speech recognition
# ============================================================

set(_vosk_dir "${CMAKE_SOURCE_DIR}/redist/vosk")

if(EXISTS "${_vosk_dir}/vosk_api.h" AND EXISTS "${_vosk_dir}/libvosk.lib")
    set(VOSK_AVAILABLE TRUE)
    message(STATUS "Vosk found: ${_vosk_dir}")
else()
    set(VOSK_AVAILABLE FALSE)
    message(WARNING
            "Vosk NOT found in redist/vosk/. Voice input will be disabled.\n"
            "Download: https://alphacephei.com/vosk/install\n"
            "Extract vosk_api.h, libvosk.lib, libvosk.dll to: ${_vosk_dir}/")
endif()

# ============================================================
# OpenCV — optional, for face detection and security camera
# ============================================================

set(_opencv_dir "${CMAKE_SOURCE_DIR}/redist/opencv")

find_package(OpenCV QUIET COMPONENTS core imgproc objdetect videoio highgui)
if(OpenCV_FOUND)
    set(JARVIS_HAS_OPENCV TRUE)
    message(STATUS "OpenCV: FOUND (${OpenCV_VERSION}) — security camera enabled")
elseif(EXISTS "${_opencv_dir}/include/opencv2/core.hpp")
    # Use bundled OpenCV from redist/
    set(JARVIS_HAS_OPENCV TRUE)
    set(OpenCV_INCLUDE_DIRS "${_opencv_dir}/include")
    file(GLOB OpenCV_LIBS "${_opencv_dir}/lib/*.lib")
    message(STATUS "OpenCV: FOUND in redist/ — security camera enabled")
else()
    set(JARVIS_HAS_OPENCV FALSE)
    message(STATUS "OpenCV: NOT FOUND — security camera disabled.\n"
            "  Install: choco install opencv  OR\n"
            "  Download to: ${_opencv_dir}/")
endif()

# --- Global compile definitions ---
add_compile_definitions(
    JARVIS_VERSION="${PROJECT_VERSION}"
    JARVIS_GITHUB_USER="Bohdan99py"
    JARVIS_GITHUB_REPO="jarvis"
    JARVIS_CORE_STATIC
    UNICODE
    _UNICODE
)

# ============================================================
# Module targets (OBJECT libraries)
# ============================================================

add_subdirectory(src/common)
add_subdirectory(src/db)
add_subdirectory(src/llm)
add_subdirectory(src/voice)
add_subdirectory(src/platform)
add_subdirectory(src/media)
add_subdirectory(src/intelligence)
add_subdirectory(src/network)
add_subdirectory(src/modules)
add_subdirectory(src/plugins)
add_subdirectory(src/engine)

# ============================================================
# Vosk — conditional compile definitions for voice module
# ============================================================

if(VOSK_AVAILABLE)
    target_include_directories(jarvis_voice PRIVATE "${_vosk_dir}")
    target_compile_definitions(jarvis_voice PRIVATE JARVIS_VOSK_AVAILABLE)
else()
    target_compile_definitions(jarvis_voice PRIVATE JARVIS_VOSK_STUB)
endif()

# ============================================================
# JarvisCore — combined static library from all modules
# ============================================================

add_library(JarvisCore STATIC
    $<TARGET_OBJECTS:jarvis_common>
    $<TARGET_OBJECTS:jarvis_db>
    $<TARGET_OBJECTS:jarvis_llm>
    $<TARGET_OBJECTS:jarvis_voice>
    $<TARGET_OBJECTS:jarvis_platform>
    $<TARGET_OBJECTS:jarvis_media>
    $<TARGET_OBJECTS:jarvis_intelligence>
    $<TARGET_OBJECTS:jarvis_network>
    $<TARGET_OBJECTS:jarvis_modules>
    $<TARGET_OBJECTS:jarvis_plugins>
    $<TARGET_OBJECTS:jarvis_engine>
)

# Propagate all module include directories through JarvisCore
target_include_directories(JarvisCore PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src/common
    ${CMAKE_CURRENT_SOURCE_DIR}/src/db
    ${CMAKE_CURRENT_SOURCE_DIR}/src/llm
    ${CMAKE_CURRENT_SOURCE_DIR}/src/voice
    ${CMAKE_CURRENT_SOURCE_DIR}/src/platform
    ${CMAKE_CURRENT_SOURCE_DIR}/src/media
    ${CMAKE_CURRENT_SOURCE_DIR}/src/intelligence
    ${CMAKE_CURRENT_SOURCE_DIR}/src/network
    ${CMAKE_CURRENT_SOURCE_DIR}/src/modules
    ${CMAKE_CURRENT_SOURCE_DIR}/src/plugins
    ${CMAKE_CURRENT_SOURCE_DIR}/src/engine
)

# zlib shipped with Qt6
target_include_directories(JarvisCore PRIVATE
    ${Qt6Core_INCLUDE_DIRS}/../QtZlib
)

target_link_libraries(JarvisCore PUBLIC
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::Network
    Qt6::Sql
    Qt6::Concurrent
    Qt6::Multimedia
    Qt6::MultimediaWidgets
    Qt6::Svg
    Qt6::SvgWidgets
)

if(JARVIS_HAS_WEBENGINE)
    target_link_libraries(JarvisCore PUBLIC Qt6::WebEngineWidgets)
    target_compile_definitions(JarvisCore PUBLIC JARVIS_HAS_WEBENGINE=1)
endif()

if(JARVIS_HAS_OPENCV)
    target_include_directories(JarvisCore PUBLIC ${OpenCV_INCLUDE_DIRS})
    target_link_libraries(JarvisCore PUBLIC ${OpenCV_LIBS})
    target_compile_definitions(JarvisCore PUBLIC JARVIS_HAS_OPENCV=1)
endif()

target_link_libraries(JarvisCore PRIVATE
    ole32
    oleaut32
    shell32
    user32
    advapi32
    psapi
    PowrProf
)

if(VOSK_AVAILABLE)
    target_link_libraries(JarvisCore PUBLIC "${_vosk_dir}/libvosk.lib")
endif()

if(MSVC)
    # Per-module compile options are inherited; set remaining on JarvisCore
    target_compile_options(JarvisCore PRIVATE /W4 /utf-8 /bigobj)
    target_compile_options(JarvisCore PRIVATE $<$<CONFIG:Release>:/O2 /GL>)
endif()

# ============================================================
# jarvis.exe — GUI executable
# ============================================================

qt_add_executable(jarvis WIN32
    jarvis.rc
)

# App sources added via src/app/CMakeLists.txt
add_subdirectory(src/app)

# Qt resource file
set(_qrc "${CMAKE_CURRENT_SOURCE_DIR}/assets/resources.qrc")
if(EXISTS "${_qrc}")
    target_sources(jarvis PRIVATE "${_qrc}")
endif()

target_link_libraries(jarvis PRIVATE JarvisCore)

set_target_properties(jarvis PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY         ${CMAKE_BINARY_DIR}/bin
    RUNTIME_OUTPUT_DIRECTORY_DEBUG   ${CMAKE_BINARY_DIR}/bin
    RUNTIME_OUTPUT_DIRECTORY_RELEASE ${CMAKE_BINARY_DIR}/bin
)

if(MSVC)
    target_compile_options(jarvis PRIVATE /W4 /utf-8 /bigobj)
    target_compile_options(jarvis PRIVATE $<$<CONFIG:Release>:/O2 /GL>)
    target_link_options(jarvis PRIVATE $<$<CONFIG:Release>:/LTCG>)
    target_link_options(jarvis PRIVATE /STACK:4194304)
endif()

# ============================================================
# Post-build: deploy Qt runtime (Windows)
# ============================================================

if(WIN32)
    get_target_property(_qmake_exec Qt6::qmake IMPORTED_LOCATION)
    get_filename_component(_qt_bin_dir "${_qmake_exec}" DIRECTORY)
    find_program(WINDEPLOYQT_EXECUTABLE windeployqt HINTS "${_qt_bin_dir}")

    if(WINDEPLOYQT_EXECUTABLE)
        add_custom_command(TARGET jarvis POST_BUILD
                COMMAND "${WINDEPLOYQT_EXECUTABLE}"
                --no-translations
                --no-system-d3d-compiler
                --no-opengl-sw
                --no-compiler-runtime
                "$<TARGET_FILE:jarvis>"
                COMMENT "Deploying Qt runtime..."
        )
    endif()
endif()

# ============================================================
# Post-build: copy Vosk DLLs
# ============================================================

if(VOSK_AVAILABLE)
    foreach(_vosk_dll
            "${_vosk_dir}/libvosk.dll"
            "${_vosk_dir}/libgcc_s_seh-1.dll"
            "${_vosk_dir}/libstdc++-6.dll"
            "${_vosk_dir}/libwinpthread-1.dll"
    )
        if(EXISTS "${_vosk_dll}")
            add_custom_command(TARGET jarvis POST_BUILD
                    COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${_vosk_dll}"
                    "$<TARGET_FILE_DIR:jarvis>"
                    COMMENT "Copying Vosk runtime: ${_vosk_dll}"
            )
        endif()
    endforeach()
    message(STATUS "Vosk DLLs will be copied to bin/ on build")
endif()

# ============================================================
# Post-build: copy Poppler DLLs
# ============================================================

set(_poppler_bin "")
foreach(_candidate
        "${CMAKE_SOURCE_DIR}/redist/poppler/bin"
        "${CMAKE_SOURCE_DIR}/redist/poppler-26.02.0/Library/bin"
        "${CMAKE_SOURCE_DIR}/redist/poppler-25.12.0/Library/bin"
)
    if(EXISTS "${_candidate}")
        set(_poppler_bin "${_candidate}")
        break()
    endif()
endforeach()

if(_poppler_bin)
    add_custom_command(TARGET jarvis POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${_poppler_bin}"
            "$<TARGET_FILE_DIR:jarvis>"
            COMMENT "Copying Poppler DLLs..."
    )
    message(STATUS "Poppler found: ${_poppler_bin}")
else()
    message(WARNING "Poppler not found in redist/.")
endif()

# ============================================================
# Post-build: copy Tesseract
# ============================================================

set(_tesseract_dir "")
foreach(_candidate
        "${CMAKE_SOURCE_DIR}/redist/tesseract"
        "${CMAKE_SOURCE_DIR}/redist/Tesseract-OCR"
)
    if(EXISTS "${_candidate}/tesseract.exe")
        set(_tesseract_dir "${_candidate}")
        break()
    endif()
endforeach()

if(_tesseract_dir)
    add_custom_command(TARGET jarvis POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${_tesseract_dir}"
            "$<TARGET_FILE_DIR:jarvis>/Tesseract-OCR"
            COMMENT "Copying Tesseract OCR..."
    )
    message(STATUS "Tesseract found: ${_tesseract_dir}")
else()
    message(WARNING "Tesseract not found in redist/.")
endif()

# --- Default startup project in IDEs ---
set_property(DIRECTORY ${CMAKE_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT jarvis)
