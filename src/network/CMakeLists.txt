## src/network — Telegram bot gateway, mesh connector, mobile pairing
## Depends on: common
## Private: db, media, modules (used in .cpp files via forward declarations)

add_library(jarvis_network OBJECT
    j2j_telegram_gateway.h
    j2j_telegram_gateway.cpp
    telegram_access_manager.h
    telegram_access_manager.cpp
    command_dispatcher_tg.h
    command_dispatcher_tg.cpp
    j2j_mesh_connector.h
    j2j_mesh_connector.cpp
    mobile_pairing_manager.h
    mobile_pairing_manager.cpp
)

target_include_directories(jarvis_network PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(jarvis_network
    PUBLIC  jarvis_common Qt6::Core Qt6::Network
    PRIVATE jarvis_db jarvis_media jarvis_modules
)
