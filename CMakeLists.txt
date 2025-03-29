cmake_minimum_required(VERSION 3.16)

include(ImHexPlugin)

add_imhex_plugin(
    NAME
        auto_reload
    SOURCES
        plugin_auto_reload.cpp
    LIBRARIES
        fonts
    INCLUDES
        ${CMAKE_CURRENT_SOURCE_DIR}/../fonts/include
) 