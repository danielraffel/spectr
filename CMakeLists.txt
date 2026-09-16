cmake_minimum_required(VERSION 3.24)
project(Spectr VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# The imported browser document owns its 1320 x 860 authored coordinate system.
# Pulp's fixed design viewport opens smaller and the browser scales that canvas
# into it. Keeping these concepts distinct makes AUv3's compile-time initial
# size agree with every adapter's runtime preferred size.
set(SPECTR_AUTHORED_WIDTH 1320)
set(SPECTR_AUTHORED_HEIGHT 860)
set(SPECTR_HOST_PREFERRED_WIDTH 990)
set(SPECTR_HOST_PREFERRED_HEIGHT 645)
set(SPECTR_HOST_MIN_WIDTH 792)
set(SPECTR_HOST_MIN_HEIGHT 516)
set(SPECTR_HOST_MAX_WIDTH 2640)
set(SPECTR_HOST_MAX_HEIGHT 1720)
foreach(_spectr_size_kind PREFERRED MIN MAX)
    math(EXPR _spectr_width_cross
         "${SPECTR_HOST_${_spectr_size_kind}_WIDTH} * ${SPECTR_AUTHORED_HEIGHT}")
    math(EXPR _spectr_height_cross
         "${SPECTR_HOST_${_spectr_size_kind}_HEIGHT} * ${SPECTR_AUTHORED_WIDTH}")
    if(NOT _spectr_width_cross EQUAL _spectr_height_cross)
        message(FATAL_ERROR
            "Spectr ${_spectr_size_kind} editor size must preserve the authored aspect")
    endif()
endforeach()

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
# SpectralFrameEngine uses a fixed-latency read cursor so host block boundaries
# cannot move the output. Its causal bound is one FFT frame plus one analysis
# hop (see SpectralFrameEngine::latency_samples()).
math(EXPR SPECTR_PROCESSING_LATENCY
     "${SPECTR_FFT_SIZE} + ${SPECTR_ANALYSIS_HOP}")

find_package(Pulp 0.801.0 REQUIRED)

# Product provenance is recomputed on every configure. Never preserve a SHA
# from a prior checkout in CMakeCache.txt: acceptance packages must identify
# the exact Spectr tree that produced them.
set(_spectr_source_git_sha "")
set(_spectr_source_git_dirty TRUE)
find_package(Git QUIET)
if(GIT_FOUND)
    execute_process(
        COMMAND "${GIT_EXECUTABLE}" -C "${CMAKE_CURRENT_SOURCE_DIR}"
                rev-parse --show-toplevel
        RESULT_VARIABLE _spectr_git_root_result
        OUTPUT_VARIABLE _spectr_git_root
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET)
    file(REAL_PATH "${CMAKE_CURRENT_SOURCE_DIR}" _spectr_source_root)
    if(_spectr_git_root_result EQUAL 0)
        file(REAL_PATH "${_spectr_git_root}" _spectr_git_root)
    endif()
endif()
if(GIT_FOUND AND _spectr_git_root_result EQUAL 0 AND
   _spectr_git_root STREQUAL _spectr_source_root)
    execute_process(
        COMMAND "${GIT_EXECUTABLE}" -C "${CMAKE_CURRENT_SOURCE_DIR}"
                rev-parse --verify HEAD
        RESULT_VARIABLE _spectr_git_sha_result
        OUTPUT_VARIABLE _spectr_source_git_sha
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET)
    string(LENGTH "${_spectr_source_git_sha}" _spectr_source_git_sha_length)
    if(_spectr_git_sha_result EQUAL 0 AND
       _spectr_source_git_sha MATCHES "^[0-9a-f]+$" AND
       _spectr_source_git_sha_length EQUAL 40)
        execute_process(
            COMMAND "${GIT_EXECUTABLE}" -C "${CMAKE_CURRENT_SOURCE_DIR}"
                    status --porcelain --untracked-files=normal
            RESULT_VARIABLE _spectr_git_status_result
            OUTPUT_VARIABLE _spectr_git_status
            OUTPUT_STRIP_TRAILING_WHITESPACE
            ERROR_QUIET)
        if(_spectr_git_status_result EQUAL 0 AND _spectr_git_status STREQUAL "")
            set(_spectr_source_git_dirty FALSE)
        endif()
    else()
        set(_spectr_source_git_sha "")
    endif()
endif()
set(SPECTR_SOURCE_GIT_SHA "${_spectr_source_git_sha}" CACHE INTERNAL
    "Exact Spectr source Git SHA recomputed at configure time" FORCE)
set(SPECTR_SOURCE_GIT_DIRTY "${_spectr_source_git_dirty}" CACHE INTERNAL
    "Whether the configured Spectr source tree is dirty or unverifiable" FORCE)

set(SPECTR_EXPECTED_PRODUCT_GIT_SHA "" CACHE STRING
    "Exact clean Spectr Git SHA required for an accepted Release build")
if(NOT SPECTR_EXPECTED_PRODUCT_GIT_SHA STREQUAL "")
    if(NOT CMAKE_BUILD_TYPE STREQUAL "Release")
        message(FATAL_ERROR
            "SPECTR_EXPECTED_PRODUCT_GIT_SHA admits Release builds only")
    endif()
    string(LENGTH "${SPECTR_EXPECTED_PRODUCT_GIT_SHA}"
           _spectr_expected_product_git_sha_length)
    if(NOT SPECTR_EXPECTED_PRODUCT_GIT_SHA MATCHES "^[0-9a-f]+$" OR
       NOT _spectr_expected_product_git_sha_length EQUAL 40)
        message(FATAL_ERROR
            "SPECTR_EXPECTED_PRODUCT_GIT_SHA must be one exact lowercase 40-character Git SHA")
    endif()
    if(NOT SPECTR_SOURCE_GIT_SHA STREQUAL SPECTR_EXPECTED_PRODUCT_GIT_SHA)
        message(FATAL_ERROR
            "Resolved Spectr source SHA is '${SPECTR_SOURCE_GIT_SHA}', expected "
            "'${SPECTR_EXPECTED_PRODUCT_GIT_SHA}'")
    endif()
    if(SPECTR_SOURCE_GIT_DIRTY)
        message(FATAL_ERROR
            "Accepted Spectr Release source is dirty or unverifiable")
    endif()
endif()

set(SPECTR_PULP_SDK_SOURCE_GIT_SHA_VALUE "")
if(DEFINED PULP_SDK_SOURCE_GIT_SHA)
    string(LENGTH "${PULP_SDK_SOURCE_GIT_SHA}"
           _spectr_pulp_sdk_source_git_sha_length)
    if(PULP_SDK_SOURCE_GIT_SHA MATCHES "^[0-9a-f]+$" AND
       _spectr_pulp_sdk_source_git_sha_length EQUAL 40)
        set(SPECTR_PULP_SDK_SOURCE_GIT_SHA_VALUE
            "${PULP_SDK_SOURCE_GIT_SHA}")
    endif()
endif()

# Native cutover candidates are admitted by immutable SDK provenance, not by
# whichever compatible Pulp package happens to be first on CMake's search path.
# The shipping/acceptance command passes the exact merged Pulp SHA here; normal
# source consumers may leave it empty when they do not need that stronger gate.
set(SPECTR_EXPECTED_PULP_SDK_SHA "" CACHE STRING
    "Exact source Git SHA required from the resolved Pulp SDK provenance")
if(NOT SPECTR_EXPECTED_PULP_SDK_SHA STREQUAL "")
    string(LENGTH "${SPECTR_EXPECTED_PULP_SDK_SHA}"
           _spectr_expected_pulp_sdk_sha_length)
    if(NOT SPECTR_EXPECTED_PULP_SDK_SHA MATCHES "^[0-9a-f]+$" OR
       NOT _spectr_expected_pulp_sdk_sha_length EQUAL 40)
        message(FATAL_ERROR
            "SPECTR_EXPECTED_PULP_SDK_SHA must be one exact lowercase 40-character Git SHA")
    endif()
    if(NOT DEFINED PULP_SDK_SOURCE_GIT_SHA OR
       NOT PULP_SDK_SOURCE_GIT_SHA STREQUAL SPECTR_EXPECTED_PULP_SDK_SHA)
        message(FATAL_ERROR
            "Resolved Pulp SDK source SHA is '${PULP_SDK_SOURCE_GIT_SHA}', expected "
            "'${SPECTR_EXPECTED_PULP_SDK_SHA}'; refusing an older or substituted SDK")
    endif()
endif()

# ── Core Spectr sources ────────────────────────────────────────────────
#
# Public headers live under include/spectr/.
# Implementation lives under src/.
# See planning/Spectr-V1-Build-Plan.md for the milestone sequence.

set(SPECTR_SOURCES
    src/spectr.cpp
    src/mask_renderer.cpp
    src/param_surface.cpp
    src/edit_engine.cpp
    src/editor_authority.cpp
    src/editor_bridge.cpp
    src/pattern.cpp
    src/preset_format.cpp
    src/snapshot.cpp
    src/ui/editor_view.cpp
)

set(SPECTR_HEADERS
    include/spectr/spectr.hpp
    include/spectr/mask_renderer.hpp
    include/spectr/param_surface.hpp
    include/spectr/band_state.hpp
    include/spectr/viewport.hpp
    include/spectr/edit_modes.hpp
    include/spectr/editor_authority.hpp
    include/spectr/editor_resize.hpp
    include/spectr/editor_bridge.hpp
    include/spectr/preset_format.hpp
    include/spectr/snapshot.hpp
    include/spectr/ui/editor_view.hpp
)

# ── Native-editor N1 vertical slice ──────────────────────────────────
#
# This is a separate browser-free graph. NATIVE_UI selects Pulp's native
# scripted Skia/Dawn link floor, while this source list excludes editor_view.cpp
# and the HTML binary-data target. A native editor failure returns an empty GPU
# root rather than silently selecting AutoUi or the WebView baseline.
set(SPECTR_NATIVE_SOURCES
    src/spectr.cpp
    src/mask_renderer.cpp
    src/param_surface.cpp
    src/edit_engine.cpp
    src/editor_authority.cpp
    src/editor_bridge.cpp
    src/pattern.cpp
    src/preset_format.cpp
    src/snapshot.cpp
    src/ui/native_editor.cpp
)

# Shipping Spectr is a native cutover consumer. Requiring the dedicated target
# makes an older compatibility SDK fail during configure instead of silently
# selecting a less capable rendering surface.
if(TARGET Pulp::view-native)
    set(SPECTR_NATIVE_VIEW_TARGET Pulp::view-native)
else()
    message(FATAL_ERROR
        "Resolved Pulp SDK exports no Pulp::view-native target; refusing the legacy view fallback")
endif()

set(SPECTR_NATIVE_ASSET_SOURCES
    native-ui/materialized/runtime.js
    native-ui/materialized/materialized-document.runtime.json
    native-ui/materialized/design.js
    # The help overlay's copy, deliberately its own asset rather than text
    # inside the materialized document. That document is a checked-in artifact
    # neither generator on main can rebuild, so copy living there would make
    # every wording change a surgical patch against an 800 KB single-line blob.
    # native_editor.cpp writes this beside the others and evaluates it after
    # design.js; it assigns one string to globalThis.SPECTR_HELP_TEXT.
    native-ui/materialized/help-content.js
)
foreach(_forbidden_native_asset
        resources/editor.html
        native-ui/materialized/editor.ir.json)
    list(FIND SPECTR_NATIVE_ASSET_SOURCES "${_forbidden_native_asset}"
         _forbidden_native_asset_index)
    if(NOT _forbidden_native_asset_index EQUAL -1)
        message(FATAL_ERROR
            "Shipping Spectr must not embed ${_forbidden_native_asset}")
    endif()
endforeach()

pulp_add_binary_data(spectr_native_assets
    SOURCES ${SPECTR_NATIVE_ASSET_SOURCES}
    NAMESPACE spectr_native
)

# Test-only automation-perf fixture (SPECTR_AUTOMATION_PERF_FIXTURE env var,
# read in Spectr::tick_native_analyzer_) forces an extra host-automation
# projection each tick to stress the live-state dispatch path under load.
# Default OFF so the literal env-var name and its branch are compiled out of
# every shipping and default test target; opt in explicitly
# (-DSPECTR_ENABLE_PERF_FIXTURES=ON) only for a dedicated perf-fixture build.
option(SPECTR_ENABLE_PERF_FIXTURES
       "Compile in the test-only SPECTR_AUTOMATION_PERF_FIXTURE tick hook" OFF)

function(spectr_configure_native_target target)
    target_include_directories(${target} PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    )
    target_compile_definitions(${target} PRIVATE
        SPECTR_NATIVE_EDITOR=1
        $<$<BOOL:${SPECTR_ENABLE_PERF_FIXTURES}>:SPECTR_ENABLE_PERF_FIXTURES=1>
        SPECTR_AUTHORED_DESIGN_W=${SPECTR_AUTHORED_WIDTH}
        SPECTR_AUTHORED_DESIGN_H=${SPECTR_AUTHORED_HEIGHT}
        SPECTR_HOST_PREFERRED_W=${SPECTR_HOST_PREFERRED_WIDTH}
        SPECTR_HOST_PREFERRED_H=${SPECTR_HOST_PREFERRED_HEIGHT}
        SPECTR_HOST_MIN_W=${SPECTR_HOST_MIN_WIDTH}
        SPECTR_HOST_MIN_H=${SPECTR_HOST_MIN_HEIGHT}
        SPECTR_HOST_MAX_W=${SPECTR_HOST_MAX_WIDTH}
        SPECTR_HOST_MAX_H=${SPECTR_HOST_MAX_HEIGHT}
        SPECTR_FFT_SIZE=${SPECTR_FFT_SIZE}
        SPECTR_ANALYSIS_HOP=${SPECTR_ANALYSIS_HOP}
        SPECTR_PULP_SDK_SOURCE_GIT_SHA="${SPECTR_PULP_SDK_SOURCE_GIT_SHA_VALUE}"
        SPECTR_PULP_SDK_PROVENANCE_EXACT=$<BOOL:${SPECTR_PULP_SDK_SOURCE_GIT_SHA_VALUE}>
        SPECTR_PRODUCT_GIT_SHA="${SPECTR_SOURCE_GIT_SHA}"
        SPECTR_PRODUCT_GIT_DIRTY=$<BOOL:${SPECTR_SOURCE_GIT_DIRTY}>
    )
endfunction()

option(SPECTR_NATIVE_PREVIEW_IDENTITY
       "Build the native editor with a collision-free local preview identity" OFF)
if(SPECTR_NATIVE_PREVIEW_IDENTITY)
    set(SPECTR_NATIVE_PLUGIN_TARGET SpectrNativePreview)
    set(SPECTR_NATIVE_PLUGIN_NAME "Spectr Native Preview")
    set(SPECTR_NATIVE_BUNDLE_ID "com.pulp.spectr.native-preview")
    set(SPECTR_NATIVE_PLUGIN_CODE "SpNP")
else()
    set(SPECTR_NATIVE_PLUGIN_TARGET Spectr)
    set(SPECTR_NATIVE_PLUGIN_NAME "Spectr")
    set(SPECTR_NATIVE_BUNDLE_ID "com.pulp.spectr")
    set(SPECTR_NATIVE_PLUGIN_CODE "Spec")
endif()
set(SPECTR_NATIVE_CORE_TARGET "${SPECTR_NATIVE_PLUGIN_TARGET}_Core")
set(SPECTR_NATIVE_STANDALONE_TARGET "${SPECTR_NATIVE_PLUGIN_TARGET}_Standalone")
set(SPECTR_NATIVE_VST3_TARGET "${SPECTR_NATIVE_PLUGIN_TARGET}_VST3")
set(SPECTR_NATIVE_AU_TARGET "${SPECTR_NATIVE_PLUGIN_TARGET}_AU")
set(SPECTR_NATIVE_CLAP_TARGET "${SPECTR_NATIVE_PLUGIN_TARGET}_CLAP")

pulp_add_plugin(${SPECTR_NATIVE_PLUGIN_TARGET}
    NATIVE_UI
    FORMATS VST3 AU CLAP Standalone
    SOURCES ${SPECTR_NATIVE_SOURCES}
    PROCESSOR_FACTORY spectr::create_spectr
    PLUGIN_NAME "${SPECTR_NATIVE_PLUGIN_NAME}"
    BUNDLE_ID "${SPECTR_NATIVE_BUNDLE_ID}"
    MANUFACTURER "Pulp"
    PLUGIN_CODE "${SPECTR_NATIVE_PLUGIN_CODE}"
    MANUFACTURER_CODE "Pulp"
    VERSION "1.0.0"
    CATEGORY Effect
    DESIGN_WIDTH ${SPECTR_AUTHORED_WIDTH}
    DESIGN_HEIGHT ${SPECTR_AUTHORED_HEIGHT}
    DESIGN_MIN_WIDTH ${SPECTR_HOST_MIN_WIDTH}
    DESIGN_MIN_HEIGHT ${SPECTR_HOST_MIN_HEIGHT}
    DESIGN_MAX_WIDTH ${SPECTR_HOST_MAX_WIDTH}
    DESIGN_MAX_HEIGHT ${SPECTR_HOST_MAX_HEIGHT})
target_link_libraries(${SPECTR_NATIVE_CORE_TARGET} PUBLIC
    ${SPECTR_NATIVE_VIEW_TARGET} spectr_native_assets)
foreach(_spectr_native_target
        ${SPECTR_NATIVE_CORE_TARGET}
        ${SPECTR_NATIVE_STANDALONE_TARGET}
        ${SPECTR_NATIVE_VST3_TARGET}
        ${SPECTR_NATIVE_AU_TARGET}
        ${SPECTR_NATIVE_CLAP_TARGET})
    if(TARGET ${_spectr_native_target})
        spectr_configure_native_target(${_spectr_native_target})
        if(SPECTR_NATIVE_PREVIEW_IDENTITY)
            target_compile_definitions(${_spectr_native_target} PRIVATE
                SPECTR_NATIVE_PREVIEW_IDENTITY=1)
        endif()
    endif()
endforeach()

# ── Product icon ───────────────────────────────────────────────────────
# Spectr's own five-bar spectrum mark, on the dark ground the editor chrome
# already uses. tools/icon/generate_icon.py rebuilds every asset from the
# logo geometry; resources/icon/spectr-icon.svg is the vector source.
#
# The icon is attached to all four macOS bundles. The plugin formats carry
# it for artifact completeness, not for display: measured on macOS 26,
# CFBundleIconFile is inert on a non-.app bundle. A .vst3/.component/.clap
# that declares the key and ships the .icns still draws Finder's generic
# bundle icon, and third-party plugins that declare it (Roland's, and
# others in /Library/Audio/Plug-Ins) draw generic too. The only mechanism
# that does render is a custom Finder icon -- an `Icon\r` resource fork
# plus a com.apple.FinderInfo xattr on the bundle directory -- which
# `codesign --strict` rejects as "resource fork, Finder information, or
# similar detritus not allowed". Spectr ships signed and notarized, so that
# route is deliberately not taken. Nothing here promises a visible icon;
# it puts the asset where Apple documents it belongs.
#
# No DAW reads the key either -- including AVAudioUnitComponent.icon, the
# one plausible host-side route, which returns a generic image for every
# component that declares it. The mechanism that does put artwork in a host
# is the VST3 Snapshots convention: a PNG under Contents/Resources/Snapshots
# named for the processor's class UID, which Cubase and Nuendo consume.
# Spectr ships none yet; that is the path to take if branding is wanted.
#
# The committed .icns is attached directly rather than through
# pulp_app_icon(). That helper takes a single 1024 PNG and downscales it
# with sips, whose Lanczos kernel overshoots on the tile edge: at 16px it
# rings the border with bright halos and smears the five bars into one
# blob. resources/icon/Spectr.icns instead carries a render laid out on
# each size's own pixel grid. Both mechanisms are stock CMake bundle
# wiring; this one just supplies the finished asset.
if(APPLE)
    set_source_files_properties(resources/icon/Spectr.icns PROPERTIES
        MACOSX_PACKAGE_LOCATION Resources)
    foreach(_spectr_icon_target
            ${SPECTR_NATIVE_STANDALONE_TARGET}
            ${SPECTR_NATIVE_VST3_TARGET}
            ${SPECTR_NATIVE_AU_TARGET}
            ${SPECTR_NATIVE_CLAP_TARGET})
        if(TARGET ${_spectr_icon_target})
            target_sources(${_spectr_icon_target} PRIVATE
                resources/icon/Spectr.icns)
            set_target_properties(${_spectr_icon_target} PROPERTIES
                MACOSX_BUNDLE_ICON_FILE "Spectr.icns")
        endif()
    endforeach()
endif()


foreach(_forbidden_native_source
        src/ui/editor_view.cpp
        resources/editor.html)
    list(FIND SPECTR_NATIVE_SOURCES "${_forbidden_native_source}"
         _forbidden_native_source_index)
    if(NOT _forbidden_native_source_index EQUAL -1)
        message(FATAL_ERROR
            "Shipping Spectr must not compile ${_forbidden_native_source}")
    endif()
endforeach()

# ── Source-preserving editor asset ─────────────────────────────────────
# The reviewed Release 1 HTML remains available as an explicit, independently
# buildable oracle. It is never part of the default shipping artifact graph.
option(SPECTR_BUILD_WEBVIEW_REFERENCE
       "Build the frozen WebView comparison artifacts" OFF)
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

# Does the resolved Pulp SDK actually ship the WebView editor path? A cutover SDK
# is built with PULP_BUILD_WEBVIEW=OFF, which removes EditorBridge::attach_webview,
# WebViewPanel::create and the embedded-resource fetcher. Link them anyway and the
# test binaries fail with undefined arm64 symbols while the shipping plugin targets
# — which build from SPECTR_NATIVE_SOURCES — stay green, so the breakage looks like
# a product failure when it is only the legacy test graph.
set(SPECTR_SDK_HAS_WEBVIEW OFF)
if(TARGET Pulp::view)
    get_target_property(_spectr_pulp_view_defs Pulp::view INTERFACE_COMPILE_DEFINITIONS)
    if(_spectr_pulp_view_defs MATCHES "PULP_BUILD_WEBVIEW=1")
        set(SPECTR_SDK_HAS_WEBVIEW ON)
    endif()
    unset(_spectr_pulp_view_defs)
endif()

if(SPECTR_SDK_HAS_WEBVIEW)
    set(SPECTR_TEST_SOURCES ${SPECTR_SOURCES})
    set(SPECTR_TEST_WEBVIEW_CASES test/test_editor_bridge.cpp)
    # spectr_native_assets is an embedded-data object library, not the native
    # editor implementation, so it links on a WebView SDK too. The materialized
    # document/runtime.js contract cases read those blobs and nothing else; gate
    # them on the blobs being present, never on which editor path the SDK ships,
    # or they silently compile out on exactly the SDK the acceptance gate uses.
    set(SPECTR_TEST_ASSET_TARGET spectr_native_assets spectr_editor_assets)
else()
    # Browser-free SDK: use the same source list the shipping plugins use and drop
    # the one case that exercises the WebView editor directly.
    set(SPECTR_TEST_SOURCES ${SPECTR_NATIVE_SOURCES})
    set(SPECTR_TEST_WEBVIEW_CASES)
    # The native source list carries native_editor.cpp, whose generated JS/document
    # blobs live in spectr_native_assets. Keep spectr_editor_assets linked too: the
    # import-fidelity cases compare the native render against editor.html, and that
    # is a TEST FIXTURE. N5 forbids editor.html inside a shipping bundle, not inside
    # a test binary, so dropping it here would delete the browser-oracle coverage
    # this cutover depends on while proving nothing about the product.
    set(SPECTR_TEST_ASSET_TARGET spectr_native_assets spectr_editor_assets)
endif()

add_executable(Spectr-test
    test/test_spectr.cpp
    test/test_band_state.cpp
    test/test_viewport.cpp
    test/test_analyzer_bridge.cpp
    test/test_m4_state.cpp
    test/test_edit_engine.cpp
    test/test_editor_resize.cpp
    test/test_pattern.cpp
    ${SPECTR_TEST_WEBVIEW_CASES}
    test/test_import_fidelity.cpp
    test/test_preset.cpp
    test/test_snapshot.cpp
    test/test_param_surface.cpp
    test/test_param_macro.cpp
    test/test_mode_bridge.cpp
    test/test_mask_renderer.cpp
    test/test_tracking_transition.cpp
    test/test_tracking_transition_sweep.cpp
    test/test_render_mode.cpp
    ${SPECTR_TEST_SOURCES}
)
target_link_libraries(Spectr-test PRIVATE
    Pulp::format Pulp::view
    Catch2::Catch2WithMain
    ${SPECTR_TEST_ASSET_TARGET}
)
target_include_directories(Spectr-test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_compile_definitions(Spectr-test PRIVATE
    $<$<NOT:$<BOOL:${SPECTR_SDK_HAS_WEBVIEW}>>:SPECTR_NATIVE_EDITOR=1>
    SPECTR_NATIVE_ASSETS=1
    SPECTR_AUTHORED_DESIGN_W=${SPECTR_AUTHORED_WIDTH}
    SPECTR_AUTHORED_DESIGN_H=${SPECTR_AUTHORED_HEIGHT}
    SPECTR_HOST_PREFERRED_W=${SPECTR_HOST_PREFERRED_WIDTH}
    SPECTR_HOST_PREFERRED_H=${SPECTR_HOST_PREFERRED_HEIGHT}
    SPECTR_HOST_MIN_W=${SPECTR_HOST_MIN_WIDTH}
    SPECTR_HOST_MIN_H=${SPECTR_HOST_MIN_HEIGHT}
    SPECTR_HOST_MAX_W=${SPECTR_HOST_MAX_WIDTH}
    SPECTR_HOST_MAX_H=${SPECTR_HOST_MAX_HEIGHT}
    PULP_PLUGIN_DESIGN_W=${SPECTR_HOST_PREFERRED_WIDTH}
    PULP_PLUGIN_DESIGN_H=${SPECTR_HOST_PREFERRED_HEIGHT}
    PULP_PLUGIN_DESIGN_MIN_W=${SPECTR_HOST_MIN_WIDTH}
    PULP_PLUGIN_DESIGN_MIN_H=${SPECTR_HOST_MIN_HEIGHT}
    PULP_PLUGIN_DESIGN_MAX_W=${SPECTR_HOST_MAX_WIDTH}
    PULP_PLUGIN_DESIGN_MAX_H=${SPECTR_HOST_MAX_HEIGHT}
    SPECTR_FFT_SIZE=${SPECTR_FFT_SIZE}
    SPECTR_ANALYSIS_HOP=${SPECTR_ANALYSIS_HOP}
    SPECTR_PULP_SDK_SOURCE_GIT_SHA="${SPECTR_PULP_SDK_SOURCE_GIT_SHA_VALUE}"
    SPECTR_PULP_SDK_PROVENANCE_EXACT=$<BOOL:${SPECTR_PULP_SDK_SOURCE_GIT_SHA_VALUE}>
    SPECTR_PRODUCT_GIT_SHA="${SPECTR_SOURCE_GIT_SHA}"
    SPECTR_PRODUCT_GIT_DIRTY=$<BOOL:${SPECTR_SOURCE_GIT_DIRTY}>
)
catch_discover_tests(Spectr-test)

# The recall rule's negative controls.
#
# "A project saved before the mode existed reopens in Mixing" is the contract
# that makes this feature safe to ship, and a contract nobody has watched
# reject the defect it forbids is not a contract. SPECTR_RENDER_MODE_PLANT
# reinstates exactly that defect in the product -- absence resolving to some
# other mode instead of the authored one -- and the two cases below invert
# their own verdict when they see the plant, so each row is green only when
# the contract OBSERVES the defect.
#
# The inversion lives inside the binary rather than in WILL_FAIL because
# WILL_FAIL accepts any non-zero exit: a usage error, a missing symbol, or a
# filter that matches nothing would all have satisfied it while proving
# nothing. Verified by hand before registering: unplanted the case exits 0
# with 9 assertions, planted it exits 0 with 3 (the inverted branch), and a
# filter matching no case exits 2 rather than 0 -- so a renamed test fails
# this row instead of silently passing it.
add_test(NAME Spectr-render-mode-migration-negative-control
    COMMAND ${CMAKE_COMMAND} -E env
        SPECTR_RENDER_MODE_PLANT=migration-adopts-other-mode
        $<TARGET_FILE:Spectr-test>
        "A project authored before the render mode existed reopens Mixing")
set_tests_properties(Spectr-render-mode-migration-negative-control PROPERTIES
    TIMEOUT 120)

# The same plant against the unplanted contract, so the pair proves the rule
# holds AND can be broken. Without this row the one above could be green
# because the contract is inert rather than because the plant was caught.
add_test(NAME Spectr-render-mode-migration-holds
    COMMAND $<TARGET_FILE:Spectr-test>
        "A project authored before the render mode existed reopens Mixing")
set_tests_properties(Spectr-render-mode-migration-holds PROPERTIES
    TIMEOUT 120)

# ── Tracking transition shaping: six gates, each with its inversion ────────
#
# Each plant substitutes a DEFECT into the subject of one gate and requires the
# gate to reject it. The inversion lives inside the binary rather than in
# WILL_FAIL, which accepts any non-zero exit: a usage error, a missing symbol,
# or a filter matching nothing would all satisfy WILL_FAIL while measuring
# nothing.
#
# SPECTR_TRANSITION_PLANT_REQUIRED closes the mirror of that hole. Without it a
# plant row is vacuously green the moment its plant variable is stripped or
# misspelled -- `planted()` returns false, the ORDINARY path runs, and the row
# exits 0 having proved nothing. With it, the test refuses to pass unless its
# plant actually arrived. Verified: stripping or mistyping the plant on any of
# the six rows exits 42 rather than 0, and a filter matching no case exits 2.
#
# Verified in BOTH directions before registering. With its defect removed each
# row exits 42 with exactly one failed assertion, so none of them is an
# inversion that cannot fail; a filter matching no case exits 2 rather than 0,
# so a renamed test reddens these rows instead of silently passing them.
#
# The plants, and what each would let through if it were absent:
#   brick-wall         the design that shipped before this change, as its own
#                      committed measured numbers. Its three rules are asserted
#                      SEPARATELY, because one REQUIRE_FALSE over the
#                      conjunction is a single assertion that whichever rule
#                      happened to trip would satisfy, leaving the other two
#                      undemonstrated.
#   narrow-regression  a real 5 dB regression injected into band 6 -- the
#                      clamped regime, where shaping can change a band without
#                      room to deepen it, and the only place this change is
#                      known to cost anything.
#   roomy-band         swaps the field for one whose bands have room, proving
#                      the "left exactly as drawn" comparison can observe a
#                      difference at all.
#   eq-drift           adds 1 dB to the drawn reference, proving the ordinary-EQ
#                      gate observes a drift rather than agreeing with whatever
#                      it measured.
#   detection-floor-clamped
#                      an instrument that bottoms out at -75 dB -- above the
#                      -80 dB depth gate -- proving the detection-floor control
#                      rejects a path that cannot see past the line it guards.
#   whole-bin-edges    places every band edge on a whole design bin, through the
#                      shaping function's OWN quantum argument rather than a
#                      rounding step written beside it, so the control is the
#                      product at a different argument. That is the placement
#                      this design replaced, and it reads 6.96 Hz of peak
#                      frequency modulation against 1.24 Hz for sub-bin
#                      placement, so the gate is not merely passing on a number
#                      it cannot move.
#   symmetric-kept     substitutes the surviving kept-band widths the SYMMETRIC
#                      placement actually measured through this same renderer,
#                      proving the kept-width gate rejects the placement it was
#                      written to replace rather than restating today's build.
# The depth rule's OTHER control: the instrument, not the design.
#
# "Tracking realises a drawn null at the design floor" asserts a supremum at or
# under -80 dB, which is worth nothing unless the path can report a number under
# -80 dB at all. The case proves that by drawing a ladder of depths and showing
# the reading follows the drawing down to a floor with margin under the gate.
# SPECTR_TRANSITION_PLANT=detection-floor-clamped substitutes the exact defect
# that control forbids -- an instrument that bottoms out at -75 dB, above the
# gate -- and the row is green only when both halves of the control reject it.
#
# It is a separate row from brick-wall because the two plant different things:
# brick-wall replaces the DESIGN and proves the three product rules can fail;
# this replaces the INSTRUMENT and proves those rules are reading something.
add_test(NAME Spectr-tracking-transition-detection-floor-negative-control
    COMMAND ${CMAKE_COMMAND} -E env
        SPECTR_TRANSITION_PLANT=detection-floor-clamped
        SPECTR_TRANSITION_PLANT_REQUIRED=1
        $<TARGET_FILE:Spectr-test>
        "Tracking realises a drawn null at the design floor")
set_tests_properties(Spectr-tracking-transition-detection-floor-negative-control
    PROPERTIES TIMEOUT 300)

add_test(NAME Spectr-tracking-transition-depth-negative-control
    COMMAND ${CMAKE_COMMAND} -E env
        SPECTR_TRANSITION_PLANT=brick-wall
        SPECTR_TRANSITION_PLANT_REQUIRED=1
        $<TARGET_FILE:Spectr-test>
        "Tracking realises a drawn null at the design floor")
set_tests_properties(Spectr-tracking-transition-depth-negative-control
    PROPERTIES TIMEOUT 300)

# The kept-side rule's negative control.
#
# This is the rule the shaping change shipped WITHOUT, and its absence is the
# whole reason a kept band could lose a third of its width with four gates
# green. So its control matters more than most: SPECTR_TRANSITION_PLANT=
# symmetric-kept substitutes the surviving widths the SYMMETRIC placement
# actually measured through this same renderer at c9e9c3e, and the row is green
# only when the rule rejects them.
#
# It cannot be replaced by the depth rows above. Coverage reads 100 % on a
# configuration whose kept band measures 18 % of its drawn width, because it
# only ever looks inside the band being REMOVED -- so every existing row here
# would stay green while the defect this rule guards returned in full.
add_test(NAME Spectr-tracking-transition-kept-negative-control
    COMMAND ${CMAKE_COMMAND} -E env
        SPECTR_TRANSITION_PLANT=symmetric-kept
        SPECTR_TRANSITION_PLANT_REQUIRED=1
        $<TARGET_FILE:Spectr-test>
        "A kept band comes back the width it was drawn")
set_tests_properties(Spectr-tracking-transition-kept-negative-control
    PROPERTIES TIMEOUT 300)

add_test(NAME Spectr-tracking-transition-regression-negative-control
    COMMAND ${CMAKE_COMMAND} -E env
        SPECTR_TRANSITION_PLANT=narrow-regression
        SPECTR_TRANSITION_PLANT_REQUIRED=1
        $<TARGET_FILE:Spectr-test>
        "No band is left worse than the unshaped design it replaced")
set_tests_properties(Spectr-tracking-transition-regression-negative-control
    PROPERTIES TIMEOUT 300)

add_test(NAME Spectr-tracking-transition-narrow-band-negative-control
    COMMAND ${CMAKE_COMMAND} -E env
        SPECTR_TRANSITION_PLANT=roomy-band
        SPECTR_TRANSITION_PLANT_REQUIRED=1
        $<TARGET_FILE:Spectr-test>
        "A band with no room for a transition is left exactly as drawn")
set_tests_properties(Spectr-tracking-transition-narrow-band-negative-control
    PROPERTIES TIMEOUT 300)

add_test(NAME Spectr-tracking-transition-eq-negative-control
    COMMAND ${CMAKE_COMMAND} -E env
        SPECTR_TRANSITION_PLANT=eq-drift
        SPECTR_TRANSITION_PLANT_REQUIRED=1
        $<TARGET_FILE:Spectr-test>
        "Transition shaping leaves an ordinary EQ curve where it was drawn")
set_tests_properties(Spectr-tracking-transition-eq-negative-control
    PROPERTIES TIMEOUT 300)

add_test(NAME Spectr-tracking-drag-glide-negative-control
    COMMAND ${CMAKE_COMMAND} -E env
        SPECTR_TRANSITION_PLANT=whole-bin-edges
        SPECTR_TRANSITION_PLANT_REQUIRED=1
        $<TARGET_FILE:Spectr-test>
        "A viewport drag moves Tracking's phase as a glide rather than a staircase")
set_tests_properties(Spectr-tracking-drag-glide-negative-control
    PROPERTIES TIMEOUT 300)

# The swap-handoff rules' negative controls.
#
# Tracking replaces an impulse response underneath running audio on every
# redesign, and what that costs is decided by whether the incoming response
# inherits the convolver's input delay line or starts from silence. The
# shipping renderer takes the instantaneous swap, which inherits it.
# SPECTR_SWAP_PLANT=history-reset reinstates the parallel crossfade -- the one
# alternative the convolver offers, and the code that shipped before -- whose
# swap path installs the incoming response with a ZEROED delay line. Both rows
# below invert their own verdict when they see the plant, so each is green only
# when its rule OBSERVES the defect.
#
# Deliberately only two rows. The third swap rule, on the boundary step a
# discrete change makes, is NOT registered here: a parallel fade is precisely
# what smooths a boundary, so the plant scores BETTER on that rule and a
# control row for it would be vacuously green. That rule carries an instrument
# control of its own instead.
add_test(NAME Spectr-swap-unchanged-mask-negative-control
    COMMAND ${CMAKE_COMMAND} -E env
        SPECTR_SWAP_PLANT=history-reset
        SPECTR_SWAP_PLANT_REQUIRED=1
        $<TARGET_FILE:Spectr-test>
        "A redesign that changes nothing changes no sample")
set_tests_properties(Spectr-swap-unchanged-mask-negative-control
    PROPERTIES TIMEOUT 300)

add_test(NAME Spectr-swap-gesture-artifact-negative-control
    COMMAND ${CMAKE_COMMAND} -E env
        SPECTR_SWAP_PLANT=history-reset
        SPECTR_SWAP_PLANT_REQUIRED=1
        $<TARGET_FILE:Spectr-test>
        "A gesture costs no more artifact than the other mode")
set_tests_properties(Spectr-swap-gesture-artifact-negative-control
    PROPERTIES TIMEOUT 300)

add_executable(Spectr-native-n1-test
    test/test_native_n1.cpp
    test/test_native_state_parity.cpp
)
target_link_libraries(Spectr-native-n1-test PRIVATE
    ${SPECTR_NATIVE_CORE_TARGET} Catch2::Catch2WithMain
)
spectr_configure_native_target(Spectr-native-n1-test)
# Both cases mount the same heavyweight native materialized editor runtime.
# Keep them mutually exclusive so a CPU-starved frame pump cannot make the
# shorter interaction case observe an event before its host-state response.
catch_discover_tests(Spectr-native-n1-test
    PROPERTIES RESOURCE_LOCK spectr_native_materialized_editor)

# The product icon must actually arrive in all four shipped bundles. The
# CMake wiring above is not evidence: CFBundleIconFile is substituted at
# generate time and the .icns is copied by a bundle rule, so either half can
# silently stop happening while the source still reads correctly. This
# inspects the built bundles.
#
# Exit 3 (no bundles built) is deliberately NOT a pass -- CTest fails it --
# so the check cannot go quiet by measuring nothing. Its own negative
# controls are registered beside it: each plants a defect and is required to
# come back RED, which is what keeps this from passing vacuously.
#
# Bundles exist only on Apple, and the detector needs an interpreter; absent
# either, the gate is not registered and the configure log says so, rather
# than being registered as something that trivially succeeds.
find_package(Python3 QUIET COMPONENTS Interpreter)
if(APPLE AND Python3_Interpreter_FOUND)
    add_test(NAME Spectr-bundle-icons
        COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/tools/bundle_icon_invariants.py
            --build-dir ${CMAKE_CURRENT_BINARY_DIR})
    set_tests_properties(Spectr-bundle-icons PROPERTIES TIMEOUT 60)

    foreach(_spectr_icon_plant strip-key strip-icns)
        add_test(NAME Spectr-bundle-icons-control-${_spectr_icon_plant}
            COMMAND ${Python3_EXECUTABLE}
                ${CMAKE_CURRENT_SOURCE_DIR}/tools/bundle_icon_invariants.py
                --build-dir ${CMAKE_CURRENT_BINARY_DIR}
                --plant ${_spectr_icon_plant})
        set_tests_properties(Spectr-bundle-icons-control-${_spectr_icon_plant}
            PROPERTIES TIMEOUT 60)
    endforeach()
else()
    message(STATUS
        "Spectr: Spectr-bundle-icons not registered (needs APPLE + Python3)")
endif()

# Headless capture of the SHIPPING native editor. The browser design source
# carries no Settings > MODULATION group -- the native patch layer injects it --
# so a browser screenshot cannot review that surface. This mounts the real
# materialized runtime through Processor::create_view() and rasterises it with
# Skia (offscreen Dawn+Skia by default, CPU Skia raster on request).
add_executable(Spectr-native-shot
    tools/native_shot.cpp
)
target_link_libraries(Spectr-native-shot PRIVATE
    ${SPECTR_NATIVE_CORE_TARGET}
)
spectr_configure_native_target(Spectr-native-shot)

# Registered so the shipping native surface is actually captured by CI, not
# merely buildable. This binary's whole point is the offscreen Dawn+Skia GPU
# path (its default backend), so on a host without GPU capture it MUST
# degrade to a loud, CTest-recognized SKIP (exit 77, matched by
# SKIP_RETURN_CODE below) rather than a bare FAIL indistinguishable from a
# real regression -- and CTest reports a SKIP as "Not Run", never "Passed",
# so a SKIP can never be misread as a PASS.
add_test(NAME Spectr-native-shot
    COMMAND $<TARGET_FILE:Spectr-native-shot>
        --out=${CMAKE_CURRENT_BINARY_DIR}/spectr-native-shots)
set_tests_properties(Spectr-native-shot PROPERTIES
    TIMEOUT 180
    SKIP_RETURN_CODE 77
    # Shares the lock with Spectr-native-n1-test: both mount the same
    # heavyweight native materialized editor runtime and must not race.
    RESOURCE_LOCK spectr_native_materialized_editor)

# Resize reachability (COR-4, editor half): sweeps host sizes through the
# shipping resize path and asserts every hit-test leaf stays inside the design
# viewport. Two stages -- capture, then a snapshot detector -- so the exit codes
# stay distinguishable: 0 pass, 1 a control left the viewport, 3 the premise is
# unproven (the resize path never reached the runtime, or a capture is
# missing), 77 no GPU capture on this host. 3 and 77 must never read as a pass.
# Needs a Python interpreter for the detector; without one the gate is not
# silently dropped, it is simply not registered and the configure log says so.
find_package(Python3 QUIET COMPONENTS Interpreter)
if(Python3_Interpreter_FOUND)
    add_test(NAME Spectr-resize-reachability
        COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/tools/run_resize_reachability.sh
            $<TARGET_FILE:Spectr-native-shot>
            ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_BINARY_DIR}/spectr-resize-reachability)
    set_tests_properties(Spectr-resize-reachability PROPERTIES
        TIMEOUT 600
        SKIP_RETURN_CODE 77
        RESOURCE_LOCK spectr_native_materialized_editor)
else()
    message(STATUS
        "Spectr: no Python3 interpreter -- Spectr-resize-reachability not registered")
endif()

# The band context menu's UNMUTE must give the band back the level it carried
# at mute time. Muting stashes that level; the click path, the SHIFT+DRAG mute
# brush and the `m` group toggle all read it back, and the menu flattened to
# 0 dB instead -- so the same gesture kept or destroyed the user's level
# depending only on which surface issued it.
#
# The assertion is on the LEVEL, in dB, read off the band field the DSP
# consumes -- NOT on the muted flag, which toggled correctly throughout and
# would have passed while the bug was fully present.
#
# Exit: 0 restored, 1 the level was lost, 3 the premise is unproven (the level
# never reached the band, or the menu never opened), 77 no view carries a
# context-menu handler under the pinned SDK, so the menu cannot be opened at
# all. 3 and 77 must never read as a pass; CTest reports 77 as "Not Run".
add_test(NAME Spectr-band-menu-unmute-restores
    COMMAND ${CMAKE_COMMAND} -E env SPECTR_BAND_MENU_UNMUTE=1
        $<TARGET_FILE:Spectr-native-shot>
        --out=${CMAKE_CURRENT_BINARY_DIR}/spectr-band-menu-unmute
        --backend=skia)
set_tests_properties(Spectr-band-menu-unmute-restores PROPERTIES
    TIMEOUT 300
    SKIP_RETURN_CODE 77
    RESOURCE_LOCK spectr_native_materialized_editor)

# Its negative control. Unmutes through the real menu, then overwrites the
# restored level with the 0 dB the defect produced, and REQUIRES the drift
# comparison to catch it. The inversion lives inside the binary rather than in
# WILL_FAIL, because WILL_FAIL accepts any non-zero exit -- a usage error or an
# unproven premise would satisfy it while measuring nothing.
add_test(NAME Spectr-band-menu-unmute-restores-negative-control
    COMMAND ${CMAKE_COMMAND} -E env SPECTR_BAND_MENU_UNMUTE=1
        SPECTR_BAND_MENU_UNMUTE_PLANT=1
        $<TARGET_FILE:Spectr-native-shot>
        --out=${CMAKE_CURRENT_BINARY_DIR}/spectr-band-menu-unmute-negctl
        --backend=skia)
set_tests_properties(Spectr-band-menu-unmute-restores-negative-control
    PROPERTIES
        TIMEOUT 300
        SKIP_RETURN_CODE 77
        RESOURCE_LOCK spectr_native_materialized_editor)

# PRESET OPERATIONS. Every action the preset manager offers, driven in the real
# native editor and judged by a count or an identity taken before and after.
#
# Exit codes: 0 pass, 1 a finding, 3 the premise is unproven (the manager never
# opened, no action resolved, the row counter cannot move). 3 must never read as
# a pass.
add_test(NAME Spectr-preset-operations
    COMMAND ${CMAKE_COMMAND} -E env SPECTR_PRESET_OPS=1
        $<TARGET_FILE:Spectr-native-shot>
        --out=${CMAKE_CURRENT_BINARY_DIR}/spectr-preset-operations
        --backend=skia)
set_tests_properties(Spectr-preset-operations PROPERTIES
    TIMEOUT 300
    SKIP_RETURN_CODE 77
    RESOURCE_LOCK spectr_native_materialized_editor)

# Its RED arm. Collapses the delete confirmation's own DELETE to the 0x0 box an
# unreachable control paints -- NATIVELY, because a JS style write is reverted
# by the next React commit and a gate that went green because its plant was
# undone is worse than no plant -- and requires the gate to notice. The
# inversion lives in the binary rather than in WILL_FAIL, which accepts any
# non-zero exit: the exit-3 "premise unproven" path would satisfy it while
# measuring nothing.
add_test(NAME Spectr-preset-operations-negative-control
    COMMAND ${CMAKE_COMMAND} -E env SPECTR_PRESET_OPS=1
        SPECTR_PRESET_OPS_PLANT=1
        $<TARGET_FILE:Spectr-native-shot>
        --out=${CMAKE_CURRENT_BINARY_DIR}/spectr-preset-operations-negctl
        --backend=skia)
set_tests_properties(Spectr-preset-operations-negative-control PROPERTIES
    TIMEOUT 300
    SKIP_RETURN_CODE 77
    RESOURCE_LOCK spectr_native_materialized_editor)

# The band context menu must not eat the keyboard, and must dismiss the way
# every other overlay in this editor dismisses.
#
# Both behaviours are long-standing code that only became observable when the
# SDK pin reached v0.854.1 and a right-click first reached the menu's handler.
# Neither was a regression, and neither was visible to the probe that already
# aimed here: SPECTR_BAND_MENU_RELAYOUT reads the menu's row COUNT and its
# group HEADER NAMES, and both are correct while the menu is unusable.
#
# What is asserted, and what each would read if the defect were maximal:
#   - a shortcut pressed with the menu open reaches the app (the edit-mode
#     PARAMETER moves, read natively off the store rather than scraped from
#     the runtime). Defect: it stays where the control put it.
#   - that shortcut also dismisses the menu, matching the row printing the
#     same letter. Defect: the menu stays.
#   - Escape UNMOUNTS the menu. The framework's return value reads `overlay`
#     in both states -- it was dismissing the slot correctly all along -- so
#     the unmount is the assertion. Defect: slot dismissed, menu still there,
#     which is precisely what a DAW shows, because a plugin host consumes
#     Escape in the shared policy and never fans it out to script.
#   - an outside press dismisses, asked of the same shared policy the hosts
#     consult rather than a replica of it.
#   - Settings, a real typing surface, STILL blocks the same key. The guard
#     was narrowed, not removed, and without this a change that deleted it
#     outright would pass everything above.
#
# Exit: 0 all hold, 1 an invariant is violated, 3 the premise is unproven
# (the key path is dead with nothing open, or the menu would not open), 77 no
# view carries a context-menu handler under the pinned SDK. 3 and 77 must
# never read as a pass; CTest reports 77 as "Not Run".
add_test(NAME Spectr-band-menu-keyboard-and-dismissal
    COMMAND ${CMAKE_COMMAND} -E env SPECTR_BAND_MENU_KEYS=1
        $<TARGET_FILE:Spectr-native-shot>
        --out=${CMAKE_CURRENT_BINARY_DIR}/spectr-band-menu-keys
        --backend=skia)
set_tests_properties(Spectr-band-menu-keyboard-and-dismissal PROPERTIES
    TIMEOUT 300
    SKIP_RETURN_CODE 77
    RESOURCE_LOCK spectr_native_materialized_editor)

# Its negative control, planted in the PRODUCT rather than in the comparison.
# It strips the one attribute the shortcut guard's exemption keys on, so the
# real guard runs against the real menu and returns exactly what it returned
# before the fix; the run then REQUIRES the shortcut to be blocked. A gate
# that still saw the key land would not be wired to the thing it covers.
#
# The inversion lives inside the binary rather than in WILL_FAIL, which would
# accept a usage error or an unproven premise as if it had caught the defect.
add_test(NAME Spectr-band-menu-keyboard-and-dismissal-negative-control
    COMMAND ${CMAKE_COMMAND} -E env SPECTR_BAND_MENU_KEYS=1
        SPECTR_BAND_MENU_KEYS_PLANT=1
        $<TARGET_FILE:Spectr-native-shot>
        --out=${CMAKE_CURRENT_BINARY_DIR}/spectr-band-menu-keys-negctl
        --backend=skia)
set_tests_properties(Spectr-band-menu-keyboard-and-dismissal-negative-control
    PROPERTIES
        TIMEOUT 300
        SKIP_RETURN_CODE 77
        RESOURCE_LOCK spectr_native_materialized_editor)

# Every row of the band context menu, driven in the SHIPPING STANDALONE
# through the platform host's own press sequence.
#
# This is the gate that is actually about the product. The offscreen one below
# is faster and sweeps band counts, but it drives a rig; this one launches
# build/Spectr.app, opens the menu with a real context press, and presses each
# row at the painted centre of that row's own label -- where the words the user
# is reading actually are.
#
# Two things in the driver are load-bearing, and getting either wrong invents a
# defect the product does not have:
#   * a press inside the active overlay is routed by
#     route_press_to_active_overlay and delivered with bubble=FALSE. A driver
#     that hit-tests directly bubbles the press into the spectrum surface the
#     menu is mounted inside, the surface claims the pointer, and every row
#     reads inert. (`View::simulate_click` does exactly that, which is how an
#     earlier revision of this lane "found" and "fixed" a defect that was not
#     there.)
#   * the click is fired by mouseUp's MouseUpHost::fire_click. A
#     default-constructed host fires nothing.
#
# Every row is judged on TWO separate assertions -- the effect it promises
# (band gains and mute flags from the processor's own field, the edit-mode
# parameter, the viewport in Hz) and whether the menu CLOSED -- because they
# fail separately. A press that closes the menu while firing a different row's
# action and a press that fires nothing and leaves the menu up are different
# bugs, and collapsing them into one pass is how this surface stayed green
# while being unusable.
#
# It also pins what the menu does while a SELECTION is live, which is the state
# the user reported: the container does not grow past 376px, the overflowing
# rows restack over the first ones, and a press aimed at "Mute / Unmute"
# reaches Boost, at "Reset to 0 dB" reaches Glide, at "Solo" reaches the VIEW
# divider (nothing happens and the menu stays open) and at "Select all"
# reaches Fit full range. Four consecutive reopens are measured, because
# closing and reopening was reported to help and does not.
#
# Runs headless and asserts the run reported opening no audio device.
find_package(Python3 QUIET COMPONENTS Interpreter)
if(APPLE AND Python3_Interpreter_FOUND)
add_test(NAME Spectr-standalone-band-menu-rows
    COMMAND ${Python3_EXECUTABLE}
        ${CMAKE_CURRENT_SOURCE_DIR}/tools/menu_scenario_check.py
        --binary ${CMAKE_CURRENT_BINARY_DIR}/Spectr.app/Contents/MacOS/Spectr
        --out ${CMAKE_CURRENT_BINARY_DIR}/spectr-standalone-band-menu)
set_tests_properties(Spectr-standalone-band-menu-rows PROPERTIES
    TIMEOUT 900
    RESOURCE_LOCK spectr_native_materialized_editor)

# Its negative control: every measured row press becomes a no-op wait and
# nothing else changes -- the menu still opens, the arrangement drags still
# run, the geometry is still read. Green only when NO press-driven assertion
# still passes. Escape and the outside press are excluded because this control
# cannot neuter them; they carry their own mounted-before/unmounted-after
# control inside the main run.
add_test(NAME Spectr-standalone-band-menu-rows-negative-control
    COMMAND ${Python3_EXECUTABLE}
        ${CMAKE_CURRENT_SOURCE_DIR}/tools/menu_scenario_check.py
        --binary ${CMAKE_CURRENT_BINARY_DIR}/Spectr.app/Contents/MacOS/Spectr
        --out ${CMAKE_CURRENT_BINARY_DIR}/spectr-standalone-band-menu-negctl
        --plant-no-press)
set_tests_properties(Spectr-standalone-band-menu-rows-negative-control
    PROPERTIES
        TIMEOUT 900
        RESOURCE_LOCK spectr_native_materialized_editor)
else()
    message(STATUS "Spectr: Spectr-standalone-band-menu-rows not registered "
                   "(needs APPLE + Python3)")
endif()

# Every row of the band context menu, driven by a press at the pixels its own
# label occupies, and judged on the STATE it claims to change.
#
# The distinction from every gate that came before it is load-bearing. All of
# them drive this menu through `rig.activate(selector)`, which resolves a row by
# CSS selector and never consults `hit_test` -- so they were green while a real
# press on a row did nothing at all. Measured on this menu, with the row's rect
# taken from the live view tree:
#
#   row->on_click()                      -> the band changed  (handler fine)
#   rig.activate("[data-spectr-band-action=…]") -> the band changed  (seam fine)
#   root.simulate_click(row centre)      -> NOTHING            (the press is not)
#
# The menu is a DOM child of the spectrum surface, whose pointer handler starts
# a band drag and takes pointer capture, so a press on a row bubbled into it and
# the click was never resolved on the button.
#
# What is asserted, per row, and what each would read if the row were inert:
#   - the band field the DSP consumes (gain_db / muted), not the menu
#   - the edit-mode HOST PARAMETER, read off the store
#   - the processor's viewport, in Hz
#   - the editor's own selection, and the group-mute shortcut reaching it
# Each press also prints WHICH view owns the point it was aimed at, so a state
# change is attributable to that row rather than to a neighbour stacked on it.
#
# It also pins, exactly, the four rows that a pointer cannot reach while a
# selection is live -- the upstream layout defect in which the menu's container
# does not grow past 376px and the overflowing rows restack over the first ones.
# Growing that list is a regression; SHRINKING it means the upstream fix landed
# and this gate's own statement has gone stale, so both fail.
#
# Exit: 0 every row did what it says, 1 a row did not (or the unreachable set
# moved), 3 a premise could not be arranged, 77 no view carries a context-menu
# handler under the pinned SDK. 3 and 77 must never read as a pass.
add_test(NAME Spectr-band-menu-items-do-what-they-say
    COMMAND ${CMAKE_COMMAND} -E env SPECTR_BAND_MENU_ITEMS=1
        $<TARGET_FILE:Spectr-native-shot>
        --out=${CMAKE_CURRENT_BINARY_DIR}/spectr-band-menu-items
        --backend=skia)
set_tests_properties(Spectr-band-menu-items-do-what-they-say PROPERTIES
    TIMEOUT 900
    SKIP_RETURN_CODE 77
    RESOURCE_LOCK spectr_native_materialized_editor)

# Its negative control. Every MEASURED press is skipped and nothing else
# changes: the arrangement still runs, the menu still opens, every row is still
# resolved and its rect still printed. The run is green only when NO row
# assertion still reads PASS -- an assertion that survives its own item being
# inert cannot see the defect it claims to cover, and that is precisely how
# this menu stayed green through a press path that never worked.
#
# The inversion lives inside the binary rather than in WILL_FAIL, which would
# accept a usage error or an unproven premise as if it had caught the defect.
add_test(NAME Spectr-band-menu-items-negative-control
    COMMAND ${CMAKE_COMMAND} -E env SPECTR_BAND_MENU_ITEMS=1
        SPECTR_BAND_MENU_ITEMS_PLANT=nopress
        $<TARGET_FILE:Spectr-native-shot>
        --out=${CMAKE_CURRENT_BINARY_DIR}/spectr-band-menu-items-negctl
        --backend=skia)
set_tests_properties(Spectr-band-menu-items-negative-control PROPERTIES
    TIMEOUT 900
    SKIP_RETURN_CODE 77
    RESOURCE_LOCK spectr_native_materialized_editor)

# Status overlay (section 5): the overlay must sit where the design puts it,
# track a live drag, and then dismiss itself. The last of those is a TIME
# property and the first is a PIXEL property, so this gate drives the shipping
# standalone through a scripted band drag and reads both -- a layout receipt
# alone cannot see a box that is painted in the wrong place, and a single
# screenshot cannot see one that never goes away. The runner refuses a verdict
# unless its drag and probe drivers left liveness markers, because an overlay
# that was never raised is pixel-identical to one that dismissed correctly.
# Exit: 0 pass, 1 an invariant is violated, 3 the premise is unproven, 77 no
# GPU editor on this host. 3 and 77 must never read as a pass.
if(Python3_Interpreter_FOUND AND TARGET ${SPECTR_NATIVE_PLUGIN_TARGET}_Standalone)
    add_test(NAME Spectr-status-overlay
        COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/tools/run_status_overlay.sh
            $<TARGET_FILE:${SPECTR_NATIVE_PLUGIN_TARGET}_Standalone>
            ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_BINARY_DIR}/spectr-status-overlay)
    set_tests_properties(Spectr-status-overlay PROPERTIES
        TIMEOUT 300
        SKIP_RETURN_CODE 77
        RESOURCE_LOCK spectr_native_materialized_editor)
else()
    message(STATUS
        "Spectr: Spectr-status-overlay not registered (needs Python3 and the standalone)")
endif()

# Painted-frame cadence during a scripted drag. The interaction gate scores the
# duration of `frame` trace slices, so a stall that swallows whole vsyncs leaves
# no slice behind and reads as fast; this measures the wall-clock gap between
# painted frames instead. Timings drift with host load, so the gate is a RATIO
# against an idle control captured in the same invocation, never an absolute
# millisecond figure. The probe refuses a verdict unless the gesture capture
# diverges from the idle capture, because a driver that moved nothing produces a
# clean fast run that is indistinguishable from a pass.
#
# The press and release gaps are reported per run and deliberately excluded from
# the gated figure: a single expensive transition handler and a drag that cannot
# hold its frame rate are different defects, and one threshold cannot hold both.
#
# Exit: 0 pass, 1 the ratio is exceeded, 3 the premise is unproven, 77 no
# runnable editor. 3 and 77 must never read as a pass.
if(Python3_Interpreter_FOUND)
    add_test(NAME Spectr-frame-cadence-selftest
        COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/tools/frame_cadence_probe.py --self-test)
    set_tests_properties(Spectr-frame-cadence-selftest PROPERTIES TIMEOUT 60)
endif()

# The dropdown detector measures whichever checkout --root names. A default
# pointing at another checkout makes a bare run report a verdict about code
# that is not under test -- and a PASS describing a foreign tree is
# indistinguishable from a PASS describing this one. This asserts the default
# is this checkout and that the marker gate rejects a foreign root.
if(Python3_Interpreter_FOUND)
    add_test(NAME Spectr-menu-dropdown-detector-root-selftest
        COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/tools/spectr-detectors/menu_dropdown_affordances.py
            --self-test)
    set_tests_properties(Spectr-menu-dropdown-detector-root-selftest
        PROPERTIES TIMEOUT 60)
endif()

# The dropdown affordances themselves, driven through Pulp's popup handler:
# Escape dismisses, each ArrowDown moves exactly one row, Return closes, and an
# outside press dismisses. Stepping is circular, so the assertion is
# (i + 1) % count -- a raw delta of 1 would score a correct wrap as a skip.
if(Python3_Interpreter_FOUND AND TARGET ${SPECTR_NATIVE_PLUGIN_TARGET}_Standalone)
    add_test(NAME Spectr-menu-dropdown-affordances
        COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/tools/spectr-detectors/menu_dropdown_affordances.py
            --root ${CMAKE_CURRENT_SOURCE_DIR}
            --app $<TARGET_FILE:${SPECTR_NATIVE_PLUGIN_TARGET}_Standalone>
            --out ${CMAKE_CURRENT_BINARY_DIR}/spectr-dropdown-affordances)
    set_tests_properties(Spectr-menu-dropdown-affordances PROPERTIES
        TIMEOUT 600
        RESOURCE_LOCK spectr_native_materialized_editor)

    # This detector spent a release reporting two FAILs against a healthy app
    # because it dispatched keys the popup handler had no state to act on. A
    # repaired measurement has to be shown catching the defect it claims to
    # watch for, so this injects a second keyboard owner -- which double-steps
    # the cursor, the user-reported symptom exactly -- and REQUIRES the arrow
    # assertion to catch it. It fails if the measurement goes green.
    add_test(NAME Spectr-menu-dropdown-affordances-negative-control
        COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/tools/spectr-detectors/menu_dropdown_affordances.py
            --root ${CMAKE_CURRENT_SOURCE_DIR}
            --app $<TARGET_FILE:${SPECTR_NATIVE_PLUGIN_TARGET}_Standalone>
            --out ${CMAKE_CURRENT_BINARY_DIR}/spectr-dropdown-affordances-negctl
            --negative-control)
    set_tests_properties(Spectr-menu-dropdown-affordances-negative-control
        PROPERTIES
            TIMEOUT 600
            RESOURCE_LOCK spectr_native_materialized_editor)
endif()

# Arrow stepping in a dropdown must move exactly one row. The affordance test
# above asserts a destination (three ArrowDowns then Return land on a named
# caption), which a handler that steps two rows can also satisfy; this reads the
# highlight index after every press instead. The self-test needs no binary.
if(Python3_Interpreter_FOUND)
    add_test(NAME Spectr-menu-arrow-step-selftest
        COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/tools/spectr-detectors/menu_arrow_step_exactly_one.py
            --self-test)
    set_tests_properties(Spectr-menu-arrow-step-selftest PROPERTIES TIMEOUT 60)
endif()

if(Python3_Interpreter_FOUND AND TARGET ${SPECTR_NATIVE_PLUGIN_TARGET}_Standalone)
    add_test(NAME Spectr-menu-arrow-step
        COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/tools/spectr-detectors/menu_arrow_step_exactly_one.py
            --root ${CMAKE_CURRENT_SOURCE_DIR}
            --app $<TARGET_FILE:${SPECTR_NATIVE_PLUGIN_TARGET}_Standalone>
            --out ${CMAKE_CURRENT_BINARY_DIR}/spectr-arrowstep)
    set_tests_properties(Spectr-menu-arrow-step PROPERTIES
        TIMEOUT 300
        RESOURCE_LOCK spectr_native_materialized_editor)

    # A gate nobody has watched fail is not a gate. This injects the second
    # keyboard owner that was deliberately removed from this app and REQUIRES
    # the assertion above to catch it; it fails if the measurement goes green.
    add_test(NAME Spectr-menu-arrow-step-negative-control
        COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/tools/spectr-detectors/menu_arrow_step_exactly_one.py
            --root ${CMAKE_CURRENT_SOURCE_DIR}
            --app $<TARGET_FILE:${SPECTR_NATIVE_PLUGIN_TARGET}_Standalone>
            --out ${CMAKE_CURRENT_BINARY_DIR}/spectr-arrowstep-negctl
            --negative-control)
    set_tests_properties(Spectr-menu-arrow-step-negative-control PROPERTIES
        TIMEOUT 300
        RESOURCE_LOCK spectr_native_materialized_editor)
endif()

if(Python3_Interpreter_FOUND AND TARGET ${SPECTR_NATIVE_PLUGIN_TARGET}_Standalone)
    # Band drawing holds its cadence today (gesture p95 sits at or below the idle
    # control), so this ratio is a real floor rather than a rubber stamp.
    add_test(NAME Spectr-band-drag-cadence
        COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/tools/frame_cadence_probe.py
            --app $<TARGET_FILE:${SPECTR_NATIVE_PLUGIN_TARGET}_Standalone>
            --mode bands --runs 2
            --max-sustained-p95-ratio 1.35
            --json ${CMAKE_CURRENT_BINARY_DIR}/spectr-cadence-bands.json)
    set_tests_properties(Spectr-band-drag-cadence PROPERTIES
        TIMEOUT 300
        SKIP_RETURN_CODE 77
        RESOURCE_LOCK spectr_native_materialized_editor)

    # Minimap dragging does NOT hold its cadence -- the sustained p95 runs at
    # roughly twice the idle control, and the cost is charged natively after the
    # processing-state message returns rather than in the script handlers. No
    # threshold is attached: the gesture's cost is load-sensitive in a way the
    # idle control is not, so the ratio was observed swinging between 1.8 and 2.9
    # on one host within ten minutes, and a gate that noisy reports the machine
    # rather than the code. This case records the figures and still fails when the
    # premise is unproven, which keeps the instrument honest until the native cost
    # is fixed and a real threshold can be set.
    add_test(NAME Spectr-minimap-drag-cadence-report
        COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/tools/frame_cadence_probe.py
            --app $<TARGET_FILE:${SPECTR_NATIVE_PLUGIN_TARGET}_Standalone>
            --mode minimap --runs 2
            --json ${CMAKE_CURRENT_BINARY_DIR}/spectr-cadence-minimap.json)
    set_tests_properties(Spectr-minimap-drag-cadence-report PROPERTIES
        TIMEOUT 300
        SKIP_RETURN_CODE 77
        RESOURCE_LOCK spectr_native_materialized_editor)
else()
    message(STATUS
        "Spectr: frame-cadence gates not registered (needs Python3 and the standalone)")
endif()

add_executable(Spectr-editor-authority-test
    test/test_editor_authority.cpp
)
target_link_libraries(Spectr-editor-authority-test PRIVATE
    ${SPECTR_NATIVE_CORE_TARGET} Catch2::Catch2WithMain
)
spectr_configure_native_target(Spectr-editor-authority-test)
catch_discover_tests(Spectr-editor-authority-test)

# Real Chromium oracle for the imported adapter's native analyzer and input
# policy. This runs the authored bundle with a mock Pulp bridge and proves
# native analyzer data, finite hydration/reopen, mute gestures, and shortcut
# ownership through the actual React event handlers.
find_program(SPECTR_NODE_EXECUTABLE NAMES node)
find_program(SPECTR_CHROME_EXECUTABLE
    NAMES "Google Chrome" google-chrome chromium chromium-browser
    PATHS "/Applications/Google Chrome.app/Contents/MacOS")

# Node-only, deliberately outside the Chrome-gated block below: this executes
# the shipped reattach ladder out of the materialized document directly, so it
# still runs on a host with no Chrome -- where every other oracle silently
# vanishes.
#
# What it guards: the ladder's wrappers must return what the React callback
# returned. A handler stops a press from reaching the nodes behind it by
# returning a propagation marker, and Settings depends on that -- its panel
# calls stopPropagation so a press inside never reaches the scrim, whose
# "pressed outside" handler closes it. Wrappers that dropped the value shipped
# the #1 user-reported defect: Settings closed on a tap that landed inside it,
# and toggles appeared to vanish because the panel closed instead of the toggle
# flipping.
if(SPECTR_NODE_EXECUTABLE)
    add_test(NAME Spectr-materialized-propagation
        COMMAND ${SPECTR_NODE_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/test/test_materialized_propagation.mjs
            ${CMAKE_CURRENT_SOURCE_DIR}/native-ui/materialized/materialized-document.runtime.json)
    set_tests_properties(Spectr-materialized-propagation PROPERTIES TIMEOUT 30)
    # The negative control. --plant-swallow reinstates the value-dropping
    # wrappers -- the exact pre-fix code -- and --expect-fail inverts the
    # verdict, so this row is green only when the suite REJECTS them. The
    # inversion lives in the script rather than in WILL_FAIL because WILL_FAIL
    # accepts any non-zero exit: a missing module, a usage error, or a broken
    # JSON read all satisfied it, and the control read green while proving
    # nothing. With --expect-fail those cases exit non-zero and fail the row.
    add_test(NAME Spectr-materialized-propagation-negative-control
        COMMAND ${SPECTR_NODE_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/test/test_materialized_propagation.mjs
            ${CMAKE_CURRENT_SOURCE_DIR}/native-ui/materialized/materialized-document.runtime.json
            --plant-swallow --expect-fail)
    set_tests_properties(Spectr-materialized-propagation-negative-control PROPERTIES
        TIMEOUT 30)

    # The Latency control: does it exist, does it mount, and does changing it
    # say the right thing on the wire. The mode was saved, recalled and
    # migrated three PRs before anything could set it, so the risk here is
    # reachability rather than correctness of the mode itself.
    add_test(NAME Spectr-materialized-latency-mode
        COMMAND ${SPECTR_NODE_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/test/test_materialized_latency_mode.mjs
            ${CMAKE_CURRENT_SOURCE_DIR}/native-ui/materialized/materialized-document.runtime.json)
    set_tests_properties(Spectr-materialized-latency-mode PROPERTIES TIMEOUT 30)

    # Four plants, because each is a DIFFERENT wrong implementation and one
    # plant that trips everything cannot show which check is load bearing.
    # --plant-index and --plant-post-index look redundant and are not: the
    # first breaks the chip labels and fails the label assertion before ever
    # reaching the wire check, so only the second can prove that the control
    # sends a TOKEN rather than a position. --plant-truthiness is the one that
    # matters most in practice: hydrating on truthiness instead of presence
    # makes the control reset itself on the next automation frame, which no
    # single-frame test would ever see. --expect-fail lives in the script
    # rather than in WILL_FAIL because WILL_FAIL accepts any non-zero exit, so
    # a usage error or a missing document would read green while proving
    # nothing.
    foreach(_latency_plant index post-index no-notify uneven-hint
            truthiness typed-label)
        add_test(NAME Spectr-materialized-latency-mode-control-${_latency_plant}
            COMMAND ${SPECTR_NODE_EXECUTABLE}
                ${CMAKE_CURRENT_SOURCE_DIR}/test/test_materialized_latency_mode.mjs
                ${CMAKE_CURRENT_SOURCE_DIR}/native-ui/materialized/materialized-document.runtime.json
                --plant-${_latency_plant} --expect-fail)
        set_tests_properties(Spectr-materialized-latency-mode-control-${_latency_plant}
            PROPERTIES TIMEOUT 30)
    endforeach()

    # A child's AUTHORED index must be its NATIVE index, including for a
    # subtree that mounts late into a parent that kept its other children.
    # Every native createX APPENDS, so the renderer has to move a non-last
    # child into place with insertChild right after creating it. The vendored
    # runtime.js predated Pulp #8272 and dropped attach()'s computed index at
    # materialize(), so a re-opened dropdown remounted its rows AFTER the
    # siblings that stayed and every caption separated from the description it
    # labels -- the reported "dropdown gets messed up" after a preset apply or
    # a snapshot recall re-renders the overlay subtree.
    #
    # This runs the artifact's OWN attach/materialize/materializeUnder against
    # a recording bridge that appends like the native factory, and asserts the
    # resulting native order. It deliberately does NOT grep for insertChild:
    # the artifact already carried that identifier in insertBefore's
    # same-parent REORDER branch, which predates #8272 and says nothing about
    # whether the MOUNT path is wired.
    add_test(NAME Spectr-materialized-insert-index
        COMMAND ${SPECTR_NODE_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/test/test_materialized_insert_index.mjs
            ${CMAKE_CURRENT_SOURCE_DIR}/native-ui/materialized/runtime.js)
    set_tests_properties(Spectr-materialized-insert-index PROPERTIES TIMEOUT 30)

    # Three negative controls, because each is a DIFFERENT wrong implementation
    # and a single plant that trips everything cannot show which check is load
    # bearing. --plant-drop-index is the exact defect that shipped;
    # --plant-append-only is the plausible half-fix that threads the index but
    # never emits the call, and is the reason the assertion is on resulting
    # native order rather than on call arity; --plant-queue-order restores
    # queue-order draining of a deferred parent, which only the third scenario
    # can see. --expect-fail lives in the script rather than WILL_FAIL because
    # WILL_FAIL accepts any non-zero exit, so a usage error or a missing file
    # would read green while proving nothing.
    add_test(NAME Spectr-materialized-insert-index-control-drop-index
        COMMAND ${SPECTR_NODE_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/test/test_materialized_insert_index.mjs
            ${CMAKE_CURRENT_SOURCE_DIR}/native-ui/materialized/runtime.js
            --plant-drop-index --expect-fail)
    set_tests_properties(Spectr-materialized-insert-index-control-drop-index
        PROPERTIES TIMEOUT 30)

    add_test(NAME Spectr-materialized-insert-index-control-append-only
        COMMAND ${SPECTR_NODE_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/test/test_materialized_insert_index.mjs
            ${CMAKE_CURRENT_SOURCE_DIR}/native-ui/materialized/runtime.js
            --plant-append-only --expect-fail)
    set_tests_properties(Spectr-materialized-insert-index-control-append-only
        PROPERTIES TIMEOUT 30)

    add_test(NAME Spectr-materialized-insert-index-control-queue-order
        COMMAND ${SPECTR_NODE_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/test/test_materialized_insert_index.mjs
            ${CMAKE_CURRENT_SOURCE_DIR}/native-ui/materialized/runtime.js
            --plant-queue-order --expect-fail)
    set_tests_properties(Spectr-materialized-insert-index-control-queue-order
        PROPERTIES TIMEOUT 30)

    # The modulation display, MEASURED rather than asserted. This drives the
    # shipped rAF `draw` and `applyModulationFrame` out of the materialized
    # document against a known producer and requires the painted peak-to-peak
    # to match the published peak-to-peak, then mounts the real
    # SpectrModulationSettings against a mock bridge and requires it to follow
    # a native live-state frame. Both were user-visible: the rAF smoother was
    # pulling every painted sample back toward the unmodulated value on every
    # frame (a rate-independent ~29% of the true excursion lost), and the
    # settings panel read its nine LFO scalars exactly once, at mount, so a
    # host automating them animated the overlay while the panel stayed pinned.
    add_test(NAME Spectr-materialized-modulation
        COMMAND ${SPECTR_NODE_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/test/test_materialized_modulation.mjs
            ${CMAKE_CURRENT_SOURCE_DIR}/native-ui/materialized/materialized-document.runtime.json)
    set_tests_properties(Spectr-materialized-modulation PROPERTIES TIMEOUT 60)
    # Negative controls, one per contract. Each reinstates the exact pre-fix
    # line and inverts the verdict, so the row is green only when the suite
    # REJECTS it. As above the inversion lives in the script, not in WILL_FAIL,
    # because WILL_FAIL accepts any non-zero exit -- including the ones that
    # mean the control never ran.
    add_test(NAME Spectr-materialized-modulation-smoothing-control
        COMMAND ${SPECTR_NODE_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/test/test_materialized_modulation.mjs
            ${CMAKE_CURRENT_SOURCE_DIR}/native-ui/materialized/materialized-document.runtime.json
            --plant-smoothing --expect-fail)
    set_tests_properties(Spectr-materialized-modulation-smoothing-control PROPERTIES
        TIMEOUT 60)
    add_test(NAME Spectr-materialized-modulation-oneshot-control
        COMMAND ${SPECTR_NODE_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/test/test_materialized_modulation.mjs
            ${CMAKE_CURRENT_SOURCE_DIR}/native-ui/materialized/materialized-document.runtime.json
            --plant-oneshot --expect-fail)
    set_tests_properties(Spectr-materialized-modulation-oneshot-control PROPERTIES
        TIMEOUT 60)

    # The morph slider's precondition, made legible. Morph interpolates the
    # band field between snapshot A and snapshot B, so with either slot empty
    # it has no endpoints. That was always ENFORCED -- `disabled` forwards to
    # View::setEnabled(false), the pointer handlers return early, and
    # bank.setMorph re-checks its own mirror -- but never EXPLAINED: the
    # control had no visible name and its disabled state was carried by
    # opacity alone, so a user who had not captured both slots saw a dim
    # unlabelled track that ignored them ("i can't figure out how to see the
    # morph slider actually morph"). This suite executes the shipped
    # MorphSlider and requires a caption naming the MISSING slot while one is
    # empty, readable rather than sharing the control's dim, and -- the scope
    # control -- that the gate still refuses a pointer commit when disabled
    # and still accepts one when enabled, including after a slot is cleared on
    # a live component.
    #
    # WHERE that caption goes is the second half, and the first attempt got it
    # wrong: it was painted across the inside of the 90x16 groove, so a
    # half-configured control read as `A ---- SET B ---- B` -- guidance that
    # reads as a rendering fault ("it seems like a bug the way it's displayed
    # I like the intent"). The Pulp Design System settles it: a disabled
    # control is OPACITY ONLY with no instructional text on it, and guidance
    # goes in a mono/10px/faint caption beside it. So the caption is now a
    # sibling BELOW the row, and the suite requires that it is not a
    # descendant of the track.
    #
    # It stays absolutely positioned, on both axes and for two different
    # reasons. Horizontally the transport row has no spare width, and an
    # earlier attempt that added flex siblings crushed that track to 39.5px.
    # Vertically an in-flow caption would grow the control's box from 20 to
    # ~35px, and the bar centres its items -- so the track would ride 7.5px
    # off the centreline its 26px button neighbours sit on, then JUMP BACK
    # DOWN the moment the second slot was captured and the caption unmounted.
    add_test(NAME Spectr-materialized-morph-affordance
        COMMAND ${SPECTR_NODE_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/test/test_materialized_morph_affordance.mjs
            ${CMAKE_CURRENT_SOURCE_DIR}/native-ui/materialized/materialized-document.runtime.json)
    set_tests_properties(Spectr-materialized-morph-affordance PROPERTIES TIMEOUT 60)
    # Negative controls, one per half of the fix. Each reinstates the exact
    # pre-fix text and inverts the verdict, so the row is green only when the
    # suite REJECTS it. As above the inversion lives in the script, not in
    # WILL_FAIL, because WILL_FAIL accepts any non-zero exit -- including the
    # ones that mean the control never ran.
    add_test(NAME Spectr-materialized-morph-affordance-dim-control
        COMMAND ${SPECTR_NODE_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/test/test_materialized_morph_affordance.mjs
            ${CMAKE_CURRENT_SOURCE_DIR}/native-ui/materialized/materialized-document.runtime.json
            --plant-wrapper-dim --expect-fail)
    set_tests_properties(Spectr-materialized-morph-affordance-dim-control PROPERTIES
        TIMEOUT 60)
    add_test(NAME Spectr-materialized-morph-affordance-hint-control
        COMMAND ${SPECTR_NODE_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/test/test_materialized_morph_affordance.mjs
            ${CMAKE_CURRENT_SOURCE_DIR}/native-ui/materialized/materialized-document.runtime.json
            --plant-nohint --expect-fail)
    set_tests_properties(Spectr-materialized-morph-affordance-hint-control PROPERTIES
        TIMEOUT 60)
    # The reported defect itself, verbatim: the sentence back inside the
    # groove. A suite that cannot reject this does not cover the bug.
    add_test(NAME Spectr-materialized-morph-affordance-groove-control
        COMMAND ${SPECTR_NODE_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/test/test_materialized_morph_affordance.mjs
            ${CMAKE_CURRENT_SOURCE_DIR}/native-ui/materialized/materialized-document.runtime.json
            --plant-hint-in-track --expect-fail)
    set_tests_properties(Spectr-materialized-morph-affordance-groove-control PROPERTIES
        TIMEOUT 60)
    # ...and the same move with TODAY'S wording and treatment untouched, so
    # the row above cannot be passing on the strength of a changed string:
    # this one isolates the PLACEMENT rule.
    add_test(NAME Spectr-materialized-morph-affordance-placement-control
        COMMAND ${SPECTR_NODE_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/test/test_materialized_morph_affordance.mjs
            ${CMAKE_CURRENT_SOURCE_DIR}/native-ui/materialized/materialized-document.runtime.json
            --plant-caption-into-groove --expect-fail)
    set_tests_properties(Spectr-materialized-morph-affordance-placement-control PROPERTIES
        TIMEOUT 60)
    # The MODULATION group must read as two LFOs plus a shared destination
    # block, not as one flat run of ten sibling rows. `Target` used to be gated
    # on LFO 1, which put it inside LFO 1's disclosure -- but
    # resolve_modulation_target_mask reads it for BOTH LFOs (src/spectr.cpp
    # copies the whole settings struct into the second pass), so that gate
    # stated something the audio path does not do, and it is why `Target` and
    # `Targets` read as one duplicated control.
    #
    # The suite RUNS the shipped component rather than grepping it: a static
    # check cannot see a row emitted in the wrong order by a conditional, and
    # `new Function` proves syntax without proving scope.
    add_test(NAME Spectr-materialized-modulation-grouping
        COMMAND ${SPECTR_NODE_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/test/test_materialized_modulation_grouping.mjs
            ${CMAKE_CURRENT_SOURCE_DIR}/native-ui/materialized/materialized-document.runtime.json)
    set_tests_properties(Spectr-materialized-modulation-grouping PROPERTIES TIMEOUT 60)
    # Negative controls, one per gate. Each reinstates a real defect and inverts
    # the verdict inside the script, so the row is green only when the suite
    # REJECTS it. The inversion is deliberately not WILL_FAIL: WILL_FAIL accepts
    # any non-zero exit, including the ones that mean the control never ran.
    add_test(NAME Spectr-materialized-modulation-grouping-order-control
        COMMAND ${SPECTR_NODE_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/test/test_materialized_modulation_grouping.mjs
            ${CMAKE_CURRENT_SOURCE_DIR}/native-ui/materialized/materialized-document.runtime.json
            --plant-order --expect-fail)
    set_tests_properties(Spectr-materialized-modulation-grouping-order-control PROPERTIES
        TIMEOUT 60)
    # Parses clean and `node --check` exits 0 on it; only running the component
    # surfaces it. That is the whole reason the SCOPE gate exists.
    add_test(NAME Spectr-materialized-modulation-grouping-scope-control
        COMMAND ${SPECTR_NODE_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/test/test_materialized_modulation_grouping.mjs
            ${CMAKE_CURRENT_SOURCE_DIR}/native-ui/materialized/materialized-document.runtime.json
            --plant-scope --expect-fail)
    set_tests_properties(Spectr-materialized-modulation-grouping-scope-control PROPERTIES
        TIMEOUT 60)
    # THE regression control. A conditional mount is invisible in a browser --
    # the row still appears in the right place -- but the native bridge has no
    # insert-at-index and no move (probed: globalThis.insertChild and
    # globalThis.moveWidget are undefined), so a row that mounts late is
    # APPENDED and the group re-scrambles as the user flips toggles. That is
    # the defect the user reported.
    add_test(NAME Spectr-materialized-modulation-grouping-remount-control
        COMMAND ${SPECTR_NODE_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/test/test_materialized_modulation_grouping.mjs
            ${CMAKE_CURRENT_SOURCE_DIR}/native-ui/materialized/materialized-document.runtime.json
            --plant-remount --expect-fail)
    set_tests_properties(Spectr-materialized-modulation-grouping-remount-control PROPERTIES
        TIMEOUT 60)
    add_test(NAME Spectr-materialized-modulation-grouping-homograph-control
        COMMAND ${SPECTR_NODE_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/test/test_materialized_modulation_grouping.mjs
            ${CMAKE_CURRENT_SOURCE_DIR}/native-ui/materialized/materialized-document.runtime.json
            --plant-homograph --expect-fail)
    set_tests_properties(Spectr-materialized-modulation-grouping-homograph-control PROPERTIES
        TIMEOUT 60)
endif()

# The zoom readout must be a leaf that subscribes to the viewport, never a
# value the app root polls into root state. The app root used to hold a 150ms
# setInterval that read the live viewport off the bank handle; a memo guard
# kept it out of unchanged frames, which hid it, but zoom is exactly what moves
# while the minimap is being dragged -- so the one interaction that has to stay
# cheap committed the whole imported document every 150ms.
#
# Static checks alone cannot see a declaration that landed in the wrong
# function body (that parses clean), so the suite also evaluates the readout's
# own script block in a vm with a hook runtime and drives it: it must
# subscribe, repaint on a zoom change, commit NOTHING across 200 pan samples
# (a pan preserves the span, so the printed value cannot move), and release
# its subscription on unmount.
if(SPECTR_NODE_EXECUTABLE)
    add_test(NAME Spectr-materialized-zoom-readout
        COMMAND ${SPECTR_NODE_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/test/test_materialized_zoom_readout.mjs
            ${CMAKE_CURRENT_SOURCE_DIR}/native-ui/materialized/materialized-document.runtime.json)
    set_tests_properties(Spectr-materialized-zoom-readout PROPERTIES TIMEOUT 30)
    # The negative control. --plant-poller restores the pre-fix shape -- the
    # root interval comes back and the readout reverts to a value handed down
    # from root state -- and --expect-fail inverts the verdict, so this row is
    # green only when the suite REJECTS that document. The plant asserts each
    # of its three needles matched exactly once, so a drifted needle fails the
    # control instead of quietly re-testing the current document. The
    # inversion lives in the script rather than in WILL_FAIL for the same
    # reason as the row above: WILL_FAIL accepts any non-zero exit.
    add_test(NAME Spectr-materialized-zoom-readout-negative-control
        COMMAND ${SPECTR_NODE_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/test/test_materialized_zoom_readout.mjs
            ${CMAKE_CURRENT_SOURCE_DIR}/native-ui/materialized/materialized-document.runtime.json
            --plant-poller --expect-fail)
    set_tests_properties(Spectr-materialized-zoom-readout-negative-control PROPERTIES
        TIMEOUT 30)
    # The second control, and the one that matters most. --plant-live-commit
    # keeps the subscription but drops the live guard, which is the measured
    # REGRESSION: coalescing live samples to one commit per frame is still a
    # commit per frame, and it took minimap frame gaps >= 25ms from 16 to 29.
    # Separate from the row above because the two fail different assertions,
    # and a single plant that trips everything cannot show which check is
    # load bearing.
    add_test(NAME Spectr-materialized-zoom-readout-live-commit-control
        COMMAND ${SPECTR_NODE_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/test/test_materialized_zoom_readout.mjs
            ${CMAKE_CURRENT_SOURCE_DIR}/native-ui/materialized/materialized-document.runtime.json
            --plant-live-commit --expect-fail)
    set_tests_properties(Spectr-materialized-zoom-readout-live-commit-control PROPERTIES
        TIMEOUT 30)
endif()

# The band readout never prints an index from one band layout against another
# layout's divisor.
#
# A user screenshot read "155.5kHz  24.0 dB  BAND 42/32" on a bank hovered as
# one of 64. Both halves are one mismatch: index 41 was measured under a
# 64-band layout while `N` -- the divisor in the label AND in bandCenterFreq --
# had already become 32, and bandCenterFreq carried no clamp, so the wrong
# divisor extrapolated 155473 Hz out of a 20Hz-20kHz view (band 42 of 64 is
# 1763 Hz). `N` is `settings.bandCount`, and band count is host parameter 3003,
# so automation / a preset recall / a live-state resync rewrites it with NO
# pointer event while `hover` keeps the index it last measured.
#
# The suite reads NUMBERS, never the presence of a label -- the defect produced
# a perfectly well-formed label, so any presence check passes on it. It lifts
# the shipping source of the gate, the label builder and bandCenterFreq out of
# the document and executes them in a vm against the exact screenshot stimulus.
if(SPECTR_NODE_EXECUTABLE)
    add_test(NAME Spectr-materialized-band-readout
        COMMAND ${SPECTR_NODE_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/test/test_materialized_band_readout.mjs
            ${CMAKE_CURRENT_SOURCE_DIR}/native-ui/materialized/materialized-document.runtime.json)
    set_tests_properties(Spectr-materialized-band-readout PROPERTIES TIMEOUT 30)
    # The control that matters: --plant-no-guard restores the pre-fix document
    # exactly, and it reproduces the reported string character for character
    # ("155473Hz   24.0 dB   BAND 42/32"). If this row ever goes green the
    # suite above cannot see the defect it was written for.
    add_test(NAME Spectr-materialized-band-readout-stale-layout-control
        COMMAND ${SPECTR_NODE_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/test/test_materialized_band_readout.mjs
            ${CMAKE_CURRENT_SOURCE_DIR}/native-ui/materialized/materialized-document.runtime.json
            --plant-no-guard --expect-fail)
    set_tests_properties(Spectr-materialized-band-readout-stale-layout-control
        PROPERTIES TIMEOUT 30)
    # The clamp on its own. The layout gate above already refuses a stale
    # reading, so without this row the clamp could be deleted tomorrow with
    # every test still green -- and an unclamped extrapolation resurfaces
    # wherever a new caller forgets.
    add_test(NAME Spectr-materialized-band-readout-unclamped-control
        COMMAND ${SPECTR_NODE_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/test/test_materialized_band_readout.mjs
            ${CMAKE_CURRENT_SOURCE_DIR}/native-ui/materialized/materialized-document.runtime.json
            --plant-no-clamp --expect-fail)
    set_tests_properties(Spectr-materialized-band-readout-unclamped-control
        PROPERTIES TIMEOUT 30)
    # The regression in the other direction, and a mistake actually made while
    # writing the fix: stamp the ref copy and hand the bare object to setHover,
    # and the gate refuses every reading forever -- the readout does not go
    # wrong, it disappears. Nothing that only checks "no bad reading" can see
    # that, which is why it is its own row.
    add_test(NAME Spectr-materialized-band-readout-unstamped-control
        COMMAND ${SPECTR_NODE_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/test/test_materialized_band_readout.mjs
            ${CMAKE_CURRENT_SOURCE_DIR}/native-ui/materialized/materialized-document.runtime.json
            --plant-unstamped-state --expect-fail)
    set_tests_properties(Spectr-materialized-band-readout-unstamped-control
        PROPERTIES TIMEOUT 30)
endif()

# The output level readout, and the Output trim beside it.
#
# `kOutputTrim` is applied post-render and proved exact by a unit test, and
# `bridge_.process()` has always been fed the POST-TRIM buffer -- yet neither
# "Output" nor "Mix" appeared anywhere in the shipping editor document or its
# help text, and `Spectr::read_meter()`'s only caller was a test. So the level
# handed to the host was computed and discarded, and the trim was unreachable
# from the plug-in.
#
# The readout is labelled OVER rather than CLIP: Spectr is float end to end and
# clips nothing internally, so what it reports is that the signal LEAVING the
# plug-in passed full scale. It shows the dBFS number rather than a lamp,
# because how far over is the actionable part. The hold only rises and is
# cleared by clicking it -- a decaying hold would let the loudest moment of a
# session vanish while nobody was looking, which is the thing a person opens a
# meter to find out.
if(SPECTR_NODE_EXECUTABLE)
    add_test(NAME Spectr-materialized-output-meter
        COMMAND ${SPECTR_NODE_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/test/test_materialized_output_meter.mjs
            ${CMAKE_CURRENT_SOURCE_DIR}/native-ui/materialized/materialized-document.runtime.json)
    set_tests_properties(Spectr-materialized-output-meter PROPERTIES TIMEOUT 30)
    # A hold that follows every frame. Invisible to any check that reads the
    # current frame, and it destroys the one thing the readout is for.
    add_test(NAME Spectr-materialized-output-meter-falling-hold-control
        COMMAND ${SPECTR_NODE_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/test/test_materialized_output_meter.mjs
            ${CMAKE_CURRENT_SOURCE_DIR}/native-ui/materialized/materialized-document.runtime.json
            --plant-falling-hold --expect-fail)
    set_tests_properties(Spectr-materialized-output-meter-falling-hold-control
        PROPERTIES TIMEOUT 30)
    # An OVER that clears itself on the next quiet frame. Separate row because
    # it fails a different assertion than the hold: the hold can be correct
    # while a transient overload is gone before anyone sees it.
    add_test(NAME Spectr-materialized-output-meter-latch-control
        COMMAND ${SPECTR_NODE_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/test/test_materialized_output_meter.mjs
            ${CMAKE_CURRENT_SOURCE_DIR}/native-ui/materialized/materialized-document.runtime.json
            --plant-no-latch --expect-fail)
    set_tests_properties(Spectr-materialized-output-meter-latch-control
        PROPERTIES TIMEOUT 30)
    # The wording. CLIP is a claim about Spectr that is false, and a label is
    # exactly the kind of thing a later edit "tidies" without knowing why.
    add_test(NAME Spectr-materialized-output-meter-clip-label-control
        COMMAND ${SPECTR_NODE_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/test/test_materialized_output_meter.mjs
            ${CMAKE_CURRENT_SOURCE_DIR}/native-ui/materialized/materialized-document.runtime.json
            --plant-clip-label --expect-fail)
    set_tests_properties(Spectr-materialized-output-meter-clip-label-control
        PROPERTIES TIMEOUT 30)
    # The readout's own type. It shipped declaring no font at all, so it
    # inherited the document body default while the PEAK button 14pt away
    # declared var(--mono)/10/rgba(255,255,255,0.72): rastered through the
    # shipping native editor its ink measured 11.0pt against 7.5pt for every
    # other readout in the header, and `+12.0` painted a 37pt run inside its
    # own 34pt box. This plant restores exactly that document, so the row is
    # green only when the suite rejects the state that merged.
    add_test(NAME Spectr-materialized-output-meter-readout-type-control
        COMMAND ${SPECTR_NODE_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/test/test_materialized_output_meter.mjs
            ${CMAKE_CURRENT_SOURCE_DIR}/native-ui/materialized/materialized-document.runtime.json
            --plant-untyped-readout --expect-fail)
    set_tests_properties(Spectr-materialized-output-meter-readout-type-control
        PROPERTIES TIMEOUT 30)
    # The trim's on-screen label. It shipped carrying only aria-label and
    # title -- neither of which a DAW ever draws -- so a user met an
    # unlabelled slider beside a meter and had to guess what it moved, in a
    # header where every other control is labelled. This plant removes the
    # visible label again, so the row is green only when the suite rejects
    # the state that shipped.
    add_test(NAME Spectr-materialized-output-meter-trim-label-control
        COMMAND ${SPECTR_NODE_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/test/test_materialized_output_meter.mjs
            ${CMAKE_CURRENT_SOURCE_DIR}/native-ui/materialized/materialized-document.runtime.json
            --plant-unlabelled-trim --expect-fail)
    set_tests_properties(Spectr-materialized-output-meter-trim-label-control
        PROPERTIES TIMEOUT 30)
    # The trim's resolution. The runtime maps a press across the FULL declared
    # track width under a fixed ~20pt thumb, so 58pt resolved 0.83 dB per
    # point -- a 0.5 dB step roughly every device pixel at 2x, measured
    # through the shipping standalone as 20.5 dB from a 25pt drag. A separate
    # row from the label because a labelled control that still cannot be
    # aimed is only half fixed.
    add_test(NAME Spectr-materialized-output-meter-trim-track-control
        COMMAND ${SPECTR_NODE_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/test/test_materialized_output_meter.mjs
            ${CMAKE_CURRENT_SOURCE_DIR}/native-ui/materialized/materialized-document.runtime.json
            --plant-short-track --expect-fail)
    set_tests_properties(Spectr-materialized-output-meter-trim-track-control
        PROPERTIES TIMEOUT 30)
endif()

# Morph moves the viewport, not just the bands.
#
# A snapshot has always captured the window it was taken under; morph ignored
# it, so the bands blended continuously while the window they are drawn in --
# and masked with -- snapped at t=0.5. Three properties each read green if you
# check the wrong thing, so each gets its own control:
#
#   The interpolation runs in LOG space. A test that pins only t=0 and t=1
#   passes unchanged against a snap, because a snap already gives the right
#   answer at both ends. Only the midpoint discriminates, and it must reject
#   BOTH a snap and a linear-Hz lerp, which is the plausible wrong version.
#
#   Morph is dragged AND is an LFO/automation destination, so it is exactly
#   the value that moves every sample. It must publish the viewport with
#   `live = deferReact` so the canvas keeps moving every rAF while React
#   commits nothing until the gesture settles -- the same contract the zoom
#   readout rows above exist to protect.
#
#   The switch is a PLAYBACK switch: capture always records the window.
#
# Static text can see none of that, so the suite extracts the real helper and
# runs it in a vm.
if(SPECTR_NODE_EXECUTABLE)
    add_test(NAME Spectr-materialized-morph-viewport
        COMMAND ${SPECTR_NODE_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/test/test_materialized_morph_viewport.mjs
            ${CMAKE_CURRENT_SOURCE_DIR}/native-ui/materialized/materialized-document.runtime.json)
    set_tests_properties(Spectr-materialized-morph-viewport PROPERTIES TIMEOUT 30)
    # Four controls, one per wrong implementation, because each fails a
    # DIFFERENT assertion and a single plant that trips everything cannot show
    # which check is load bearing. The inversion lives in the script rather
    # than in WILL_FAIL, which accepts any non-zero exit -- including a usage
    # error that proves nothing. Each plant asserts its needle matched exactly
    # once, so a drifted needle fails the control instead of quietly
    # re-testing the current document.
    add_test(NAME Spectr-materialized-morph-viewport-snap-control
        COMMAND ${SPECTR_NODE_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/test/test_materialized_morph_viewport.mjs
            ${CMAKE_CURRENT_SOURCE_DIR}/native-ui/materialized/materialized-document.runtime.json
            --plant-snap --expect-fail)
    set_tests_properties(Spectr-materialized-morph-viewport-snap-control PROPERTIES
        TIMEOUT 30)
    add_test(NAME Spectr-materialized-morph-viewport-linear-control
        COMMAND ${SPECTR_NODE_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/test/test_materialized_morph_viewport.mjs
            ${CMAKE_CURRENT_SOURCE_DIR}/native-ui/materialized/materialized-document.runtime.json
            --plant-linear --expect-fail)
    set_tests_properties(Spectr-materialized-morph-viewport-linear-control PROPERTIES
        TIMEOUT 30)
    add_test(NAME Spectr-materialized-morph-viewport-live-commit-control
        COMMAND ${SPECTR_NODE_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/test/test_materialized_morph_viewport.mjs
            ${CMAKE_CURRENT_SOURCE_DIR}/native-ui/materialized/materialized-document.runtime.json
            --plant-live-commit --expect-fail)
    set_tests_properties(Spectr-materialized-morph-viewport-live-commit-control PROPERTIES
        TIMEOUT 30)
    add_test(NAME Spectr-materialized-morph-viewport-switch-control
        COMMAND ${SPECTR_NODE_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/test/test_materialized_morph_viewport.mjs
            ${CMAKE_CURRENT_SOURCE_DIR}/native-ui/materialized/materialized-document.runtime.json
            --plant-ignore-switch --expect-fail)
    set_tests_properties(Spectr-materialized-morph-viewport-switch-control PROPERTIES
        TIMEOUT 30)
endif()

# Two defects reported from the shipped AU, at the seam where native
# publications write the editor's JS refs directly. Neither is visible from a
# screenshot or from the standalone.
#
# CLEAR published its status pill and left every band standing. The band field
# reaches C++ through one effect whose echo suppressor is a ONE-SHOT: a
# projection arms it, the render that projection causes consumes it.
# applyHostAutomationState armed it and fired no shot -- it writes refs only,
# calls no state setter, so the publication effect never re-runs for it and the
# flag stays latched until an unrelated later edit consumes it instead. CLEAR
# was that edit, because it mutated the refs without raising the "local edit"
# signal commitGain/commitMany use to override the suppressor. The host
# dependence is the same fact: processing_state_live is published only when
# host_automation_revision moves, which needs a writer OUTSIDE the plugin.
#
# And a muted band paints on the -Infinity sentinel, which the draw loop holds
# by collapsing any finite value it finds on a muted band. All three
# projections into renderGains overrode the sentinel with 0, so every
# publication restarted an eight-frame collapse: measured on the pre-fix
# document, one constant value across 20 idle frames and -0.36 / -0.59 / -0.74
# repeating once projections arrive. No LFO is required -- host projections
# alone do it in a DAW -- but an LFO makes them continuous.
#
# The suite mounts the shipping document's own bank block in a vm and asserts
# on the payload that crosses the bridge and on the painted value. It asserts
# nothing about the status message: the banner said exactly the right thing
# while the product did the wrong thing.
if(SPECTR_NODE_EXECUTABLE)
    add_test(NAME Spectr-materialized-clear-and-mute-overlay
        COMMAND ${SPECTR_NODE_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/test/test_materialized_clear_and_mute_overlay.mjs
            ${CMAKE_CURRENT_SOURCE_DIR}/native-ui/materialized/materialized-document.runtime.json)
    set_tests_properties(Spectr-materialized-clear-and-mute-overlay PROPERTIES
        TIMEOUT 60)
    # Control 1: restore the pre-fix publication half -- the projection arms
    # the suppressor again AND the local mutators stop declaring their edit.
    # Both, because either alone keeps CLEAR working, which is exactly why the
    # fix carries both. The runtime rows for CLEAR and for preset-apply must
    # go red.
    add_test(NAME Spectr-materialized-clear-suppressor-control
        COMMAND ${SPECTR_NODE_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/test/test_materialized_clear_and_mute_overlay.mjs
            ${CMAKE_CURRENT_SOURCE_DIR}/native-ui/materialized/materialized-document.runtime.json
            --plant-latched-suppressor --expect-fail)
    set_tests_properties(Spectr-materialized-clear-suppressor-control PROPERTIES
        TIMEOUT 60)
    # Control 2: restore only the muted-band flattening. Separate row because
    # it fails different assertions, and a single plant that trips everything
    # cannot show which check is load bearing.
    add_test(NAME Spectr-materialized-mute-overlay-control
        COMMAND ${SPECTR_NODE_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/test/test_materialized_clear_and_mute_overlay.mjs
            ${CMAKE_CURRENT_SOURCE_DIR}/native-ui/materialized/materialized-document.runtime.json
            --plant-flatten-muted --expect-fail)
    set_tests_properties(Spectr-materialized-mute-overlay-control PROPERTIES
        TIMEOUT 60)
    # Control 3: drop clearGains' edit declaration only. With the suppressor
    # no longer armed by a path that cannot consume it, this cannot fail at
    # runtime -- so it proves the STATIC half is load bearing, which is the
    # only way that half can be asserted at all.
    add_test(NAME Spectr-materialized-clear-declaration-control
        COMMAND ${SPECTR_NODE_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/test/test_materialized_clear_and_mute_overlay.mjs
            ${CMAKE_CURRENT_SOURCE_DIR}/native-ui/materialized/materialized-document.runtime.json
            --plant-undeclared-clear --expect-fail)
    set_tests_properties(Spectr-materialized-clear-declaration-control PROPERTIES
        TIMEOUT 60)

    # The unmute flourish answers a TAP. A morph sweep or a slot recall carries
    # dozens of bands back across the mute threshold at once -- each a real
    # transition by the commit functions' own rules -- so on a loop the flair
    # repeats and reads as noise. This suite EXECUTES the shipping commit
    # functions and the two replay entry points and asserts on the pulse ref
    # being written PER SOURCE; a capture cannot adjudicate a repeating
    # animation, since a screenshot at any instant is a valid picture of both
    # builds.
    add_test(NAME Spectr-materialized-unmute-pulse-source
        COMMAND ${SPECTR_NODE_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/test/test_materialized_unmute_pulse_source.mjs
            ${CMAKE_CURRENT_SOURCE_DIR}/native-ui/materialized/materialized-document.runtime.json)
    set_tests_properties(Spectr-materialized-unmute-pulse-source PROPERTIES
        TIMEOUT 60)
    # Four controls, one per pre-fix spelling, as separate rows: a single plant
    # that trips everything cannot show which check is load bearing. The first
    # two restore the ungated pulse in each commit function; the second two
    # stop a replay entry point declaring itself, which is the half no
    # assertion inside the commit functions can reach.
    foreach(_pulse_plant ungate-gain ungate-many unmarked-morph unmarked-recall)
        add_test(NAME Spectr-materialized-unmute-pulse-control-${_pulse_plant}
            COMMAND ${SPECTR_NODE_EXECUTABLE}
                ${CMAKE_CURRENT_SOURCE_DIR}/test/test_materialized_unmute_pulse_source.mjs
                ${CMAKE_CURRENT_SOURCE_DIR}/native-ui/materialized/materialized-document.runtime.json
                --plant-${_pulse_plant} --expect-fail)
        set_tests_properties(Spectr-materialized-unmute-pulse-control-${_pulse_plant}
            PROPERTIES TIMEOUT 60)
    endforeach()

    # Group mute on `m`, and the keys it must not have taken. The context
    # menu's group action is a one-way mute, so the toggle this adds is a
    # capability the product had by no route -- both directions are asserted.
    add_test(NAME Spectr-materialized-mute-selection
        COMMAND ${SPECTR_NODE_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/test/test_materialized_mute_selection.mjs
            ${CMAKE_CURRENT_SOURCE_DIR}/native-ui/materialized/materialized-document.runtime.json)
    set_tests_properties(Spectr-materialized-mute-selection PROPERTIES
        TIMEOUT 60)
    # Four controls, separate rows so it is visible which check is load
    # bearing. `stale-closure` is the one that matters: the bank effect does
    # not depend on `selection`, so a method closing over it reads the empty
    # Set from mount and the shortcut silently does nothing.
    foreach(_mutesel_plant stale-closure unconditional-mute flatten-unmute unbound)
        add_test(NAME Spectr-materialized-mute-selection-control-${_mutesel_plant}
            COMMAND ${SPECTR_NODE_EXECUTABLE}
                ${CMAKE_CURRENT_SOURCE_DIR}/test/test_materialized_mute_selection.mjs
                ${CMAKE_CURRENT_SOURCE_DIR}/native-ui/materialized/materialized-document.runtime.json
                --plant-${_mutesel_plant} --expect-fail)
        set_tests_properties(Spectr-materialized-mute-selection-control-${_mutesel_plant}
            PROPERTIES TIMEOUT 60)
    endforeach()

    # What that selection LOOKS like on a muted band. A muted band's painted
    # value is the -Infinity sentinel, which the painter normalises to zero
    # rather than to -1.02, so its body has no height and the selection rect
    # collapsed to its own two outsets -- a 5px sliver on the zero line, which
    # is exactly where the mute chip's speaker glyph is drawn. Reported as "a
    # small bright box around an icon sitting inside a larger unhighlighted
    # button". Measured against the chip's own box, so a mark that merely grew
    # without covering the button still fails.
    add_test(NAME Spectr-materialized-mute-selection-highlight
        COMMAND ${SPECTR_NODE_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/test/test_materialized_mute_selection_highlight.mjs
            ${CMAKE_CURRENT_SOURCE_DIR}/native-ui/materialized/materialized-document.runtime.json)
    set_tests_properties(Spectr-materialized-mute-selection-highlight PROPERTIES
        TIMEOUT 60)
    # Four controls, separate rows so it is visible which check is load
    # bearing. `sliver` is the one that separates "the code grew a branch" from
    # "the mark covers the button", and `transparent` covers this suite's own
    # blind spot: it records drawing commands, so it cannot see an alpha-0
    # stroke without being told to look.
    foreach(_mutehl_plant body-rect sliver unconditional-ring transparent)
        add_test(NAME Spectr-materialized-mute-selection-highlight-control-${_mutehl_plant}
            COMMAND ${SPECTR_NODE_EXECUTABLE}
                ${CMAKE_CURRENT_SOURCE_DIR}/test/test_materialized_mute_selection_highlight.mjs
                ${CMAKE_CURRENT_SOURCE_DIR}/native-ui/materialized/materialized-document.runtime.json
                --plant-${_mutehl_plant} --expect-fail)
        set_tests_properties(Spectr-materialized-mute-selection-highlight-control-${_mutehl_plant}
            PROPERTIES TIMEOUT 60)
    endforeach()

    # The mute TRANSITION, sampled every frame rather than at its endpoints.
    # renderGainsRef was ramped toward -1.02 so the next line could notice it
    # crossed -1.01 and snap to the -Infinity sentinel -- a tripwire for the
    # state machine, not a gain the band travels to. The painter cannot tell
    # those apart, so it spent eleven frames drawing a box from the zero line to
    # 4.5px past the bottom of the plot. Reported as "an animation flash and is
    # just too much". The ramp now travels to 0 and trips on arrival.
    add_test(NAME Spectr-materialized-mute-collapse-direction
        COMMAND ${SPECTR_NODE_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/test/test_materialized_mute_collapse_direction.mjs
            ${CMAKE_CURRENT_SOURCE_DIR}/native-ui/materialized/materialized-document.runtime.json)
    set_tests_properties(Spectr-materialized-mute-collapse-direction PROPERTIES
        TIMEOUT 60)
    # Four controls, separate rows so it is visible which check is load bearing.
    # `midframe-dip` is the one that matters: it dips to the floor for a single
    # mid-transition frame, so it is absent at both endpoints. It caught a real
    # hole -- at the deep value the painter's own guard skips the band, so the
    # deepest frame records NO marks, and a neighbour-to-neighbour comparison
    # stepped straight over the gap. The check now runs against a running
    # maximum.
    foreach(_mutecd_plant plunge grow frozen midframe-dip)
        add_test(NAME Spectr-materialized-mute-collapse-direction-control-${_mutecd_plant}
            COMMAND ${SPECTR_NODE_EXECUTABLE}
                ${CMAKE_CURRENT_SOURCE_DIR}/test/test_materialized_mute_collapse_direction.mjs
                ${CMAKE_CURRENT_SOURCE_DIR}/native-ui/materialized/materialized-document.runtime.json
                --plant-${_mutecd_plant} --expect-fail)
        set_tests_properties(Spectr-materialized-mute-collapse-direction-control-${_mutecd_plant}
            PROPERTIES TIMEOUT 60)
    endforeach()

    # Emptying a snapshot slot, and RESET ALL finally meaning what it says.
    add_test(NAME Spectr-materialized-clear-snapshot
        COMMAND ${SPECTR_NODE_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/test/test_materialized_clear_snapshot.mjs
            ${CMAKE_CURRENT_SOURCE_DIR}/native-ui/materialized/materialized-document.runtime.json)
    set_tests_properties(Spectr-materialized-clear-snapshot PROPERTIES
        TIMEOUT 60)
    # Four controls, separate rows so it is visible which check is load
    # bearing. `reset-keeps-bank` is the one that matters: it is invisible to
    # a screenshot taken before the next projection lands, and reads as a
    # working reset right up until it undoes itself.
    foreach(_clearsnap_plant no-native-clear reset-keeps-bank anything-clearable status-sticks)
        add_test(NAME Spectr-materialized-clear-snapshot-control-${_clearsnap_plant}
            COMMAND ${SPECTR_NODE_EXECUTABLE}
                ${CMAKE_CURRENT_SOURCE_DIR}/test/test_materialized_clear_snapshot.mjs
                ${CMAKE_CURRENT_SOURCE_DIR}/native-ui/materialized/materialized-document.runtime.json
                --plant-${_clearsnap_plant} --expect-fail)
        set_tests_properties(Spectr-materialized-clear-snapshot-control-${_clearsnap_plant}
            PROPERTIES TIMEOUT 60)
    endforeach()
endif()

# The two graph axis headings must be column headers over their own rulers,
# not captions floating inside the plot. Both were reported by the user as
# "text on the graph": each was anchored to a plot EDGE while the ticks it
# names are anchored 8px OUTSIDE that edge with the opposite alignment.
#
# The axis rulers are painted onto a canvas, so no layout detector can see
# them -- there are no nodes, no boxes and no measured_text_boxes to read. The
# only way to measure a canvas heading is to run the painter, so this suite
# lifts `drawRulers` out of the shipping document, executes it against a
# recording 2D-context stub at four window sizes, and asserts each heading
# shares BOTH its anchor x and its textAlign with a tick unique to its own
# column. It pins no pixel constant, which is the point: a hardcoded offset
# would drift on the next resize or band-count change.
#
# It also really PARSES every script block via dynamic import, discriminating
# on the error class -- `node --check` exits 0 on broken input and is not a
# syntax checker -- and executing the ruler body is what catches a scope
# defect, which parses clean by definition.
if(SPECTR_NODE_EXECUTABLE)
    # Preset-manager operations, asserted against the shipping document: the
    # delete path must not call a browser global this runtime does not define,
    # the USER empty state must distinguish an empty library from a query that
    # matched nothing, EXPORT must check for a file sink before claiming a
    # file, DUPLICATE and SET AS DEFAULT must reach the pattern library, and
    # the search field must read its value the guarded way its siblings do.
    add_test(NAME Spectr-materialized-preset-operations
        COMMAND ${SPECTR_NODE_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/test/test_materialized_preset_operations.mjs
            ${CMAKE_CURRENT_SOURCE_DIR}/native-ui/materialized/materialized-document.runtime.json)
    set_tests_properties(Spectr-materialized-preset-operations PROPERTIES
        TIMEOUT 60)
    # One plant per rule, and each plant restores EXACTLY the pre-fix shape of
    # its own rule. The harness requires a planted run to redden that rule AND
    # NO OTHER -- a stricter contract than --expect-fail alone, which a plant
    # tripping four rules at once satisfies while proving nothing about any of
    # them. An UNPLANTED run under --expect-fail exits non-zero, so the
    # inversion is not a free pass.
    foreach(_preset_plant delete-confirm empty-state export-file-sink
                          duplicate-library set-default-library search-value)
        add_test(NAME Spectr-materialized-preset-operations-control-${_preset_plant}
            COMMAND ${SPECTR_NODE_EXECUTABLE}
                ${CMAKE_CURRENT_SOURCE_DIR}/test/test_materialized_preset_operations.mjs
                ${CMAKE_CURRENT_SOURCE_DIR}/native-ui/materialized/materialized-document.runtime.json
                --plant ${_preset_plant} --expect-fail)
        set_tests_properties(Spectr-materialized-preset-operations-control-${_preset_plant}
            PROPERTIES TIMEOUT 60)
    endforeach()

    add_test(NAME Spectr-materialized-axis-headers
        COMMAND ${SPECTR_NODE_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/test/test_materialized_axis_headers.mjs
            ${CMAKE_CURRENT_SOURCE_DIR}/native-ui/materialized/materialized-document.runtime.json)
    set_tests_properties(Spectr-materialized-axis-headers PROPERTIES TIMEOUT 60)
    # Four negative controls, each restoring a different half of the pre-fix
    # shape, so a failure names which assertion is load bearing instead of one
    # plant tripping everything. `--expect-fail` inverts the verdict inside the
    # script rather than via WILL_FAIL, which would accept any non-zero exit --
    # including the exit 2 the harness uses when it cannot run at all. Each
    # plant asserts its needle matched exactly once, so a drifted needle fails
    # the control instead of quietly re-testing the current document.
    #
    #   nudge   moves the dB heading 8px and changes nothing else, so the
    #           GEOMETRY assertion is the only thing that can fire.
    #   align   restores the full pre-fix heading (caption text, left aligned,
    #           anchored at the plot edge).
    #   caption restores the SPECTRAL mode label inside the plot.
    #   syntax  breaks the ruler body, proving the parse arm can fail.
    foreach(_axis_plant nudge align caption syntax)
        add_test(NAME Spectr-materialized-axis-headers-control-${_axis_plant}
            COMMAND ${SPECTR_NODE_EXECUTABLE}
                ${CMAKE_CURRENT_SOURCE_DIR}/test/test_materialized_axis_headers.mjs
                ${CMAKE_CURRENT_SOURCE_DIR}/native-ui/materialized/materialized-document.runtime.json
                --plant ${_axis_plant} --expect-fail)
        set_tests_properties(Spectr-materialized-axis-headers-control-${_axis_plant}
            PROPERTIES TIMEOUT 60)
    endforeach()
endif()

# The response curve is plotted through band CENTRES, so at the two extremes
# it began and ended halfway across the first and last band -- a visibly
# half-drawn band at each end of the plot. It also ran straight across MUTED
# bands, so an audible group's line trailed past its own edge before dropping
# to 0 at the mute, which is what the user reported. The rule the suite pins:
# the curve breaks at every muted band, and each audible run is held flat from
# its own first band's left edge to its own last band's right edge -- exactly
# what the fft stair-step already draws, and the plot extremes fall out of it.
#
# Like the axis headings above, this is canvas ink with no layout node, so
# painted_vs_measured_width / box_intersection / ink_extents are structurally
# blind to it. This suite runs the shipping document's own bank block against
# a recording 2D context and reads the coordinates the real painter emitted,
# asserting them against the document's OWN bandLeftX / getGeom expressions at
# 32 and 64 bands and two window sizes. It pins no pixel constant: band count
# and window size are exactly what the user changes.
if(SPECTR_NODE_EXECUTABLE)
    add_test(NAME Spectr-materialized-curve-edge-span
        COMMAND ${SPECTR_NODE_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/test/test_materialized_curve_edge_span.mjs
            ${CMAKE_CURRENT_SOURCE_DIR}/native-ui/materialized/materialized-document.runtime.json)
    set_tests_properties(Spectr-materialized-curve-edge-span PROPERTIES TIMEOUT 60)
    # Five negative controls, each restoring one pre-fix form so a failure
    # names which assertion is load bearing. `--expect-fail` inverts the
    # verdict inside the script rather than via WILL_FAIL, which would accept
    # the exit 2 the harness uses when it cannot run at all -- a usage error
    # would satisfy the control and prove nothing.
    #
    #   response-centres      the RESPONSE line goes back to band centres AND
    #                         back to plunging across every mute.
    #   response-spans-mutes  the RESPONSE line keeps both outer plot edges but
    #                         plunges across interior mutes again. This is the
    #                         exact form the user reported, so a suite that
    #                         cannot reject it does not cover the defect.
    #   fill-spans-mutes      the iir fill slides back under muted bands its
    #                         own stroke skips.
    #   overlay-centres       the iir/hybrid dsp curve goes back to centres.
    #   geometry-follows-view band columns start tracking the zoom window,
    #                         which would make the first/last VISIBLE band
    #                         stop being band 0 / N-1 and silently point every
    #                         span in this suite at the wrong columns.
    foreach(_curve_plant response-centres response-spans-mutes fill-spans-mutes
                         overlay-centres geometry-follows-view)
        add_test(NAME Spectr-materialized-curve-edge-control-${_curve_plant}
            COMMAND ${SPECTR_NODE_EXECUTABLE}
                ${CMAKE_CURRENT_SOURCE_DIR}/test/test_materialized_curve_edge_span.mjs
                ${CMAKE_CURRENT_SOURCE_DIR}/native-ui/materialized/materialized-document.runtime.json
                --plant-${_curve_plant} --expect-fail)
        set_tests_properties(Spectr-materialized-curve-edge-control-${_curve_plant}
            PROPERTIES TIMEOUT 60)
    endforeach()
endif()

# Appearance invariants over a dump_layout_tree() snapshot: two painted glyph
# runs must not overlap, and a run must fit the box laid out for it. Both
# adjudicate on measured_text_boxes[].rect after intersecting with the node's
# clip, never on the node's layout rect -- a layout box carries padding and
# centring slack, and a node scrolled out of its container keeps visible:true
# while painting nothing, so reading either naively manufactures overlaps that
# are not on screen.
#
# The fixture is a real dump from the shipping standalone with Settings open.
# It is adjudicated, not assumed: every run this suite reports on was checked
# against the ink under its rect in the matching screenshot.
#
# Coverage is partial and the suite says so on every run. The dump reports a
# painted width of 0.0 for 63 of the 101 text-bearing Labels -- the tell is an
# integer box height, the layout line box, where a shaped run carries a
# fractional one -- and 28 of those have proven ink, including every label in
# the Settings body. Neither detector can see a defect in those runs.
find_package(Python3 COMPONENTS Interpreter QUIET)
if(Python3_Interpreter_FOUND)
    add_test(NAME Spectr-appearance-invariants
        COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/tools/appearance_invariants.py
            ${CMAKE_CURRENT_SOURCE_DIR}/test/fixtures/settings-open-layout.json)
    set_tests_properties(Spectr-appearance-invariants PROPERTIES TIMEOUT 60)

    # The negative controls. Each plant injects the exact defect its detector
    # exists to catch -- a straddling collision, and a run wider than its box --
    # and the row passes only when the detector REJECTS it. A detector that
    # cannot be made to fail proves nothing, so these turn a weakened assertion
    # green->red instead of letting it agree silently.
    add_test(NAME Spectr-appearance-invariants-overlap-control
        COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/tools/appearance_invariants.py
            ${CMAKE_CURRENT_SOURCE_DIR}/test/fixtures/settings-open-layout.json
            --plant-overlap)
    set_tests_properties(Spectr-appearance-invariants-overlap-control PROPERTIES
        TIMEOUT 60
        WILL_FAIL TRUE)

    add_test(NAME Spectr-appearance-invariants-overflow-control
        COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/tools/appearance_invariants.py
            ${CMAKE_CURRENT_SOURCE_DIR}/test/fixtures/settings-open-layout.json
            --plant-overflow)
    set_tests_properties(Spectr-appearance-invariants-overflow-control PROPERTIES
        TIMEOUT 60
        WILL_FAIL TRUE)
endif()
# The seven browser oracles below are the ONLY coverage for the status overlay,
# preset detail, Settings polish, and dropdown rows. If node or Chrome is
# missing they are simply never registered, so a run without them is not a
# weaker green -- it is a green with that entire surface unmeasured, and
# nothing says so. Make the absence loud.
#
# Off by default so a contributor without Chrome can still build and test.
# NOTE: as of this writing no CI workflow passes
# -DSPECTR_REQUIRE_BROWSER_LANE=ON (verified: zero occurrences of this option
# name under .github/workflows/). CI configures with the same default as a
# local build, so on any CI host missing node/Chrome this degrades to the
# WARNING branch below, not a hard failure -- the browser-oracle surface can
# go silently unmeasured on CI too. If you are relying on this flag to make
# CI enforce the browser lane, it currently does not; wire
# -DSPECTR_REQUIRE_BROWSER_LANE=ON into the relevant workflow's configure step
# (and confirm that runner actually has node + Chrome) before trusting that.
option(SPECTR_REQUIRE_BROWSER_LANE
    "Fail if the headless-Chromium oracles cannot be registered" OFF)
if(NOT (SPECTR_NODE_EXECUTABLE AND SPECTR_CHROME_EXECUTABLE))
    if(SPECTR_REQUIRE_BROWSER_LANE)
        message(FATAL_ERROR
            "SPECTR_REQUIRE_BROWSER_LANE=ON but the browser oracle lane cannot run: "
            "node=${SPECTR_NODE_EXECUTABLE} chrome=${SPECTR_CHROME_EXECUTABLE}. "
            "Every status-overlay, preset-detail, Settings-polish and dropdown "
            "assertion lives in that lane; without it they are unmeasured.")
    else()
        message(WARNING
            "Spectr browser oracle lane DISABLED (node=${SPECTR_NODE_EXECUTABLE} "
            "chrome=${SPECTR_CHROME_EXECUTABLE}). The status-overlay, preset-detail, "
            "Settings-polish and dropdown rows will not be measured by this build.")
    endif()
endif()
if(SPECTR_NODE_EXECUTABLE AND SPECTR_CHROME_EXECUTABLE)
    add_test(NAME Spectr-browser-analyzer
        COMMAND ${SPECTR_NODE_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/test/test_editor_analyzer_browser.mjs
            ${CMAKE_CURRENT_SOURCE_DIR}/resources/editor.html
            ${SPECTR_CHROME_EXECUTABLE})
    set_tests_properties(Spectr-browser-analyzer PROPERTIES TIMEOUT 150)
    add_test(NAME Spectr-browser-resize
        COMMAND ${SPECTR_NODE_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/test/test_editor_analyzer_browser.mjs
            ${CMAKE_CURRENT_SOURCE_DIR}/resources/editor.html
            ${SPECTR_CHROME_EXECUTABLE}
            --resize-only)
    set_tests_properties(Spectr-browser-resize PROPERTIES TIMEOUT 90)
    add_test(NAME Spectr-browser-popups
        COMMAND ${SPECTR_NODE_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/test/test_editor_analyzer_browser.mjs
            ${CMAKE_CURRENT_SOURCE_DIR}/resources/editor.html
            ${SPECTR_CHROME_EXECUTABLE}
            --popup-only)
    set_tests_properties(Spectr-browser-popups PROPERTIES TIMEOUT 90)
    # Ten checks: five edit modes x redraw-unmutes on/off, asserting the band
    # state each mode's real pointer handler publishes -- not the shared
    # decision point, which would pass even if a mode bypassed it.
    add_test(NAME Spectr-browser-mute-modes
        COMMAND ${SPECTR_NODE_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/test/test_editor_analyzer_browser.mjs
            ${CMAKE_CURRENT_SOURCE_DIR}/resources/editor.html
            ${SPECTR_CHROME_EXECUTABLE}
            --mute-modes)
    set_tests_properties(Spectr-browser-mute-modes PROPERTIES TIMEOUT 180)
    add_test(NAME Spectr-browser-build-info-timeout
        COMMAND ${SPECTR_NODE_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/test/test_materialized_build_info_browser.mjs
            ${CMAKE_CURRENT_SOURCE_DIR}/native-ui/materialized/materialized-document.runtime.json
            ${CMAKE_CURRENT_SOURCE_DIR}/resources/editor.html
            ${SPECTR_CHROME_EXECUTABLE})
    set_tests_properties(Spectr-browser-build-info-timeout PROPERTIES TIMEOUT 45)
    add_test(NAME Spectr-browser-ux-polish
        COMMAND ${SPECTR_NODE_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/test/test_materialized_ux_polish_browser.mjs
            ${CMAKE_CURRENT_SOURCE_DIR}/native-ui/materialized/materialized-document.runtime.json
            ${CMAKE_CURRENT_SOURCE_DIR}/resources/editor.html
            ${SPECTR_CHROME_EXECUTABLE})
    # This suite launches Chrome nine times in series (six scenarios plus three
    # negative controls), each bounded to 30s by the runner itself, so its own
    # ceiling is ~270s and no wall-clock under that can bound it. A 90s budget
    # was under the suite: six consecutive fully-passing runs on a loaded host
    # measured 72/75/77/92/97/111s, so 90s would have killed three of the six
    # mid-run and reported a ctest timeout instead of naming the slow launch.
    # Budget above the runner so a real hang surfaces as the runner's own
    # per-launch timeout, which says which scenario hung.
    set_tests_properties(Spectr-browser-ux-polish PROPERTIES TIMEOUT 300)
    add_test(NAME Spectr-browser-preset-parity
        COMMAND ${SPECTR_NODE_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/test/test_preset_parity_browser.mjs
            ${CMAKE_CURRENT_SOURCE_DIR}/resources/editor.html
            --shipping-only
            ${SPECTR_CHROME_EXECUTABLE})
    set_tests_properties(Spectr-browser-preset-parity PROPERTIES TIMEOUT 120)
    # The negative control for the check above. test_preset_parity_browser.mjs
    # has always accepted --plant-overlap, which injects an action collision
    # into factory:flat, but nothing ever passed it: the flag is the 5th
    # positional argument and the registration above supplies three. So the
    # overlap assertion has been running green since it was written without any
    # evidence it can go red -- a blind oracle.
    #
    # WILL_FAIL inverts the verdict: this test passes only when the script
    # REJECTS the planted overlap. If someone weakens the overlap check, the
    # control turns green->red here rather than silently agreeing with them.
    # Verified by hand before registering: baseline exits 0, planted exits 1
    # with "factory:flat action overlap: APPLY/★ DEFAULT".
    add_test(NAME Spectr-browser-preset-parity-negative-control
        COMMAND ${SPECTR_NODE_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/test/test_preset_parity_browser.mjs
            ${CMAKE_CURRENT_SOURCE_DIR}/resources/editor.html
            --shipping-only
            ${SPECTR_CHROME_EXECUTABLE}
            ${CMAKE_CURRENT_BINARY_DIR}/preset-parity-negative-control
            --plant-overlap)
    set_tests_properties(Spectr-browser-preset-parity-negative-control PROPERTIES
        TIMEOUT 120
        WILL_FAIL TRUE)
    # Second negative control, for the long-name truncation assertion. Same
    # reasoning: the check is only worth having if it can go red, and this
    # proves it does. --plant-untruncated reports the title box at its full
    # unwrapped width, as if the name had never been clipped; the containment
    # assertion must reject it. Verified: baseline exits 0, planted exits 1 with
    # "preset title spills 308px past the detail panel instead of truncating".
    add_test(NAME Spectr-browser-preset-truncation-negative-control
        COMMAND ${SPECTR_NODE_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/test/test_preset_parity_browser.mjs
            ${CMAKE_CURRENT_SOURCE_DIR}/resources/editor.html
            --shipping-only
            ${SPECTR_CHROME_EXECUTABLE}
            ${CMAKE_CURRENT_BINARY_DIR}/preset-truncation-negative-control
            --plant-untruncated)
    set_tests_properties(Spectr-browser-preset-truncation-negative-control PROPERTIES
        TIMEOUT 120
        WILL_FAIL TRUE)
endif()

# These need node only -- NOT chrome. Registered under the browser
# oracle's node+chrome guard they would silently not exist on a runner
# without chrome, which is exactly what the acceptance runner is, and
# the `materialized` acceptance pattern would still match its other
# tests and report nothing wrong.
if(SPECTR_NODE_EXECUTABLE)
    # The marquee's RESULT. A person asked to hold two separate frequency
    # ranges at once, so the assertion is the selection SET -- both regions
    # present and the gap between them absent -- rather than "a handler ran",
    # which a handler computing the wrong set also satisfies. The suite drives
    # the shipped pointer handlers out of the materialized document under a
    # minimal React shim, because a discontiguous selection has no layout node
    # to look at.
    add_test(NAME Spectr-materialized-additive-marquee
        COMMAND ${SPECTR_NODE_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/test/test_materialized_additive_marquee.mjs
            ${CMAKE_CURRENT_SOURCE_DIR}/native-ui/materialized/materialized-document.runtime.json)
    set_tests_properties(Spectr-materialized-additive-marquee PROPERTIES TIMEOUT 60)
    # Three controls, one per way this can rot. Each reinstates a real defect
    # and inverts the verdict inside the script; WILL_FAIL is deliberately not
    # used, because it accepts any non-zero exit including the ones that mean
    # the control never ran.
    add_test(NAME Spectr-materialized-additive-marquee-replaces-control
        COMMAND ${SPECTR_NODE_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/test/test_materialized_additive_marquee.mjs
            ${CMAKE_CURRENT_SOURCE_DIR}/native-ui/materialized/materialized-document.runtime.json
            --plant-replaces --expect-fail)
    set_tests_properties(Spectr-materialized-additive-marquee-replaces-control
        PROPERTIES TIMEOUT 60)
    # Add-only passes the discontiguous assertion and fails only the remove
    # half, which is exactly the shape of a row whose label over-promises.
    add_test(NAME Spectr-materialized-additive-marquee-add-only-control
        COMMAND ${SPECTR_NODE_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/test/test_materialized_additive_marquee.mjs
            ${CMAKE_CURRENT_SOURCE_DIR}/native-ui/materialized/materialized-document.runtime.json
            --plant-add-only --expect-fail)
    set_tests_properties(Spectr-materialized-additive-marquee-add-only-control
        PROPERTIES TIMEOUT 60)
    # Toggling against the live selection instead of the base frozen at press.
    # A short drag still looks right; the selection strobes as the pointer
    # keeps sampling inside a band it already flipped.
    add_test(NAME Spectr-materialized-additive-marquee-live-toggle-control
        COMMAND ${SPECTR_NODE_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/test/test_materialized_additive_marquee.mjs
            ${CMAKE_CURRENT_SOURCE_DIR}/native-ui/materialized/materialized-document.runtime.json
            --plant-live-toggle --expect-fail)
    set_tests_properties(Spectr-materialized-additive-marquee-live-toggle-control
        PROPERTIES TIMEOUT 60)
endif()

# No SHORTCUTS row may wrap, and none may be ABLE to. The budget half reads
# the shipping artifact and needs no binary, so it holds even where the
# standalone cannot run; the rendered half opens the panel in the shipping
# native editor and measures every row's line box.
if(Python3_Interpreter_FOUND)
    add_test(NAME Spectr-shortcut-panel-budget
        COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/tools/spectr-detectors/shortcut_panel_single_line.py
            --budget-only)
    set_tests_properties(Spectr-shortcut-panel-budget PROPERTIES TIMEOUT 60)
    # A budget nobody has watched reject a row is not a budget. --plant
    # long-label restores the wording that wrapped in the shipped build.
    add_test(NAME Spectr-shortcut-panel-budget-negative-control
        COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/tools/spectr-detectors/shortcut_panel_single_line.py
            --budget-only --plant long-label)
    set_tests_properties(Spectr-shortcut-panel-budget-negative-control PROPERTIES
        TIMEOUT 60
        WILL_FAIL TRUE)
endif()

# The About guide's route in, its scroll, and the two properties of its tail
# that only this file asserts: the caption is nested markup rather than the
# button's own text, and the body is memoised. Both are invisible to every
# other gate here -- a flat caption renders a label that is merely 7.1px high
# in its box, and an unmemoised body renders pixel for pixel the same and only
# costs more. Artifact-only, so it holds wherever Python does.
if(Python3_Interpreter_FOUND)
    add_test(NAME Spectr-materialized-help-overlay-contract
        COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/tools/spectr-detectors/help_overlay_contract.py)
    set_tests_properties(Spectr-materialized-help-overlay-contract PROPERTIES
        TIMEOUT 60)
    # A contract nobody has watched reject the shipped defect is not a
    # contract. --plant flat-caption rebuilds the flat markup that put the
    # caption at the top of its own box.
    add_test(NAME Spectr-materialized-help-overlay-contract-negative-control
        COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/tools/spectr-detectors/help_overlay_contract.py
            --plant flat-caption)
    set_tests_properties(Spectr-materialized-help-overlay-contract-negative-control
        PROPERTIES TIMEOUT 60 WILL_FAIL TRUE)
    # The guide now states a latency figure per mode, and forbids two phrases
    # the design ruling rejects. One plant each, because a single plant cannot
    # show that every rule is load bearing: staling only the Mixing figure
    # would leave the Tracking check unexercised and free to be wrong, and a
    # plant that removed a figure while adding a forbidden phrase would be
    # caught by the figure rule with the phrase rule never firing. Each plant
    # below was verified by hand to fail on ITS OWN rule and no other.
    foreach(_help_plant
            stale-latency stale-latency-tracking
            zero-latency-claim seamless-claim
            lost-percussive-guidance
            # The automation guidance. `stale-param-name` is the only one of
            # these caught by the parameter-name rule rather than the approved
            # copy rule: it renames the control in the PROSE, which reads
            # perfectly and sends the user hunting for something their host
            # does not list. The other three each delete one sentence that
            # answers a question the parameter list cannot -- that untouched
            # bands stay put under a morph, that mute alone does not blend,
            # and that MIDI CC is the wrong instrument entirely.
            stale-param-name lost-range-guidance
            lost-mute-caveat lost-midi-answer lost-mix-target
            # The four macro lanes leave the enumeration. They are registered
            # unconditionally, so every host lists them whether or not the
            # guide admits it; a list that claims to name what is worth
            # knowing and omits four lanes sends a user hunting.
            lost-macro-lanes)
        add_test(NAME Spectr-materialized-help-overlay-control-${_help_plant}
            COMMAND ${Python3_EXECUTABLE}
                ${CMAKE_CURRENT_SOURCE_DIR}/tools/spectr-detectors/help_overlay_contract.py
                --plant ${_help_plant})
        set_tests_properties(Spectr-materialized-help-overlay-control-${_help_plant}
            PROPERTIES TIMEOUT 60 WILL_FAIL TRUE)
    endforeach()
    # Plants against the REGISTRATION SITES rather than the copy, which is the
    # half the copy alone can never defend: a parameter renamed in
    # param_surface.cpp leaves every word of the guide correct while the name
    # it tells a user to find stops existing. `renamed-morph` is that rename;
    # `unpadded-band-names` drops the zero padding, so "Band 01 Gain" becomes
    # a name no host will show. Both are silent to every other gate here.
    # `padded-macro-names` is the same drift on the macro lanes: the format
    # string stays findable, so the plant exercises the name comparison rather
    # than the parser's own missing-format error, which a WILL_FAIL row would
    # accept without the rule ever running.
    foreach(_help_source_plant renamed-morph unpadded-band-names
            padded-macro-names)
        add_test(NAME Spectr-materialized-help-overlay-control-${_help_source_plant}
            COMMAND ${Python3_EXECUTABLE}
                ${CMAKE_CURRENT_SOURCE_DIR}/tools/spectr-detectors/help_overlay_contract.py
                --plant-source ${_help_source_plant})
        set_tests_properties(Spectr-materialized-help-overlay-control-${_help_source_plant}
            PROPERTIES TIMEOUT 60 WILL_FAIL TRUE)
    endforeach()
endif()

# A dropdown must keep its OWN captured state after both snapshot slots fill.
# `snapshots-morph` matches a LEVEL -- [data-spectr-snapshots-ready="true"] is
# true from the second capture until relaunch -- and array order is the whole
# precedence rule in resolveCapturedStateFromAtlas(), so sitting above
# edit/analyzer/pattern it shadowed all three for the rest of the session.
# Their option rows then lost the bindings that stack label over description
# and re-laid out side-by-side. The receipt half adjudicates a committed
# reading and needs no binary; the live half drives the shipping standalone.
if(Python3_Interpreter_FOUND)
    add_test(NAME Spectr-materialized-captured-state-survives-snapshots-receipt
        COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/tools/spectr-detectors/captured_state_survives_snapshots.py
            ${CMAKE_CURRENT_SOURCE_DIR}/docs/evidence/2026-09-13/CAPTURED-STATE-GREEN-menus-survive-snapshots.json)
    set_tests_properties(Spectr-materialized-captured-state-survives-snapshots-receipt
        PROPERTIES TIMEOUT 60)
    # A rule nobody has watched reject the defect is not a rule. --plant
    # shadowed restores exactly what the pre-fix binary reported.
    add_test(NAME Spectr-materialized-captured-state-survives-snapshots-negative-control
        COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/tools/spectr-detectors/captured_state_survives_snapshots.py
            ${CMAKE_CURRENT_SOURCE_DIR}/docs/evidence/2026-09-13/CAPTURED-STATE-GREEN-menus-survive-snapshots.json
            --plant shadowed)
    set_tests_properties(Spectr-materialized-captured-state-survives-snapshots-negative-control
        PROPERTIES TIMEOUT 60 WILL_FAIL TRUE)
    # The opposite over-fix: silencing snapshots-morph entirely would pass
    # every menu case above while deleting the morph surface's own layout.
    add_test(NAME Spectr-materialized-captured-state-level-still-resolves-control
        COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/tools/spectr-detectors/captured_state_survives_snapshots.py
            ${CMAKE_CURRENT_SOURCE_DIR}/docs/evidence/2026-09-13/CAPTURED-STATE-GREEN-menus-survive-snapshots.json
            --plant level-lost)
    set_tests_properties(Spectr-materialized-captured-state-level-still-resolves-control
        PROPERTIES TIMEOUT 60 WILL_FAIL TRUE)
endif()

if(Python3_Interpreter_FOUND AND TARGET ${SPECTR_NATIVE_PLUGIN_TARGET}_Standalone)
    add_test(NAME Spectr-materialized-captured-state-survives-snapshots-live
        COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/tools/spectr-detectors/captured_state_survives_snapshots.py
            --app $<TARGET_FILE:${SPECTR_NATIVE_PLUGIN_TARGET}_Standalone>)
    set_tests_properties(Spectr-materialized-captured-state-survives-snapshots-live
        PROPERTIES
        TIMEOUT 900
        RESOURCE_LOCK spectr_native_materialized_editor)
endif()

if(Python3_Interpreter_FOUND AND TARGET ${SPECTR_NATIVE_PLUGIN_TARGET}_Standalone)
    add_test(NAME Spectr-shortcut-panel-single-line
        COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/tools/spectr-detectors/shortcut_panel_single_line.py
            --app $<TARGET_FILE:${SPECTR_NATIVE_PLUGIN_TARGET}_Standalone>
            --out-dir ${CMAKE_CURRENT_BINARY_DIR}/spectr-shortcut-panel)
    set_tests_properties(Spectr-shortcut-panel-single-line PROPERTIES
        TIMEOUT 300
        RESOURCE_LOCK spectr_native_materialized_editor)
endif()

# Switching the Latency mode must not move one pixel of the Settings panel.
#
# The reserve that was supposed to settle this already had a test, and that
# test read the DECLARED constant (`style.minHeight`) out of the document and
# required both selections to declare the same one. They always did -- it is a
# single literal -- so it read 52 against 52 while "Mixing" rendered 61px tall
# and the panel still moved 9px on every switch, and it would read 52 against
# 52 if the panel moved 200px. A reserve is a claim about RENDERED height, so
# the only instrument that can settle it is the renderer.
#
# The receipt half adjudicates a committed capture pair and needs no binary, so
# it holds wherever Python does; the live half drives the shipping standalone,
# presses each chip and compares the two settled layouts box by box. The
# control half re-adjudicates the PRE-FIX pair recorded in the same receipt --
# real captures from the build the owner reported, in which 302 boxes move and
# the reserved row goes 61 -> 52.
if(Python3_Interpreter_FOUND)
    add_test(NAME Spectr-settings-render-mode-no-reflow-receipt
        COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/tools/spectr-detectors/settings_render_mode_no_reflow.py
            --receipt
            ${CMAKE_CURRENT_SOURCE_DIR}/docs/evidence/2026-09-15/SETTINGS-RENDER-MODE-no-reflow.json)
    set_tests_properties(Spectr-settings-render-mode-no-reflow-receipt PROPERTIES
        TIMEOUT 60)
    # --expect-fail lives in the script rather than in WILL_FAIL because
    # WILL_FAIL accepts ANY non-zero exit, so a missing receipt or a usage
    # error would read green while proving nothing -- and this detector also
    # exits 2 for "no verdict reached", which WILL_FAIL cannot tell from
    # "rejected the defect".
    add_test(NAME Spectr-settings-render-mode-no-reflow-negative-control
        COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/tools/spectr-detectors/settings_render_mode_no_reflow.py
            --receipt
            ${CMAKE_CURRENT_SOURCE_DIR}/docs/evidence/2026-09-15/SETTINGS-RENDER-MODE-no-reflow.json
            --plant fixed-reserve-too-small --expect-fail)
    set_tests_properties(Spectr-settings-render-mode-no-reflow-negative-control
        PROPERTIES TIMEOUT 60)
    # A SECOND plant, because the first exercises only the two "nothing moves"
    # rules and leaves the two rules about the reserve's SIZE proven by
    # nothing. This one pins every reserve below the text it has to hold,
    # identically in both captures: the panel moves not one pixel -- the first
    # two rules stay green -- while the taller sentence is clipped. That is the
    # case a box-against-box comparison reads as 0 and cannot see, which is why
    # the fit rule measures SHAPED text height instead.
    add_test(NAME Spectr-settings-render-mode-no-reflow-clipped-control
        COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/tools/spectr-detectors/settings_render_mode_no_reflow.py
            --receipt
            ${CMAKE_CURRENT_SOURCE_DIR}/docs/evidence/2026-09-15/SETTINGS-RENDER-MODE-no-reflow.json
            --plant clipped-reserve --expect-fail)
    set_tests_properties(Spectr-settings-render-mode-no-reflow-clipped-control
        PROPERTIES TIMEOUT 60)
endif()

if(Python3_Interpreter_FOUND AND TARGET ${SPECTR_NATIVE_PLUGIN_TARGET}_Standalone)
    add_test(NAME Spectr-settings-render-mode-no-reflow-live
        COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/tools/spectr-detectors/settings_render_mode_no_reflow.py
            --app $<TARGET_FILE:${SPECTR_NATIVE_PLUGIN_TARGET}_Standalone>
            --out-dir ${CMAKE_CURRENT_BINARY_DIR}/spectr-render-mode-reflow)
    set_tests_properties(Spectr-settings-render-mode-no-reflow-live PROPERTIES
        TIMEOUT 600
        RESOURCE_LOCK spectr_native_materialized_editor)
endif()

# ── Plugin targets ─────────────────────────────────────────────────────

if(SPECTR_BUILD_WEBVIEW_REFERENCE)
pulp_add_plugin(SpectrWebViewReference
    FORMATS         VST3 AU CLAP Standalone
    PLUGIN_NAME     "Spectr WebView Reference"
    BUNDLE_ID       "com.pulp.spectr.webview-reference"
    MANUFACTURER    "Pulp"
    VERSION         "1.0.0"
    CATEGORY        Effect
    PLUGIN_CODE     "SpWR"
    MANUFACTURER_CODE "Pulp"
    AAX_PRODUCT_CODE "SpeP"
    AAX_NATIVE_CODE "Spec"
    DESIGN_WIDTH      ${SPECTR_HOST_PREFERRED_WIDTH}
    DESIGN_HEIGHT     ${SPECTR_HOST_PREFERRED_HEIGHT}
    DESIGN_MIN_WIDTH  ${SPECTR_HOST_MIN_WIDTH}
    DESIGN_MIN_HEIGHT ${SPECTR_HOST_MIN_HEIGHT}
    DESIGN_MAX_WIDTH  ${SPECTR_HOST_MAX_WIDTH}
    DESIGN_MAX_HEIGHT ${SPECTR_HOST_MAX_HEIGHT}
)

# Attach our sources + include path to each Spectr plugin target that pulp
# generated. Targets are named Spectr_VST3 / Spectr_AU / Spectr_CLAP /
# Spectr_Standalone.
foreach(fmt VST3 AU CLAP Standalone)
    set(tgt SpectrWebViewReference_${fmt})
    if(TARGET ${tgt})
        target_sources(${tgt} PRIVATE ${SPECTR_SOURCES})
        target_include_directories(${tgt} PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/include
        )
        target_link_libraries(${tgt} PRIVATE spectr_editor_assets)
        target_compile_definitions(${tgt} PRIVATE
            SPECTR_WEBVIEW_REFERENCE=1
            SPECTR_AUTHORED_DESIGN_W=${SPECTR_AUTHORED_WIDTH}
            SPECTR_AUTHORED_DESIGN_H=${SPECTR_AUTHORED_HEIGHT}
            SPECTR_HOST_PREFERRED_W=${SPECTR_HOST_PREFERRED_WIDTH}
            SPECTR_HOST_PREFERRED_H=${SPECTR_HOST_PREFERRED_HEIGHT}
            SPECTR_HOST_MIN_W=${SPECTR_HOST_MIN_WIDTH}
            SPECTR_HOST_MIN_H=${SPECTR_HOST_MIN_HEIGHT}
            SPECTR_HOST_MAX_W=${SPECTR_HOST_MAX_WIDTH}
            SPECTR_HOST_MAX_H=${SPECTR_HOST_MAX_HEIGHT}
            SPECTR_FFT_SIZE=${SPECTR_FFT_SIZE}
            SPECTR_ANALYSIS_HOP=${SPECTR_ANALYSIS_HOP}
        )
    endif()
endforeach()
endif()

# Artifact-level host proof: load the built CLAP, VST3 and AU through
# Pulp::host and run real audio across each binary boundary. This is
# deliberately separate from Spectr-test's in-process Processor fixtures.
if((TARGET ${SPECTR_NATIVE_CLAP_TARGET} OR TARGET ${SPECTR_NATIVE_VST3_TARGET} OR
    TARGET ${SPECTR_NATIVE_AU_TARGET} OR
    TARGET SpectrWebViewReference_CLAP OR TARGET SpectrWebViewReference_VST3) AND
   TARGET Pulp::host)
    # Same browser-free selection as Spectr-test: a cutover SDK has no WebView
    # editor to link against.
    add_executable(Spectr-artifact-test
        test/test_built_clap.cpp
        ${SPECTR_TEST_SOURCES})
    target_link_libraries(Spectr-artifact-test PRIVATE
        Pulp::host Pulp::format Pulp::view
        Catch2::Catch2WithMain ${SPECTR_TEST_ASSET_TARGET})
    target_include_directories(Spectr-artifact-test PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include)
    target_compile_definitions(Spectr-artifact-test PRIVATE
        $<$<NOT:$<BOOL:${SPECTR_SDK_HAS_WEBVIEW}>>:SPECTR_NATIVE_EDITOR=1>
        SPECTR_AUTHORED_DESIGN_W=${SPECTR_AUTHORED_WIDTH}
        SPECTR_AUTHORED_DESIGN_H=${SPECTR_AUTHORED_HEIGHT}
        SPECTR_HOST_PREFERRED_W=${SPECTR_HOST_PREFERRED_WIDTH}
        SPECTR_HOST_PREFERRED_H=${SPECTR_HOST_PREFERRED_HEIGHT}
        SPECTR_HOST_MIN_W=${SPECTR_HOST_MIN_WIDTH}
        SPECTR_HOST_MIN_H=${SPECTR_HOST_MIN_HEIGHT}
        SPECTR_HOST_MAX_W=${SPECTR_HOST_MAX_WIDTH}
        SPECTR_HOST_MAX_H=${SPECTR_HOST_MAX_HEIGHT}
        SPECTR_EXPECTED_LATENCY=${SPECTR_PROCESSING_LATENCY}
        SPECTR_FFT_SIZE=${SPECTR_FFT_SIZE}
        SPECTR_ANALYSIS_HOP=${SPECTR_ANALYSIS_HOP})
    if(TARGET ${SPECTR_NATIVE_CLAP_TARGET})
        target_compile_definitions(Spectr-artifact-test PRIVATE
            SPECTR_HAVE_TEST_CLAP=1
            SPECTR_TEST_CLAP_PATH="${CMAKE_BINARY_DIR}/CLAP/${SPECTR_NATIVE_PLUGIN_NAME}.clap")
        add_dependencies(Spectr-artifact-test ${SPECTR_NATIVE_CLAP_TARGET})
    endif()
    if(TARGET ${SPECTR_NATIVE_VST3_TARGET})
        target_compile_definitions(Spectr-artifact-test PRIVATE
            SPECTR_HAVE_TEST_VST3=1
            SPECTR_TEST_VST3_PATH="${CMAKE_BINARY_DIR}/VST3/${SPECTR_NATIVE_PLUGIN_NAME}.vst3")
        add_dependencies(Spectr-artifact-test ${SPECTR_NATIVE_VST3_TARGET})
    endif()
    if(TARGET ${SPECTR_NATIVE_AU_TARGET} AND APPLE)
        # The AU case reaches its bundle through AudioComponentRegister rather
        # than a path, so it needs the two frameworks that own that registry and
        # the bundle it reads the component's declared identity from. The
        # triplet below is the same one the acceptance gate's
        # `auval -v aufx Spec Pulp` uses; the test asserts the built bundle's
        # Info.plist still declares exactly this, so the two cannot drift apart.
        target_compile_definitions(Spectr-artifact-test PRIVATE
            SPECTR_HAVE_TEST_AU=1
            SPECTR_TEST_AU_PATH="${CMAKE_BINARY_DIR}/AU/${SPECTR_NATIVE_PLUGIN_NAME}.component"
            SPECTR_TEST_AU_TYPE="aufx"
            SPECTR_TEST_AU_SUBTYPE="${SPECTR_NATIVE_PLUGIN_CODE}"
            SPECTR_TEST_AU_MANUFACTURER="Pulp")
        target_link_libraries(Spectr-artifact-test PRIVATE
            "-framework AudioToolbox" "-framework CoreFoundation")
        add_dependencies(Spectr-artifact-test ${SPECTR_NATIVE_AU_TARGET})

        # A host that changes a band by WRITING the parameter instead of
        # sending an event -- an AU generic control, a plain
        # AudioUnitSetParameter -- used to leave the adoption entirely to
        # Spectr's sync worker, which is a thread. That made the audible
        # result depend on the worker being scheduled rather than on samples
        # processed, and the AU case above lost that race roughly one run in
        # eight, at the mute assertion or at the unmute one depending on which
        # write the worker was late for. A flaky row is worse than a missing
        # one: it reads as a regression in whatever PR happens to catch it.
        #
        # This row holds the worker back for 200 ms -- far longer than the
        # whole render takes -- so the case can only pass if the audio path
        # reaches the store on its own. It is the negative control made
        # permanent: against the pre-fix processor it fails EVERY run, and it
        # is the only form in which that can be asserted at all, since the
        # defect is a race no ordinary run reproduces on demand.
        add_test(NAME Spectr-au-param-write-without-sync-worker
            COMMAND Spectr-artifact-test
                "Pulp host loads and processes the built Spectr AU artifact")
        set_tests_properties(Spectr-au-param-write-without-sync-worker
            PROPERTIES
                ENVIRONMENT "SPECTR_TEST_PARAM_SYNC_STALL_MS=200"
                TIMEOUT 180)
    endif()
    if(TARGET SpectrWebViewReference_CLAP)
        target_compile_definitions(Spectr-artifact-test PRIVATE
            SPECTR_HAVE_WEBVIEW_REFERENCE_TEST_CLAP=1
            SPECTR_WEBVIEW_REFERENCE_TEST_CLAP_PATH="${CMAKE_BINARY_DIR}/CLAP/Spectr WebView Reference.clap")
        add_dependencies(Spectr-artifact-test SpectrWebViewReference_CLAP)
    endif()
    if(TARGET SpectrWebViewReference_VST3)
        target_compile_definitions(Spectr-artifact-test PRIVATE
            SPECTR_HAVE_WEBVIEW_REFERENCE_TEST_VST3=1
            SPECTR_WEBVIEW_REFERENCE_TEST_VST3_PATH="${CMAKE_BINARY_DIR}/VST3/Spectr WebView Reference.vst3")
        add_dependencies(Spectr-artifact-test SpectrWebViewReference_VST3)
    endif()
    catch_discover_tests(Spectr-artifact-test)
endif()

if(TARGET ${SPECTR_NATIVE_STANDALONE_TARGET} AND TARGET Pulp::platform AND TARGET Pulp::view)
    add_executable(Spectr-native-standalone-artifact-test
        test/test_built_standalone.cpp)
    target_link_libraries(Spectr-native-standalone-artifact-test PRIVATE
        Pulp::platform Pulp::view Catch2::Catch2WithMain)
    target_compile_definitions(Spectr-native-standalone-artifact-test PRIVATE
        SPECTR_TEST_STANDALONE_PATH="$<TARGET_FILE:${SPECTR_NATIVE_STANDALONE_TARGET}>"
        SPECTR_HOST_PREFERRED_W=${SPECTR_HOST_PREFERRED_WIDTH}
        SPECTR_HOST_PREFERRED_H=${SPECTR_HOST_PREFERRED_HEIGHT})
    add_dependencies(Spectr-native-standalone-artifact-test
        ${SPECTR_NATIVE_STANDALONE_TARGET})
    catch_discover_tests(Spectr-native-standalone-artifact-test
        TEST_PREFIX "native: ")
endif()

# Standalone artifact proof: launch the exact packaged executable headlessly,
# capture its real GPU back buffer, and verify screenshot-only mode stays clear
# of the audio device. This is intentionally downstream of the CLI/DSP suite.
if(TARGET SpectrWebViewReference_Standalone AND TARGET Pulp::platform AND TARGET Pulp::view)
    add_executable(Spectr-standalone-artifact-test
        test/test_built_standalone.cpp)
    target_link_libraries(Spectr-standalone-artifact-test PRIVATE
        Pulp::platform Pulp::view Catch2::Catch2WithMain)
    target_compile_definitions(Spectr-standalone-artifact-test PRIVATE
        SPECTR_TEST_STANDALONE_PATH="$<TARGET_FILE:SpectrWebViewReference_Standalone>"
        SPECTR_HOST_PREFERRED_W=${SPECTR_HOST_PREFERRED_WIDTH}
        SPECTR_HOST_PREFERRED_H=${SPECTR_HOST_PREFERRED_HEIGHT})
    add_dependencies(Spectr-standalone-artifact-test SpectrWebViewReference_Standalone)
    catch_discover_tests(Spectr-standalone-artifact-test)
endif()

# The zero-latency render path must read no clock. A schedule expressed in
# samples is correct at any render speed by construction; one that consults
# wall-clock time is correct only while the host paces at real time, and two of
# Spectr's three formats never learn that it has stopped doing so. The scanner
# carries its own positive control and exits 2 (NO VERDICT, which CTest reports
# as a failure, never a pass) if that control does not fire — so a clean result
# from it is a reading of the source and not of a broken instrument.
add_test(NAME Spectr-render-path-clock
    COMMAND ${Python3_EXECUTABLE}
        ${CMAKE_CURRENT_SOURCE_DIR}/tools/ci/check_render_path_clock.py
        ${CMAKE_CURRENT_SOURCE_DIR}/src/mask_renderer.cpp)
set_tests_properties(Spectr-render-path-clock PROPERTIES TIMEOUT 60)

# The same scan over the product's own audio callback, which is where the
# renderer a mode switch installs is actually adopted. Retiring a renderer
# underneath a running audio thread is the kind of handshake that invites a
# lock or a timestamp, so the adoption code is inside the scanned region and
# this row is what keeps it there. The region deliberately stops before the
# modulated-field publication, whose steady_clock read stamps a UI snapshot no
# audio sample depends on.
add_test(NAME Spectr-render-path-clock-product
    COMMAND ${Python3_EXECUTABLE}
        ${CMAKE_CURRENT_SOURCE_DIR}/tools/ci/check_render_path_clock.py
        ${CMAKE_CURRENT_SOURCE_DIR}/src/spectr.cpp)
set_tests_properties(Spectr-render-path-clock-product PROPERTIES TIMEOUT 60)

# The merge driver for the materialized editor document. That artifact is one
# minified line, so git's default merge conflicts on ANY two changes to it --
# four PRs paid for that on 2026-09-15 alone. The driver merges its decoded
# `html` payload and refuses whatever it cannot compose; this row is what keeps
# it honest, because a merge tool that quietly takes one side still exits 0.
# The suite replays real conflicting commits out of this repo's own history,
# asserts the specific content each side contributed, and pairs every mergeable
# fixture with a control proving git's default merge really does conflict on it.
# Pure Python against the checked-in artifact: no binary, no node, no network.
if(Python3_Interpreter_FOUND)
    add_test(NAME Spectr-materialized-merge-driver
        COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/tools/git/test_merge_materialized_runtime.py)
    set_tests_properties(Spectr-materialized-merge-driver PROPERTIES TIMEOUT 120)
else()
    message(STATUS
        "Spectr: Python3 interpreter not found; "
        "Spectr-materialized-merge-driver is NOT registered")
endif()
