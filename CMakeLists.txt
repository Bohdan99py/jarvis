cmake_minimum_required(VERSION 3.20)
project(Jarvis VERSION 2.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

# Путь к Qt — CLion/Visual Studio найдут автоматически,
# или передай: cmake -DCMAKE_PREFIX_PATH="C:/Qt/6.11.0/msvc2022_64" ..
if(NOT DEFINED CMAKE_PREFIX_PATH)
    set(CMAKE_PREFIX_PATH "C:/Qt/6.11.0/msvc2022_64")
endif()

find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets Network)

# --- GitHub-репозиторий для автообновления ---
set(GITHUB_USER "Bohdan99py" CACHE STRING "GitHub username")
set(GITHUB_REPO "jarvis"     CACHE STRING "GitHub repository name")

# ============================================================
# Все исходники — один exe, без DLL, без плагинов
# ============================================================

add_executable(Jarvis WIN32
        # --- app/ ---
        app/main.cpp
        app/mainwindow.h
        app/mainwindow.cpp

        # --- core/ ---
        core/jarvis.h
        core/jarvis.cpp
        core/theme.h
        core/command_registry.h
        core/command_registry.cpp
        core/virtual_keyboard.h
        core/virtual_keyboard.cpp
        core/claude_api.h
        core/claude_api.cpp
        core/session_memory.h
        core/session_memory.cpp
        core/action_predictor.h
        core/action_predictor.cpp
        core/auto_updater.h
        core/auto_updater.cpp
        core/embedded_key.h
)

# Чтобы #include "jarvis.h" работал из app/
target_include_directories(Jarvis PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/core
        ${CMAKE_CURRENT_SOURCE_DIR}/app
)

target_link_libraries(Jarvis PRIVATE
        Qt6::Core
        Qt6::Gui
        Qt6::Widgets
        Qt6::Network
        ole32
        oleaut32
        shell32
)

# Версия и GitHub info → в код через дефайны
target_compile_definitions(Jarvis PRIVATE
        UNICODE
        _UNICODE
        JARVIS_VERSION="${PROJECT_VERSION}"
        JARVIS_GITHUB_USER="${GITHUB_USER}"
        JARVIS_GITHUB_REPO="${GITHUB_REPO}"
)

if(MSVC)
    target_compile_options(Jarvis PRIVATE /W4)
    target_compile_options(Jarvis PRIVATE $<$<CONFIG:Release>:/O2 /GL>)
    target_link_options(Jarvis PRIVATE $<$<CONFIG:Release>:/LTCG>)
endif()

install(TARGETS Jarvis RUNTIME DESTINATION .)