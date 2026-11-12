# Configures scratch projects against a real Pulp SDK package with no AAX SDK
# present and checks the AAX contract end to end through find_package(Pulp)
# and cmake/SpectrAAX.cmake, the same path Spectr's CMakeLists.txt takes:
#   - a default configure succeeds and builds no AAX, even when the
#     environment names an SDK (the environment never opts in);
#   - -DPULP_ENABLE_AAX=ON with no SDK fails and names PULP_AAX_SDK_DIR;
#   - -DPULP_ENABLE_AAX=ON with a directory that is not an AAX SDK fails;
#   - an SDK placed inside the source tree is refused, even when it has the
#     expected layout (the probe's stand-in is three empty files, not SDK code);
#   - -DSPECTR_AAX_HEADLESS=ON without enabling AAX fails and says how.
#   cmake -DSPECTR_SOURCE_DIR=<src> -DPULP_DIR_UNDER_TEST=<Pulp_DIR>
#         -DSCRATCH=<dir> -P aax_configure_test.cmake
if(NOT SPECTR_SOURCE_DIR OR NOT SCRATCH)
    message(FATAL_ERROR "SPECTR_SOURCE_DIR and SCRATCH are required")
endif()
if(NOT PULP_DIR_UNDER_TEST OR NOT EXISTS "${PULP_DIR_UNDER_TEST}/PulpConfig.cmake")
    message(FATAL_ERROR
        "PULP_DIR_UNDER_TEST must name a Pulp SDK package directory "
        "(got '${PULP_DIR_UNDER_TEST}')")
endif()

file(REMOVE_RECURSE "${SCRATCH}")
file(MAKE_DIRECTORY "${SCRATCH}/src" "${SCRATCH}/not-an-aax-sdk")
file(WRITE "${SCRATCH}/src/CMakeLists.txt" "cmake_minimum_required(VERSION 3.24)
project(SpectrAaxConfigureProbe LANGUAGES C CXX)
find_package(Pulp REQUIRED)
option(SPECTR_AAX_HEADLESS \"\" OFF)
include([[${SPECTR_SOURCE_DIR}/cmake/SpectrAAX.cmake]])
spectr_aax_platform(_platform)
spectr_resolve_aax(_aax \"\${PULP_ENABLE_AAX}\" \"\${PULP_HAS_AAX}\"
    \"\${SPECTR_AAX_HEADLESS}\" \"\${_platform}\")
if(_aax_ERROR)
    message(FATAL_ERROR \"Spectr: \${_aax_ERROR}\")
endif()
file(WRITE \"\${CMAKE_BINARY_DIR}/aax-mode.txt\" \"\${_aax_MODE}\")
")

set(_failures 0)
# _configure(<label> <env-sdk> <args...>): configure with PULP_AAX_SDK_DIR
# unset in the environment, or set to <env-sdk> when it is not "-".
function(_configure label env_sdk)
    set(build "${SCRATCH}/build-${label}")
    if(env_sdk STREQUAL "-")
        set(env_args --unset=PULP_AAX_SDK_DIR)
    else()
        set(env_args "PULP_AAX_SDK_DIR=${env_sdk}")
    endif()
    execute_process(
        COMMAND ${CMAKE_COMMAND} -E env ${env_args}
            ${CMAKE_COMMAND} -S "${SCRATCH}/src" -B "${build}"
            -DCMAKE_BUILD_TYPE=Release "-DPulp_DIR=${PULP_DIR_UNDER_TEST}" ${ARGN}
        RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_VARIABLE err)
    set(RC "${rc}" PARENT_SCOPE)
    set(LOG "${out}\n${err}" PARENT_SCOPE)
    set(MODE "" PARENT_SCOPE)
    if(EXISTS "${build}/aax-mode.txt")
        file(READ "${build}/aax-mode.txt" mode)
        set(MODE "${mode}" PARENT_SCOPE)
    endif()
endfunction()

macro(_expect_ok label)
    if(NOT RC EQUAL 0)
        message(SEND_ERROR "${label}: configure failed (rc=${RC}):\n${LOG}")
        math(EXPR _failures "${_failures} + 1")
    elseif(NOT MODE STREQUAL "off")
        message(SEND_ERROR "${label}: expected AAX off, resolved '${MODE}'")
        math(EXPR _failures "${_failures} + 1")
    endif()
endmacro()
macro(_expect_refused label regex)
    if(RC EQUAL 0)
        message(SEND_ERROR "${label}: configure succeeded (AAX '${MODE}'); it must be refused")
        math(EXPR _failures "${_failures} + 1")
    elseif(NOT LOG MATCHES "${regex}")
        message(SEND_ERROR "${label}: refused, but without the expected message '${regex}':\n${LOG}")
        math(EXPR _failures "${_failures} + 1")
    endif()
endmacro()

_configure(default -)
_expect_ok("default configure")

_configure(env-only "${SCRATCH}/not-an-aax-sdk")
_expect_ok("PULP_AAX_SDK_DIR in the environment without PULP_ENABLE_AAX")

_configure(enabled-no-sdk - -DPULP_ENABLE_AAX=ON)
_expect_refused("PULP_ENABLE_AAX=ON with no SDK" "PULP_AAX_SDK_DIR")

_configure(enabled-bad-sdk - -DPULP_ENABLE_AAX=ON
    "-DPULP_AAX_SDK_DIR=${SCRATCH}/not-an-aax-sdk")
_expect_refused("PULP_ENABLE_AAX=ON with a directory that is not an AAX SDK"
    "not look like a valid AAX SDK root")

set(_in_tree_sdk "${SCRATCH}/src/aax-sdk")
foreach(_stub Interfaces/AAX.h Interfaces/AAX_Exports.cpp Libs/AAXLibrary/CMakeLists.txt)
    file(WRITE "${_in_tree_sdk}/${_stub}" "")
endforeach()
_configure(enabled-in-tree-sdk - -DPULP_ENABLE_AAX=ON
    "-DPULP_AAX_SDK_DIR=${_in_tree_sdk}")
_expect_refused("PULP_ENABLE_AAX=ON with an SDK inside the source tree"
    "must point outside")

_configure(headless-not-enabled - -DSPECTR_AAX_HEADLESS=ON)
_expect_refused("SPECTR_AAX_HEADLESS=ON without PULP_ENABLE_AAX"
    "SPECTR_AAX_HEADLESS=ON builds a processor-only AAX")

if(_failures GREATER 0)
    message(FATAL_ERROR "${_failures} AAX configure case(s) failed")
endif()
message(STATUS "AAX configure: default is off and every unusable opt-in is refused")
