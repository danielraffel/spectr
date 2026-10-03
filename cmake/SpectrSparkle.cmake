# Sparkle 2 auto-update for the Spectr standalone app.
#
# Only Spectr.app carries the updater. The update it installs is the full signed
# and notarized installer package (AU, VST3, CLAP, Standalone, Spectr
# Diagnostics), so updating the app updates every format. Plug-in bundles never
# contain Sparkle: they run inside someone else's process.
#
# Feeds (see docs/updates.md):
#   release   https://github.com/danielraffel/spectr/releases/latest/download/appcast.xml
#             GitHub resolves "latest" to the newest NON-prerelease release, so a
#             preview published as a prerelease can never reach release users.
#   practice  https://github.com/danielraffel/spectr/releases/download/sparkle-practice/appcast-practice.xml
#             a separate feed for rehearsing an update; release builds never read it.
#   anything else (a file:// feed for a local rehearsal) via SPECTR_SPARKLE_FEED_URL.
#
# The PUBLIC half of the EdDSA key pair is below. The private half lives only in
# ~/.config/pulp/secrets/sparkle/spectr_ed25519 and 1Password; it never enters
# this repository.

set(SPECTR_SPARKLE_PUBLIC_ED_KEY "mosCtB7H9gxWzbWUYyHiHTapl4sWMgkd4t09iIUnO2g=")
set(SPECTR_SPARKLE_RELEASE_FEED
    "https://github.com/danielraffel/spectr/releases/latest/download/appcast.xml")
set(SPECTR_SPARKLE_PRACTICE_FEED
    "https://github.com/danielraffel/spectr/releases/download/sparkle-practice/appcast-practice.xml")

set(SPECTR_SPARKLE_CHANNEL "release" CACHE STRING
    "Which update feed Spectr.app reads: release, practice, or off")
set_property(CACHE SPECTR_SPARKLE_CHANNEL PROPERTY STRINGS release practice off)
set(SPECTR_SPARKLE_FEED_URL "" CACHE STRING
    "Override the update feed URL (e.g. file:///path/appcast.xml for a local rehearsal)")
set(SPECTR_APP_BUILD_VERSION "" CACHE STRING
    "CFBundleVersion for Spectr.app only (default: PROJECT_VERSION); practice packages use a fourth component, e.g. 1.0.7.1")

# ── Delete on SDK bump ───────────────────────────────────────────────────────
# Fallback for Pulp SDKs that predate pulp_add_sparkle(). It mirrors the SDK
# function's behaviour for this one app (pinned download, embed, link,
# Info.plist keys). Once Spectr pins an SDK that provides pulp_add_sparkle(),
# delete this function, src/mac/sparkle_updater_shim.mm, and the
# _spectr_sparkle_shim branch below.
set(_SPECTR_SPARKLE_VERSION "2.10.0")
set(_SPECTR_SPARKLE_SHA256
    "c2bf58aa8387266ac179357b1415d6f2635f044da8be41042af32425dae6da0c")

function(_spectr_sparkle_shim_embed target feed_url public_key)
    set(_root "${CMAKE_BINARY_DIR}/_deps/sparkle-${_SPECTR_SPARKLE_VERSION}")
    set(_archive "${_root}/Sparkle-${_SPECTR_SPARKLE_VERSION}.tar.xz")
    set(_stamp "${_root}/.extracted-${_SPECTR_SPARKLE_SHA256}")
    if(NOT EXISTS "${_stamp}")
        file(MAKE_DIRECTORY "${_root}")
        if(NOT EXISTS "${_archive}")
            message(STATUS "Spectr: downloading Sparkle ${_SPECTR_SPARKLE_VERSION}")
            file(DOWNLOAD
                "https://github.com/sparkle-project/Sparkle/releases/download/${_SPECTR_SPARKLE_VERSION}/Sparkle-${_SPECTR_SPARKLE_VERSION}.tar.xz"
                "${_archive}" EXPECTED_HASH SHA256=${_SPECTR_SPARKLE_SHA256}
                TLS_VERIFY ON STATUS _status)
            list(GET _status 0 _code)
            if(NOT _code EQUAL 0)
                file(REMOVE "${_archive}")
                message(FATAL_ERROR "Spectr: Sparkle download failed: ${_status}")
            endif()
        endif()
        file(SHA256 "${_archive}" _have)
        if(NOT _have STREQUAL _SPECTR_SPARKLE_SHA256)
            file(REMOVE "${_archive}")
            message(FATAL_ERROR "Spectr: Sparkle archive hash mismatch; removed it, re-run configure")
        endif()
        file(REMOVE_RECURSE "${_root}/dist")
        file(MAKE_DIRECTORY "${_root}/dist")
        execute_process(COMMAND "${CMAKE_COMMAND}" -E tar xf "${_archive}"
            WORKING_DIRECTORY "${_root}/dist" RESULT_VARIABLE _rc)
        if(NOT _rc EQUAL 0 OR NOT EXISTS "${_root}/dist/Sparkle.framework")
            message(FATAL_ERROR "Spectr: could not extract ${_archive}")
        endif()
        file(WRITE "${_stamp}" "${_SPECTR_SPARKLE_VERSION}\n")
    endif()
    set(_dist "${_root}/dist")
    find_program(SPECTR_DITTO ditto REQUIRED)
    find_program(SPECTR_CODESIGN codesign REQUIRED)
    set(_fw_dir "$<TARGET_BUNDLE_CONTENT_DIR:${target}>/Frameworks")
    set(_fw "${_fw_dir}/Sparkle.framework")
    target_link_options(${target} PRIVATE "-F${_dist}" "-Wl,-needed_framework,Sparkle")
    set_property(TARGET ${target} APPEND PROPERTY BUILD_RPATH "@executable_path/../Frameworks")
    set_property(TARGET ${target} APPEND PROPERTY INSTALL_RPATH "@executable_path/../Frameworks")
    add_custom_command(TARGET ${target} POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E rm -rf "${_fw}"
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${_fw_dir}"
        COMMAND "${SPECTR_DITTO}" "${_dist}/Sparkle.framework" "${_fw}"
        # Non-sandboxed: Sparkle 2's XPC services are only for sandboxed apps.
        COMMAND "${CMAKE_COMMAND}" -E rm -rf "${_fw}/Versions/B/XPCServices" "${_fw}/XPCServices"
        COMMAND "${SPECTR_CODESIGN}" --force --sign - --options runtime "${_fw}"
        COMMENT "Embedding Sparkle.framework in ${target}"
        VERBATIM)
    # The keys go into the Info.plist TEMPLATE at the end of configure, so a
    # reconfigure (which regenerates the bundle's Info.plist) never drops them.
    set_target_properties(${target} PROPERTIES
        SPECTR_SPARKLE_PLIST_XML
        "\t<key>SUFeedURL</key>\n\t<string>${feed_url}</string>\n\t<key>SUPublicEDKey</key>\n\t<string>${public_key}</string>\n")
    cmake_language(EVAL CODE
        "cmake_language(DEFER DIRECTORY [[${CMAKE_SOURCE_DIR}]] CALL _spectr_sparkle_shim_plist [[${target}]])")
endfunction()
function(_spectr_sparkle_shim_plist target)
    get_target_property(_template ${target} MACOSX_BUNDLE_INFO_PLIST)
    if(NOT _template)
        set(_template "${CMAKE_ROOT}/Modules/MacOSXBundleInfo.plist.in")
    endif()
    file(READ "${_template}" _text)
    get_target_property(_keys ${target} SPECTR_SPARKLE_PLIST_XML)
    string(FIND "${_text}" "</dict>" _close REVERSE)
    string(SUBSTRING "${_text}" 0 ${_close} _head)
    string(SUBSTRING "${_text}" ${_close} -1 _tail)
    set(_out "${CMAKE_BINARY_DIR}/SpectrSparkle/${target}-Info.plist.in")
    file(WRITE "${_out}" "${_head}${_keys}${_tail}")
    set_target_properties(${target} PROPERTIES MACOSX_BUNDLE_INFO_PLIST "${_out}")
endfunction()
# ── end delete on SDK bump ───────────────────────────────────────────────────

function(spectr_configure_sparkle target kind)
    set(SPECTR_SPARKLE_EMBEDDED OFF PARENT_SCOPE)
    set(SPECTR_SPARKLE_FEED_EFFECTIVE "" PARENT_SCOPE)
    if(NOT APPLE OR NOT TARGET ${target})
        return()
    endif()
    set(_feed "")
    if(NOT SPECTR_SPARKLE_FEED_URL STREQUAL "")
        set(_feed "${SPECTR_SPARKLE_FEED_URL}")
    elseif(SPECTR_SPARKLE_CHANNEL STREQUAL "release")
        # Only the shipping identity reads the release feed: a preview or dev
        # identity is a different app (different bundle id) and must never be
        # offered a release build over itself.
        if(kind STREQUAL "shipping")
            set(_feed "${SPECTR_SPARKLE_RELEASE_FEED}")
        endif()
    elseif(SPECTR_SPARKLE_CHANNEL STREQUAL "practice")
        set(_feed "${SPECTR_SPARKLE_PRACTICE_FEED}")
    elseif(NOT SPECTR_SPARKLE_CHANNEL STREQUAL "off")
        message(FATAL_ERROR "SPECTR_SPARKLE_CHANNEL must be release, practice or off")
    endif()

    if(NOT SPECTR_APP_BUILD_VERSION STREQUAL "")
        if(NOT SPECTR_APP_BUILD_VERSION MATCHES "^${PROJECT_VERSION}(\\.[0-9]+)?$")
            message(FATAL_ERROR
                "SPECTR_APP_BUILD_VERSION (${SPECTR_APP_BUILD_VERSION}) must be "
                "${PROJECT_VERSION} or ${PROJECT_VERSION}.<n>")
        endif()
        # Spectr.app only; the plug-in bundles keep PROJECT_VERSION.
        set_target_properties(${target} PROPERTIES
            MACOSX_BUNDLE_BUNDLE_VERSION "${SPECTR_APP_BUILD_VERSION}")
    endif()

    if(_feed STREQUAL "")
        message(STATUS "Spectr: Sparkle updater disabled for ${target}")
        return()
    endif()
    message(STATUS "Spectr: Sparkle updater in ${target}, feed ${_feed}")
    set(SPECTR_SPARKLE_EMBEDDED ON PARENT_SCOPE)
    set(SPECTR_SPARKLE_FEED_EFFECTIVE "${_feed}" PARENT_SCOPE)
    if(COMMAND pulp_add_sparkle)
        pulp_add_sparkle(${target}
            FEED_URL "${_feed}"
            PUBLIC_ED_KEY "${SPECTR_SPARKLE_PUBLIC_ED_KEY}")
    else()
        # Delete on SDK bump (see above).
        _spectr_sparkle_shim_embed(${target} "${_feed}" "${SPECTR_SPARKLE_PUBLIC_ED_KEY}")
        target_sources(${target} PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/src/mac/sparkle_updater_shim.mm)
        target_link_libraries(${target} PRIVATE "-framework Security" "-framework Cocoa")
    endif()
endfunction()
