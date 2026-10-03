# LSMinimumSystemVersion for Spectr's bundles; see the block that includes this
# in CMakeLists.txt. Delete on the Pulp SDK bump whose templates carry the key.

# Adds LSMinimumSystemVersion = CMAKE_OSX_DEPLOYMENT_TARGET to each target's
# Info.plist template at the end of configure (deferred, so it composes with
# template shims that also run then, such as Spectr.app's Sparkle keys, as long
# as they were deferred first). A target that does not exist, or a configure
# with no deployment target, is left alone rather than failing.
function(spectr_declare_minimum_system_version)
    foreach(_target IN LISTS ARGN)
        # Through EVAL so the target name is bound now: a deferred call
        # expands its arguments only when it runs, after this loop ends.
        cmake_language(EVAL CODE
            "cmake_language(DEFER DIRECTORY [[${CMAKE_SOURCE_DIR}]] CALL _spectr_min_os_shim_plist [[${_target}]])")
    endforeach()
endfunction()

function(_spectr_min_os_shim_plist target)
    if("${target}" STREQUAL "" OR NOT TARGET "${target}")
        return()
    endif()
    if("${CMAKE_OSX_DEPLOYMENT_TARGET}" STREQUAL "")
        message(STATUS "Spectr: no CMAKE_OSX_DEPLOYMENT_TARGET; ${target} declares no LSMinimumSystemVersion")
        return()
    endif()
    get_target_property(_template "${target}" MACOSX_BUNDLE_INFO_PLIST)
    if(NOT _template)
        set(_template "${CMAKE_ROOT}/Modules/MacOSXBundleInfo.plist.in")
    endif()
    file(READ "${_template}" _text)
    if(_text MATCHES "LSMinimumSystemVersion")
        return()
    endif()
    string(FIND "${_text}" "</dict>" _close REVERSE)
    if(_close EQUAL -1)
        message(FATAL_ERROR "Spectr: ${_template} has no </dict> to add LSMinimumSystemVersion before")
    endif()
    string(SUBSTRING "${_text}" 0 ${_close} _head)
    string(SUBSTRING "${_text}" ${_close} -1 _tail)
    set(_out "${CMAKE_BINARY_DIR}/SpectrMinOS/${target}-Info.plist.in")
    file(WRITE "${_out}"
        "${_head}\t<key>LSMinimumSystemVersion</key>\n\t<string>${CMAKE_OSX_DEPLOYMENT_TARGET}</string>\n${_tail}")
    set_target_properties("${target}" PROPERTIES MACOSX_BUNDLE_INFO_PLIST "${_out}")
endfunction()
