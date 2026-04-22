cmake_minimum_required(VERSION 3.24)
project(Spectr VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Pulp 0.34.0 REQUIRED)

# ── Core Spectr sources ────────────────────────────────────────────────
#
# Public headers live under include/spectr/.
# Implementation lives under src/.
# See planning/Spectr-V1-Build-Plan.md for the milestone sequence.

set(SPECTR_SOURCES
    src/spectr.cpp
    src/block_fft_engine.cpp
)

set(SPECTR_HEADERS
    include/spectr/spectr.hpp
    include/spectr/band_state.hpp
    include/spectr/viewport.hpp
    include/spectr/engine.hpp
    include/spectr/edit_modes.hpp
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
    test/test_engine_factory.cpp
    test/test_block_fft_engine.cpp
    test/test_analyzer_bridge.cpp
    test/test_m4_state.cpp
    ${SPECTR_SOURCES}
)
target_link_libraries(Spectr-test PRIVATE Pulp::view)
target_link_libraries(Spectr-test PRIVATE Pulp::format Catch2::Catch2WithMain)
target_include_directories(Spectr-test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
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
    endif()
endforeach()
