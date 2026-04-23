#pragma once

// Pattern library — data model, factory presets, import/export.
//
// A Pattern captures ONLY canonical band gains + mutes (kMaxBands slots).
// It does NOT encode viewport, layout, response/engine mode, analyzer
// mode, or any editor chrome — patterns are portable and compose across
// layouts. Matches the prototype's pattern contract in
// spectr-design/Spectr-2/src/patterns.js.
//
// Persistence: PatternLibrary is held in Spectr's supplemental plugin
// state (the JSON blob serialized via pulp#625's
// Processor::serialize_plugin_state hook). Factory patterns are generated
// lazily; user patterns are stored as canonical dB arrays.
//
// UI wiring: the webview-embedded prototype invokes PatternLibrary over
// the JS bridge — import_json / export_json mirror the prototype's
// pattern-manager modal (save, rename, duplicate, delete, import, export,
// set-as-default).

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "spectr/band_state.hpp"

namespace spectr {

/// Canonical pattern schema version. Bump when the JSON layout changes
/// in a non-backward-compatible way; importers reject unknown versions.
inline constexpr int kPatternSchemaVersion = 1;

enum class PatternSource : std::uint8_t { Factory, User };

struct Pattern {
    std::string   id;          ///< "factory:flat", "user:<uuid>", etc.
    std::string   name;
    PatternSource source = PatternSource::User;
    std::array<float, kMaxBands> gain_db{};
    std::array<bool,  kMaxBands> muted{};
    std::string   created_at;  ///< ISO 8601, user patterns only
    std::string   updated_at;  ///< ISO 8601, user patterns only
    std::vector<std::string> tags;

    /// Apply this pattern to a BandField — copies gain/mute onto the
    /// canonical 64-slot array. Layout projection happens in the engine
    /// (it reads only the first n_visible slots).
    void apply_to(BandField& field) const;
};

/// Factory IDs — stable, referenced by string.
namespace factory_ids {
inline constexpr const char* kFlat       = "factory:flat";
inline constexpr const char* kHarmonic   = "factory:harmonic";
inline constexpr const char* kAlternate  = "factory:alternate";
inline constexpr const char* kComb       = "factory:comb";
inline constexpr const char* kVocal      = "factory:vocal";
inline constexpr const char* kSubOnly    = "factory:sub";
inline constexpr const char* kTilt       = "factory:tilt";
inline constexpr const char* kAirLift    = "factory:air";
} // namespace factory_ids

/// Generate a factory pattern's gain/mute arrays by id. Returns nullopt
/// for an unknown id. Values mapped from the prototype's [-1, +1] /
/// -Infinity semantics to Spectr's dB range: positive → kDbMax,
/// negative → kDbMin, -Infinity → muted.
std::optional<Pattern> build_factory_pattern(std::string_view id);

/// All eight factory patterns, in display order.
std::vector<Pattern> all_factory_patterns();

/// Library of factory + user patterns. Held inside Spectr's state so
/// it persists through the supplemental plugin-state blob.
class PatternLibrary {
public:
    PatternLibrary();   ///< initializes with all factory patterns + default

    const std::vector<Pattern>& factory() const noexcept { return factory_; }
    const std::vector<Pattern>& user()    const noexcept { return user_; }
    const std::string&          default_id() const noexcept { return default_id_; }

    /// CRUD on user patterns. save_current and duplicate return a copy
    /// of the newly-added pattern — re-look-up via find(id) if you need
    /// the live object (the returned copy stays stable across
    /// subsequent mutations).
    Pattern  save_current(const BandField& current_state,
                          std::string name = "");  ///< empty name = auto-named PATTERN NN
    bool     rename(const std::string& id, std::string new_name);
    std::optional<Pattern> duplicate(const std::string& source_id);
    bool     remove(const std::string& id);
    bool     update_from_current(const std::string& id, const BandField& current);

    /// Default-pattern (loaded on plugin open).
    bool     set_default(const std::string& id);

    /// Lookup by id across factory + user.
    const Pattern* find(const std::string& id) const;

    /// Serialize the user-patterns + default-id to a JSON envelope that
    /// round-trips through import_json(). Does NOT include factory
    /// patterns (they're rebuilt on construction). Envelope shape:
    /// `{"format":"spectr.patterns","version":1,"patterns":[...],
    ///   "default_id":"..."}`.
    std::string export_json() const;

    /// Parse a JSON envelope produced by export_json() (or by the
    /// prototype's pattern-manager modal). Appends imported user
    /// patterns — clashing names get suffixed (e.g. "MYPATTERN (2)").
    /// Returns number of patterns imported; 0 on any parse failure.
    std::size_t import_json(std::string_view json);

private:
    std::vector<Pattern> factory_;
    std::vector<Pattern> user_;
    std::string          default_id_ = factory_ids::kFlat;
    int                  next_user_number_ = 1;
};

} // namespace spectr
