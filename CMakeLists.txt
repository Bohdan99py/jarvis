cmake_minimum_required(VERSION 3.20)
project(Jarvis VERSION 2.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

# ============================================================
# Настройки путей
# ============================================================

# Путь к Qt — измени под свою систему или передай через -DCMAKE_PREFIX_PATH=...
if(NOT DEFINED CMAKE_PREFIX_PATH)
    set(CMAKE_PREFIX_PATH "C:/Qt/6.11.0/msvc2022_64" CACHE PATH "Qt installation path")
endif()

# Все DLL/EXE складываются в одну папку bin/
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

# Для multi-config генераторов (Visual Studio)
foreach(CONFIG_TYPE ${CMAKE_CONFIGURATION_TYPES})
    string(TOUPPER ${CONFIG_TYPE} CONFIG_TYPE_UPPER)
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_${CONFIG_TYPE_UPPER} ${CMAKE_BINARY_DIR}/bin)
    set(CMAKE_LIBRARY_OUTPUT_DIRECTORY_${CONFIG_TYPE_UPPER} ${CMAKE_BINARY_DIR}/bin)
    set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY_${CONFIG_TYPE_UPPER} ${CMAKE_BINARY_DIR}/lib)
endforeach()

# ============================================================
# Находим Qt
# ============================================================

find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets Network)

# ============================================================
# Подпроекты
# ============================================================

# 1. Ядро (DLL) — можно обновлять отдельно
add_subdirectory(core)

# 2. Плагины (DLL) — каждый обновляется независимо
add_subdirectory(plugins)

# 3. Приложение (EXE) — тонкий лаунчер
add_subdirectory(app)

# ============================================================
# Установка (install)
# ============================================================

install(TARGETS JarvisApp JarvisCore
    RUNTIME DESTINATION bin
    LIBRARY DESTINATION bin
    ARCHIVE DESTINATION lib
)

# Копируем конфиг плагинов
install(FILES ${CMAKE_SOURCE_DIR}/plugins/plugins.json
    DESTINATION bin
)
