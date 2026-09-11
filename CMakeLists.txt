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
    test/test_mode_bridge.cpp
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

# Artifact-level host proof: load the built CLAP and VST3 through Pulp::host
# and run real audio across each binary boundary. This is deliberately
# separate from Spectr-test's in-process Processor fixtures.
if((TARGET ${SPECTR_NATIVE_CLAP_TARGET} OR TARGET ${SPECTR_NATIVE_VST3_TARGET} OR
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
