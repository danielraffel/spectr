# Which update feed a Spectr.app may be configured with. Pure (no cache, no
# targets) so test/sparkle_feed_policy_test.cmake can run it under `cmake -P`.
#
# The shipping identity numbered as a RELEASE (CFBundleVersion == the product
# version) must read the release feed: a release-numbered app that reads a
# practice or overridden feed can never be offered the next release, and
# nothing at run time would say so. Practice and preview builds carry their own
# build numbers (see docs/updates.md) and may read any feed; `off` builds no
# updater at all (package.sh refuses to package a release without one).
#
# Sets <out_var> to an error message, or to "" when the configuration is fine.
function(spectr_sparkle_feed_policy_error out_var kind app_build project_version
         feed_url channel)
    set(_error "")
    set(_build "${app_build}")
    if(_build STREQUAL "")
        set(_build "${project_version}")
    endif()
    if(kind STREQUAL "shipping" AND _build STREQUAL project_version)
        if(NOT feed_url STREQUAL "")
            set(_error
                "SPECTR_SPARKLE_FEED_URL (${feed_url}) overrides the release feed of a "
                "release-numbered shipping Spectr.app (${_build}). A rehearsal build must "
                "carry a practice build number: -DSPECTR_APP_BUILD_VERSION=${project_version}.<n>")
        elseif(channel STREQUAL "practice")
            set(_error
                "SPECTR_SPARKLE_CHANNEL=practice on a release-numbered shipping Spectr.app "
                "(${_build}). Practice builds carry a practice build number: "
                "-DSPECTR_APP_BUILD_VERSION=${project_version}.<n>")
        endif()
    endif()
    set(${out_var} "${_error}" PARENT_SCOPE)
endfunction()
