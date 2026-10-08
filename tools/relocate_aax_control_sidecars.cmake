# Keep Pulp control-shipping evidence in Resources so macOS codesign does not
# interpret JSON sidecars under Contents/MacOS as nested code.
if(NOT DEFINED BUNDLE_CONTENTS)
  message(FATAL_ERROR "BUNDLE_CONTENTS is required")
endif()
set(_macos "${BUNDLE_CONTENTS}/MacOS")
set(_resources "${BUNDLE_CONTENTS}/Resources")
if(NOT IS_DIRECTORY "${_macos}")
  message(FATAL_ERROR "AAX MacOS directory is missing: ${_macos}")
endif()
file(MAKE_DIRECTORY "${_resources}")
file(GLOB _sidecars "${_macos}/*.inspector-capabilities.json"
                    "${_macos}/*.control-shipping.json"
                    "${_macos}/*.control-shipping-report.json")
foreach(_sidecar IN LISTS _sidecars)
  get_filename_component(_name "${_sidecar}" NAME)
  file(RENAME "${_sidecar}" "${_resources}/${_name}")
endforeach()
