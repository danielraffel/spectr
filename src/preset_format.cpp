#include "spectr/preset_format.hpp"
#include "spectr/spectr.hpp"

#include <pulp/format/processor.hpp>
#include <pulp/runtime/base64.hpp>
#include <pulp/state/store.hpp>

#include <choc/containers/choc_Value.h>
#include <choc/text/choc_JSON.h>

#include <fstream>
#include <sstream>
#include <string_view>

namespace spectr {

namespace {

constexpr const char* kFormatTag = "spectr.preset";

/// Emit the supplemental plugin-state blob as a parsed JSON object,
/// embedded inline rather than stringified. Returns a default empty
/// object if the blob is not valid JSON (shouldn't happen under normal
/// flow — Spectr always emits well-formed JSON).
choc::value::Value plugin_state_as_object_(const pulp::format::Processor& proc) {
    const auto bytes = proc.serialize_plugin_state();
    if (bytes.empty()) return choc::value::createObject("EmptyPluginState");
    const std::string_view text(reinterpret_cast<const char*>(bytes.data()),
                                bytes.size());
    try {
        return choc::json::parse(text);
    } catch (...) {
        return choc::value::createObject("EmptyPluginState");
    }
}

std::string read_file_(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f.good()) return {};
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

bool write_file_(const std::string& path, const std::string& contents) {
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    if (!f.good()) return false;
    f.write(contents.data(), static_cast<std::streamsize>(contents.size()));
    return f.good();
}

std::string get_string_(const choc::value::ValueView& v, const char* key) {
    if (!v.isObject() || !v.hasObjectMember(key)) return {};
    const auto e = v[key];
    if (!e.isString()) return {};
    return std::string(e.getString());
}

} // namespace

// ── Save ───────────────────────────────────────────────────────────────

std::string save_preset_to_string(const pulp::format::Processor& proc,
                                  const PresetMetadata& metadata)
{
    using choc::value::createObject;

    auto root = createObject("SpectrPreset");
    root.addMember("format",          kFormatTag);
    root.addMember("schema_version",  static_cast<int32_t>(kPresetSchemaVersion));
    root.addMember("plugin_version",  proc.descriptor().version);

    auto meta = createObject("Metadata");
    meta.addMember("name",        metadata.name);
    meta.addMember("author",      metadata.author);
    meta.addMember("description", metadata.description);
    meta.addMember("created_at",  metadata.created_at);
    meta.addMember("modified_at", metadata.modified_at);
    root.addMember("metadata", meta);

    auto state = createObject("State");
    // StateStore blob — binary, base64-encode for transport through JSON.
    const auto store_bytes = proc.state().serialize();
    const std::string store_b64 =
        pulp::runtime::base64_encode(store_bytes.data(), store_bytes.size());
    state.addMember("state_store", store_b64);
    // Plugin supplemental state — already JSON text, embed as object.
    state.addMember("plugin_state", plugin_state_as_object_(proc));
    root.addMember("state", state);

    return choc::json::toString(root, /*useLineBreaks=*/true);
}

bool save_preset_to_file(const pulp::format::Processor& proc,
                         const PresetMetadata& metadata,
                         const std::string& path)
{
    return write_file_(path, save_preset_to_string(proc, metadata));
}

// ── Load ───────────────────────────────────────────────────────────────

PresetLoadResult load_preset_from_string(pulp::format::Processor& proc,
                                         std::string_view json)
{
    PresetLoadResult r;

    choc::value::Value root;
    try {
        root = choc::json::parse(json);
    } catch (...) {
        r.error = PresetLoadError::MalformedJson;
        return r;
    }
    if (!root.isObject()) { r.error = PresetLoadError::MalformedJson; return r; }

    // format tag.
    if (!root.hasObjectMember("format")) {
        r.error = PresetLoadError::NotASpectrPreset; return r;
    }
    const auto fmt = root["format"];
    if (!fmt.isString() || fmt.getString() != kFormatTag) {
        r.error = PresetLoadError::NotASpectrPreset; return r;
    }

    // Schema version — must match exactly. No silent downgrade.
    int schema = 0;
    if (root.hasObjectMember("schema_version")) {
        const auto e = root["schema_version"];
        if      (e.isInt32())   schema = e.getInt32();
        else if (e.isInt64())   schema = static_cast<int>(e.getInt64());
        else if (e.isFloat64()) schema = static_cast<int>(e.getFloat64());
    }
    r.file_schema_version = schema;
    if (schema != kPresetSchemaVersion) {
        r.error = PresetLoadError::SchemaMismatch; return r;
    }

    // Metadata — purely informational, failure to read is not fatal.
    if (root.hasObjectMember("metadata") && root["metadata"].isObject()) {
        const auto m = root["metadata"];
        r.metadata.name        = get_string_(m, "name");
        r.metadata.author      = get_string_(m, "author");
        r.metadata.description = get_string_(m, "description");
        r.metadata.created_at  = get_string_(m, "created_at");
        r.metadata.modified_at = get_string_(m, "modified_at");
    }
    r.plugin_version = get_string_(root, "plugin_version");

    // State block — both halves required.
    if (!root.hasObjectMember("state") || !root["state"].isObject()) {
        r.error = PresetLoadError::MissingState; return r;
    }
    const auto state = root["state"];
    if (!state.hasObjectMember("state_store") || !state.hasObjectMember("plugin_state")) {
        r.error = PresetLoadError::MissingState; return r;
    }

    // Decode StateStore blob.
    const auto store_view = state["state_store"];
    if (!store_view.isString()) { r.error = PresetLoadError::CorruptState; return r; }
    const auto store_decoded = pulp::runtime::base64_decode(std::string_view(store_view.getString()));
    if (!store_decoded) { r.error = PresetLoadError::CorruptState; return r; }

    // Re-serialize the plugin_state subtree back to JSON text so we can
    // feed it through the Processor's existing deserialize hook without
    // reinventing its parse path here.
    const auto plugin_state_json =
        choc::json::toString(state["plugin_state"], /*useLineBreaks=*/false);
    const std::vector<uint8_t> plugin_state_bytes(plugin_state_json.begin(),
                                                  plugin_state_json.end());

    // Apply. StateStore first — deserialize fills the atomics. Plugin
    // state second so handlers that read params during their apply see
    // the restored flat state.
    if (!proc.state().deserialize(std::span<const uint8_t>(store_decoded->data(),
                                                           store_decoded->size()))) {
        r.error = PresetLoadError::CorruptState; return r;
    }
    if (!proc.deserialize_plugin_state(std::span<const uint8_t>(plugin_state_bytes.data(),
                                                                plugin_state_bytes.size()))) {
        r.error = PresetLoadError::CorruptState; return r;
    }

    return r;
}

PresetLoadResult load_preset_from_file(pulp::format::Processor& proc,
                                       const std::string& path)
{
    const auto contents = read_file_(path);
    if (contents.empty()) {
        PresetLoadResult r;
        r.error = PresetLoadError::MalformedJson;
        return r;
    }
    return load_preset_from_string(proc, contents);
}

// ── Error descriptions ────────────────────────────────────────────────

const char* describe(PresetLoadError e) noexcept {
    switch (e) {
        case PresetLoadError::None:             return "ok";
        case PresetLoadError::MalformedJson:    return "preset file is not valid JSON";
        case PresetLoadError::NotASpectrPreset: return "file is not a Spectr preset (format tag missing or wrong)";
        case PresetLoadError::SchemaMismatch:   return "preset was saved by a different Spectr schema — migrate it through the version that wrote it";
        case PresetLoadError::MissingState:     return "preset is missing required state blocks";
        case PresetLoadError::CorruptState:     return "preset state failed to decode — file may be corrupt";
    }
    return "unknown error";
}

} // namespace spectr
