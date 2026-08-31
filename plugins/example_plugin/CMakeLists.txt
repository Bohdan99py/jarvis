## -------------------------------------------------------
## plugins/example_plugin — рабочий пример плагина (API v2)
##
## Плагин НЕ линкуется с ядром: ему нужны только два заголовка
## интерфейса и Qt::Core. Так и должно быть — иначе плагин
## пришлось бы пересобирать после каждой правки ядра.
## -------------------------------------------------------

add_library(example_plugin MODULE
    example_plugin.cpp
    example_plugin.json
)

set_target_properties(example_plugin PROPERTIES
    AUTOMOC ON
    # Рядом с приложением: PluginBridge сканирует bin/plugins.
    LIBRARY_OUTPUT_DIRECTORY         ${CMAKE_BINARY_DIR}/bin/plugins
    LIBRARY_OUTPUT_DIRECTORY_DEBUG   ${CMAKE_BINARY_DIR}/bin/plugins
    LIBRARY_OUTPUT_DIRECTORY_RELEASE ${CMAKE_BINARY_DIR}/bin/plugins
    RUNTIME_OUTPUT_DIRECTORY         ${CMAKE_BINARY_DIR}/bin/plugins
    RUNTIME_OUTPUT_DIRECTORY_DEBUG   ${CMAKE_BINARY_DIR}/bin/plugins
    RUNTIME_OUTPUT_DIRECTORY_RELEASE ${CMAKE_BINARY_DIR}/bin/plugins
)

target_include_directories(example_plugin PRIVATE
    ${CMAKE_SOURCE_DIR}/src/plugins
)

target_link_libraries(example_plugin PRIVATE Qt6::Core)
