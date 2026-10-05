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
#   anything else via SPECTR_SPARKLE_FEED_URL: https://, or http://127.0.0.1:<port>/...
#   for a local rehearsal. Sparkle refuses file:// feeds at run time.
#
# The PUBLIC half of the EdDSA key pair is below. The private half lives only in
# ~/.config/pulp/secrets/sparkle/spectr_ed25519 and 1Password; it never enters
# this repository.

include(${CMAKE_CURRENT_LIST_DIR}/SpectrSparklePolicy.cmake)

set(SPECTR_SPARKLE_PUBLIC_ED_KEY "mosCtB7H9gxWzbWUYyHiHTapl4sWMgkd4t09iIUnO2g=")
set(SPECTR_SPARKLE_RELEASE_FEED
    "https://github.com/danielraffel/spectr/releases/latest/download/appcast.xml")
set(SPECTR_RELEASES_PAGE "https://github.com/danielraffel/spectr/releases")
set(SPECTR_SPARKLE_PRACTICE_FEED
    "https://github.com/danielraffel/spectr/releases/download/sparkle-practice/appcast-practice.xml")

set(SPECTR_SPARKLE_CHANNEL "release" CACHE STRING
    "Which update feed Spectr.app reads: release, practice, or off")
set_property(CACHE SPECTR_SPARKLE_CHANNEL PROPERTY STRINGS release practice off)
set(SPECTR_SPARKLE_FEED_URL "" CACHE STRING
    "Override the update feed URL (https://, or http://127.0.0.1:<port>/appcast.xml for a local rehearsal)")
set(SPECTR_APP_BUILD_VERSION "" CACHE STRING
    "CFBundleVersion for Spectr.app only (default: PROJECT_VERSION); practice packages use a fourth component, e.g. 1.0.7.1")


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
        # A PREVIEW numbers Spectr.app just below X.Y.Z, with a 9nnn
        # component: X.Y.(Z-1).9nnn, X.(Y-1).9999.9nnn for X.Y.0, or
        # (X-1).9999.9999.9nnn for X.0.0 (tools/check_release_version.py,
        # preview_prefix). Below X.Y.Z itself, so the release is offered to a
        # preview as newer, and rising from one preview to the next. (A
        # practice build's X.Y.Z.<n> sorts ABOVE the release; a preview must
        # not.)
        spectr_sparkle_preview_prefix(_spectr_preview_prefix
            ${PROJECT_VERSION_MAJOR} ${PROJECT_VERSION_MINOR} ${PROJECT_VERSION_PATCH})
        spectr_sparkle_regex_escape(_spectr_version_re "${PROJECT_VERSION}")
        spectr_sparkle_regex_escape(_spectr_preview_re "${_spectr_preview_prefix}")
        if(NOT SPECTR_APP_BUILD_VERSION MATCHES "^${_spectr_version_re}(\\.[0-9]+)?$"
           AND NOT (NOT _spectr_preview_prefix STREQUAL ""
                    AND SPECTR_APP_BUILD_VERSION MATCHES "^${_spectr_preview_re}\\.9[0-9][0-9][0-9]$"))
            message(FATAL_ERROR
                "SPECTR_APP_BUILD_VERSION (${SPECTR_APP_BUILD_VERSION}) must be "
                "${PROJECT_VERSION}, ${PROJECT_VERSION}.<n> (practice) or "
                "${_spectr_preview_prefix}.9<nnn> (preview of ${PROJECT_VERSION})")
        endif()
        # Spectr.app only; the plug-in bundles keep PROJECT_VERSION.
        set_target_properties(${target} PROPERTIES
            MACOSX_BUNDLE_BUNDLE_VERSION "${SPECTR_APP_BUILD_VERSION}")
    endif()

    spectr_sparkle_feed_policy_error(_policy_error "${kind}" "${SPECTR_APP_BUILD_VERSION}"
        "${PROJECT_VERSION}" "${SPECTR_SPARKLE_FEED_URL}" "${SPECTR_SPARKLE_CHANNEL}")
    if(NOT _policy_error STREQUAL "")
        message(FATAL_ERROR "Spectr: ${_policy_error}")
    endif()

    if(NOT _feed STREQUAL "" AND NOT _feed MATCHES "^https://" AND
       NOT _feed MATCHES "^http://(127\\.0\\.0\\.1|localhost)(:[0-9]+)?/")
        message(FATAL_ERROR
            "Spectr: the Sparkle feed must be https:// or loopback http:// "
            "(Sparkle refuses file://): ${_feed}")
    endif()
    if(_feed STREQUAL "")
        message(STATUS "Spectr: Sparkle updater disabled for ${target}")
        return()
    endif()
    message(STATUS "Spectr: Sparkle updater in ${target}, feed ${_feed}")
    # Sparkle is MIT-licensed (with bsdiff, sais-lite and ed25519 notices); the
    # app that redistributes it carries its license text.
    set_source_files_properties(
        ${CMAKE_CURRENT_SOURCE_DIR}/resources/licenses/Sparkle-LICENSE.txt
        PROPERTIES MACOSX_PACKAGE_LOCATION Resources/Licenses)
    target_sources(${target} PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/resources/licenses/Sparkle-LICENSE.txt)
    set(SPECTR_SPARKLE_EMBEDDED ON PARENT_SCOPE)
    set(SPECTR_SPARKLE_FEED_EFFECTIVE "${_feed}" PARENT_SCOPE)
    if(NOT COMMAND pulp_add_sparkle)
        message(FATAL_ERROR "Spectr: the Pulp SDK provides no pulp_add_sparkle(); "
            "Spectr.app's updater needs Pulp 0.907.0 or newer")
    endif()
    # What the Settings UPDATES note says is generated from these facts
    # (docs/updates.md): where releases are published, that an update is the
    # installer package (quits and reopens Spectr, asks for an administrator
    # password), and -- AUTOMATIC_INSTALL left off -- that nothing installs
    # without the user choosing Install. Automatic checks are on from the
    # first launch; the user turns them off in Settings. Pulp SDKs older than
    # the update service ignore RELEASES_URL and INSTALLER.
    pulp_add_sparkle(${target}
        FEED_URL "${_feed}"
        PUBLIC_ED_KEY "${SPECTR_SPARKLE_PUBLIC_ED_KEY}"
        AUTOMATIC_CHECKS ON
        RELEASES_URL "${SPECTR_RELEASES_PAGE}"
        INSTALLER package)
endfunction()
