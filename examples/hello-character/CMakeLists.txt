# In a real plugin repo this file starts with:
#   cmake_minimum_required(VERSION 3.22)
#   include(locked-in-core/cmake/LockedInDefaults.cmake)
#   project(HelloCharacter VERSION 0.1.0)
#   add_subdirectory(locked-in-core)
# Here locked-in-core is the parent project, so only the plugin call is needed.

lockedin_add_plugin(HelloCharacter
    PRODUCT_NAME "Hello Character"
    CODE         Hlch
    VERSION      0.1.0
    SOURCES
        Source/HelloCharacter.h
        Source/HelloCharacter.cpp
    ASSETS
        assets/hello-character.json
        assets/ding.wav
    PRESETS
        presets/Default.lipreset
        presets/Quieter.lipreset)
