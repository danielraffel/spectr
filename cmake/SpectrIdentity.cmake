# Resolves every identifier a host uses to recognise the native Spectr plugin.
#
# Three identities exist, and they must never share a single identifier:
#
#   shipping  Spectr / com.pulp.spectr / AU aufx Spec Pulp / the pinned VST3
#             FUID in vst3_entry.cpp. Changing any of these orphans every saved
#             session, so the default path returns exactly these values.
#   preview   SPECTR_NATIVE_PREVIEW_IDENTITY: the long-lived native-preview
#             identity (SpNP).
#   dev       SPECTR_DEV_IDENTITY=<Suffix>: a throwaway side-by-side identity for
#             trying a branch in a DAW next to an installed release. Every
#             identifier is derived from the suffix, so two dev builds with
#             different suffixes also coexist.
#
# A host keys saved state on these identifiers (CLAP plugin id, VST3 class id,
# AU type/subtype/manufacturer), so distinct identifiers are what keep a session
# saved against a dev build from resolving to the shipping plugin, and back.
#
# Outputs, all prefixed with <out>_:
#   TARGET       CMake target base name (also the AU factory prefix)
#   NAME         product name (bundle file names, descriptor name)
#   BUNDLE_ID    CFBundleIdentifier and CLAP plugin id
#   PLUGIN_CODE  AU subtype
#   MFR_CODE     AU manufacturer
#   VST3_UID     four 8-hex-digit words, or empty to use the FUID pinned in
#                vst3_entry.cpp for the shipping/preview identities
#   AU_CLASS     the AU entry class; the SDK names the factory <TARGET>AUFactory
#   KIND         shipping | preview | dev
#   AU_TYPE      aufx, or aumf for a Freeze Keys build
#   FREEZE_KEYS  ON when the build takes MIDI for Freeze Keys
#
# Freeze Keys (an optional fifth argument, ON/OFF; default: ON exactly when the
# dev suffix is "Keys") makes the plugin accept MIDI. An effect that accepts
# MIDI must be an AU music effect (aumf) -- Logic never sends MIDI to an aufx
# -- and an AU's type is part of the identity a host saves, so Freeze Keys is
# refused on the shipping and preview identities: it exists only as a dev
# identity until the product decides how it ships.
#
# Pure: no targets, no cache writes, so a -P script can call it.
function(spectr_resolve_identity out preview dev_suffix dev_au_subtype)
    set(_mfr "Pulp")
    set(_keys "")
    if(ARGC GREATER 4)
        set(_keys "${ARGV4}")
    endif()
    if("${_keys}" STREQUAL "")
        if("${dev_suffix}" STREQUAL "Keys")
            set(_keys ON)
        else()
            set(_keys OFF)
        endif()
    endif()
    if(preview AND NOT "${dev_suffix}" STREQUAL "")
        message(FATAL_ERROR
            "SPECTR_NATIVE_PREVIEW_IDENTITY and SPECTR_DEV_IDENTITY are exclusive")
    endif()
    set(_uid "")
    if(NOT "${dev_suffix}" STREQUAL "")
        if(NOT dev_suffix MATCHES "^[A-Za-z][A-Za-z0-9]*$")
            message(FATAL_ERROR
                "SPECTR_DEV_IDENTITY must be alphanumeric and start with a "
                "letter (got '${dev_suffix}')")
        endif()
        string(TOLOWER "${dev_suffix}" _lower)
        set(_kind dev)
        set(_target "Spectr${dev_suffix}Dev")
        # The name becomes bundle and executable file names, and the SDK's
        # post-link steps pass those to /bin/sh unquoted, so no shell
        # metacharacters: "Spectr Freeze (dev)" fails to link.
        set(_name "Spectr ${dev_suffix} Dev")
        set(_bundle "com.pulp.spectr.${_lower}-dev")
        if("${dev_au_subtype}" STREQUAL "")
            # "Sp" + the suffix initial + a lowercase "z". The trailing
            # lowercase z cannot match the shipping "Spec" or the preview /
            # reference codes (SpNP, SpWR); set SPECTR_DEV_AU_SUBTYPE to separate
            # two dev suffixes that share an initial.
            string(SUBSTRING "${dev_suffix}" 0 1 _initial)
            string(TOUPPER "${_initial}" _initial)
            set(_code "Sp${_initial}z")
        else()
            set(_code "${dev_au_subtype}")
        endif()
        # Deterministic per suffix, so a rebuild keeps the same class id and a
        # saved dev session keeps resolving to the dev build.
        string(MD5 _hash "spectr-vst3-class:${_bundle}")
        string(TOUPPER "${_hash}" _hash)
        foreach(_i 0 8 16 24)
            string(SUBSTRING "${_hash}" ${_i} 8 _word)
            list(APPEND _uid "${_word}")
        endforeach()
    elseif(preview)
        set(_kind preview)
        set(_target SpectrNativePreview)
        set(_name "Spectr Native Preview")
        set(_bundle "com.pulp.spectr.native-preview")
        set(_code "SpNP")
    else()
        set(_kind shipping)
        set(_target Spectr)
        set(_name "Spectr")
        set(_bundle "com.pulp.spectr")
        set(_code "Spec")
    endif()
    string(LENGTH "${_code}" _code_len)
    if(NOT _code_len EQUAL 4)
        message(FATAL_ERROR "Spectr AU subtype must be 4 characters (got '${_code}')")
    endif()
    if(_kind STREQUAL dev AND _code MATCHES "^(Spec|SpNP|SpWR)$")
        message(FATAL_ERROR
            "Spectr dev AU subtype '${_code}' collides with a shipped identity")
    endif()
    if(_keys AND NOT _kind STREQUAL dev)
        message(FATAL_ERROR
            "SPECTR_FREEZE_KEYS changes the AU type to aumf; build it only under "
            "a dev identity (e.g. -DSPECTR_DEV_IDENTITY=Keys)")
    endif()
    if(_keys)
        set(_au_type aumf)
        set(_keys ON)
    else()
        set(_au_type aufx)
        set(_keys OFF)
    endif()
    set(${out}_AU_TYPE "${_au_type}" PARENT_SCOPE)
    set(${out}_FREEZE_KEYS "${_keys}" PARENT_SCOPE)
    set(${out}_KIND "${_kind}" PARENT_SCOPE)
    set(${out}_TARGET "${_target}" PARENT_SCOPE)
    set(${out}_NAME "${_name}" PARENT_SCOPE)
    set(${out}_BUNDLE_ID "${_bundle}" PARENT_SCOPE)
    set(${out}_PLUGIN_CODE "${_code}" PARENT_SCOPE)
    set(${out}_MFR_CODE "${_mfr}" PARENT_SCOPE)
    set(${out}_VST3_UID "${_uid}" PARENT_SCOPE)
    set(${out}_AU_CLASS "${_target}AU" PARENT_SCOPE)
endfunction()
