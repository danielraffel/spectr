cmake_minimum_required(VERSION 3.24)
project(Spectr VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Pulp 0.87.0 REQUIRED)

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

# ── Design-importer opt-in (Phase 3 of import-validation roadmap) ──────
#
# When ON, the native-react editor bundle is produced from a freshly-
# imported design (via `pulp import-design --emit jsx`) rather than the
# hand-built editor.tsx. The build runs the importer over
# `resources/editor.html` → `native-react/editor.generated.tsx`, then
# bundles that with esbuild and embeds it the same way as the hand-built
# bundle. Requires SPECTR_NATIVE_EDITOR=ON, a `pulp` binary on PATH or
# at sdk_path/bin/pulp (parsed from pulp.toml), and the pulp SDK source
# tree (workaround for pulp issue #76 — `pulp import-design` requires
# its CWD to be a Pulp source root).
#
# See planning/spectr-import-execution-plan.md (in the Pulp repo) and
# pulp #1307 for the longer-term shape. Default OFF — only enable when
# validating the importer round-trip.
option(SPECTR_USE_IMPORTER
    "Re-generate native-react/editor.generated.tsx via pulp import-design before bundling (requires SPECTR_NATIVE_EDITOR=ON; see pulp issue #76)"
    OFF)

# When SPECTR_USE_IMPORTER=ON, resolve the pulp binary + source tree
# and wire the import + bundle steps before the embed.
if(SPECTR_USE_IMPORTER)
    if(NOT SPECTR_NATIVE_EDITOR)
        message(FATAL_ERROR
            "SPECTR_USE_IMPORTER=ON requires SPECTR_NATIVE_EDITOR=ON.\n"
            "Re-run cmake with -DSPECTR_NATIVE_EDITOR=ON -DSPECTR_USE_IMPORTER=ON.")
    endif()

    # Locate the `pulp-import-design` binary. We invoke it directly
    # rather than going through the Rust `pulp import-design` shim
    # because the Rust shim has been observed to dispatch to an older
    # cached delegate that doesn't honor --emit jsx (#1814 stale-cache
    # bug). pulp-import-design is the actual C++ implementation and
    # carries the full --emit jsx + --jsx-out support.
    find_program(PULP_EXECUTABLE pulp-import-design)
    if(NOT PULP_EXECUTABLE)
        file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/pulp.toml" _pulp_toml_lines)
        foreach(_line ${_pulp_toml_lines})
            if(_line MATCHES "^[ \t]*sdk_path[ \t]*=[ \t]*\"([^\"]+)\"")
                set(_PULP_SDK_PATH "${CMAKE_MATCH_1}")
            endif()
        endforeach()
        if(_PULP_SDK_PATH AND EXISTS "${_PULP_SDK_PATH}/bin/pulp-import-design")
            set(PULP_EXECUTABLE "${_PULP_SDK_PATH}/bin/pulp-import-design")
        endif()
    endif()
    if(NOT PULP_EXECUTABLE OR NOT EXISTS "${PULP_EXECUTABLE}")
        message(FATAL_ERROR
            "SPECTR_USE_IMPORTER=ON but 'pulp-import-design' executable not found.\n"
            "Add Pulp's bin/ to PATH, or set sdk_path correctly in pulp.toml.")
    endif()
    message(STATUS "SPECTR_USE_IMPORTER=ON; pulp-import-design: ${PULP_EXECUTABLE}")

    # Workaround for pulp issue #76: `pulp import-design` requires its
    # CWD to contain CMakeLists.txt + core/. Spectr doesn't satisfy
    # that. Resolve a Pulp source tree to use as WORKING_DIRECTORY.
    if(NOT DEFINED PULP_SDK_SRC_DIR)
        # Heuristic 1: sdk_path in pulp.toml is sometimes already the
        # source root.
        if(_PULP_SDK_PATH AND EXISTS "${_PULP_SDK_PATH}/core")
            set(PULP_SDK_SRC_DIR "${_PULP_SDK_PATH}")
        else()
            # Heuristic 2: well-known dev location.
            if(EXISTS "/Users/danielraffel/Code/pulp/core")
                set(PULP_SDK_SRC_DIR "/Users/danielraffel/Code/pulp")
            endif()
        endif()
    endif()
    if(NOT DEFINED PULP_SDK_SRC_DIR OR NOT EXISTS "${PULP_SDK_SRC_DIR}/core")
        message(FATAL_ERROR
            "SPECTR_USE_IMPORTER=ON: cannot find a Pulp source root with core/.\n"
            "Set -DPULP_SDK_SRC_DIR=/path/to/pulp/source on the cmake command line.\n"
            "Workaround for pulp issue #76; removable once that fix lands upstream.")
    endif()
    message(STATUS "SPECTR_USE_IMPORTER: Pulp source root: ${PULP_SDK_SRC_DIR}")

    set(SPECTR_IMPORT_INPUT  "${CMAKE_CURRENT_SOURCE_DIR}/resources/editor.html")
    set(SPECTR_IMPORT_OUTPUT "${CMAKE_CURRENT_SOURCE_DIR}/native-react/editor.generated.tsx")

    add_custom_command(
        OUTPUT  "${SPECTR_IMPORT_OUTPUT}"
        COMMAND "${PULP_EXECUTABLE}"
                --from claude
                --file "${SPECTR_IMPORT_INPUT}"
                --execute-bundle
                --emit jsx
                --jsx-out "${SPECTR_IMPORT_OUTPUT}"
        DEPENDS "${SPECTR_IMPORT_INPUT}"
                "${PULP_EXECUTABLE}"
        WORKING_DIRECTORY "${PULP_SDK_SRC_DIR}"
        COMMENT "pulp import-design: editor.html → editor.generated.tsx"
        VERBATIM)

    add_custom_target(spectr_import_design
        DEPENDS "${SPECTR_IMPORT_OUTPUT}")

    # Replicate `npm run build` from native-react/package.json directly
    # in CMake, with editor.generated.tsx as the entry instead of
    # editor.tsx. Avoids modifying package.json (which is a tracked source
    # file). Falls back to FATAL_ERROR if npm install hasn't been run.
    set(_NATIVE_REACT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/native-react")
    set(_ESBUILD     "${_NATIVE_REACT_DIR}/node_modules/.bin/esbuild")
    set(_SHIM_JS     "${_NATIVE_REACT_DIR}/shim.js")
    set(_DIST_BODY   "${_NATIVE_REACT_DIR}/dist/editor.body.js")
    set(_DIST_EDITOR "${_NATIVE_REACT_DIR}/dist/editor.js")

    if(NOT EXISTS "${_ESBUILD}")
        message(FATAL_ERROR
            "SPECTR_USE_IMPORTER=ON but esbuild not found at\n  ${_ESBUILD}\n"
            "Run: cd native-react && npm install")
    endif()

    find_package(Python3 COMPONENTS Interpreter REQUIRED)

    add_custom_command(
        OUTPUT  "${_DIST_EDITOR}"
        COMMAND "${_ESBUILD}"
                "${SPECTR_IMPORT_OUTPUT}"
                --bundle
                --format=iife
                --platform=neutral
                --target=es2020
                --jsx-factory=createElement
                --jsx-fragment=Fragment
                "--define:process.env.NODE_ENV=\"production\""
                "--outfile=${_DIST_BODY}"
        COMMAND "${Python3_EXECUTABLE}" -c
                "open('${_DIST_EDITOR}','wb').write(open('${_SHIM_JS}','rb').read()+open('${_DIST_BODY}','rb').read())"
        COMMAND "${CMAKE_COMMAND}" -E rm -f "${_DIST_BODY}"
        DEPENDS "${SPECTR_IMPORT_OUTPUT}"
                spectr_import_design
                "${_SHIM_JS}"
        WORKING_DIRECTORY "${_NATIVE_REACT_DIR}"
        COMMENT "esbuild: editor.generated.tsx → dist/editor.js (importer path)"
        VERBATIM)

    add_custom_target(spectr_native_bundle_importer
        DEPENDS "${_DIST_EDITOR}")
endif()

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

# When importer-path is active, make the asset-embedding step depend on
# the freshly-generated bundle. Hand-built path (default) is unaffected.
if(SPECTR_USE_IMPORTER)
    add_dependencies(spectr_editor_assets spectr_native_bundle_importer)
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
