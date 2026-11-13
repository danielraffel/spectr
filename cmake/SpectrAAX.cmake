# Which AAX artifact, if any, this configuration builds. Pure (no cache, no
# targets) so test/aax_option_test.cmake can run it under `cmake -P`.
#
# AAX is an optional vendor format. Spectr never ships, commits or downloads
# the Avid AAX SDK: a developer who has accepted Avid's agreement points
# PULP_AAX_SDK_DIR at their own copy outside this tree and configures with
# -DPULP_ENABLE_AAX=ON. Without that, the build, the M5 acceptance gate and the
# release pipeline see no AAX at all. See docs/aax.md.
#
#   spectr_resolve_aax(<out> <enable_aax> <has_aax> <headless> <platform>)
#
#   enable_aax  value of PULP_ENABLE_AAX (the developer's opt-in)
#   has_aax     value of PULP_HAS_AAX (the Pulp SDK resolved a usable AAX SDK)
#   headless    value of SPECTR_AAX_HEADLESS (processor-only diagnosis lane)
#   platform    "macos", "windows" or anything else
#
# Sets <out>_MODE to "off", "native" (AAX joins the editor-enabled native
# target's FORMATS) or "headless" (only the processor-only AAX target), and
# <out>_ERROR to a message when the combination must not configure, else "".
function(spectr_resolve_aax out enable_aax has_aax headless platform)
    set(_mode "off")
    set(_error "")
    if(NOT enable_aax)
        if(headless)
            string(CONCAT _error
                "SPECTR_AAX_HEADLESS=ON builds a processor-only AAX, but AAX is not "
                "enabled. AAX is optional and needs your own Avid AAX SDK: configure "
                "with -DPULP_ENABLE_AAX=ON -DPULP_AAX_SDK_DIR=<sdk outside this tree> "
                "(docs/aax.md), or drop SPECTR_AAX_HEADLESS.")
        endif()
    elseif(NOT platform STREQUAL "macos" AND NOT platform STREQUAL "windows")
        string(CONCAT _error
            "PULP_ENABLE_AAX=ON, but AAX builds only on macOS and Windows "
            "(this is '${platform}'). Configure without PULP_ENABLE_AAX.")
    elseif(NOT has_aax)
        string(CONCAT _error
            "PULP_ENABLE_AAX=ON, but the Pulp SDK did not resolve an AAX SDK "
            "(PULP_HAS_AAX is false). Point PULP_AAX_SDK_DIR at your own Avid AAX "
            "SDK, kept outside this tree (docs/aax.md).")
    elseif(headless)
        set(_mode "headless")
    else()
        set(_mode "native")
    endif()
    set(${out}_MODE "${_mode}" PARENT_SCOPE)
    set(${out}_ERROR "${_error}" PARENT_SCOPE)
endfunction()

# The platform name spectr_resolve_aax expects for the current configuration.
function(spectr_aax_platform out_var)
    if(APPLE)
        set(${out_var} "macos" PARENT_SCOPE)
    elseif(WIN32)
        set(${out_var} "windows" PARENT_SCOPE)
    else()
        set(${out_var} "${CMAKE_SYSTEM_NAME}" PARENT_SCOPE)
    endif()
endfunction()
