# cmake -P test/sparkle_feed_policy_test.cmake
# Exercises cmake/SpectrSparklePolicy.cmake: a release-numbered shipping app
# must read the release feed; practice/preview numbers and other identities may
# read any.
include(${CMAKE_CURRENT_LIST_DIR}/../cmake/SpectrSparklePolicy.cmake)

set(_failures 0)
function(expect label want_error kind build version feed channel)
    spectr_sparkle_feed_policy_error(_e "${kind}" "${build}" "${version}" "${feed}" "${channel}")
    if(want_error AND _e STREQUAL "")
        message("FAIL ${label}: expected a refusal, got none")
        math(EXPR _n "${_failures} + 1")
        set(_failures ${_n} PARENT_SCOPE)
    elseif(NOT want_error AND NOT _e STREQUAL "")
        message("FAIL ${label}: unexpected refusal: ${_e}")
        math(EXPR _n "${_failures} + 1")
        set(_failures ${_n} PARENT_SCOPE)
    else()
        message("PASS ${label}")
    endif()
endfunction()

set(_loop "http://127.0.0.1:8765/appcast-practice.xml")
expect("release build, release channel, no override" FALSE shipping "" 1.0.7 "" release)
expect("release build numbered explicitly"            FALSE shipping 1.0.7 1.0.7 "" release)
expect("release build with a feed override"           TRUE  shipping "" 1.0.7 "${_loop}" release)
expect("explicit release number with a feed override" TRUE  shipping 1.0.7 1.0.7 "https://x/a.xml" release)
expect("release build on the practice channel"        TRUE  shipping "" 1.0.7 "" practice)
expect("practice build with a loopback feed"          FALSE shipping 1.0.7.2 1.0.7 "${_loop}" practice)
expect("preview build with a feed override"           FALSE shipping 1.0.6.9001 1.0.7 "${_loop}" release)
expect("release build, updater off"                   FALSE shipping "" 1.0.7 "" off)
expect("preview identity with an override"            FALSE preview "" 1.0.7 "${_loop}" release)
expect("dev identity with an override"                FALSE dev "" 1.0.7 "${_loop}" release)

if(_failures GREATER 0)
    message(FATAL_ERROR "${_failures} sparkle feed policy case(s) failed")
endif()
