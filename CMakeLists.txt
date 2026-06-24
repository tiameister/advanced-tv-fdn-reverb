cmake_minimum_required(VERSION 3.22)
project(ProReverb LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ── DSP core library (JUCE-free) ──────────────────────────────────────────────
add_library(tvfdn_dsp STATIC
    DSP/FractionalDelayLine.cpp
    DSP/DattorroReverb.cpp
    DSP/ErTapNetwork.cpp
    DSP/ReverbEngine.cpp
)

target_include_directories(tvfdn_dsp PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

target_compile_options(tvfdn_dsp PRIVATE
    $<$<CXX_COMPILER_ID:MSVC>:/W4 /permissive->
    $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Wall -Wextra>
)

# ── Standalone DSP smoke tests (no JUCE needed) ───────────────────────────────
add_executable(tvfdn_smoke_test       Tests/smoke_test.cpp)
target_link_libraries(tvfdn_smoke_test PRIVATE tvfdn_dsp)

add_executable(reverb_smoke_test      Tests/reverb_smoke_test.cpp)
target_link_libraries(reverb_smoke_test PRIVATE tvfdn_dsp)

add_executable(decay_smoke_test       Tests/decay_smoke_test.cpp)
target_link_libraries(decay_smoke_test PRIVATE tvfdn_dsp)
# ── JUCE (Phase 4: VST3 plugin shell) ─────────────────────────────────────────
#
# JUCE is expected as a git submodule at <repo>/JUCE/.
# To add it:   git submodule add https://github.com/juce-framework/JUCE.git JUCE
#              git submodule update --init --recursive
#
# Fallback: set -DJUCE_DIR=/path/to/JUCE on the cmake command line to point
# at any JUCE 8.x installation, or let FetchContent download it automatically.

option(BUILD_VST3         "Build the VST3 plugin target (requires JUCE)" ON)
option(BUILD_STANDALONE   "Build the standalone desktop app (requires JUCE)" ON)
option(BUILD_BATCH_RENDER "Build batch_render WAV tool (requires JUCE)" ON)

if(BUILD_VST3 OR BUILD_STANDALONE OR BUILD_BATCH_RENDER)
    # ── Locate JUCE ───────────────────────────────────────────────────────────
    set(JUCE_DIR "" CACHE PATH "Optional: explicit path to a JUCE root directory")

    if(JUCE_DIR AND EXISTS "${JUCE_DIR}/CMakeLists.txt")
        message(STATUS "[ProReverb] Using JUCE from JUCE_DIR: ${JUCE_DIR}")
        add_subdirectory("${JUCE_DIR}" "${CMAKE_CURRENT_BINARY_DIR}/JUCE")
    elseif(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/JUCE/CMakeLists.txt")
        message(STATUS "[ProReverb] Using JUCE submodule.")
        add_subdirectory(JUCE)
    else()
        message(STATUS "[ProReverb] JUCE not found locally — fetching from GitHub (requires internet).")
        include(FetchContent)
        FetchContent_Declare(JUCE
            GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
            GIT_TAG        8.0.6
            GIT_SHALLOW    TRUE)
        FetchContent_MakeAvailable(JUCE)
    endif()

    # ── Plugin target ─────────────────────────────────────────────────────────
    if(BUILD_VST3 OR BUILD_STANDALONE)
    set(_plugin_formats "")
    if(BUILD_VST3)
        list(APPEND _plugin_formats VST3)
    endif()
    if(BUILD_STANDALONE)
        list(APPEND _plugin_formats Standalone)
    endif()

    juce_add_plugin(ProReverb
        COMPANY_NAME              "Tia Audio"
        COMPANY_WEBSITE           "https://tiaaudio.com"
        COMPANY_EMAIL             "info@tiaaudio.com"
        COMPANY_COPYRIGHT         "Copyright (c) 2026 Tia Audio. All rights reserved."
        PLUGIN_MANUFACTURER_CODE  TiaA
        PLUGIN_CODE               Tvrb
        PLUGIN_NAME               "TiaVerb"
        PRODUCT_NAME              "TiaVerb"
        DESCRIPTION               "Lush Dattorro plate/hall reverb with Web UI."
        VERSION                   "1.0.0"
        BUNDLE_ID                 com.tiaaudio.tiaverb
        FORMATS                   ${_plugin_formats}
        IS_SYNTH                  FALSE
        NEEDS_MIDI_INPUT          FALSE
        NEEDS_MIDI_OUTPUT         FALSE
        IS_MIDI_EFFECT            FALSE
        EDITOR_WANTS_KEYBOARD_FOCUS FALSE
        NEEDS_WEB_BROWSER         TRUE
        NEEDS_WEBVIEW2            TRUE
        COPY_PLUGIN_AFTER_BUILD   FALSE
        VST3_AUTO_MANIFEST        TRUE
        VST3_CATEGORIES           "Fx" "Reverb" "Spatial"
    )

    juce_generate_juce_header(ProReverb)

    target_sources(ProReverb PRIVATE
        Plugin/PluginProcessor.cpp
        Plugin/PluginEditor.cpp
        Plugin/PresetManager.cpp
        Plugin/WebUIEditor.cpp
    )

    # Re-generate WebUIContent.h from plugin_ui.html whenever the HTML changes.
    # Requires Python 3 in PATH.  If Python is unavailable the pre-generated
    # Plugin/WebUIContent.h (committed to source) is used as-is.
    find_program(PYTHON3 python3 NAMES python3 python)
    if(PYTHON3)
        add_custom_command(
            OUTPUT  ${CMAKE_CURRENT_SOURCE_DIR}/Plugin/WebUIContent.h
            COMMAND ${PYTHON3} ${CMAKE_CURRENT_SOURCE_DIR}/Tools/embed_html.py
            DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/WebUI/plugin_ui.html
                    ${CMAKE_CURRENT_SOURCE_DIR}/WebUI/fonts/Inter-400.woff2
                    ${CMAKE_CURRENT_SOURCE_DIR}/WebUI/fonts/Inter-500.woff2
                    ${CMAKE_CURRENT_SOURCE_DIR}/WebUI/fonts/Inter-600.woff2
                    ${CMAKE_CURRENT_SOURCE_DIR}/WebUI/fonts/Inter-700.woff2
                    ${CMAKE_CURRENT_SOURCE_DIR}/WebUI/fonts/Inter-800.woff2
                    ${CMAKE_CURRENT_SOURCE_DIR}/Tools/embed_html.py
            COMMENT "[ProReverb] Embedding plugin_ui.html → WebUIContent.h"
        )
        add_custom_target(EmbedWebUI DEPENDS
            ${CMAKE_CURRENT_SOURCE_DIR}/Plugin/WebUIContent.h)
        add_dependencies(ProReverb EmbedWebUI)
    endif()

    target_include_directories(ProReverb PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

    target_compile_features(ProReverb PRIVATE cxx_std_20)

    target_compile_options(ProReverb PRIVATE
        $<$<CXX_COMPILER_ID:MSVC>:/W4 /permissive->
        $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Wall -Wextra>
    )

    # WebUIEditor uses evaluateJavascript(), which requires WebView2 on Windows.
    if(WIN32)
        target_compile_definitions(ProReverb PRIVATE JUCE_USE_WIN_WEBVIEW2_WITH_STATIC_LINKING=1)
    endif()

    target_link_libraries(ProReverb
        PRIVATE
            tvfdn_dsp
            juce::juce_audio_processors
            juce::juce_audio_utils
            juce::juce_gui_basics
            juce::juce_gui_extra
        PUBLIC
            juce::juce_recommended_config_flags
            juce::juce_recommended_lto_flags
            juce::juce_recommended_warning_flags
    )

    if(BUILD_STANDALONE AND TARGET ProReverb_Standalone)
        add_custom_command(TARGET ProReverb_Standalone POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_SOURCE_DIR}/dist/Standalone"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                $<TARGET_FILE:ProReverb_Standalone>
                "${CMAKE_SOURCE_DIR}/dist/Standalone/TiaVerb.exe"
            COMMENT "[ProReverb] Copying Standalone -> dist/Standalone/TiaVerb.exe")
    endif()

    if(BUILD_VST3 AND BUILD_STANDALONE)
        message(STATUS "[ProReverb] VST3 + Standalone targets configured.")
    elseif(BUILD_VST3)
        message(STATUS "[ProReverb] VST3 target configured.")
    elseif(BUILD_STANDALONE)
        message(STATUS "[ProReverb] Standalone target configured.")
    endif()
    endif() # BUILD_VST3 OR BUILD_STANDALONE

    # ── batch_render CLI (JUCE audio I/O + tvfdn_dsp) ─────────────────────────
    if(BUILD_BATCH_RENDER)
    add_executable(batch_render Tools/batch_render.cpp)

    target_include_directories(batch_render PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

    target_compile_features(batch_render PRIVATE cxx_std_20)

    target_compile_options(batch_render PRIVATE
        $<$<CXX_COMPILER_ID:MSVC>:/W4 /permissive->
        $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Wall -Wextra>
    )

    target_link_libraries(batch_render
        PRIVATE
            tvfdn_dsp
            juce::juce_audio_formats
            juce::juce_audio_basics
            juce::juce_core
    )

    message(STATUS "[ProReverb] batch_render CLI target configured.")
    endif() # BUILD_BATCH_RENDER

endif() # BUILD_VST3 OR BUILD_STANDALONE OR BUILD_BATCH_RENDER

if(NOT BUILD_VST3 AND NOT BUILD_STANDALONE AND NOT BUILD_BATCH_RENDER)
    message(STATUS "[ProReverb] BUILD_VST3=OFF, BUILD_STANDALONE=OFF, BUILD_BATCH_RENDER=OFF — skipping JUCE targets.")
endif()
