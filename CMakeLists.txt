cmake_minimum_required(VERSION 3.20)
project(Jarvis VERSION 2.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

# Путь к Qt — можно переопределить при вызове cmake:
#   cmake -DCMAKE_PREFIX_PATH="C:/Qt/6.8.0/msvc2022_64" ..
if(NOT DEFINED CMAKE_PREFIX_PATH)
    set(CMAKE_PREFIX_PATH "C:/Qt/6.11.0/msvc2022_64")
endif()

find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets Network)

# --- GitHub-репозиторий для автообновления ---
# Замени на свой GitHub username:
set(GITHUB_USER "Bohdan99py" CACHE STRING "GitHub username")
set(GITHUB_REPO "jarvis"     CACHE STRING "GitHub repository name")

add_executable(Jarvis WIN32
        main.cpp
        jarvis.h
        jarvis.cpp
        mainwindow.h
        mainwindow.cpp
        theme.h
        command_registry.h
        command_registry.cpp
        virtual_keyboard.h
        virtual_keyboard.cpp
        claude_api.h
        claude_api.cpp
        session_memory.h
        session_memory.cpp
        action_predictor.h
        action_predictor.cpp
        auto_updater.h
        auto_updater.cpp
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

# Передаём версию и GitHub info в код через дефайны
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

# --- Установка (для CPack / ручного deploy) ---
install(TARGETS Jarvis RUNTIME DESTINATION .)
