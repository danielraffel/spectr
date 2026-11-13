# Asserts the AAX decision in cmake/SpectrAAX.cmake: AAX is off unless a
# developer enables it, it builds only on macOS and Windows, and every
# combination that cannot produce a working AAX is refused with a message that
# names the fix. Run: cmake -P test/aax_option_test.cmake
get_filename_component(_root "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
include("${_root}/cmake/SpectrAAX.cmake")

set(_failures 0)
macro(expect_eq actual expected what)
    if(NOT "${actual}" STREQUAL "${expected}")
        message(SEND_ERROR "${what}: expected '${expected}', got '${actual}'")
        math(EXPR _failures "${_failures} + 1")
    endif()
endmacro()
macro(expect_match actual regex what)
    if(NOT "${actual}" MATCHES "${regex}")
        message(SEND_ERROR "${what}: '${actual}' does not match '${regex}'")
        math(EXPR _failures "${_failures} + 1")
    endif()
endmacro()

# Off by default: an unset PULP_ENABLE_AAX/PULP_HAS_AAX (an SDK configured
# without AAX) builds no AAX on any platform, and that is not an error.
foreach(_platform macos windows Linux)
    spectr_resolve_aax(r "" "" OFF "${_platform}")
    expect_eq("${r_MODE}" "off" "default on ${_platform}")
    expect_eq("${r_ERROR}" "" "default on ${_platform} is not an error")
    spectr_resolve_aax(r OFF FALSE OFF "${_platform}")
    expect_eq("${r_MODE}" "off" "explicit OFF on ${_platform}")
    expect_eq("${r_ERROR}" "" "explicit OFF on ${_platform} is not an error")
endforeach()

# Opted in with a resolved SDK: the editor-enabled native target carries AAX,
# and SPECTR_AAX_HEADLESS selects the processor-only lane instead.
foreach(_platform macos windows)
    spectr_resolve_aax(r ON TRUE OFF "${_platform}")
    expect_eq("${r_MODE}" "native" "enabled on ${_platform}")
    expect_eq("${r_ERROR}" "" "enabled on ${_platform} is not an error")
    spectr_resolve_aax(r ON TRUE ON "${_platform}")
    expect_eq("${r_MODE}" "headless" "headless on ${_platform}")
    expect_eq("${r_ERROR}" "" "headless on ${_platform} is not an error")
endforeach()

# Enabled, but no usable SDK: refused, and the message names the variable.
spectr_resolve_aax(r ON FALSE OFF macos)
expect_eq("${r_MODE}" "off" "enabled without SDK builds nothing")
expect_match("${r_ERROR}" "PULP_AAX_SDK_DIR" "enabled without SDK names PULP_AAX_SDK_DIR")
spectr_resolve_aax(r ON "" OFF macos)
expect_match("${r_ERROR}" "PULP_HAS_AAX is false" "enabled with PULP_HAS_AAX unset")

# The processor-only lane cannot be asked for without enabling AAX.
spectr_resolve_aax(r OFF FALSE ON macos)
expect_eq("${r_MODE}" "off" "headless without enable builds nothing")
expect_match("${r_ERROR}" "PULP_ENABLE_AAX=ON" "headless without enable says how to enable")

# Not a Pro Tools platform.
spectr_resolve_aax(r ON TRUE OFF Linux)
expect_eq("${r_MODE}" "off" "Linux builds no AAX")
expect_match("${r_ERROR}" "only on macOS and Windows" "Linux is refused")

# Each message is one string. A list-valued message reaches the user with
# stray semicolons between its fragments.
foreach(_case "ON;FALSE;OFF;macos" "OFF;FALSE;ON;macos" "ON;TRUE;OFF;Linux")
    list(GET _case 0 _e)
    list(GET _case 1 _h)
    list(GET _case 2 _l)
    list(GET _case 3 _p)
    spectr_resolve_aax(r "${_e}" "${_h}" "${_l}" "${_p}")
    if(r_ERROR STREQUAL "" OR r_ERROR MATCHES ";")
        message(SEND_ERROR "message for ${_case} is empty or a list: '${r_ERROR}'")
        math(EXPR _failures "${_failures} + 1")
    endif()
endforeach()

if(_failures GREATER 0)
    message(FATAL_ERROR "${_failures} AAX option assertion(s) failed")
endif()
message(STATUS "AAX option: off by default; enabled, headless and refused cases resolve correctly")
