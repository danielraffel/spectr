#pragma once

// Spectr-owned preset file format (Milestone 9).
//
// Per V2 handoff §7, Spectr persists presets through its own JSON
// wrapper rather than PresetManager's default format. The wrapper
// bundles four pieces so a round-trip always restores the full sound:
//
//   - StateStore blob: flat automatable parameters (Mix, Output,
//     Response, Engine, Bands, Morph). Binary bytes are base64-encoded
//     before landing in the JSON.
//   - Plugin state: the supplemental blob (§5.4), which carries the
//     canonical BandField, viewport, layout, and the A/B snapshot
//     bank. Already JSON text — embedded as a nested object.
//   - Schema version: enables clear migration errors when loading an
//     older preset whose shape this build doesn't recognize.
//   - Metadata: name + author + description + ISO-8601 timestamps, so
//     the file stays useful outside Spectr (pattern libraries, preset
//     browsers, external tooling).
//
// Writers always emit the current schema. Readers accept the current
// schema exactly and report a migration error on any other version —
// there's no silent downgrade path.

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace pulp::format { class Processor; }

namespace spectr {

/// Per-preset human-facing metadata. All fields optional; the file is
/// valid even if everything here is empty.
struct PresetMetadata {
    std::string name;
    std::string author;
    std::string description;
    /// ISO-8601 UTC. Empty on unset.
    std::string created_at;
    std::string modified_at;
};

/// Preset file schema version. Bump when the file shape changes
/// incompatibly; the reader rejects mismatches with a migration error.
inline constexpr int kPresetSchemaVersion = 1;

/// Distinct failure modes a caller might want to surface.
enum class PresetLoadError : std::uint8_t {
    None             = 0,
    MalformedJson    = 1,
    NotASpectrPreset = 2,  ///< `format` field missing or not "spectr.preset"
    SchemaMismatch   = 3,  ///< schema_version doesn't match kPresetSchemaVersion
    MissingState     = 4,  ///< state.state_store or state.plugin_state missing
    CorruptState     = 5,  ///< base64 decode or plugin_state parse failed
};

/// Encode a preset to a JSON string from the current processor state.
/// Works on any processor that is bound to a StateStore and implements
/// the Spectr plugin-state hooks; in practice this is always Spectr.
std::string save_preset_to_string(const pulp::format::Processor& proc,
                                  const PresetMetadata& metadata);

struct PresetLoadResult {
    PresetLoadError error = PresetLoadError::None;
    PresetMetadata  metadata{};
    int             file_schema_version = 0;
    std::string     plugin_version;

    explicit operator bool() const noexcept { return error == PresetLoadError::None; }
};

/// Decode a preset JSON string and apply it to the processor. On
/// success, both StateStore and plugin-owned supplemental state are
/// restored; on failure, the processor is left untouched.
PresetLoadResult load_preset_from_string(pulp::format::Processor& proc,
                                         std::string_view json);

/// Convenience wrappers for on-disk preset files. Return an optional
/// for save (nullopt = file-system error) and the same result struct
/// for load (error set on file-system failures).
bool             save_preset_to_file(const pulp::format::Processor& proc,
                                     const PresetMetadata& metadata,
                                     const std::string& path);
PresetLoadResult load_preset_from_file(pulp::format::Processor& proc,
                                       const std::string& path);

/// Human-readable message for a given error. Stable strings, safe to
/// show in UI. "None" returns "ok".
const char* describe(PresetLoadError e) noexcept;

} // namespace spectr
