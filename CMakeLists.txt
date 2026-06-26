## ============================================================
## CMakeLists.txt — Единый файл сборки J.A.R.V.I.S.
##   core/  — исходники ядра (СТАТИЧЕСКАЯ библиотека JarvisCore)
##   app/   — main.cpp + mainwindow.* (jarvis.exe)
##
## Ядро линкуется СТАТИЧЕСКИ → нет JarvisCore.dll,
## один исполняемый файл, никаких runtime-зависимостей кроме Qt.
## ============================================================

cmake_minimum_required(VERSION 3.16)
project(jarvis VERSION 2.5.0 LANGUAGES CXX)

# --- C++17, без расширений ---
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# --- Qt MOC/RCC/UIC ---
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

# --- Все артефакты в одну папку bin/ ---
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

# ============================================================
# Поиск Qt 6
# ============================================================

# Приоритет: env переменная → конкретная версия → автодетект
if(NOT DEFINED CMAKE_PREFIX_PATH OR CMAKE_PREFIX_PATH STREQUAL "")
    if(DEFINED ENV{Qt6_DIR} AND EXISTS "$ENV{Qt6_DIR}")
        list(APPEND CMAKE_PREFIX_PATH "$ENV{Qt6_DIR}")
        message(STATUS "Qt: using Qt6_DIR env -> $ENV{Qt6_DIR}")
    elseif(DEFINED ENV{QT_DIR} AND EXISTS "$ENV{QT_DIR}")
        list(APPEND CMAKE_PREFIX_PATH "$ENV{QT_DIR}")
        message(STATUS "Qt: using QT_DIR env -> $ENV{QT_DIR}")
    else()
        # Перебираем конкретные версии от новой к старой
        # При добавлении новой Qt — просто допиши строку в начало списка
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

find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets Network Sql Concurrent Multimedia)
message(STATUS "Qt version: ${Qt6_VERSION}")

# ============================================================
# Vosk — офлайн распознавание речи (замена Whisper)
# Скачай: https://alphacephei.com/vosk/install
# Распакуй DLL в: redist/vosk/
#
# Структура:
#   redist/vosk/libvosk.dll          — основная DLL
#   redist/vosk/vosk_api.h           — заголовок
#   redist/vosk/libvosk.lib          — import lib для MSVC
#
# Models (small only — for command recognition, not dictation):
#   redist/vosk/model-en/            — vosk-model-small-en-us-0.15 (~40MB)
#   redist/vosk/model-ru-small/      — vosk-model-small-ru-0.22 (~45MB)
#
# Models are downloaded at first run. Large models removed to save ~7GB.
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

# --- Версия и GitHub ---
add_compile_definitions(
        JARVIS_VERSION="${PROJECT_VERSION}"
        JARVIS_GITHUB_USER="Bohdan99py"
        JARVIS_GITHUB_REPO="jarvis"
)

# --- Статическая сборка: JARVIS_CORE_EXPORT раскрывается в пустоту ---
add_compile_definitions(JARVIS_CORE_STATIC)

# ============================================================
# Цель: JarvisCore (СТАТИЧЕСКАЯ библиотека ядра)
# ============================================================

add_library(JarvisCore STATIC
        # Экспорт-макрос (пустой при статической сборке)
        core/jarvis_core_export.h

        # Строки интерфейса (RU/EN) — только заголовок, cpp не нужен
        core/lang.h

        # Единая точка правды для путей хранения данных на диске —
        # только заголовок, cpp не нужен (см. комментарий в файле)
        core/jarvis_paths.h

        # Ядро
        core/jarvis.h
        core/jarvis.cpp
        core/command_registry.h
        core/command_registry.cpp
        core/session_memory.h
        core/session_memory.cpp
        core/theme.h
        core/theme_manager.h
        core/audio_manager.h
        core/user_profile_manager.h
        core/system_manifest.h
        core/task_manager_dialog.h
        core/task_manager_dialog.cpp
        core/j2j_mesh_connector.h
        core/j2j_mesh_connector.cpp
        core/mobile_pairing_manager.h
        core/mobile_pairing_manager.cpp
        core/j2j_telegram_gateway.h
        core/j2j_telegram_gateway.cpp
        core/telegram_access_manager.h
        core/telegram_access_manager.cpp
        core/command_dispatcher_tg.h
        core/command_dispatcher_tg.cpp
        core/translation_engine.h
        core/translation_engine.cpp
        core/virtual_keyboard.h
        core/virtual_keyboard.cpp

        # Claude API
        core/claude_api.h
        core/claude_api.cpp
        core/embedded_key.h

        # Ollama API (локальный LLM, OpenAI-compatible)
        core/ollama_api.h

        # Gemini API (Google, fallback если Ollama недоступна)
        core/gemini_api.h
        core/gemini_api.cpp

        # Интеллект
        core/action_predictor.h
        core/action_predictor.cpp
        core/user_profile.h
        core/user_profile.cpp
        core/brain.h
        core/brain.cpp
        core/context_snapshot.h
        core/search_router.h
        core/search_router.cpp
        core/semantic_mapper.h
        core/semantic_mapper.cpp
        core/content_search.h
        core/content_search.cpp
        core/ocr_extractor.h
        core/ocr_extractor.cpp
        core/learned_commands.h
        core/learned_commands.cpp
        core/screen_agent.h
        core/screen_agent.cpp
        core/local_trainer.h
        core/local_trainer.cpp
        core/screenshot_learner.h
        core/screenshot_learner.cpp
        # Activity tracking + knowledge base
        core/activity_tracker.h
        core/activity_tracker.cpp
        # Прикрепления файлов
        core/attachments_manager.h
        core/attachments_manager.cpp

        # Индексатор проекта
        core/project_indexer.h
        core/project_indexer.cpp

        # Действия над кодом ([FILE]/[DIFF])
        core/code_actions.h
        core/code_actions.cpp

        # Плагинная система
        core/plugin_interface.h
        core/plugin_host.h
        core/plugin_manager.h
        core/plugin_manager.cpp

        # Автообновление
        core/auto_updater.h
        core/auto_updater.cpp
        core/bug_reporter.h
        core/bug_reporter.cpp

        # База данных SQLite — история чатов, команды, память, индекс файлов
        core/database_manager.h
        core/database_manager.cpp

        # Фоновое обучение — индексация проекта + анализ паттернов чата
        core/background_learner.h
        core/background_learner.cpp

        # Background training data pipeline — voice journal → training pairs → .jsonl
        core/training_processing_worker.h
        core/training_processing_worker.cpp

        # Голосовой ввод — Whisper.cpp, VAD, шёпот, wake word
        core/voice_input.h
        core/voice_input.cpp
        core/VoskSetupDialog.h
        core/VoskSetupDialog.cpp

        # Пассивная запись голоса для датасета fine-tuning
        core/passive_listener.h
        core/passive_listener.cpp

        # --- Full PC Voice Control (мышь/клавиатура/окна/система голосом) ---
        core/pc_controller.h
        core/pc_controller.cpp
        core/pc_command_registry.h
        core/pc_command_registry.cpp
        core/macro_recorder.h
        core/macro_recorder.cpp
)

target_compile_definitions(JarvisCore PRIVATE
        UNICODE
        _UNICODE
)

target_include_directories(JarvisCore PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/core
)

# zlib поставляется с Qt6 — используем встроенный
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
)

target_link_libraries(JarvisCore PRIVATE
        ole32
        oleaut32
        shell32
        user32
        advapi32
        psapi
        PowrProf
)

# Vosk — линкуем если нашли
if(VOSK_AVAILABLE)
    target_include_directories(JarvisCore PRIVATE "${_vosk_dir}")
    target_link_libraries(JarvisCore PUBLIC "${_vosk_dir}/libvosk.lib")
    target_compile_definitions(JarvisCore PRIVATE JARVIS_VOSK_AVAILABLE)
else()
    target_compile_definitions(JarvisCore PRIVATE JARVIS_VOSK_STUB)
endif()

if(MSVC)
    target_compile_options(JarvisCore PRIVATE
            /W4
            /utf-8
            /bigobj
    )
    target_compile_options(JarvisCore PRIVATE $<$<CONFIG:Release>:/O2 /GL>)
endif()

# ============================================================
# Цель: jarvis.exe (GUI)
# ============================================================

qt_add_executable(jarvis WIN32
        app/main.cpp
        app/mainwindow.h
        app/mainwindow.cpp
        jarvis.rc              # Windows resource — иконка в exe/проводнике/таскбаре
)

# Подключаем иконки через Qt-ресурсы если файл существует
set(_qrc "${CMAKE_CURRENT_SOURCE_DIR}/assets/resources.qrc")
if(EXISTS "${_qrc}")
    target_sources(jarvis PRIVATE "${_qrc}")
endif()

target_link_libraries(jarvis PRIVATE
        JarvisCore
        Qt6::Core
        Qt6::Gui
        Qt6::Widgets
        Qt6::Network
)

target_include_directories(jarvis PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/app
        ${CMAKE_CURRENT_SOURCE_DIR}/core
)

set_target_properties(jarvis PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY         ${CMAKE_BINARY_DIR}/bin
        RUNTIME_OUTPUT_DIRECTORY_DEBUG   ${CMAKE_BINARY_DIR}/bin
        RUNTIME_OUTPUT_DIRECTORY_RELEASE ${CMAKE_BINARY_DIR}/bin
)

if(MSVC)
    target_compile_options(jarvis PRIVATE
            /W4
            /utf-8
            /bigobj
    )
    target_compile_options(jarvis PRIVATE $<$<CONFIG:Release>:/O2 /GL>)
    target_link_options(jarvis PRIVATE $<$<CONFIG:Release>:/LTCG>)
    # Увеличиваем стек до 4 МБ (дефолт 1 МБ — мало при 1700+ строк в buildMenuBar)
    target_link_options(jarvis PRIVATE /STACK:4194304)
endif()

# --- Упаковка Qt-зависимостей рядом с exe (Windows) ---
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

# --- Копируем Vosk DLL рядом с exe ---
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


# --- Копируем Poppler DLL рядом с exe ---
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

# --- Копируем Tesseract рядом с exe ---
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

# --- Стартовый проект в Visual Studio / CLion ---
set_property(DIRECTORY ${CMAKE_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT jarvis)