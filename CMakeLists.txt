## ============================================================
## CMakeLists.txt — Единый файл сборки J.A.R.V.I.S.
##   Структура:
##     core/  — исходники ядра (JarvisCore.dll)
##     app/   — main.cpp + mainwindow.* (jarvis.exe)
## ============================================================

cmake_minimum_required(VERSION 3.16)
project(jarvis VERSION 2.4.7 LANGUAGES CXX)

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
#   1) -DCMAKE_PREFIX_PATH=...        (передан вручную)
#   2) Переменная окружения Qt6_DIR / QT_DIR
#   3) Автопоиск C:/Qt/6.*/msvc202[02]_64
#   4) Fallback — путь под текущую машину
# ============================================================

set(QT_FALLBACK_PATH "C:/Qt/6.11.0/msvc2022_64")

if(NOT DEFINED CMAKE_PREFIX_PATH OR CMAKE_PREFIX_PATH STREQUAL "")
    if(DEFINED ENV{Qt6_DIR} AND EXISTS "$ENV{Qt6_DIR}")
        list(APPEND CMAKE_PREFIX_PATH "$ENV{Qt6_DIR}")
        message(STATUS "Qt: using Qt6_DIR env -> $ENV{Qt6_DIR}")
    elseif(DEFINED ENV{QT_DIR} AND EXISTS "$ENV{QT_DIR}")
        list(APPEND CMAKE_PREFIX_PATH "$ENV{QT_DIR}")
        message(STATUS "Qt: using QT_DIR env -> $ENV{QT_DIR}")
    else()
        file(GLOB _qt_candidates
                LIST_DIRECTORIES TRUE
                "C:/Qt/6.*/msvc2022_64"
                "C:/Qt/6.*/msvc2019_64"
        )
        if(_qt_candidates)
            list(SORT _qt_candidates ORDER DESCENDING)
            list(GET _qt_candidates 0 _qt_best)
            list(APPEND CMAKE_PREFIX_PATH "${_qt_best}")
            message(STATUS "Qt: auto-detected -> ${_qt_best}")
        elseif(EXISTS "${QT_FALLBACK_PATH}")
            list(APPEND CMAKE_PREFIX_PATH "${QT_FALLBACK_PATH}")
            message(STATUS "Qt: using fallback -> ${QT_FALLBACK_PATH}")
        else()
            message(FATAL_ERROR
                    "Qt 6 not found. Pass -DCMAKE_PREFIX_PATH=C:/Qt/6.x.x/msvc2022_64 "
                    "or set environment variable Qt6_DIR.")
        endif()
    endif()
endif()

find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets Network)
message(STATUS "Qt version: ${Qt6_VERSION}")

# --- Версия и GitHub ---
add_compile_definitions(
        JARVIS_VERSION="${PROJECT_VERSION}"
        JARVIS_GITHUB_USER="Bohdan99py"
        JARVIS_GITHUB_REPO="jarvis"
)

# ============================================================
# Цель: JarvisCore.dll (ядро)
# ============================================================

add_library(JarvisCore SHARED
        # Экспорт-макрос
        core/jarvis_core_export.h

        # Ядро
        core/jarvis.h
        core/jarvis.cpp
        core/command_registry.h
        core/command_registry.cpp
        core/session_memory.h
        core/session_memory.cpp
        core/theme.h
        core/virtual_keyboard.h
        core/virtual_keyboard.cpp

        # Claude API
        core/claude_api.h
        core/claude_api.cpp
        core/embedded_key.h

        # Интеллект
        core/action_predictor.h
        core/action_predictor.cpp

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
        core/updater.h
        core/updater.cpp
        core/auto_updater.h
        core/auto_updater.cpp
)

# Куда класть .dll и .lib
set_target_properties(JarvisCore PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY         ${CMAKE_BINARY_DIR}/bin
        RUNTIME_OUTPUT_DIRECTORY_DEBUG   ${CMAKE_BINARY_DIR}/bin
        RUNTIME_OUTPUT_DIRECTORY_RELEASE ${CMAKE_BINARY_DIR}/bin

        LIBRARY_OUTPUT_DIRECTORY         ${CMAKE_BINARY_DIR}/bin
        LIBRARY_OUTPUT_DIRECTORY_DEBUG   ${CMAKE_BINARY_DIR}/bin
        LIBRARY_OUTPUT_DIRECTORY_RELEASE ${CMAKE_BINARY_DIR}/bin

        ARCHIVE_OUTPUT_DIRECTORY         ${CMAKE_BINARY_DIR}/bin
        ARCHIVE_OUTPUT_DIRECTORY_DEBUG   ${CMAKE_BINARY_DIR}/bin
        ARCHIVE_OUTPUT_DIRECTORY_RELEASE ${CMAKE_BINARY_DIR}/bin
)

# JARVIS_CORE_LIBRARY → __declspec(dllexport) внутри ядра
target_compile_definitions(JarvisCore PRIVATE
        JARVIS_CORE_LIBRARY
        UNICODE
        _UNICODE
)

# Заголовки ядра видны и приложению
target_include_directories(JarvisCore PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/core
)

target_link_libraries(JarvisCore PUBLIC
        Qt6::Core
        Qt6::Gui
        Qt6::Widgets
        Qt6::Network
)

target_link_libraries(JarvisCore PRIVATE
        ole32
        oleaut32
        shell32
        user32
        advapi32
        sapi
)

if(MSVC)
    target_compile_options(JarvisCore PRIVATE
            /W4
            /utf-8
            /bigobj
    )
    target_compile_options(JarvisCore PRIVATE $<$<CONFIG:Release>:/O2 /GL>)
    target_link_options(JarvisCore PRIVATE $<$<CONFIG:Release>:/LTCG>)
endif()

# ============================================================
# Цель: jarvis.exe (GUI)
# ============================================================

qt_add_executable(jarvis WIN32
        app/main.cpp
        app/mainwindow.h
        app/mainwindow.cpp
)

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

# --- Стартовый проект в Visual Studio / CLion ---
set_property(DIRECTORY ${CMAKE_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT jarvis)