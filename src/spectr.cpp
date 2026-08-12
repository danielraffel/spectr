#include "spectr/spectr.hpp"
#if !defined(SPECTR_NATIVE_EDITOR)
#include "spectr/ui/editor_view.hpp"
#endif

#include <choc/containers/choc_Value.h>
#include <choc/text/choc_JSON.h>

#include <algorithm>
#include <cmath>
#include <optional>
#include <limits>
#include <string>
#include <string_view>

namespace spectr {

Spectr::Spectr() : editor_authority_(*this) {}
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
    if (!has_a) { replace_field(snapshots_.b.field); return; }
    if (!has_b) { replace_field(snapshots_.a.field); return; }
    morph_fields(field_, snapshots_.a.field, snapshots_.b.field, t);
    publish_field();
}

void Spectr::replace_field(const BandField& field) noexcept {
    field_ = field;
    publish_field();
}

bool Spectr::replace_processing_state(const BandField& field,
                                      const Viewport& viewport,
                                      Layout layout) noexcept {
    if (!viewport.valid()) return false;
    field_ = field;
    viewport_ = viewport;
    layout_ = layout;
    publish_processing_state_();
    return true;
}

void Spectr::publish_field() noexcept {
    publish_processing_state_();
}

pulp::signal::SpectralBandLayout Spectr::make_mask_layout_() const noexcept {
    pulp::signal::SpectralBandLayout mask_layout;
    mask_layout.active_bands = static_cast<std::uint32_t>(visible_count(layout_));
    mask_layout.min_hz = viewport_.min_hz;
    mask_layout.max_hz = viewport_.max_hz;
    mask_layout.spacing = pulp::signal::SpectralBandSpacing::logarithmic;
    // Preserve Periscope-style edge ownership: the first band owns bins below
    // the focused viewport (including DC), and the last owns bins above it
    // (including Nyquist). Muting those categorical edge bands makes the
    // viewport an exact isolation boundary; leaving them open retains the
    // exterior signal at their selected gain.
    mask_layout.edge_policy = pulp::signal::SpectralBandEdgePolicy::extend_edge_band;
    mask_layout.boundary_kernel = pulp::signal::SpectralMaskBoundaryKernel::hard;
    mask_layout.transition_fraction = 0.0f;
    mask_layout.transition_frames = 0;
    for (std::size_t i = 0; i < mask_layout.active_bands; ++i) {
        mask_layout.bands[i].gain_db = field_.bands[i].gain_db;
        mask_layout.bands[i].muted = field_.bands[i].muted;
    }
    return mask_layout;
}

bool Spectr::spectral_resolution(
    pulp::signal::SpectralBandResolution& out_resolution) const noexcept {
    if (!processor_prepared_) return false;
    return pulp::signal::analyze_spectral_band_resolution(
        make_mask_layout_(), kSpectralFftSize,
        static_cast<float>(sample_rate_), out_resolution);
}

void Spectr::publish_processing_state_() noexcept {
    auto mask_layout = make_mask_layout_();

    if (!processor_prepared_) return;
    if (!mask_processor_.publish_layout(mask_layout)) {
        // Invalid control state fails closed; never leave a stale audible
        // table active after a rejected geometry update.
        for (auto& band : mask_layout.bands) band.muted = true;
        mask_layout.min_hz = 20.0f;
        mask_layout.max_hz = std::min(20000.0f,
                                     static_cast<float>(sample_rate_ * 0.5));
        (void)mask_processor_.publish_layout(mask_layout);
    }
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
    channels_    = std::max(1, ctx.output_channels);

    pulp::signal::SpectralMaskProcessorConfig config;
    config.frame.fft_size = kSpectralFftSize;
    config.frame.analysis_hop = kSpectralAnalysisHop;
    config.frame.channels = channels_;
    config.frame.max_block = std::max(max_block_, 1);
    config.frame.window = pulp::signal::WindowFunction::Type::hann;
    config.sample_rate = static_cast<float>(sample_rate_);
    config.initial_mix = std::clamp(state().get_value(kMix) / 100.0f, 0.0f, 1.0f);
    config.mix_ramp_samples = 64;
    config.mix_curve = pulp::signal::MixCurve::Linear;
    processor_prepared_ = channels_ <= static_cast<int>(kMaximumChannels)
                       && mask_processor_.prepare(config);
    output_gain_.set_ramp_time(0.01f, static_cast<float>(sample_rate_));
    output_gain_.set_immediate(std::pow(
        10.0f, state().get_value(kOutputTrim) * 0.05f));
    publish_processing_state_();
    configure_bridge_(ctx.output_channels);
}

std::unique_ptr<pulp::view::View> Spectr::create_view() {
#if defined(SPECTR_NATIVE_EDITOR)
    return create_native_editor_();
#else
    // Release 1 embeds the reviewed editor.html. Visual parity remains
    // by construction; JS↔C++ state sync flows through EditorView's
    // message handler. See include/spectr/ui/editor_view.hpp.
    // No explicit set_bounds — the framework lays us out to the window's
    // content area. EditorView attaches the native child view to that
    // actual laid-out size (or PluginViewHost::get_size() in plugins),
    // so we don't leave a gap if window chrome differs from our
    // preferred size.
    return std::make_unique<EditorView>(*this);
#endif
}

pulp::format::ViewSize Spectr::view_size() const {
#if defined(SPECTR_NATIVE_EDITOR)
    return {
        kEditorDesignWidth,
        kEditorDesignHeight,
        kEditorDesignWidth,
        kEditorDesignHeight,
        kEditorDesignWidth,
        kEditorDesignHeight,
        kEditorAspectRatio,
    };
#else
    return {
        kEditorPreferredWidth,
        kEditorPreferredHeight,
        kEditorMinimumWidth,
        kEditorMinimumHeight,
        kEditorMaximumWidth,
        kEditorMaximumHeight,
        kEditorAspectRatio,
    };
#endif
}

void Spectr::on_view_opened(pulp::view::View& view) {
#if defined(SPECTR_NATIVE_EDITOR)
    open_native_editor_(view);
#else
    if (auto* editor = dynamic_cast<EditorView*>(&view)) {
        editor->attach_if_needed();
    }
#endif
}

void Spectr::on_view_resized(pulp::view::View& view, uint32_t /*w*/, uint32_t /*h*/) {
#if defined(SPECTR_NATIVE_EDITOR)
    if (&view == native_editor_root_) hydrate_native_editor_();
#else
    if (auto* editor = dynamic_cast<EditorView*>(&view)) {
        editor->sync_to_host();
    }
#endif
}

void Spectr::on_view_closed(pulp::view::View& view) {
#if defined(SPECTR_NATIVE_EDITOR)
    if (&view == native_editor_root_) close_native_editor_();
#else
    if (auto* editor = dynamic_cast<EditorView*>(&view)) {
        editor->detach_if_needed();
    }
#endif
}

void Spectr::configure_bridge_(int num_channels) {
    pulp::view::VisualizationConfig c;
    c.fft_size         = kAnalyzerFftSize;
    c.hop_size         = kAnalyzerAnalysisHop;
    c.window           = pulp::signal::WindowFunction::Type::hann;
    c.num_channels     = std::max(1, num_channels);
    c.sample_rate      = static_cast<float>(sample_rate_);
    c.capture_waveform = true;
    c.waveform_length  = 1024;
    c.max_frames_per_poll = kAnalyzerMaxFramesPerPoll;
    bridge_.configure(c);
}

void Spectr::release() {
    mask_processor_ = {};
    processor_prepared_ = false;
    bridge_.reset();
}

int Spectr::latency_samples() const {
    // Each Release build has one measured, fixed-latency WOLA geometry.
    // Return the prepared engine's exact value when available and the same
    // deterministic contract before prepare so adapters can query early.
    return processor_prepared_ ? mask_processor_.latency_samples()
                               : kSpectralLatency;
}

void Spectr::set_layout(Layout L) {
    layout_ = L;
    publish_processing_state_();
}

void Spectr::process(
    pulp::audio::BufferView<float>& output,
    const pulp::audio::BufferView<const float>& input,
    pulp::midi::MidiBuffer& /*midi_in*/,
    pulp::midi::MidiBuffer& /*midi_out*/,
    const pulp::format::ProcessContext& ctx)
{
    // Sync the two continuously automatable audio controls each block.
    const float mix        = state().get_value(kMix) / 100.0f;
    const float out_trim_db= state().get_value(kOutputTrim);
    const float target_output_gain = std::pow(10.0f, out_trim_db * 0.05f);

    // An explicit reset or unexpected seek is a hard DSP-history boundary.
    // Preserve the continuously hot WOLA/dry-delay history across an ordinary
    // host cycle wrap so looping does not emit a fresh startup gap.
#if defined(SPECTR_NATIVE_N1_SDK_COMPAT)
    // The installed 0.803.0 Forge SDK predates should_reset_stream_history().
    // For this standalone-only N1 scaffold, honor explicit resets and avoid
    // treating ordinary loop wraps as cold starts. The shipping format graph
    // continues to compile against the newer, precise helper below.
    const bool should_reset_stream_history = ctx.reset_requested;
#else
    const bool should_reset_stream_history = ctx.should_reset_stream_history();
#endif
    if (should_reset_stream_history) {
        if (processor_prepared_)
            mask_processor_.reset();
        output_gain_.set_immediate(target_output_gain);
    }

    if (processor_prepared_
        && output.num_channels() == static_cast<std::size_t>(channels_)
        && input.num_channels() == static_cast<std::size_t>(channels_)
        && output.num_samples() == input.num_samples()) {
        for (std::size_t channel = 0; channel < output.num_channels(); ++channel) {
            input_channels_[channel] = input.channel(channel).data();
            output_channels_[channel] = output.channel(channel).data();
        }
        mask_processor_.set_mix(std::clamp(mix, 0.0f, 1.0f));
        const bool processed = mask_processor_.process(
            input_channels_.data(), output_channels_.data(),
            static_cast<int>(output.num_samples()));

        // The shared processor already mixed latency-aligned dry and wet.
        // Output trim remains a product-level post gain.
        if (target_output_gain != output_gain_.target())
            output_gain_.set_target(target_output_gain);
        if (!processed) {
            output_gain_.skip(static_cast<int>(output.num_samples()));
            for (std::size_t ch = 0; ch < output.num_channels(); ++ch) {
                auto dst = output.channel(ch);
                std::fill(dst.begin(), dst.end(), 0.0f);
            }
        } else {
            // Advance the gain once per frame, then apply that same value to
            // every channel so stereo/multichannel relationships stay exact.
            for (std::size_t sample = 0; sample < output.num_samples(); ++sample) {
                const float out_gain = output_gain_.next();
                for (std::size_t ch = 0; ch < output.num_channels(); ++ch)
                    output_channels_[ch][sample] *= out_gain;
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

    // Invalid or unprepared audio geometry fails closed.
    for (std::size_t ch = 0; ch < output.num_channels(); ++ch) {
        auto dst = output.channel(ch);
        std::fill(dst.begin(), dst.end(), 0.0f);
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

    // Layout is structured product state, not a dynamic host parameter.
    root.addMember("layout_index",
                   static_cast<int32_t>(layout_to_index(layout_)));

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

std::optional<float> read_band_gain_(const choc::value::ValueView& value) {
    double gain = 0.0;
    if      (value.isFloat64()) gain = value.getFloat64();
    else if (value.isInt64())   gain = static_cast<double>(value.getInt64());
    else if (value.isInt32())   gain = static_cast<double>(value.getInt32());
    else                        return std::nullopt;

    if (!std::isfinite(gain)) return std::nullopt;
    // Older supplemental-state versions allowed a wider gain range and did
    // not bump the schema when the Release-1 product range narrowed. Clamp in
    // double precision before narrowing so those sessions migrate safely and
    // huge-but-finite JSON numbers can never overflow to a float infinity.
    return static_cast<float>(std::clamp(
        gain, static_cast<double>(kBandGainMinDb),
        static_cast<double>(kBandGainMaxDb)));
}

std::optional<int> read_int_(const choc::value::ValueView& value) {
    if (value.isInt32()) return value.getInt32();
    if (value.isInt64()) {
        const auto number = value.getInt64();
        if (number < std::numeric_limits<int>::min()
            || number > std::numeric_limits<int>::max()) return std::nullopt;
        return static_cast<int>(number);
    }
    if (value.isFloat64()) {
        const auto number = value.getFloat64();
        if (!std::isfinite(number)
            || number < static_cast<double>(std::numeric_limits<int>::min())
            || number > static_cast<double>(std::numeric_limits<int>::max()))
            return std::nullopt;
        return static_cast<int>(number);
    }
    return std::nullopt;
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
            const auto gain = read_band_gain_(arr[i]);
            if (!gain) return false;
            staged.field.bands[i].gain_db = *gain;
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
        const auto parsed = read_int_(obj["layout_index"]);
        if (!parsed) return false;
        int idx = *parsed;
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
        publish_processing_state_();
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
    const auto parsed_version = read_int_(root["version"]);
    if (!parsed_version) return false;
    const int version = *parsed_version;
    if (version < 1 || version > kPluginStateVersion) return false;

    // Apply in a staging copy so a malformed payload leaves live state alone.
    BandField new_field;
    Viewport  new_view  = viewport_;
    Layout    new_layout = layout_;

    if (root.hasObjectMember("band_gain") && root["band_gain"].isArray()) {
        auto arr = root["band_gain"];
        const auto n = std::min<std::uint32_t>(arr.size(), kMaxBands);
        for (std::uint32_t i = 0; i < n; ++i) {
            const auto gain = read_band_gain_(arr[i]);
            if (!gain) return false;
            new_field.bands[i].gain_db = *gain;
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
        const auto parsed = read_int_(root["layout_index"]);
        if (!parsed) return false;
        int idx = *parsed;
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
            const auto parsed = read_int_(snaps["active"]);
            if (!parsed) return false;
            const int a = *parsed;
            new_bank.active = (a == 1) ? SnapshotBank::Slot::B : SnapshotBank::Slot::A;
        }
        if (snaps.hasObjectMember("a")
            && !read_snapshot_(snaps["a"], new_bank.a)) return false;
        if (snaps.hasObjectMember("b")
            && !read_snapshot_(snaps["b"], new_bank.b)) return false;
    }

    // M9.5 — user patterns. Apply into a fresh library so the factory
    // presets are present regardless of what the blob carried; import
    // appends the stored user patterns + default_id. Swap-on-success.
    PatternLibrary new_patterns{};
    if (root.hasObjectMember("patterns_json") && root["patterns_json"].isString()) {
        const auto s = root["patterns_json"].getString();
        new_patterns.import_json(std::string_view(s));
    }

    field_ = new_field;
    viewport_  = new_view;
    snapshots_ = new_bank;
    patterns_  = std::move(new_patterns);
    layout_ = new_layout;
    publish_processing_state_();
    return true;
}

} // namespace spectr
