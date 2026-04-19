## -------------------------------------------------------
## app/CMakeLists.txt — Тонкий лаунчер J.A.R.V.I.S.
##
## Минимальный EXE, который загружает JarvisCore.dll
## и создаёт главное окно. При обновлении ядра или
## плагинов — EXE пересобирать не нужно.
## -------------------------------------------------------

add_executable(JarvisApp WIN32
    main.cpp
    mainwindow.h
    mainwindow.cpp
)

target_compile_definitions(JarvisApp PRIVATE UNICODE _UNICODE)

target_link_libraries(JarvisApp PRIVATE
    JarvisCore
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
)

# Копируем plugins.json рядом с exe
add_custom_command(TARGET JarvisApp POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory
        "$<TARGET_FILE_DIR:JarvisApp>/plugins"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_SOURCE_DIR}/plugins/plugins.json"
        "$<TARGET_FILE_DIR:JarvisApp>/plugins/plugins.json"
    COMMENT "Copying plugins config..."
)

if(MSVC)
    target_compile_options(JarvisApp PRIVATE /W4)
endif()
