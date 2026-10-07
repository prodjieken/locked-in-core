# Unit tests for the headless parts of lockedin_core (MeterBus, levels,
# TransportRandom/GridClock, character JSON + state machine).
# Only needs juce_core + juce_audio_basics, so it builds fast in CI.

juce_add_console_app(LockedInCoreTests PRODUCT_NAME "LockedInCoreTests")

target_sources(LockedInCoreTests PRIVATE CoreTests.cpp)
target_include_directories(LockedInCoreTests PRIVATE ${CMAKE_CURRENT_LIST_DIR}/../modules)

target_compile_definitions(LockedInCoreTests PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_STANDALONE_APPLICATION=1)

target_link_libraries(LockedInCoreTests
    PRIVATE
        juce::juce_core
        juce::juce_audio_basics
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)

add_test(NAME LockedInCoreTests COMMAND LockedInCoreTests)
