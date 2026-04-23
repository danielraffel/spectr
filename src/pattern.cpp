#include "spectr/pattern.hpp"

#include <choc/containers/choc_Value.h>
#include <choc/text/choc_JSON.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <ctime>
#include <limits>
#include <random>
#include <string>
#include <string_view>

namespace spectr {

namespace {

constexpr float kDbMin = -60.0f;
constexpr float kDbMax = +12.0f;

/// Map the prototype's [-1, +1] band value into Spectr's dB range.
/// -Infinity in the prototype becomes muted=true in our model.
void proto_to_db(float proto_value, float& out_db, bool& out_muted) noexcept {
    if (std::isinf(proto_value) && proto_value < 0) {
        out_muted = true;
        out_db    = kDbMin;
        return;
    }
    out_muted = false;
    const float clamped = std::clamp(proto_value, -1.0f, 1.0f);
    out_db = clamped >= 0.0f ? clamped * kDbMax : -clamped * kDbMin;
    // Note: -clamped * kDbMin because kDbMin is -60 and we want proto=-0.5
    // to land at -30 dB, not +30. Derivation: -(-0.5) * (-60) = -30. ✓
}

std::string iso_now() {
    const auto t = std::time(nullptr);
    std::tm tm{};
#if defined(_WIN32)
    gmtime_s(&tm, &t);
#else
    gmtime_r(&t, &tm);
#endif
    char buf[24];
    std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &tm);
    return buf;
}

std::string make_uuid() {
    // Simple pseudo-UUID ("user:<16 hex chars>") — sufficient for ids
    // scoped to one plugin instance. Not cryptographic.
    static std::mt19937_64 rng{std::random_device{}()};
    std::uint64_t r = rng();
    char buf[32];
    std::snprintf(buf, sizeof(buf), "user:%016llx",
                  static_cast<unsigned long long>(r));
    return buf;
}

// ── Factory pattern generators ────────────────────────────────────────
// Ports of spectr-design/Spectr-2/src/patterns.js genFlat / genHarmonic /
// etc. Output values are in the prototype's [-1, +1] / -Infinity model;
// proto_to_db() then maps to Spectr's dB range.

using ProtoGains = std::array<float, kMaxBands>;
constexpr float kInf = std::numeric_limits<float>::infinity();
constexpr std::size_t N = kMaxBands;

ProtoGains gen_flat() {
    ProtoGains g{};  // zero-init
    return g;
}

ProtoGains gen_harmonic() {
    ProtoGains g{};
    for (auto& v : g) v = -kInf;
    const double lmin = std::log10(20.0);
    const double lmax = std::log10(20000.0);
    constexpr double kBase = 110.0;
    for (int h = 1; h <= 16; ++h) {
        const double f = kBase * h;
        if (f > 20000.0) break;
        const double lf = std::log10(f);
        const double pos = (lf - lmin) / (lmax - lmin);
        const int idx = static_cast<int>(std::round(pos * (N - 1)));
        if (idx >= 0 && idx < static_cast<int>(N)) {
            const float val = static_cast<float>(1.0 - (h - 1) * 0.04);
            if (std::isinf(g[idx]) || val > g[idx]) g[idx] = val;
        }
    }
    return g;
}

ProtoGains gen_alternate() {
    ProtoGains g{};
    for (std::size_t i = 0; i < N; ++i) g[i] = (i % 2 == 0) ? 0.6f : -kInf;
    return g;
}

ProtoGains gen_comb() {
    ProtoGains g{};
    for (std::size_t i = 0; i < N; ++i) g[i] = (i % 3 == 0) ? 0.4f : -0.6f;
    return g;
}

ProtoGains gen_vocal() {
    ProtoGains g{};
    for (auto& v : g) v = -kInf;
    const double lmin = std::log10(20.0);
    const double lmax = std::log10(20000.0);
    for (double f : {300.0, 900.0, 2800.0}) {
        const double lf  = std::log10(f);
        const double pos = (lf - lmin) / (lmax - lmin);
        const int c = static_cast<int>(std::round(pos * (N - 1)));
        for (int d = -2; d <= 2; ++d) {
            const int i = c + d;
            if (i < 0 || i >= static_cast<int>(N)) continue;
            g[i] = (d == 0) ? 1.0f : 0.5f;
        }
    }
    return g;
}

ProtoGains gen_sub_only() {
    ProtoGains g{};
    const double lmin = std::log10(20.0);
    const double lmax = std::log10(20000.0);
    for (std::size_t i = 0; i < N; ++i) {
        const double pos = (i + 0.5) / N;
        const double lf  = lmin + pos * (lmax - lmin);
        const double f   = std::pow(10.0, lf);
        g[i] = (f < 160.0) ? 0.5f : -kInf;
    }
    return g;
}

ProtoGains gen_tilt() {
    ProtoGains g{};
    for (std::size_t i = 0; i < N; ++i) {
        g[i] = static_cast<float>(0.5 - (static_cast<double>(i) / (N - 1)) * 1.0);
    }
    return g;
}

ProtoGains gen_air_lift() {
    ProtoGains g{};
    const double lmin = std::log10(20.0);
    const double lmax = std::log10(20000.0);
    for (std::size_t i = 0; i < N; ++i) {
        const double pos = (i + 0.5) / N;
        const double lf  = lmin + pos * (lmax - lmin);
        const double f   = std::pow(10.0, lf);
        if (f < 4000.0) {
            g[i] = 0.0f;
        } else {
            g[i] = static_cast<float>(std::min(0.7, std::log2(f / 4000.0) * 0.3));
        }
    }
    return g;
}

Pattern make_factory(std::string id, std::string name, const ProtoGains& p,
                     std::vector<std::string> tags = {}) {
    Pattern out;
    out.id     = std::move(id);
    out.name   = std::move(name);
    out.source = PatternSource::Factory;
    for (std::size_t i = 0; i < kMaxBands; ++i) {
        proto_to_db(p[i], out.gain_db[i], out.muted[i]);
    }
    out.tags = std::move(tags);
    return out;
}

} // namespace

// ── Pattern API ───────────────────────────────────────────────────────

void Pattern::apply_to(BandField& field) const {
    for (std::size_t i = 0; i < kMaxBands; ++i) {
        field.bands[i].gain_db = gain_db[i];
        field.bands[i].muted   = muted[i];
    }
}

std::optional<Pattern> build_factory_pattern(std::string_view id) {
    using namespace factory_ids;
    if (id == kFlat)      return make_factory(kFlat,      "FLAT",            gen_flat(),      {"baseline"});
    if (id == kHarmonic)  return make_factory(kHarmonic,  "HARMONIC SERIES", gen_harmonic(),  {"musical"});
    if (id == kAlternate) return make_factory(kAlternate, "ALTERNATING",     gen_alternate(), {"structural"});
    if (id == kComb)      return make_factory(kComb,      "COMB",            gen_comb(),      {"structural"});
    if (id == kVocal)     return make_factory(kVocal,     "VOCAL FORMANTS",  gen_vocal(),     {"tonal"});
    if (id == kSubOnly)   return make_factory(kSubOnly,   "SUB ONLY (< 160 Hz)", gen_sub_only(), {"tonal"});
    if (id == kTilt)      return make_factory(kTilt,      "DOWNWARD TILT",   gen_tilt(),      {"baseline"});
    if (id == kAirLift)   return make_factory(kAirLift,   "AIR LIFT (4k+)",  gen_air_lift(),  {"tonal"});
    return std::nullopt;
}

std::vector<Pattern> all_factory_patterns() {
    using namespace factory_ids;
    std::vector<Pattern> out;
    out.reserve(8);
    for (auto id : {kFlat, kHarmonic, kAlternate, kComb, kVocal, kSubOnly, kTilt, kAirLift}) {
        if (auto p = build_factory_pattern(id)) out.push_back(*std::move(p));
    }
    return out;
}

// ── PatternLibrary ────────────────────────────────────────────────────

PatternLibrary::PatternLibrary() : factory_(all_factory_patterns()) {}

Pattern PatternLibrary::save_current(const BandField& current, std::string name) {
    Pattern p;
    p.id     = make_uuid();
    if (name.empty()) {
        char buf[32];
        std::snprintf(buf, sizeof(buf), "PATTERN %02d", next_user_number_++);
        p.name = buf;
    } else {
        p.name = std::move(name);
    }
    p.source     = PatternSource::User;
    p.created_at = iso_now();
    p.updated_at = p.created_at;
    for (std::size_t i = 0; i < kMaxBands; ++i) {
        p.gain_db[i] = current.bands[i].gain_db;
        p.muted[i]   = current.bands[i].muted;
    }
    user_.push_back(p);
    return p;
}

bool PatternLibrary::rename(const std::string& id, std::string new_name) {
    for (auto& p : user_) {
        if (p.id == id) {
            p.name = std::move(new_name);
            p.updated_at = iso_now();
            return true;
        }
    }
    return false;
}

std::optional<Pattern> PatternLibrary::duplicate(const std::string& source_id) {
    const Pattern* src = find(source_id);
    if (!src) return std::nullopt;
    Pattern copy = *src;
    copy.id         = make_uuid();
    copy.name       = src->name + " COPY";
    copy.source     = PatternSource::User;
    copy.created_at = iso_now();
    copy.updated_at = copy.created_at;
    user_.push_back(copy);
    return copy;
}

bool PatternLibrary::remove(const std::string& id) {
    auto it = std::remove_if(user_.begin(), user_.end(),
                             [&](const Pattern& p) { return p.id == id; });
    if (it == user_.end()) return false;
    user_.erase(it, user_.end());
    if (default_id_ == id) default_id_ = factory_ids::kFlat;
    return true;
}

bool PatternLibrary::update_from_current(const std::string& id, const BandField& current) {
    for (auto& p : user_) {
        if (p.id == id) {
            for (std::size_t i = 0; i < kMaxBands; ++i) {
                p.gain_db[i] = current.bands[i].gain_db;
                p.muted[i]   = current.bands[i].muted;
            }
            p.updated_at = iso_now();
            return true;
        }
    }
    return false;
}

bool PatternLibrary::set_default(const std::string& id) {
    if (find(id) == nullptr) return false;
    default_id_ = id;
    return true;
}

const Pattern* PatternLibrary::find(const std::string& id) const {
    for (const auto& p : factory_) if (p.id == id) return &p;
    for (const auto& p : user_)    if (p.id == id) return &p;
    return nullptr;
}

// ── JSON envelope (import/export) ─────────────────────────────────────

std::string PatternLibrary::export_json() const {
    using choc::value::createObject;
    using choc::value::createEmptyArray;

    auto env = createObject("SpectrPatterns");
    env.addMember("format",     "spectr.patterns");
    env.addMember("version",    static_cast<int32_t>(kPatternSchemaVersion));
    env.addMember("default_id", default_id_);

    auto arr = createEmptyArray();
    for (const auto& p : user_) {
        auto po = createObject("Pattern");
        po.addMember("id",         p.id);
        po.addMember("name",       p.name);
        po.addMember("source",     std::string{"user"});
        po.addMember("created_at", p.created_at);
        po.addMember("updated_at", p.updated_at);

        auto gains = createEmptyArray();
        auto mutes = createEmptyArray();
        for (std::size_t i = 0; i < kMaxBands; ++i) {
            gains.addArrayElement(static_cast<double>(p.gain_db[i]));
            mutes.addArrayElement(p.muted[i]);
        }
        po.addMember("gain_db", gains);
        po.addMember("muted",   mutes);

        if (!p.tags.empty()) {
            auto tg = createEmptyArray();
            for (const auto& t : p.tags) tg.addArrayElement(t);
            po.addMember("tags", tg);
        }
        arr.addArrayElement(po);
    }
    env.addMember("patterns", arr);
    return choc::json::toString(env, /*useLineBreaks=*/false);
}

std::size_t PatternLibrary::import_json(std::string_view json) {
    choc::value::Value root;
    try {
        root = choc::json::parse(json);
    } catch (...) {
        return 0;
    }
    if (!root.isObject()) return 0;

    // Version gate — reject unknown versions.
    if (!root.hasObjectMember("version")) return 0;
    const auto v = root["version"];
    int version = 0;
    if      (v.isInt32())   version = v.getInt32();
    else if (v.isInt64())   version = static_cast<int>(v.getInt64());
    else if (v.isFloat64()) version = static_cast<int>(v.getFloat64());
    else return 0;
    if (version != kPatternSchemaVersion) return 0;

    // Collect existing names (factory + user) for collision suffixing.
    std::vector<std::string> existing;
    existing.reserve(factory_.size() + user_.size());
    for (const auto& p : factory_) existing.push_back(p.name);
    for (const auto& p : user_)    existing.push_back(p.name);

    auto name_clash = [&](const std::string& n) {
        for (const auto& e : existing) if (e == n) return true;
        return false;
    };

    std::size_t imported = 0;
    if (root.hasObjectMember("patterns") && root["patterns"].isArray()) {
        auto arr = root["patterns"];
        for (std::uint32_t i = 0; i < arr.size(); ++i) {
            const auto po = arr[i];
            if (!po.isObject()) continue;
            Pattern p;
            p.id     = po.hasObjectMember("id") && po["id"].isString()
                       ? std::string(po["id"].getString()) : make_uuid();
            p.name   = po.hasObjectMember("name") && po["name"].isString()
                       ? std::string(po["name"].getString()) : "IMPORTED";
            p.source = PatternSource::User;
            p.created_at = po.hasObjectMember("created_at") && po["created_at"].isString()
                           ? std::string(po["created_at"].getString()) : iso_now();
            p.updated_at = po.hasObjectMember("updated_at") && po["updated_at"].isString()
                           ? std::string(po["updated_at"].getString()) : p.created_at;

            if (po.hasObjectMember("gain_db") && po["gain_db"].isArray()) {
                auto g = po["gain_db"];
                const auto n = std::min<std::uint32_t>(g.size(), kMaxBands);
                for (std::uint32_t j = 0; j < n; ++j) {
                    const auto e = g[j];
                    float v = 0.0f;
                    if      (e.isFloat64()) v = static_cast<float>(e.getFloat64());
                    else if (e.isInt64())   v = static_cast<float>(e.getInt64());
                    p.gain_db[j] = v;
                }
            }
            if (po.hasObjectMember("muted") && po["muted"].isArray()) {
                auto m = po["muted"];
                const auto n = std::min<std::uint32_t>(m.size(), kMaxBands);
                for (std::uint32_t j = 0; j < n; ++j) {
                    const auto e = m[j];
                    p.muted[j] = e.isBool() ? e.getBool() : false;
                }
            }

            // Collision suffix so imports never overwrite existing names.
            if (name_clash(p.name)) {
                std::string base = p.name;
                int suffix = 2;
                do {
                    p.name = base + " (" + std::to_string(suffix++) + ")";
                } while (name_clash(p.name));
            }
            existing.push_back(p.name);
            user_.push_back(std::move(p));
            ++imported;
        }
    }

    // Optional default_id — only honoured if it names a known pattern.
    if (root.hasObjectMember("default_id") && root["default_id"].isString()) {
        const std::string did(root["default_id"].getString());
        if (find(did) != nullptr) default_id_ = did;
    }

    return imported;
}

} // namespace spectr
