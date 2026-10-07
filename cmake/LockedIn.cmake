# Locked In plugin build helpers.
#
#   lockedin_fetch_juce()     - makes JUCE available (called by locked-in-core itself)
#   lockedin_add_plugin(...)  - one call per plugin, applies every house convention
#
# See README.md for the full plugin template.

include_guard(GLOBAL)

set(LOCKEDIN_CORE_DIR "${CMAKE_CURRENT_LIST_DIR}/.." CACHE INTERNAL "locked-in-core root")
set(LOCKEDIN_JUCE_TAG "8.0.15" CACHE STRING "JUCE git tag fetched when JUCE_DIR is not set")

# House constants. Changing any of these breaks session recall for shipped plugins.
# CACHE INTERNAL so they are visible from the plugin repo's top-level CMakeLists:
# this file is include()d inside locked-in-core's add_subdirectory scope, and a
# plain set() there would leave them empty when lockedin_add_plugin() runs
# (JUCE then silently falls back to "yourcompany" / "Manu").
set(LOCKEDIN_COMPANY_NAME      "Locked In"                     CACHE INTERNAL "")
set(LOCKEDIN_MANUFACTURER_CODE "Lkin"                          CACHE INTERNAL "")   # shared with Eli
set(LOCKEDIN_BUNDLE_PREFIX     "com.lockedin"                  CACHE INTERNAL "")
set(LOCKEDIN_WEBSITE           "https://lockedinthestudio.com" CACHE INTERNAL "")
set(LOCKEDIN_EMAIL             "info@lockedinthestudio.com"    CACHE INTERNAL "")

if(APPLE)
    set(_lockedin_copy_default ON)    # installs into ~/Library/Audio/Plug-Ins for quick DAW testing
else()
    set(_lockedin_copy_default OFF)   # Program Files needs admin rights on Windows
endif()
option(LOCKEDIN_COPY_AFTER_BUILD "Copy built plugins into the user plug-in folders" ${_lockedin_copy_default})

# ---------------------------------------------------------------------------
macro(lockedin_fetch_juce)
    if(NOT COMMAND juce_add_plugin)
        if(JUCE_DIR)
            add_subdirectory(${JUCE_DIR} ${CMAKE_BINARY_DIR}/_deps/juce-build EXCLUDE_FROM_ALL)
        else()
            include(FetchContent)
            FetchContent_Declare(juce
                GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
                GIT_TAG        ${LOCKEDIN_JUCE_TAG}
                GIT_SHALLOW    TRUE)
            FetchContent_MakeAvailable(juce)
        endif()
    endif()
endmacro()

# ---------------------------------------------------------------------------
# lockedin_add_plugin(<Target>
#     CODE <4 chars>                 required. Unique per plugin, first letter uppercase. NEVER change after release.
#     SOURCES <files...>             required.
#     ASSETS <files...>              character JSON, sprite sheets, knob filmstrips, fonts, gag sounds.
#     PRESETS <files...>             factory presets (*.lipreset), embedded in the binary.
#     PRODUCT_NAME <name>            shown in DAWs (default: <Target>). NEVER change after release.
#     VERSION <x.y.z>                default: PROJECT_VERSION.
#     DESCRIPTION <text>
#     VST3_CATEGORIES <cats...>      default: Fx
#     INSTRUMENT | MIDI_EFFECT       plugin kind (default: audio effect)
#     NEEDS_MIDI_INPUT | NEEDS_MIDI_OUTPUT)
function(lockedin_add_plugin target)
    set(options INSTRUMENT MIDI_EFFECT NEEDS_MIDI_INPUT NEEDS_MIDI_OUTPUT)
    set(one_value CODE PRODUCT_NAME VERSION DESCRIPTION)
    set(multi_value SOURCES ASSETS PRESETS VST3_CATEGORIES)
    cmake_parse_arguments(LI "${options}" "${one_value}" "${multi_value}" ${ARGN})

    if(NOT LOCKEDIN_COMPANY_NAME OR NOT LOCKEDIN_MANUFACTURER_CODE OR NOT LOCKEDIN_BUNDLE_PREFIX)
        message(FATAL_ERROR "lockedin_add_plugin(${target}): Locked In house constants are not set (vendor/manufacturer/bundle prefix)")
    endif()
    if(LI_UNPARSED_ARGUMENTS)
        message(FATAL_ERROR "lockedin_add_plugin(${target}): unknown arguments: ${LI_UNPARSED_ARGUMENTS}")
    endif()
    if(NOT LI_CODE MATCHES "^[A-Z][A-Za-z0-9][A-Za-z0-9][A-Za-z0-9]$")
        message(FATAL_ERROR "lockedin_add_plugin(${target}): CODE must be 4 characters starting with an uppercase letter (got '${LI_CODE}')")
    endif()
    if(LI_CODE STREQUAL LOCKEDIN_MANUFACTURER_CODE)
        message(FATAL_ERROR "lockedin_add_plugin(${target}): CODE must differ from the manufacturer code")
    endif()
    if(NOT LI_SOURCES)
        message(FATAL_ERROR "lockedin_add_plugin(${target}): SOURCES is required")
    endif()

    if(NOT LI_PRODUCT_NAME)
        set(LI_PRODUCT_NAME "${target}")
    endif()
    if(NOT LI_VERSION)
        if(PROJECT_VERSION)
            set(LI_VERSION "${PROJECT_VERSION}")
        else()
            set(LI_VERSION "0.1.0")
        endif()
    endif()
    if(NOT LI_VST3_CATEGORIES)
        if(LI_INSTRUMENT)
            set(LI_VST3_CATEGORIES Instrument)
        else()
            set(LI_VST3_CATEGORIES Fx)
        endif()
    endif()
    if(NOT LI_DESCRIPTION)
        set(LI_DESCRIPTION "${LI_PRODUCT_NAME} by ${LOCKEDIN_COMPANY_NAME}")
    endif()

    # com.lockedin.<pluginname>: lowercase, letters and digits only
    string(TOLOWER "${LI_PRODUCT_NAME}" slug)
    string(REGEX REPLACE "[^a-z0-9]" "" slug "${slug}")

    set(is_synth FALSE)
    set(is_midi_fx FALSE)
    set(midi_in FALSE)
    set(midi_out FALSE)
    if(LI_INSTRUMENT)
        set(is_synth TRUE)
        set(midi_in TRUE)
    endif()
    if(LI_MIDI_EFFECT)
        set(is_midi_fx TRUE)
        set(midi_in TRUE)
        set(midi_out TRUE)
    endif()
    if(LI_NEEDS_MIDI_INPUT)
        set(midi_in TRUE)
    endif()
    if(LI_NEEDS_MIDI_OUTPUT)
        set(midi_out TRUE)
    endif()

    juce_add_plugin(${target}
        PRODUCT_NAME              "${LI_PRODUCT_NAME}"
        VERSION                   "${LI_VERSION}"
        DESCRIPTION               "${LI_DESCRIPTION}"
        COMPANY_NAME              "${LOCKEDIN_COMPANY_NAME}"
        COMPANY_WEBSITE           "${LOCKEDIN_WEBSITE}"
        COMPANY_EMAIL             "${LOCKEDIN_EMAIL}"
        BUNDLE_ID                 "${LOCKEDIN_BUNDLE_PREFIX}.${slug}"
        PLUGIN_MANUFACTURER_CODE  ${LOCKEDIN_MANUFACTURER_CODE}
        PLUGIN_CODE               ${LI_CODE}
        FORMATS                   VST3 AU            # AU is skipped automatically on Windows
        IS_SYNTH                  ${is_synth}
        IS_MIDI_EFFECT            ${is_midi_fx}
        NEEDS_MIDI_INPUT          ${midi_in}
        NEEDS_MIDI_OUTPUT         ${midi_out}
        EDITOR_WANTS_KEYBOARD_FOCUS FALSE
        VST3_CATEGORIES           ${LI_VST3_CATEGORIES}
        COPY_PLUGIN_AFTER_BUILD   ${LOCKEDIN_COPY_AFTER_BUILD})

    target_sources(${target} PRIVATE ${LI_SOURCES})

    # --- Assets + factory presets -> BinaryData, plus the lockedin::assets lookup table
    set(asset_files ${LI_ASSETS} ${LI_PRESETS})
    set(accessor "${CMAKE_CURRENT_BINARY_DIR}/${target}_LockedInAssets.cpp")

    if(asset_files)
        set(seen_names "")
        set(abs_assets "")
        foreach(f IN LISTS asset_files)
            get_filename_component(abs "${f}" ABSOLUTE)
            list(APPEND abs_assets "${abs}")
            if(NOT EXISTS "${abs}")
                message(FATAL_ERROR "lockedin_add_plugin(${target}): asset not found: ${f}")
            endif()
            get_filename_component(n "${f}" NAME)
            if(n IN_LIST seen_names)
                message(FATAL_ERROR "lockedin_add_plugin(${target}): two assets share the file name '${n}'. "
                                    "Assets are looked up by file name, so names must be unique.")
            endif()
            list(APPEND seen_names "${n}")
        endforeach()

        juce_add_binary_data(${target}_Assets
            HEADER_NAME BinaryData.h
            NAMESPACE   BinaryData
            SOURCES     ${abs_assets})
        set_target_properties(${target}_Assets PROPERTIES POSITION_INDEPENDENT_CODE ON)
        target_link_libraries(${target} PRIVATE ${target}_Assets)

        file(WRITE "${accessor}.tmp"
"// Generated by lockedin_add_plugin. Do not edit.
#include <lockedin_core/lockedin_core.h>
#include \"BinaryData.h\"

namespace lockedin::assets
{
    const char* getNamed (const juce::String& fileName, int& sizeInBytes)
    {
        for (int i = 0; i < BinaryData::namedResourceListSize; ++i)
            if (fileName == BinaryData::originalFilenames[i])
                return BinaryData::getNamedResource (BinaryData::namedResourceList[i], sizeInBytes);

        sizeInBytes = 0;
        return nullptr;
    }

    juce::StringArray list()
    {
        juce::StringArray names;
        for (int i = 0; i < BinaryData::namedResourceListSize; ++i)
            names.add (BinaryData::originalFilenames[i]);
        return names;
    }
}
")
    else()
        file(WRITE "${accessor}.tmp"
"// Generated by lockedin_add_plugin (no ASSETS given). Do not edit.
#include <lockedin_core/lockedin_core.h>

namespace lockedin::assets
{
    const char* getNamed (const juce::String&, int& sizeInBytes) { sizeInBytes = 0; return nullptr; }
    juce::StringArray list() { return {}; }
}
")
    endif()
    # Only touch the file when it changes, so builds stay incremental.
    configure_file("${accessor}.tmp" "${accessor}" COPYONLY)
    target_sources(${target} PRIVATE "${accessor}")

    # --- House compile settings
    target_compile_definitions(${target} PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
        JUCE_DISPLAY_SPLASH_SCREEN=0
        JUCE_REPORT_APP_USAGE=0
        JUCE_MODAL_LOOPS_PERMITTED=0)

    target_link_libraries(${target}
        PRIVATE
            lockedin::lockedin_core
        PUBLIC
            juce::juce_recommended_config_flags
            juce::juce_recommended_lto_flags
            juce::juce_recommended_warning_flags)

    # --- Customer INSTALL.md (packaged next to the plugins by CI)
    set(PLUGIN_NAME "${LI_PRODUCT_NAME}")
    set(PLUGIN_VERSION "${LI_VERSION}")
    set(COMPANY_NAME "${LOCKEDIN_COMPANY_NAME}")
    set(COMPANY_EMAIL "${LOCKEDIN_EMAIL}")
    set(COMPANY_WEBSITE "${LOCKEDIN_WEBSITE}")
    configure_file("${LOCKEDIN_CORE_DIR}/templates/INSTALL.md.in"
                   "${CMAKE_CURRENT_BINARY_DIR}/${target}_artefacts/INSTALL.md" @ONLY)

    if(APPLE AND NOT CMAKE_OSX_ARCHITECTURES MATCHES "arm64" OR APPLE AND NOT CMAKE_OSX_ARCHITECTURES MATCHES "x86_64")
        message(WARNING "${target}: not a universal build (CMAKE_OSX_ARCHITECTURES='${CMAKE_OSX_ARCHITECTURES}'). "
                        "include(locked-in-core/cmake/LockedInDefaults.cmake) before project().")
    endif()

    message(STATUS "Locked In plugin: ${LI_PRODUCT_NAME} ${LI_VERSION}  code=${LI_CODE}  id=${LOCKEDIN_BUNDLE_PREFIX}.${slug}")
endfunction()
