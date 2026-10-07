cmake_minimum_required(VERSION 3.22)

include(${CMAKE_CURRENT_LIST_DIR}/cmake/LockedInDefaults.cmake)

project(LockedInCore VERSION 0.1.0 LANGUAGES C CXX)

include(${CMAKE_CURRENT_LIST_DIR}/cmake/LockedIn.cmake)

# JUCE: uses an existing juce target if the parent already added one,
# a local checkout if -DJUCE_DIR=... is given, otherwise fetches the pinned tag.
lockedin_fetch_juce()

juce_add_module(${CMAKE_CURRENT_LIST_DIR}/modules/lockedin_core ALIAS_NAMESPACE lockedin)

if(PROJECT_IS_TOP_LEVEL)
    option(LOCKEDIN_BUILD_EXAMPLES "Build examples/hello-character" ON)
    option(LOCKEDIN_BUILD_TESTS "Build the core unit tests" ON)

    if(LOCKEDIN_BUILD_EXAMPLES)
        add_subdirectory(examples/hello-character)
    endif()

    if(LOCKEDIN_BUILD_TESTS)
        enable_testing()
        add_subdirectory(tests)
    endif()
endif()
