cmake_minimum_required(VERSION 3.22)
project(ProReverb LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ── DSP core library (JUCE-free) ──────────────────────────────────────────────
add_library(tvfdn_dsp STATIC
    # Phase 1
    DSP/FractionalDelayLine.cpp
    DSP/TVFDNEngine.cpp
    # Phase 2
    DSP/VelvetNoiseGenerator.cpp
    DSP/EarlyReflections.cpp
    DSP/ReverbEngine.cpp
    # Phase 3
    DSP/BiquadFilter.cpp
    DSP/AbsorptionBank.cpp
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
# at any JUCE 7.x installation, or let FetchContent download it automatically.

option(BUILD_VST3 "Build the VST3 plugin target (requires JUCE)" ON)

if(BUILD_VST3)
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
            GIT_TAG        7.0.12
            GIT_SHALLOW    TRUE)
        FetchContent_MakeAvailable(JUCE)
    endif()

    # ── Plugin target ─────────────────────────────────────────────────────────
    juce_add_plugin(ProReverb
        COMPANY_NAME              "StudioDSP"
        PLUGIN_MANUFACTURER_CODE  Sdsp
        PLUGIN_CODE               Rvb1
        FORMATS                   VST3 Standalone
        PRODUCT_NAME              "Pro Reverb"
        IS_SYNTH                  FALSE
        NEEDS_MIDI_INPUT          FALSE
        NEEDS_MIDI_OUTPUT         FALSE
        IS_MIDI_EFFECT            FALSE
        EDITOR_WANTS_KEYBOARD_FOCUS FALSE
        COPY_PLUGIN_AFTER_BUILD   FALSE
        VST3_CATEGORIES           "Fx" "Reverb"
    )

    juce_generate_juce_header(ProReverb)

    target_sources(ProReverb PRIVATE
        Plugin/PluginProcessor.cpp
        Plugin/PluginEditor.cpp
    )

    target_include_directories(ProReverb PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

    target_compile_features(ProReverb PRIVATE cxx_std_20)

    target_compile_options(ProReverb PRIVATE
        $<$<CXX_COMPILER_ID:MSVC>:/W4 /permissive->
        $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Wall -Wextra>
    )

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

    # Convenience alias so IDE shows "ProReverb_VST3" clearly
    message(STATUS "[ProReverb] VST3 + Standalone targets configured.")
else()
    message(STATUS "[ProReverb] BUILD_VST3=OFF — skipping JUCE plugin target.")
endif()
