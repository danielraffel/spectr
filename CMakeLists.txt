cmake_minimum_required(VERSION 3.24)
project(Spectr VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# One authoritative imported-design viewport contract. The minimum preserves
# the 1320:860 aspect ratio (rounded to whole logical pixels); the maximum is
# exactly 2x the authored canvas.
set(SPECTR_DESIGN_WIDTH 1320)
set(SPECTR_DESIGN_HEIGHT 860)
set(SPECTR_DESIGN_MIN_WIDTH 800)
set(SPECTR_DESIGN_MIN_HEIGHT 521)
set(SPECTR_DESIGN_MAX_WIDTH 2640)
set(SPECTR_DESIGN_MAX_HEIGHT 1720)

# Build-time spectral profile seam. Balanced (8192/2048) is the product
# default. Live (1024/256) and Maximum (16384/4096) are alternate build
# profiles, not modes that can be switched dynamically at runtime. Separate
# build directories can benchmark them without changing source or pretending
# zoom creates FFT resolution.
set(SPECTR_FFT_SIZE 8192 CACHE STRING "Spectr WOLA FFT size")
set(SPECTR_ANALYSIS_HOP 2048 CACHE STRING "Spectr WOLA analysis hop")
set(_spectr_supported_fft_sizes 256 512 1024 2048 4096 8192 16384)
list(FIND _spectr_supported_fft_sizes "${SPECTR_FFT_SIZE}"
     _spectr_fft_size_index)
if(_spectr_fft_size_index EQUAL -1)
    message(FATAL_ERROR
        "SPECTR_FFT_SIZE=${SPECTR_FFT_SIZE} is unsupported; Pulp 0.801.0 "
        "requires a power of two from 256 through 16384")
endif()
math(EXPR _spectr_max_analysis_hop "${SPECTR_FFT_SIZE} / 2")
if(SPECTR_ANALYSIS_HOP LESS 1 OR
   SPECTR_ANALYSIS_HOP GREATER _spectr_max_analysis_hop)
    message(FATAL_ERROR
        "SPECTR_ANALYSIS_HOP=${SPECTR_ANALYSIS_HOP} is invalid for "
        "SPECTR_FFT_SIZE=${SPECTR_FFT_SIZE}; expected 1..${_spectr_max_analysis_hop}")
endif()
math(EXPR SPECTR_PROCESSING_LATENCY
     "${SPECTR_FFT_SIZE} + ${SPECTR_ANALYSIS_HOP}")

find_package(Pulp 0.801.0 REQUIRED)

# ── Core Spectr sources ────────────────────────────────────────────────
#
# Public headers live under include/spectr/.
# Implementation lives under src/.
# See planning/Spectr-V1-Build-Plan.md for the milestone sequence.

set(SPECTR_SOURCES
    src/spectr.cpp
    src/edit_engine.cpp
    src/editor_bridge.cpp
    src/pattern.cpp
    src/preset_format.cpp
    src/snapshot.cpp
    src/ui/editor_view.cpp
)

set(SPECTR_HEADERS
    include/spectr/spectr.hpp
    include/spectr/band_state.hpp
    include/spectr/viewport.hpp
    include/spectr/edit_modes.hpp
    include/spectr/editor_bridge.hpp
    include/spectr/preset_format.hpp
    include/spectr/snapshot.hpp
    include/spectr/ui/editor_view.hpp
)

# ── Source-preserving editor asset ─────────────────────────────────────
# Release 1 embeds the reviewed HTML prototype and hosts it through Pulp's
# native WebView bridge. Native-editor experiments remain outside this branch.
pulp_add_binary_data(spectr_editor_assets
    SOURCES resources/editor.html
    NAMESPACE spectr_editor
)

# ── Tests ──────────────────────────────────────────────────────────────

include(FetchContent)
FetchContent_Declare(Catch2
    GIT_REPOSITORY https://github.com/catchorg/Catch2.git
    GIT_TAG v3.7.1
)
FetchContent_MakeAvailable(Catch2)
include(CTest)
list(APPEND CMAKE_MODULE_PATH ${catch2_SOURCE_DIR}/extras)
include(Catch)

add_executable(Spectr-test
    test/test_spectr.cpp
    test/test_band_state.cpp
    test/test_viewport.cpp
    test/test_analyzer_bridge.cpp
    test/test_m4_state.cpp
    test/test_edit_engine.cpp
    test/test_pattern.cpp
    test/test_editor_bridge.cpp
    test/test_import_fidelity.cpp
    test/test_preset.cpp
    test/test_snapshot.cpp
    ${SPECTR_SOURCES}
)
target_link_libraries(Spectr-test PRIVATE
    Pulp::format Pulp::view
    Catch2::Catch2WithMain
    spectr_editor_assets
)
target_include_directories(Spectr-test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_compile_definitions(Spectr-test PRIVATE
    PULP_PLUGIN_DESIGN_W=${SPECTR_DESIGN_WIDTH}
    PULP_PLUGIN_DESIGN_H=${SPECTR_DESIGN_HEIGHT}
    PULP_PLUGIN_DESIGN_MIN_W=${SPECTR_DESIGN_MIN_WIDTH}
    PULP_PLUGIN_DESIGN_MIN_H=${SPECTR_DESIGN_MIN_HEIGHT}
    PULP_PLUGIN_DESIGN_MAX_W=${SPECTR_DESIGN_MAX_WIDTH}
    PULP_PLUGIN_DESIGN_MAX_H=${SPECTR_DESIGN_MAX_HEIGHT}
    SPECTR_FFT_SIZE=${SPECTR_FFT_SIZE}
    SPECTR_ANALYSIS_HOP=${SPECTR_ANALYSIS_HOP}
)
catch_discover_tests(Spectr-test)

# ── Plugin targets ─────────────────────────────────────────────────────

pulp_add_plugin(Spectr
    FORMATS         VST3 AU CLAP Standalone
    PLUGIN_NAME     "Spectr"
    BUNDLE_ID       "com.pulp.spectr"
    MANUFACTURER    "Pulp"
    VERSION         "1.0.0"
    CATEGORY        Effect
    PLUGIN_CODE     "Spec"
    MANUFACTURER_CODE "Pulp"
    AAX_PRODUCT_CODE "SpeP"
    AAX_NATIVE_CODE "Spec"
    DESIGN_WIDTH      ${SPECTR_DESIGN_WIDTH}
    DESIGN_HEIGHT     ${SPECTR_DESIGN_HEIGHT}
    DESIGN_MIN_WIDTH  ${SPECTR_DESIGN_MIN_WIDTH}
    DESIGN_MIN_HEIGHT ${SPECTR_DESIGN_MIN_HEIGHT}
    DESIGN_MAX_WIDTH  ${SPECTR_DESIGN_MAX_WIDTH}
    DESIGN_MAX_HEIGHT ${SPECTR_DESIGN_MAX_HEIGHT}
)

# Attach our sources + include path to each Spectr plugin target that pulp
# generated. Targets are named Spectr_VST3 / Spectr_AU / Spectr_CLAP /
# Spectr_Standalone.
foreach(fmt VST3 AU CLAP Standalone)
    set(tgt Spectr_${fmt})
    if(TARGET ${tgt})
        target_sources(${tgt} PRIVATE ${SPECTR_SOURCES})
        target_include_directories(${tgt} PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/include
        )
        target_link_libraries(${tgt} PRIVATE spectr_editor_assets)
        target_compile_definitions(${tgt} PRIVATE
            SPECTR_FFT_SIZE=${SPECTR_FFT_SIZE}
            SPECTR_ANALYSIS_HOP=${SPECTR_ANALYSIS_HOP}
        )
    endif()
endforeach()

# Artifact-level host proof: load the built CLAP and VST3 through Pulp::host
# and run real audio across each binary boundary. This is deliberately
# separate from Spectr-test's in-process Processor fixtures.
if(TARGET Spectr_CLAP AND TARGET Spectr_VST3 AND TARGET Pulp::host)
    add_executable(Spectr-artifact-test
        test/test_built_clap.cpp
        ${SPECTR_SOURCES})
    target_link_libraries(Spectr-artifact-test PRIVATE
        Pulp::host Pulp::format Pulp::view
        Catch2::Catch2WithMain spectr_editor_assets)
    target_include_directories(Spectr-artifact-test PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include)
    target_compile_definitions(Spectr-artifact-test PRIVATE
        SPECTR_TEST_CLAP_PATH="${CMAKE_BINARY_DIR}/CLAP/Spectr.clap"
        SPECTR_TEST_VST3_PATH="${CMAKE_BINARY_DIR}/VST3/Spectr.vst3"
        SPECTR_EXPECTED_LATENCY=${SPECTR_PROCESSING_LATENCY}
        SPECTR_FFT_SIZE=${SPECTR_FFT_SIZE}
        SPECTR_ANALYSIS_HOP=${SPECTR_ANALYSIS_HOP})
    add_dependencies(Spectr-artifact-test Spectr_CLAP Spectr_VST3)
    catch_discover_tests(Spectr-artifact-test)
endif()

# Standalone artifact proof: launch the exact packaged executable headlessly,
# capture its real GPU back buffer, and verify screenshot-only mode stays clear
# of the audio device. This is intentionally downstream of the CLI/DSP suite.
if(TARGET Spectr_Standalone AND TARGET Pulp::platform AND TARGET Pulp::view)
    add_executable(Spectr-standalone-artifact-test
        test/test_built_standalone.cpp)
    target_link_libraries(Spectr-standalone-artifact-test PRIVATE
        Pulp::platform Pulp::view Catch2::Catch2WithMain)
    target_compile_definitions(Spectr-standalone-artifact-test PRIVATE
        SPECTR_TEST_STANDALONE_PATH="$<TARGET_FILE:Spectr_Standalone>")
    add_dependencies(Spectr-standalone-artifact-test Spectr_Standalone)
    catch_discover_tests(Spectr-standalone-artifact-test)
endif()
