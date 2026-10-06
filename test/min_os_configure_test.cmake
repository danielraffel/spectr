# Configures a scratch project through cmake/SpectrMinimumSystemVersion.cmake
# twice -- with CMAKE_OSX_DEPLOYMENT_TARGET set and with it unset -- and checks
# that both configure, that the set case writes the key with that value into
# the target's template, and that the unset case leaves the template alone.
#   cmake -DSPECTR_SOURCE_DIR=<src> -DSCRATCH=<dir> -P min_os_configure_test.cmake
if(NOT SPECTR_SOURCE_DIR OR NOT SCRATCH)
    message(FATAL_ERROR "SPECTR_SOURCE_DIR and SCRATCH are required")
endif()
file(REMOVE_RECURSE "${SCRATCH}")
file(MAKE_DIRECTORY "${SCRATCH}/src")
file(WRITE "${SCRATCH}/src/Info.plist.in" [=[<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
    <key>CFBundleExecutable</key>
    <string>probe</string>
</dict>
</plist>
]=])
file(WRITE "${SCRATCH}/src/CMakeLists.txt" "cmake_minimum_required(VERSION 3.25)
project(SpectrMinOsProbe NONE)
include([[${SPECTR_SOURCE_DIR}/cmake/SpectrMinimumSystemVersion.cmake]])
add_custom_target(probe)
set_target_properties(probe PROPERTIES MACOSX_BUNDLE_INFO_PLIST [[${SCRATCH}/src/Info.plist.in]])
# An empty name and a missing target are ignored, never a configure error.
spectr_declare_minimum_system_version(probe \"\" no_such_target)
cmake_language(DEFER DIRECTORY \"\${CMAKE_SOURCE_DIR}\" CALL _probe_report)
function(_probe_report)
    get_target_property(_t probe MACOSX_BUNDLE_INFO_PLIST)
    file(WRITE \"\${CMAKE_BINARY_DIR}/template-path.txt\" \"\${_t}\")
endfunction()
")

function(_configure label)
    set(build "${SCRATCH}/build-${label}")
    execute_process(
        COMMAND ${CMAKE_COMMAND} -E env --unset=MACOSX_DEPLOYMENT_TARGET
            ${CMAKE_COMMAND} -S "${SCRATCH}/src" -B "${build}" ${ARGN}
        RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_VARIABLE err)
    if(NOT rc EQUAL 0)
        message(FATAL_ERROR "configure (${label}) failed:\n${out}\n${err}")
    endif()
    file(READ "${build}/template-path.txt" template)
    file(READ "${template}" text)
    set(TEMPLATE_TEXT "${text}" PARENT_SCOPE)
endfunction()

_configure(set -DCMAKE_OSX_DEPLOYMENT_TARGET=13.4)
if(NOT TEMPLATE_TEXT MATCHES "<key>LSMinimumSystemVersion</key>[ \t\r\n]*<string>13\\.4</string>")
    message(FATAL_ERROR "deployment target 13.4: template lacks LSMinimumSystemVersion 13.4:\n${TEMPLATE_TEXT}")
endif()

_configure(unset -UCMAKE_OSX_DEPLOYMENT_TARGET)
if(TEMPLATE_TEXT MATCHES "LSMinimumSystemVersion")
    message(FATAL_ERROR "no deployment target: template should be untouched:\n${TEMPLATE_TEXT}")
endif()

_configure(empty "-DCMAKE_OSX_DEPLOYMENT_TARGET=")
if(TEMPLATE_TEXT MATCHES "LSMinimumSystemVersion")
    message(FATAL_ERROR "empty deployment target: template should be untouched:\n${TEMPLATE_TEXT}")
endif()
message(STATUS "min-os configure test: set, unset and empty all configure correctly")
