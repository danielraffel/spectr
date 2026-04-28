cmake_minimum_required(VERSION 3.24)
project(Spectr VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Pulp 0.49.0 REQUIRED)

# ── Core Spectr sources ────────────────────────────────────────────────
#
# Public headers live under include/spectr/.
# Implementation lives under src/.
# See planning/Spectr-V1-Build-Plan.md for the milestone sequence.

# ── Native React editor (opt-in) ───────────────────────────────────────
# When SPECTR_NATIVE_EDITOR is ON, Spectr's create_view() returns a
# NativeEditorView that renders an @pulp/react-built editor through the
# WidgetBridge directly (no WebView). Requires native-react/dist/editor.js
# to exist at configure time — produce it with `cd native-react && npm
# run build`. Default OFF — the WebView path remains the released editor.
option(SPECTR_NATIVE_EDITOR "Use the @pulp/react native editor instead of the WebView path" OFF)

set(SPECTR_SOURCES
    src/spectr.cpp
    src/block_fft_engine.cpp
    src/edit_engine.cpp
    src/editor_bridge.cpp
    src/pattern.cpp
    src/preset_format.cpp
    src/snapshot.cpp
    src/windowed_stft_engine.cpp
    src/ui/editor_view.cpp
    src/ui/native_editor_view.cpp
)

set(SPECTR_HEADERS
    include/spectr/spectr.hpp
    include/spectr/band_state.hpp
    include/spectr/viewport.hpp
    include/spectr/engine.hpp
    include/spectr/edit_modes.hpp
    include/spectr/editor_bridge.hpp
    include/spectr/preset_format.hpp
    include/spectr/snapshot.hpp
    include/spectr/windowed_stft_engine.hpp
    include/spectr/ui/editor_view.hpp
    include/spectr/ui/native_editor_view.hpp
)

# ── Editor assets embedded as C++ binary blobs ─────────────────────────
# editor.html is always embedded (used by the WebView path).
# editor.js is embedded only when SPECTR_NATIVE_EDITOR=ON; the file must
# exist at configure time.
set(SPECTR_EDITOR_ASSET_SOURCES resources/editor.html)
if(SPECTR_NATIVE_EDITOR)
    set(SPECTR_NATIVE_EDITOR_JS ${CMAKE_CURRENT_SOURCE_DIR}/native-react/dist/editor.js)
    if(NOT EXISTS ${SPECTR_NATIVE_EDITOR_JS})
        message(FATAL_ERROR
            "SPECTR_NATIVE_EDITOR=ON but ${SPECTR_NATIVE_EDITOR_JS} not found. "
            "Run: cd native-react && npm install && npm run build")
    endif()
    list(APPEND SPECTR_EDITOR_ASSET_SOURCES ${SPECTR_NATIVE_EDITOR_JS})
endif()
pulp_add_binary_data(spectr_editor_assets
    SOURCES ${SPECTR_EDITOR_ASSET_SOURCES}
    NAMESPACE spectr_editor
)
if(SPECTR_NATIVE_EDITOR)
    target_compile_definitions(spectr_editor_assets INTERFACE SPECTR_NATIVE_EDITOR=1)
    target_compile_definitions(spectr_editor_assets INTERFACE SPECTR_NATIVE_EDITOR_JS_EMBEDDED=1)
endif()

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
    test/test_edit_engine.cpp
    test/test_pattern.cpp
    test/test_editor_bridge.cpp
    test/test_preset.cpp
    test/test_snapshot.cpp
    test/test_windowed_stft_engine.cpp
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
        target_link_libraries(${tgt} PRIVATE spectr_editor_assets)
    endif()
endforeach()
