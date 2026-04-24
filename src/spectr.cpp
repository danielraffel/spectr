#include "spectr/spectr.hpp"
#include "spectr/ui/editor_view.hpp"

#include <choc/containers/choc_Value.h>
#include <choc/text/choc_JSON.h>

#include <algorithm>
#include <cmath>
#include <string>
#include <string_view>

namespace spectr {

Spectr::Spectr()  = default;
Spectr::~Spectr() = default;

pulp::format::PluginDescriptor Spectr::descriptor() const {
    return make_descriptor();
}

namespace {

constexpr std::size_t kLayoutCount = 5;
constexpr std::array<Layout, kLayoutCount> kLayoutValues = {
    Layout::Bands32, Layout::Bands40, Layout::Bands48,
    Layout::Bands56, Layout::Bands64,
};

Layout layout_from_index(float idx_float) noexcept {
    const int idx = std::clamp(static_cast<int>(idx_float + 0.5f), 0,
                               static_cast<int>(kLayoutCount) - 1);
    return kLayoutValues[static_cast<std::size_t>(idx)];
}

int layout_to_index(Layout L) noexcept {
    for (std::size_t i = 0; i < kLayoutCount; ++i) {
        if (kLayoutValues[i] == L) return static_cast<int>(i);
    }
    return 0;
}

} // namespace

void Spectr::define_parameters(pulp::state::StateStore& store) {
    store.add_parameter({
        .id    = kMix,
        .name  = "Mix",
        .unit  = "%",
        .range = {0.0f, 100.0f, 100.0f},
    });
    store.add_parameter({
        .id    = kOutputTrim,
        .name  = "Output",
        .unit  = "dB",
        .range = {-24.0f, 24.0f, 0.0f},
    });
    store.add_parameter({
        .id    = kResponseMode,
        .name  = "Response",
        .unit  = "",
        .range = {0.0f, 1.0f, 1.0f},   // default Precision
    });
    store.add_parameter({
        .id    = kEngineMode,
        .name  = "Engine",
        .unit  = "",
        .range = {0.0f, 2.0f, 1.0f},   // default Fft
    });
    store.add_parameter({
        .id    = kBandCount,
        .name  = "Bands",
        .unit  = "",
        .range = {0.0f, 4.0f, 0.0f},   // default 32-band layout
    });
    store.add_parameter({
        .id    = kMorph,
        .name  = "Morph",
        .unit  = "",
        .range = {0.0f, 1.0f, 0.0f},   // default A (no morph)
    });

    // Wire ABCompare now that the store reference is live. Keeps the
    // StateStore-side A/B under pulp::view::ABCompare and the band-field
    // side under SnapshotBank — UI drives both together.
    ab_ = std::make_unique<pulp::view::ABCompare>(&store);
}

// ── Snapshot A/B (Milestone 8) ─────────────────────────────────────────

void Spectr::capture_snapshot(SnapshotBank::Slot slot) noexcept {
    snapshots_.capture_into(slot, field_, viewport_, layout_);
}

void Spectr::apply_morph_to_live(float t) noexcept {
    const bool has_a = snapshots_.has(SnapshotBank::Slot::A);
    const bool has_b = snapshots_.has(SnapshotBank::Slot::B);
    if (!has_a && !has_b) return;
    if (!has_a) { field_ = snapshots_.b.field; return; }
    if (!has_b) { field_ = snapshots_.a.field; return; }
    morph_fields(field_, snapshots_.a.field, snapshots_.b.field, t);
}

pulp::view::ABCompare* Spectr::ab_compare() noexcept {
    // Constructed in define_parameters once the StateStore reference is
    // live. Callers that invoke this before define_parameters get a
    // nullptr — don't dereference without checking.
    return ab_.get();
}

void Spectr::prepare(const pulp::format::PrepareContext& ctx) {
    sample_rate_ = ctx.sample_rate;
    max_block_   = ctx.max_buffer_size;
    rebuild_engine_();
    configure_bridge_(ctx.output_channels);
}

std::unique_ptr<pulp::view::View> Spectr::create_view() {
    // The editor is the prototype HTML embedded verbatim through Pulp's
    // WebView bridge. Pixel-perfect visual match by construction; JS↔C++
    // state sync flows through EditorView's message handler. See
    // include/spectr/ui/editor_view.hpp.
    // No explicit set_bounds — the framework lays us out to the window's
    // content area. EditorView attaches the native child view to that
    // actual laid-out size (or PluginViewHost::get_size() in plugins), so
    // we don't leave a gap if window chrome differs from our preferred
    // size.
    return std::make_unique<EditorView>(*this);
}

void Spectr::on_view_opened(pulp::view::View& view) {
    if (auto* editor = dynamic_cast<EditorView*>(&view)) {
        editor->attach_if_needed();
    }
}

void Spectr::on_view_resized(pulp::view::View& view, uint32_t /*w*/, uint32_t /*h*/) {
    if (auto* editor = dynamic_cast<EditorView*>(&view)) {
        editor->sync_to_host();
    }
}

void Spectr::on_view_closed(pulp::view::View& view) {
    if (auto* editor = dynamic_cast<EditorView*>(&view)) {
        editor->detach_if_needed();
    }
}

void Spectr::configure_bridge_(int num_channels) {
    pulp::view::VisualizationConfig c;
    c.fft_size         = 1024;
    c.hop_size         = 256;
    c.window           = pulp::signal::WindowFunction::Type::hann;
    c.num_channels     = std::max(1, num_channels);
    c.sample_rate      = static_cast<float>(sample_rate_);
    c.capture_waveform = true;
    c.waveform_length  = 1024;
    bridge_.configure(c);
}

void Spectr::release() {
    if (engine_) engine_->release();
    bridge_.reset();
}

void Spectr::set_layout(Layout L) {
    layout_ = L;
    rebuild_engine_();
}

void Spectr::set_engine_kind(EngineKind k) {
    engine_kind_ = k;
    rebuild_engine_();
}

void Spectr::rebuild_engine_() {
    engine_ = make_engine(engine_kind_);
    if (engine_) {
        EnginePrepare p;
        p.sample_rate = sample_rate_;
        p.max_block   = max_block_;
        p.layout      = layout_;
        p.viewport    = viewport_;
        engine_->prepare(p);
    }
}

void Spectr::process(
    pulp::audio::BufferView<float>& output,
    const pulp::audio::BufferView<const float>& input,
    pulp::midi::MidiBuffer& /*midi_in*/,
    pulp::midi::MidiBuffer& /*midi_out*/,
    const pulp::format::ProcessContext& /*ctx*/)
{
    // Sync host-automatable params into the engine's working state. Doing
    // this each block is cheap (5 atomic loads) and keeps host automation
    // responsive without extra plumbing. Tests that drive Spectr without a
    // StateStore wired up still work because the Processor base asserts on
    // state() dereference before this runs.
    const float mix        = state().get_value(kMix) / 100.0f;
    const float out_trim_db= state().get_value(kOutputTrim);
    const auto  rm         = static_cast<ResponseMode>(static_cast<int>(
        std::clamp(state().get_value(kResponseMode) + 0.5f, 0.0f, 1.0f)));
    const auto  ek         = static_cast<EngineKind>(static_cast<int>(
        std::clamp(state().get_value(kEngineMode) + 0.5f, 0.0f, 2.0f)));
    const Layout desired_layout = layout_from_index(state().get_value(kBandCount));

    if (rm != response_mode_) response_mode_ = rm;
    if (ek != engine_kind_)  set_engine_kind(ek);
    if (desired_layout != layout_) set_layout(desired_layout);

    if (engine_) {
        engine_->process(output, input, field_, viewport_, layout_, response_mode_);

        // Apply output trim (dB → linear) and dry/wet mix in one pass.
        const float out_gain = std::pow(10.0f, out_trim_db * 0.05f);
        const float dry_gain = (1.0f - mix) * out_gain;
        const float wet_gain = mix * out_gain;
        for (std::size_t ch = 0; ch < output.num_channels(); ++ch) {
            auto dst = output.channel(ch);
            auto src = input.channel(ch);
            for (std::size_t i = 0; i < dst.size(); ++i) {
                dst[i] = dry_gain * src[i] + wet_gain * dst[i];
            }
        }

        // Publish post-engine audio to the UI thread via VisualizationBridge.
        const auto nc = output.num_channels();
        if (nc > 0 && nc <= 8) {
            const float* ptrs[8];
            for (std::size_t ch = 0; ch < nc; ++ch) {
                ptrs[ch] = output.channel(ch).data();
            }
            bridge_.process(ptrs, static_cast<int>(nc),
                            static_cast<int>(output.num_samples()));
        }
        return;
    }

    // Fallback: straight copy.
    for (std::size_t ch = 0; ch < output.num_channels(); ++ch) {
        auto dst = output.channel(ch);
        auto src = input.channel(ch);
        for (std::size_t i = 0; i < dst.size(); ++i) dst[i] = src[i];
    }
}

// ── Supplemental plugin state (pulp#625) ──────────────────────────────

namespace {

// Turn a FieldSnapshot into a JSON object. Symmetric with
// read_snapshot_() below.
choc::value::Value write_snapshot_(const FieldSnapshot& s) {
    using choc::value::createObject;
    using choc::value::createEmptyArray;

    auto obj = createObject("FieldSnapshot");
    obj.addMember("populated", s.populated);

    auto gains = createEmptyArray();
    auto mutes = createEmptyArray();
    for (const auto& b : s.field.bands) {
        gains.addArrayElement(static_cast<double>(b.gain_db));
        mutes.addArrayElement(b.muted);
    }
    obj.addMember("band_gain", gains);
    obj.addMember("band_mute", mutes);
    obj.addMember("view_min_hz", static_cast<double>(s.viewport.min_hz));
    obj.addMember("view_max_hz", static_cast<double>(s.viewport.max_hz));
    obj.addMember("layout_index", static_cast<int32_t>(layout_to_index(s.layout)));
    return obj;
}

} // namespace

std::vector<uint8_t> Spectr::serialize_plugin_state() const {
    using choc::value::createObject;
    using choc::value::createEmptyArray;

    auto root = createObject("SpectrPluginState");
    root.addMember("version", static_cast<int32_t>(kPluginStateVersion));

    // band_gain[64] + band_mute[64] — canonical slots.
    auto gains = createEmptyArray();
    auto mutes = createEmptyArray();
    for (const auto& b : field_.bands) {
        gains.addArrayElement(static_cast<double>(b.gain_db));
        mutes.addArrayElement(b.muted);
    }
    root.addMember("band_gain", gains);
    root.addMember("band_mute", mutes);

    // Viewport (sound-defining, per §5.5.1).
    root.addMember("view_min_hz", static_cast<double>(viewport_.min_hz));
    root.addMember("view_max_hz", static_cast<double>(viewport_.max_hz));

    // Layout — also exposed as a flat param, but persist here too so the
    // full restore is self-contained if a host replays only the plugin
    // blob (defensive against adapter-edge bugs).
    //
    // Read directly from StateStore rather than the cached `layout_`
    // member. `layout_` is materialized from `kBandCount` during
    // process(); a host that writes the param via the CLAP flush path
    // (or VST3 setParamNormalized, or AU kAudioUnitSetProperty) and
    // then calls state-save without an intervening process() block
    // would otherwise see a stale cached value. This was caught by
    // clap-validator's `state-reproducibility-flush` test.
    const auto band_count_idx = static_cast<int32_t>(
        layout_to_index(layout_from_index(state().get_value(kBandCount))));
    root.addMember("layout_index", band_count_idx);

    // Editor state placeholders — analyzer / edit mode UI selection. Not
    // sound-defining for V1; M5+ fills them in.
    root.addMember("analyzer_mode", static_cast<int32_t>(0));
    root.addMember("edit_mode",     static_cast<int32_t>(0));

    // M8 — snapshot bank. Absent or empty on a v1 blob; new v2 writers
    // always include it so a round-trip preserves the A/B selection
    // across session reloads.
    auto snaps = createObject("SnapshotBank");
    snaps.addMember("active", static_cast<int32_t>(snapshots_.active));
    snaps.addMember("a", write_snapshot_(snapshots_.a));
    snaps.addMember("b", write_snapshot_(snapshots_.b));
    root.addMember("snapshots", snaps);

    // M9.5 — user patterns. PatternLibrary::export_json() emits only
    // user patterns (factory presets are rebuilt at construction) so
    // the blob stays compact and a session reload rebuilds factories
    // from code, not from the stored state. Embedded as a string
    // because the library owns its own envelope shape and versioning
    // — keeps the two serializers decoupled. Absent on a pre-9.5
    // writer; readers treat absence as "no user patterns".
    root.addMember("patterns_json", patterns_.export_json());

    auto json = choc::json::toString(root, /*useLineBreaks=*/false);
    return {json.begin(), json.end()};
}

namespace {

void reset_supplemental_state_(BandField& f, Viewport& v, Layout& l,
                               SnapshotBank& bank, PatternLibrary& patterns) {
    f.reset();
    v = Viewport{};
    l = Layout::Bands32;
    bank = SnapshotBank{};
    patterns = PatternLibrary{};  // restores factories, drops user patterns
}

// Symmetric with write_snapshot_(). Returns true if `obj` was read
// into `dst` without error. An unpopulated slot (empty object, or
// `populated == false`) resets dst to default.
//
// Takes a ValueView (what `parent["key"]` returns) rather than a Value
// so callers don't have to copy the subtree out of the parent.
bool read_snapshot_(const choc::value::ValueView& obj, FieldSnapshot& dst) {
    if (!obj.isObject()) { dst = FieldSnapshot{}; return true; }

    FieldSnapshot staged{};
    staged.populated = false;

    if (obj.hasObjectMember("populated")) {
        const auto e = obj["populated"];
        staged.populated = e.isBool() ? e.getBool() : false;
    }
    if (obj.hasObjectMember("band_gain") && obj["band_gain"].isArray()) {
        auto arr = obj["band_gain"];
        const auto n = std::min<std::uint32_t>(arr.size(), kMaxBands);
        for (std::uint32_t i = 0; i < n; ++i) {
            const auto e = arr[i];
            float g = 0.0f;
            if      (e.isFloat64()) g = static_cast<float>(e.getFloat64());
            else if (e.isInt64())   g = static_cast<float>(e.getInt64());
            else if (e.isInt32())   g = static_cast<float>(e.getInt32());
            staged.field.bands[i].gain_db = g;
        }
    }
    if (obj.hasObjectMember("band_mute") && obj["band_mute"].isArray()) {
        auto arr = obj["band_mute"];
        const auto n = std::min<std::uint32_t>(arr.size(), kMaxBands);
        for (std::uint32_t i = 0; i < n; ++i) {
            const auto e = arr[i];
            staged.field.bands[i].muted = e.isBool() ? e.getBool() : false;
        }
    }
    if (obj.hasObjectMember("view_min_hz")) {
        const auto e = obj["view_min_hz"];
        if      (e.isFloat64()) staged.viewport.min_hz = static_cast<float>(e.getFloat64());
        else if (e.isInt64())   staged.viewport.min_hz = static_cast<float>(e.getInt64());
    }
    if (obj.hasObjectMember("view_max_hz")) {
        const auto e = obj["view_max_hz"];
        if      (e.isFloat64()) staged.viewport.max_hz = static_cast<float>(e.getFloat64());
        else if (e.isInt64())   staged.viewport.max_hz = static_cast<float>(e.getInt64());
    }
    if (!staged.viewport.valid()) staged.viewport = Viewport{};
    if (obj.hasObjectMember("layout_index")) {
        const auto e = obj["layout_index"];
        int idx = 0;
        if      (e.isInt32())   idx = e.getInt32();
        else if (e.isInt64())   idx = static_cast<int>(e.getInt64());
        else if (e.isFloat64()) idx = static_cast<int>(e.getFloat64());
        idx = std::clamp(idx, 0, static_cast<int>(kLayoutCount) - 1);
        staged.layout = kLayoutValues[static_cast<std::size_t>(idx)];
    }

    dst = staged;
    return true;
}

} // namespace

bool Spectr::deserialize_plugin_state(std::span<const uint8_t> bytes) {
    // Empty span = legacy blob or caller signalling "reset to defaults"
    // per the pulp#625 hook contract.
    if (bytes.empty()) {
        reset_supplemental_state_(field_, viewport_, layout_, snapshots_, patterns_);
        return true;
    }

    std::string_view text(reinterpret_cast<const char*>(bytes.data()), bytes.size());
    choc::value::Value root;
    try {
        root = choc::json::parse(text);
    } catch (...) {
        return false;
    }
    if (!root.isObject()) return false;

    // Version gate — accept v1 (legacy pre-M8) and v2 (with snapshots).
    // Reject anything else we don't know how to read.
    if (!root.hasObjectMember("version")) return false;
    const auto v = root["version"];
    int version = 0;
    if      (v.isInt32())    version = v.getInt32();
    else if (v.isInt64())    version = static_cast<int>(v.getInt64());
    else if (v.isFloat64())  version = static_cast<int>(v.getFloat64());
    else                     return false;
    if (version < 1 || version > kPluginStateVersion) return false;

    // Apply in a staging copy so a malformed payload leaves live state alone.
    BandField new_field;
    Viewport  new_view  = viewport_;
    Layout    new_layout = layout_;

    if (root.hasObjectMember("band_gain") && root["band_gain"].isArray()) {
        auto arr = root["band_gain"];
        const auto n = std::min<std::uint32_t>(arr.size(), kMaxBands);
        for (std::uint32_t i = 0; i < n; ++i) {
            const auto e = arr[i];
            float g = 0.0f;
            if      (e.isFloat64()) g = static_cast<float>(e.getFloat64());
            else if (e.isInt64())   g = static_cast<float>(e.getInt64());
            else if (e.isInt32())   g = static_cast<float>(e.getInt32());
            new_field.bands[i].gain_db = g;
        }
    }
    if (root.hasObjectMember("band_mute") && root["band_mute"].isArray()) {
        auto arr = root["band_mute"];
        const auto n = std::min<std::uint32_t>(arr.size(), kMaxBands);
        for (std::uint32_t i = 0; i < n; ++i) {
            const auto e = arr[i];
            new_field.bands[i].muted = e.isBool() ? e.getBool() : false;
        }
    }
    if (root.hasObjectMember("view_min_hz")) {
        const auto e = root["view_min_hz"];
        if      (e.isFloat64()) new_view.min_hz = static_cast<float>(e.getFloat64());
        else if (e.isInt64())   new_view.min_hz = static_cast<float>(e.getInt64());
    }
    if (root.hasObjectMember("view_max_hz")) {
        const auto e = root["view_max_hz"];
        if      (e.isFloat64()) new_view.max_hz = static_cast<float>(e.getFloat64());
        else if (e.isInt64())   new_view.max_hz = static_cast<float>(e.getInt64());
    }
    if (root.hasObjectMember("layout_index")) {
        const auto e = root["layout_index"];
        int idx = 0;
        if      (e.isInt32())   idx = e.getInt32();
        else if (e.isInt64())   idx = static_cast<int>(e.getInt64());
        else if (e.isFloat64()) idx = static_cast<int>(e.getFloat64());
        idx = std::clamp(idx, 0, static_cast<int>(kLayoutCount) - 1);
        new_layout = kLayoutValues[static_cast<std::size_t>(idx)];
    }

    // Viewport sanity — fall back to defaults on garbage values.
    if (!new_view.valid()) new_view = Viewport{};

    // M8 — snapshot bank (version 2+). Absent or malformed resets the
    // bank to empty; a well-formed block round-trips exactly.
    SnapshotBank new_bank{};
    if (version >= 2 && root.hasObjectMember("snapshots") && root["snapshots"].isObject()) {
        const auto snaps = root["snapshots"];
        if (snaps.hasObjectMember("active")) {
            const auto e = snaps["active"];
            int a = 0;
            if      (e.isInt32())   a = e.getInt32();
            else if (e.isInt64())   a = static_cast<int>(e.getInt64());
            else if (e.isFloat64()) a = static_cast<int>(e.getFloat64());
            new_bank.active = (a == 1) ? SnapshotBank::Slot::B : SnapshotBank::Slot::A;
        }
        if (snaps.hasObjectMember("a")) read_snapshot_(snaps["a"], new_bank.a);
        if (snaps.hasObjectMember("b")) read_snapshot_(snaps["b"], new_bank.b);
    }

    // M9.5 — user patterns. Apply into a fresh library so the factory
    // presets are present regardless of what the blob carried; import
    // appends the stored user patterns + default_id. Swap-on-success.
    PatternLibrary new_patterns{};
    if (root.hasObjectMember("patterns_json") && root["patterns_json"].isString()) {
        const auto s = root["patterns_json"].getString();
        new_patterns.import_json(std::string_view(s));
    }

    field_     = new_field;
    viewport_  = new_view;
    snapshots_ = new_bank;
    patterns_  = std::move(new_patterns);
    if (new_layout != layout_) set_layout(new_layout);
    return true;
}

} // namespace spectr
