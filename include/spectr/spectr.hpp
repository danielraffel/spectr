#pragma once

// Spectr — zoomable frequency-slicer audio effect.
//
// See README.md for a product summary and planning/ for the full design
// package. Milestone 1 (Foundation) layered the project; real DSP arrives in
// Milestone 2 (DSP truth spike). State registration (#625 gated) is
// Milestone 4.

#include <pulp/format/processor.hpp>
// The SDK compiles a scripted editor's scripts and verifies its document on a
// background worker when a host instantiates the plug-in, if the plug-in says
// what they are (Processor::editor_prewarm). Older SDKs have no hook.
#if __has_include(<pulp/format/editor_prewarm.hpp>)
#define SPECTR_HAS_EDITOR_PREWARM 1
#else
#define SPECTR_HAS_EDITOR_PREWARM 0
#endif
#include <pulp/format/background_task_lane.hpp>
#include <pulp/signal/spectral_band_mask.hpp>
#include <pulp/signal/spectral_mask_processor.hpp>
#include <pulp/signal/smoothed_value.hpp>
#include "spectr/level_controls.hpp"
#include "spectr/auto_gain_material.hpp"
#include <pulp/runtime/triple_buffer.hpp>
#include <pulp/view/ab_compare.hpp>
#include <pulp/view/visualization_bridge.hpp>
#include <array>

// The build defines this from CMake's PROJECT_VERSION. A translation unit
// compiled without it would report a version no bundle or installer carries.
#if !defined(SPECTR_PRODUCT_VERSION)
#error "SPECTR_PRODUCT_VERSION must come from Spectr's CMake PROJECT_VERSION"
#endif
#include <atomic>
#include <bitset>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <limits>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <type_traits>
#include <vector>

#if defined(SPECTR_NATIVE_EDITOR)
#include <filesystem>
#include <pulp/view/command_registry.hpp>
#include <pulp/view/editor_bridge.hpp>
#include <pulp/view/frame_clock.hpp>
#include <pulp/view/scripted_ui.hpp>
#include "spectr/editor_bridge.hpp"
#endif

#include "spectr/band_state.hpp"
#include "spectr/gpu_audio_status.hpp"
#include "spectr/edit_modes.hpp"
#include "spectr/editor_authority.hpp"
#include "spectr/param_surface.hpp"
#include "spectr/render_mode.hpp"
#include "spectr/pattern.hpp"
#include "spectr/snapshot.hpp"
#include "spectr/viewport.hpp"
#include "spectr/editor_resize.hpp"
#include "spectr/freeze_source.hpp"
#include "spectr/freeze_length.hpp"
#include "spectr/macro_field.hpp"
#include "spectr/modulation.hpp"

#ifndef SPECTR_EDITOR_BACKGROUND_RGB
// Defined by CMake from the materialized document's `:root { --bg }`; this
// fallback only serves a translation unit built outside Spectr's targets.
#define SPECTR_EDITOR_BACKGROUND_RGB 0x05070A
#endif
#ifndef SPECTR_FFT_SIZE
#define SPECTR_FFT_SIZE 8192
#endif
#ifndef SPECTR_ANALYSIS_HOP
#define SPECTR_ANALYSIS_HOP 2048
#endif

namespace spectr {

namespace detail {
/// Parameter-sync tasks handed to a worker and not yet applied, summed over
/// every Spectr in the process. See `spectr_param_sync_backlog_v1()`.
extern std::atomic<std::uint64_t> g_param_sync_backlog;

/// Test seams for the parameter-sync worker. Each costs one relaxed load in
/// a shipping run.
///
/// `g_param_sync_test_stall_ms`: the worker sleeps this long before it
/// applies a task (the in-process twin of SPECTR_TEST_PARAM_SYNC_STALL_MS),
/// so a test can starve the worker an offline block waits on.
extern std::atomic<int> g_param_sync_test_stall_ms;
/// `g_param_sync_spawned_hook`: called on the audio thread right after a
/// parameter-sync task is handed to the worker, so a test can let the worker
/// run to completion at exactly that point -- the worst interleaving the
/// scheduler could produce -- and prove the outcome does not depend on it.
/// Null in every shipping run.
extern std::atomic<void (*)()> g_param_sync_spawned_hook;
} // namespace detail

/// The most one host block flagged offline waits for Spectr's own workers
/// (parameter sync, then mask design) before it renders with whatever is
/// adopted. A design takes milliseconds, so a paced-equivalent bounce never
/// gets near it; the budget exists for a starved worker, and for a host whose
/// offline flag outlived its bounce, where every realtime block would
/// otherwise wait. Once spent, the block renders and Spectr logs it once.
inline constexpr auto kOfflineBlockWaitBudget = std::chrono::milliseconds(250);

struct ProcessingStateSnapshot {
    BandField field{};
    Viewport viewport{};
    Layout layout = Layout::Bands32;
    SnapshotBank snapshots{};
};

/// The Preset destination's neighbourhood: the band gains of the presets
/// around the current one, in the preset menu's order (factory, then user),
/// as the editor resolves them at the current band count. Index
/// kPresetModulationSteps is the current preset itself (unused: the centre is
/// the field as it stands, edits included); `below` / `above` neighbours
/// exist. POD so it rides the audio modulation publication.
struct PresetModulationNeighbours {
    std::array<std::array<float, kMaxBands>, kPresetNeighbourCount> gains{};
    int below = 0;
    int above = 0;
    bool valid = false;
};

struct AudioModulationState {
    ModulationSettings settings{};
    SnapshotBank snapshots{};
    /// Whether a morph also moves the viewport. Published alongside the bank
    /// because the audio thread derives the mask's frequency window from the
    /// morph parameter, so it has to know the same answer the editor does.
    bool morph_applies_viewport = true;
    /// Whether the field currently in force was DERIVED from the snapshot
    /// bank by a morph — `morph_derived_`, the control worker's own answer.
    ///
    /// The audio thread re-derives the morph per block, but the control worker
    /// applies it only when the morph parameter CHANGES. Without this flag the
    /// two disagree from the moment both slots are populated: the audio thread
    /// rebuilds `host_field` from A and B on every block, at a default morph
    /// of 0.0, so the authored field is replaced by snapshot A wholesale and
    /// every later band edit is inert. Measured end to end — a band muted
    /// after both captures came back at snapshot A's -6 dB, linear gain 0.50,
    /// and was heard.
    bool morph_derived = false;
    /// Bands written explicitly since the morph derived the field, as a bit
    /// per canonical slot — `morph_overrides_`, published for the same reason
    /// the bank is: the audio thread re-derives the morph per block and must
    /// reach the same precedence the control worker already applies, namely
    /// that "an automated band value overrides the morph that derived it".
    ///
    /// Without it the two disagree, and the audio thread wins: it rebuilds
    /// `host_field` from the two snapshots on EVERY block and hands that to
    /// both the DSP and the editor's modulation frame, so a band the user
    /// muted after capturing A and B is silently un-muted and heard. Measured
    /// at every morph value, not just at the dominance flip.
    std::uint64_t morph_overrides = 0;
    /// Macro membership, one bit per canonical slot per macro.
    ///
    /// Carried here for the same reason `morph_overrides` is: the audio owner
    /// composes the macro overlay itself (the VALUES are host lanes it reads
    /// per block through the cursor), so it needs the membership the control
    /// thread holds. Raw bits rather than `std::bitset` to keep the
    /// publication trivially copyable and its size explicit.
    ///
    /// Values are deliberately NOT carried: they are ordinary automatable
    /// parameters, and reading them from the cursor is what makes a macro
    /// sample-accurate within a block instead of one publication behind.
    std::array<std::uint64_t, kMacroCount> macro_members{};
    /// The Preset destination's neighbours (set_preset_modulation).
    PresetModulationNeighbours preset{};
};
static_assert(std::is_trivially_copyable_v<AudioModulationState>,
              "audio modulation publication must remain allocation-free POD");

/// The band field the audio owner is actually rendering this block, after the
/// internal LFOs have been applied.
///
/// This exists so the editor can DRAW what the user hears. The modulated field
/// is computed on the audio thread and consumed by the mask processor; without
/// a publication the modulator is audible but invisible, and a control an LFO
/// is sweeping never moves. `sequence` advances once per processed block so a
/// UI tick can tell a fresh frame from a repeat; `active` is false whenever no
/// modulator is running, which is the editor's cue to fall back to canonical
/// state rather than freeze on the last modulated frame.
///
/// Strictly a display value. It never re-enters canonical state or a host
/// parameter lane — see `apply_internal_modulation`.
struct ModulatedFieldSnapshot {
    BandField     field{};
    std::uint64_t sequence = 0;
    bool          active   = false;

    // ── Display-time reconstruction inputs ──────────────────────────────
    //
    // `field` alone is a zero-order hold: the audio owner samples the LFO once
    // per processed block and the editor consumes it once per display frame.
    // Those two clocks are unrelated, so a 60 Hz consumer reading a ~190 Hz
    // producer advances three, four or five producer steps per painted frame
    // and the animation moves unevenly at every waveform -- the jitter is in
    // the resampling, not in the oscillator.
    //
    // Publishing the LFO's INPUTS as well lets the editor evaluate the same
    // pure `lfo_value` / `apply_internal_modulation` at its own frame time, so
    // the painted value is a continuous function of display time. The DSP is
    // not duplicated: the editor calls the identical functions the audio owner
    // does. `field` remains the audio owner's own last sample.
    BandField          pre_field{};   ///< post-morph, pre-LFO input field
    ModulationSettings settings{};
    /// The bank the audio owner composed against. Carried rather than read
    /// from `Spectr::snapshots()` on the consumer side: a capture lands in the
    /// control-thread bank immediately but only reaches the audio owner one
    /// publication later, and for that tick the two would disagree. Carrying
    /// it makes "the drawn field equals the audible one" exact instead of
    /// almost-always.
    SnapshotBank       snapshots{};
    float              host_morph = 0.0f;
    /// The viewport the audio owner modulated from (the user's window, after
    /// any morph derivation) and the audible one it rendered. Equal when no
    /// viewport destination is routed. Viewport modulation is audible only --
    /// the editor keeps drawing the user's window -- so these are the one
    /// place the rendered window can be read back (tests, probes).
    Viewport           base_viewport{};
    Viewport           viewport{};
    double             phase = 0.0;   ///< LFO 1 phase at `published_ns`
    double             phase_2 = 0.0; ///< LFO 2 phase at `published_ns`
    double             phase_per_second = 0.0;
    double             phase_2_per_second = 0.0;
    /// Shape crossfades at `published_ns`; the consumer advances them by the
    /// same elapsed time as the phases.
    LfoShapeFade       shape_fade{};
    LfoShapeFade       shape_2_fade{};
    /// steady_clock nanoseconds at which `phase`/`phase_2` were sampled. Zero
    /// means the publication carries no usable clock and the consumer must
    /// fall back to `field` rather than extrapolate from an unknown origin.
    std::int64_t       published_ns = 0;
};
static_assert(std::is_trivially_copyable_v<ModulatedFieldSnapshot>,
              "modulated field publication must remain allocation-free POD");

/// Declare that this build's format gives the user no way to resize the
/// editor, so the editor must draw its own resize grip (see
/// `create_native_editor_`).
///
/// Opt-IN, default false, and asserted by exactly one format entry point:
/// `au_v2_entry.cpp`. AU v2 is the only format Spectr ships where the host
/// offers no resize affordance at all — `AUCocoaUIView` passes a size
/// plugin-ward once at creation and never again, and Logic's plugin window
/// exposes no grow area (no `AXGrowArea`, and it refuses `AXSize`). A
/// plugin-drawn corner grip is the mechanism there, gated on the wrapper type,
/// which is also the pattern JUCE recommends for exactly this asymmetry.
///
/// Every other surface already has a working affordance and must NOT get a
/// second one competing with it:
///   * VST3    — `IPlugView::checkSizeConstraint` / `onSize` (verified in REAPER)
///   * CLAP    — `gui_adjust_size` / `gui_set_size` (verified in REAPER)
///   * AU v3   — host-initiated resize through the plug-in window border
///   * Standalone — macOS owns the bottom-right corner of a resizable NSWindow
///     and consumes press and click there before the content view is asked
///     (measured: grip present, painted, correctly placed, and receiving
///     nothing at all while the window resized underneath it)
///
/// Declared by the entry point rather than sniffed at runtime.
/// `pulp::format::detect_host_type()` cannot answer this: it reports
/// `HostType::Standalone` by matching "pulp" in the process name, and this
/// product's standalone is "Spectr Native Preview". Pulp exposes no
/// wrapper-type enum to a `Processor`, so the linked entry point is the only
/// place that knows the answer.
void set_editor_owns_resize_grip(bool value);
bool editor_owns_resize_grip();

/// Whether this process is Spectr's standalone app rather than a plug-in host.
///
/// Decides the plain-key shortcut policy. A DAW owns its plain keys -- Logic's
/// Musical Typing plays notes on A S D F G H J K L and W E T Y U O P -- so in
/// a plug-in the editor's single-letter shortcuts are off unless the user
/// turns on "Keyboard shortcuts in DAW"; the standalone owns its window and
/// keeps them. Default false (hosted), asserted true by the two standalone
/// entry points only, for the same reason as the resize grip above: the
/// linked entry point is the one place that knows the wrapper.
void set_editor_is_standalone(bool value);
bool editor_is_standalone();

/// Make the plug-in host view take a click that lands while its window is not
/// key, so the first press after the user has worked elsewhere in the DAW acts
/// on the control under it instead of only focusing the editor window. A
/// stopgap for a Pulp SDK that lacks the override; returns how many host view
/// classes gained it, 0 when the SDK already has its own. macOS only (0
/// elsewhere); idempotent.
int install_host_view_first_mouse();

/// The editor's own background, 0xRRGGBB: the materialized document's
/// `:root { --bg }`, read from the document at configure time
/// (SPECTR_EDITOR_BACKGROUND_RGB). A plug-in host shows nothing but this colour
/// until the document has mounted, so the editor opens looking like Spectr
/// rather than like the SDK default.
inline constexpr std::uint32_t kEditorBackgroundRgb = SPECTR_EDITOR_BACKGROUND_RGB;

inline constexpr int kSpectralFftSize = SPECTR_FFT_SIZE;
inline constexpr int kSpectralAnalysisHop = SPECTR_ANALYSIS_HOP;
// SpectralFrameEngine reads through a fixed causal cursor of one complete FFT
// frame plus one analysis hop, keeping latency invariant to host block
// partitioning.
inline constexpr int kSpectralLatency =
    kSpectralFftSize + kSpectralAnalysisHop;
// VisualizationBridge publishes at most 4097 bins. Derive analyzer geometry
// from the product profile, but cap Maximum's 16384 processing FFT at 8192 so
// its upper spectrum is never silently truncated.
inline constexpr int kAnalyzerFftSize =
    kSpectralFftSize > 8192 ? 8192 : kSpectralFftSize;
inline constexpr int kAnalyzerAnalysisHopUncapped =
    (kSpectralAnalysisHop * kAnalyzerFftSize + kSpectralFftSize - 1)
        / kSpectralFftSize;
inline constexpr int kAnalyzerAnalysisHop =
    kAnalyzerAnalysisHopUncapped < 1 ? 1
    : (kAnalyzerAnalysisHopUncapped > kAnalyzerFftSize / 2
        ? kAnalyzerFftSize / 2 : kAnalyzerAnalysisHopUncapped);
// VisualizationBridge's capture capacity: four analyzer windows. The editor
// polls with the latest_window backlog policy, so one poll consumes whatever
// is waiting however slowly the UI ticks; audio is dropped only when a single
// gap between polls outlasts this buffer.
inline constexpr int kAnalyzerCaptureFrames = 4 * kAnalyzerFftSize;

/// The analyzer bridge configuration Spectr runs with. The editor is a display,
/// so the bridge uses `latest_window`: every poll analyzes the whole backlog
/// and the published spectrum trails the newest audio by under one hop, rather
/// than a poll analyzing a bounded slice and a slow UI tick leaving the rest
/// queued until the capture buffer overflows and blanks the spectrum.
[[nodiscard]] pulp::view::VisualizationConfig
analyzer_config(double sample_rate, int num_channels) noexcept;

static_assert(kSpectralFftSize >= pulp::signal::kSpectralFrameEngineMinimumFftSize
              && kSpectralFftSize
                     <= pulp::signal::kSpectralFrameEngineMaximumFftSize
              && (kSpectralFftSize & (kSpectralFftSize - 1)) == 0
              && kSpectralAnalysisHop > 0
              && kSpectralAnalysisHop <= kSpectralFftSize / 2,
              "Spectr build selected unsupported Pulp spectral geometry");
static_assert(kAnalyzerFftSize / 2 + 1
                  <= pulp::view::SpectrumData::kMaxBins,
              "Spectr analyzer geometry exceeds Pulp spectrum capacity");

enum ParamIDs : pulp::state::ParamID {
    kMix          = 1,
    kOutputTrim   = 2,   ///< dB, [-24, +24]
};

inline pulp::format::PluginDescriptor make_descriptor() {
    return {
#if defined(SPECTR_DEV_IDENTITY)
        .name         = SPECTR_DEV_PLUGIN_NAME,
#elif defined(SPECTR_NATIVE_PREVIEW_IDENTITY)
        .name         = "Spectr Native Preview",
#else
        .name         = "Spectr",
#endif
        .manufacturer = "Pulp",
        // The CLAP adapter uses this as the plugin ID, which is the identity a
        // host persists in session state. It must differ from production or an
        // installed preview collides with it: a session saved against one can
        // resolve to the other. REAPER hides this by keying its cache on
        // filename, so the collision is invisible until a host keys by ID.
#if defined(SPECTR_DEV_IDENTITY)
        .bundle_id    = SPECTR_DEV_BUNDLE_ID,
#elif defined(SPECTR_NATIVE_PREVIEW_IDENTITY)
        .bundle_id    = "com.pulp.spectr.native-preview",
#else
        .bundle_id    = "com.pulp.spectr",
#endif
        .version      = SPECTR_PRODUCT_VERSION,
        .category     = pulp::format::PluginCategory::Effect,
    };
}

/// Top-level Spectr plugin. Owns the product state and Pulp's reusable
/// streaming spectral-mask processor.
class Spectr : public pulp::format::Processor
#if defined(SPECTR_NATIVE_EDITOR)
             , private pulp::view::CommandHandler
#endif
{
public:
    // UI/control thread only. Never stops processing or changes engine selection.
    [[nodiscard]] GpuAudioStatus gpu_audio_status() const;
    // Whether the renderer built most recently accepted the freeze source.
    // False means Freeze cannot reach the audio in the current mode.
    [[nodiscard]] bool freeze_source_wired() const noexcept {
        return freeze_source_wired_.load(std::memory_order_acquire);
    }
#if defined(SPECTR_SHARED_PRODUCT_ACCEPTANCE)
    // Stopped/control lane only; never race prepare/release/mode replacement.
    // Quantum output selections before product mix/trim, not GPU completions.
    struct SharedProductSnapshot {
        bool shared_renderer = false;
        unsigned state = 0;
        std::uint64_t epoch = 0, gpu_selected = 0, cpu_selected = 0,
                      cancelled = 0, lost_records = 0;
    };
    SharedProductSnapshot shared_product_snapshot() const noexcept;
    std::uint64_t shared_product_trace_run_id_stopped() const noexcept;
    // Destructive diagnostic seal: caller has stopped processing. Stops the
    // control publisher and renderer worker, checks release, then snapshots.
    // No further process() until ordinary release()/prepare().
    bool finalize_shared_product_snapshot(SharedProductSnapshot& out) noexcept;
    bool set_shared_product_force_cpu(bool force) noexcept;
#endif
    Spectr();
    ~Spectr() override;

    pulp::format::PluginDescriptor descriptor() const override;
    void define_parameters(pulp::state::StateStore& store) override;
    void prepare(const pulp::format::PrepareContext& ctx) override;
    void release() override;
    int latency_samples() const override;
    pulp::format::ViewSize view_size() const override;

    // ── Render mode ────────────────────────────────────────────────────
    //
    // Which realisation of the drawn magnitude is live. A genuine trade, not
    // a quality setting: linear phase cuts far deeper, zero latency costs a
    // fraction of the delay and puts no smear before a transient. See
    // spectr/render_mode.hpp for the names and the new-instance default.

    /// The mode this instance is currently rendering through.
    [[nodiscard]] MaskRenderMode render_mode() const noexcept { return render_mode_; }

    /// The rate the instance was last prepared at, for deriving the millisecond
    /// figures the UI shows. 48 kHz before the first prepare, which is what an
    /// editor opened ahead of audio should display rather than zero.
    [[nodiscard]] double sample_rate() const noexcept { return sample_rate_; }

    /// Delay this mode costs at the live sample rate, in samples and in
    /// milliseconds. Derived from the renderer's own contract so no
    /// user-facing figure is ever a number somebody typed.
    ///
    /// This is the figure the host is told for that mode (latency_samples()
    /// reports the same number once prepared): in a build with the shared GPU
    /// renderer, Mixing adds that renderer's fixed lead to the linear-phase
    /// latency, whether a block is delivered by the GPU or by its CPU
    /// fallback -- both keep the same alignment.
    [[nodiscard]] int render_mode_latency_samples(MaskRenderMode mode) const noexcept;
    [[nodiscard]] int render_mode_latency_samples(MaskRenderMode mode, bool gpu) const noexcept;

    /// GPU processing for Mixing. Saved with the session, not a host
    /// parameter: switching it rebuilds the renderer and moves the latency the
    /// host is told, exactly like a Tracking/Mixing switch, so it must not be
    /// automatable. Off by default: GPU output is the CPU linear-phase output
    /// (no sonic difference) at a higher latency. Tracking is always CPU.
    /// Control thread only; same failure contract as set_render_mode.
    bool set_gpu_processing(bool enabled);
    [[nodiscard]] bool gpu_processing() const noexcept { return gpu_processing_; }
    /// Whether this build can render Mixing on the GPU at all.
    [[nodiscard]] static constexpr bool gpu_processing_available() noexcept {
#if defined(SPECTR_EXPERIMENTAL_SHARED_RENDERER)
        return true;
#else
        return false;
#endif
    }
    [[nodiscard]] double render_mode_latency_ms(MaskRenderMode mode) const noexcept {
        return sample_rate_ > 0.0
            ? 1000.0 * static_cast<double>(render_mode_latency_samples(mode)) / sample_rate_
            : 0.0;
    }

    /// Switch modes on a live instance.
    ///
    /// Control thread only — it builds a whole new renderer, which allocates.
    /// The running renderer keeps rendering until the replacement is prepared,
    /// so a failed switch is a no-op rather than a gap: on failure the previous
    /// mode is still live and this returns false. On success the host is told
    /// its delay compensation is stale via `flag_latency_changed()`.
    bool set_render_mode(MaskRenderMode mode);

    /// True when the last restore could not build the renderer the project
    /// asked for, so the instance is still running the mode it had. A project
    /// that merely NAMES an unknown mode is rejected outright rather than
    /// substituted (see `deserialize_plugin_state`); this covers the narrower
    /// case where a known mode failed to prepare. It is how a caller finds out
    /// instead of inferring it from audio that does not sound as it was
    /// saved.
    [[nodiscard]] bool render_mode_unknown_on_load() const noexcept {
        return render_mode_unknown_on_load_;
    }

    void process(
        pulp::audio::BufferView<float>& output,
        const pulp::audio::BufferView<const float>& input,
        pulp::midi::MidiBuffer& midi_in,
        pulp::midi::MidiBuffer& midi_out,
        const pulp::format::ProcessContext& ctx) override;

    /// Host offline-render intent from a caller that cannot put it on
    /// `ProcessContext` (every shipping adapter now does). Any thread.
    /// Either this or `ProcessContext::is_offline()` makes a block offline.
    /// `prepare()` clears it: the flag describes one render session, and a
    /// host that never writes it back must not leave every later realtime
    /// block waiting on the workers.
    void set_host_offline_render(bool offline) noexcept {
        host_offline_render_.store(offline, std::memory_order_relaxed);
    }
    [[nodiscard]] bool host_offline_render() const noexcept {
        return host_offline_render_.load(std::memory_order_relaxed);
    }
    /// Offline blocks whose wait for the workers ran out of
    /// `kOfflineBlockWaitBudget` and rendered anyway. Any thread.
    [[nodiscard]] std::uint64_t offline_wait_budget_exhausted_count() const noexcept {
        return offline_wait_budget_exhausted_.load(std::memory_order_relaxed);
    }
    /// Parameter-sync publishes dropped because a newer request -- the audio
    /// path's own mask for that block -- had already been made. Any thread.
    [[nodiscard]] std::uint64_t param_sync_superseded_count() const noexcept {
        return param_sync_superseded_.load(std::memory_order_relaxed);
    }

    // ── Supplemental plugin state (pulp#625 / PR#628 hooks) ─────────────
    //
    // Under V2 handoff §5.4, Spectr's richer state (canonical band field,
    // viewport bounds, analyzer/edit mode) rides through the host adapters
    // as an opaque versioned JSON payload alongside StateStore's flat
    // parameter blob. See planning/Spectr-V2-Pulp-Handoff.md §5.4.
    std::vector<uint8_t> serialize_plugin_state() const override;
    bool deserialize_plugin_state(std::span<const uint8_t> bytes) override;

    /// Supplemental-state schema version. Bump when the JSON shape changes
    /// in a non-backward-compatible way; deserialize rejects unknown
    /// versions.
    ///
    /// v2 (M8) extends v1 with an optional `snapshots` object holding the
    /// A/B snapshot bank. Absent `snapshots` is legal — reading a v1 blob
    /// is always a reset-to-default for the bank.
    ///
    /// v4 adds `render_mode`, and is the one bump whose POINT is the rejection
    /// it enables. A v4 writer always emits the field, so a v4 blob without it
    /// is corruption and is refused; a blob at v3 or below cannot have been
    /// written by a build that had more than one mode, so its silence means
    /// linear phase and can never mean anything else. Those two rules together
    /// are what make the mode a project was authored in knowable forever
    /// without inferring it from a default that may since have changed.
    ///
    /// The cost, accepted deliberately: an older Spectr refuses a v4 blob
    /// outright. That is correct — it cannot realise a mode it does not have,
    /// and loading the sound in the wrong mode with the wrong delay
    /// compensation, silently, is worse than a clear error.
    static constexpr int kPluginStateVersion = 4;

    // ── Editor view ────────────────────────────────────────────────────
    std::unique_ptr<pulp::view::View> create_view() override;
#if SPECTR_HAS_EDITOR_PREWARM && defined(SPECTR_NATIVE_EDITOR)
    /// The materialized editor's runtime, design and help scripts and its
    /// captured document, byte-identical to what an open evaluates.
    EditorPrewarm editor_prewarm() const override;
#endif
#if !defined(PULP_FORMAT_HAS_EDITOR_BACKGROUND)
#error "Spectr requires a Pulp SDK with Processor::editor_background()"
#endif
    /// Every frame a host paints before the document mounts, and the backing
    /// layer behind them, is this colour.
    std::optional<std::uint32_t> editor_background() const override {
        return kEditorBackgroundRgb;
    }
    void on_view_opened(pulp::view::View& view) override;
    void on_view_resized(pulp::view::View& view, uint32_t w, uint32_t h) override;
    void on_view_closed(pulp::view::View& view) override;
#if defined(SPECTR_NATIVE_EDITOR)
    pulp::view::ScriptedUiSession* active_scripted_ui() override {
        return native_scripted_ui_.get();
    }
    const pulp::view::ScriptedUiSession* active_scripted_ui() const override {
        return native_scripted_ui_.get();
    }
    EditorRevision native_editor_revision() const noexcept {
        return editor_authority_.revision();
    }
#endif
    // ── Accessors — primarily for tests and the UI layer ───────────────

    const BandField&  field()     const noexcept { return field_; }
    BandField&        field()           noexcept { return field_; }
    /// Coherent copy for runtime readers. The reference accessors above are
    /// retained for construction-time setup and legacy tests; code that can
    /// overlap host automation must use this snapshot.
    ProcessingStateSnapshot processing_state_snapshot() const noexcept;
    void replace_field(const BandField& field) noexcept;
    bool replace_processing_state(const BandField& field,
                                  const Viewport& viewport,
                                  Layout layout) noexcept;
    void publish_field() noexcept;
    /// Publish one of the four editor mode controls to the host parameter
    /// surface. This is the editor-to-host lane only; host-to-editor mode
    /// observation remains owned by spectr#37.
    [[nodiscard]] bool set_editor_mode_param(pulp::state::ParamID id,
                                             float value) noexcept;
    const Viewport&   viewport()  const noexcept { return viewport_; }
    Viewport&         viewport()        noexcept { return viewport_; }
    Layout            layout()    const noexcept { return layout_; }
    EditorAuthority& editor_authority() noexcept { return editor_authority_; }
    const EditorAuthority& editor_authority() const noexcept { return editor_authority_; }

    void set_layout(Layout L);

    /// Analyze how many of the current layout's authored bands own at least
    /// one distinct bin at the compiled FFT geometry and current sample rate.
    /// Control-thread only; unavailable before prepare, and `out_resolution`
    /// is unchanged on failure.
    [[nodiscard]] bool spectral_resolution(
        pulp::signal::SpectralBandResolution& out_resolution) const noexcept;

    // ── Snapshot A/B + morph (Milestone 8) ──────────────────────────────
    //
    // Spectr tracks two kinds of A/B state:
    //
    //   - Flat StateStore params (Mix and Output): handled by
    //     pulp::view::ABCompare over the StateStore. Access via
    //     ab_compare().
    //   - Band-field + viewport + layout: held in snapshots_ below, with
    //     editor-local per-band morph via morph_fields(). Serialized in the
    //     plugin state blob so it survives session reload without exposing a
    //     misleading host-automation parameter.
    //
    // UI drives both in lockstep for the full A/B experience.

    const SnapshotBank& snapshots() const noexcept { return snapshots_; }
    SnapshotBank&       snapshots()       noexcept { return snapshots_; }

    /// Copy the current field + viewport + layout into the named slot.
    /// Marks the slot populated.
    void capture_snapshot(SnapshotBank::Slot slot) noexcept;

    /// Empty the named slot, so morph and recall stop seeing it.
    ///
    /// Until this existed a filled slot could only be OVERWRITTEN, never
    /// emptied -- which is also why RESET ALL could not do what its name
    /// says: it cleared the editor's mirror of the bank while the processor
    /// kept both slots, and the next full projection lit them again.
    void clear_snapshot(SnapshotBank::Slot slot) noexcept;

    /// Write the morph of A and B at t into `field_`, and — when
    /// `morph_applies_viewport()` is set — the log-space morph of their
    /// viewports into `viewport_`. If either slot is unpopulated, falls back
    /// to the populated side (or leaves field_ alone if neither slot has been
    /// captured). Never touches `layout_`: band count is discrete and the
    /// selectable counts do not share a band grid, so there is nothing to
    /// interpolate. See snapshot.hpp for the full capture-vs-apply rule.
    void apply_morph_to_live(float t) noexcept;

    /// Whether a morph moves the viewport as well as the bands.
    ///
    /// This is a PLAYBACK switch, deliberately not a capture switch. A
    /// snapshot always records the viewport it was taken under, so turning
    /// this on later just works, and turning it off never destroys anything.
    /// Turning it off stops morph writing the viewport and leaves the user on
    /// whatever window they are looking at.
    ///
    /// Defaults to enabled. Persisted in the supplemental plugin-state blob,
    /// not exposed as a host parameter: it selects a behaviour rather than
    /// carrying a value a host should be automating.
    [[nodiscard]] bool morph_applies_viewport() const noexcept;
    void set_morph_applies_viewport(bool enabled) noexcept;

    /// "Keyboard shortcuts in DAW": whether the editor's plain-key shortcuts
    /// (S L B F G, A / 6, M, T) are live inside a plug-in host. Off by
    /// default so the host's own keys reach it; ignored by the standalone,
    /// where they are always live. Persisted in the supplemental plugin-state
    /// blob like morph_applies_viewport.
    [[nodiscard]] bool keyboard_shortcuts_in_daw() const noexcept;
    void set_keyboard_shortcuts_in_daw(bool enabled) noexcept;

    /// "Show tooltips" (Settings > FEEDBACK): whether hovering a header
    /// control shows its tooltip. On by default. Saved with the session, like
    /// Keyboard shortcuts in DAW, so each project keeps its own choice.
    [[nodiscard]] bool show_tooltips() const noexcept;
    void set_show_tooltips(bool enabled) noexcept;

    /// "Ask before overriding modulation" (Settings > MODULATION, and the
    /// header context menus): whether operating a control an LFO drives asks
    /// first. On by default. Saved with the session like Show tooltips.
    [[nodiscard]] bool ask_before_override() const noexcept;
    void set_ask_before_override(bool enabled) noexcept;

    /// The editor's Range: the plot's vertical scale and the reach of a
    /// full-height edit, in dB (3, 6, 12 or 24; level_controls.hpp). Editor
    /// state persisted in the supplemental blob, never a host parameter, and
    /// never part of what the plug-in sounds like.
    [[nodiscard]] int editor_range_db() const noexcept;
    /// Refuses (false) anything but one of kEditorRangeChoicesDb.
    bool set_editor_range_db(int range_db) noexcept;

    /// Auto Gain compensation the audio owner is applying now, in dB. Any
    /// thread; a reading, not a control.
    [[nodiscard]] float auto_gain_applied_db() const noexcept {
        return auto_gain_applied_db_.load(std::memory_order_relaxed);
    }

    /// Which Auto Gain computation AUTO runs (auto_gain_material.hpp). v2 is
    /// the product; v1 stays selectable here, and only here, so the corpus
    /// sweep and the tests can measure the two side by side. Not saved, not
    /// a parameter. Takes effect at the next block.
    void set_auto_gain_model(AutoGainModel model) noexcept {
        auto_gain_model_.store(static_cast<int>(model), std::memory_order_relaxed);
        auto_gain_legacy_v1_.store(0, std::memory_order_relaxed);
    }
    /// True while a session saved with AUTO on before v2 still runs v1 (until
    /// AUTO is switched off and on again).
    [[nodiscard]] bool auto_gain_legacy_v1() const noexcept {
        return auto_gain_legacy_v1_.load(std::memory_order_relaxed) != 0;
    }
    [[nodiscard]] AutoGainModel auto_gain_model() const noexcept {
        return static_cast<AutoGainModel>(auto_gain_model_.load(std::memory_order_relaxed));
    }
    /// v2's material estimator. Audio-thread state: read it only where
    /// process() cannot be running (tests, between renders).
    [[nodiscard]] const AutoGainMaterial& auto_gain_material() const noexcept {
        return auto_gain_material_;
    }

    /// Freeze's musical Length: how much of the incoming sound the next
    /// freeze takes in (freeze_length.hpp). Host parameter 4 (Freeze Length)
    /// picks one of the header's common lengths or "Custom", the custom
    /// length below, which is persisted in the supplemental plugin-state
    /// blob. The length in force, from either. Any thread.
    [[nodiscard]] FreezeLength freeze_length() const noexcept;
    /// The Freeze Length parameter as a preset index; kLengthPresetCustom
    /// selects freeze_custom_length().
    [[nodiscard]] int freeze_length_preset() const noexcept;
    [[nodiscard]] FreezeLength freeze_custom_length() const noexcept {
        return unpack_length(freeze_custom_length_.load(std::memory_order_relaxed));
    }
    /// Store a custom length. Refuses (false) an invalid one. Any thread.
    bool set_freeze_custom_length(FreezeLength length) noexcept;
    /// The editor's commit of a length: a common one selects its preset, any
    /// other becomes the custom length and selects "Custom"; the parameter
    /// moves inside one host gesture, like set_freeze_from_editor. UI
    /// thread. False for an invalid length or before the store exists.
    bool set_freeze_length_from_editor(FreezeLength length) noexcept;

    /// The host transport the audio thread last saw (120 BPM 4/4 until it
    /// has seen one, and wherever the host gives none). Any thread.
    [[nodiscard]] double transport_tempo_bpm() const noexcept {
        return transport_tempo_bpm_.load(std::memory_order_relaxed);
    }
    [[nodiscard]] int transport_time_sig_numerator() const noexcept {
        return transport_time_sig_numerator_.load(std::memory_order_relaxed);
    }
    [[nodiscard]] int transport_time_sig_denominator() const noexcept {
        return transport_time_sig_denominator_.load(std::memory_order_relaxed);
    }
    /// The Length in seconds at that transport.
    [[nodiscard]] double freeze_length_seconds() const noexcept;
    /// The longest loop this instance holds at its sample rate and channel
    /// count (FreezeSource::loop_cap_seconds).
    [[nodiscard]] double freeze_loop_cap_seconds() const noexcept;

    /// Tests and diagnostics: hold exactly `seconds` instead of the Length
    /// converted at the transport tempo; negative turns it off. Not
    /// persisted, not reachable from the editor or a host.
    void set_freeze_seconds_override(double seconds) noexcept {
        freeze_seconds_override_.store(seconds, std::memory_order_relaxed);
    }

    /// The editor's write of Freeze (the LIVE / FROZEN toggle, its keys):
    /// the parameter set inside one complete host gesture, begin -> value ->
    /// end, so a host in Touch / Latch / Write records the press. UI thread.
    /// Returns false before the parameter store exists.
    bool set_freeze_from_editor(bool frozen) noexcept;

    /// The freeze source. Audio-thread state: read it only where process()
    /// cannot be running (tests, offline renders).
    [[nodiscard]] const FreezeSource& freeze_source() const noexcept {
        return freeze_source_;
    }

    /// Accessor for the StateStore-level ABCompare. Lazily constructed
    /// the first time it's requested (after define_parameters has wired
    /// the store). Returns nullptr if the store isn't available yet.
    pulp::view::ABCompare* ab_compare() noexcept;

    // ── Host parameter surface sync (spectr#34) ──────────────────────────
    //
    // The StateStore owns the host-visible parameter values; field_/
    // viewport_/layout_ remain the DSP-facing canonical state. Two
    // control-thread directions keep them consistent:
    //
    //   apply_surface_params() — params → canonical state. Diffs the store
    //   against the applied-value cache; band gains/mutes, viewport, and
    //   band count apply directly, the morph parameter (when apply_morph)
    //   re-derives the field from the snapshot bank, and mode params advance
    //   the editor projection without republishing the DSP mask. Runs on
    //   the sync worker (spawned from process() on drift) and synchronously
    //   from prepare().
    //
    //   sync_params_from_field() — canonical state → params. Pushes only
    //   slots that changed since the last sync (delta against synced_*), so
    //   a paint gesture writes the swept bands and nothing else. UI edits
    //   thereby become host-visible value changes the adapter can record.
    //
    // Both directions are one-way per call site, so there is no echo loop:
    // the drain never pushes, and the push path marks the applied cache so
    // the next process()-side sweep sees no drift.
    bool apply_surface_params(bool apply_morph) noexcept;

    [[nodiscard]] EditorRevision host_automation_revision() const noexcept {
        return host_automation_revision_.load(std::memory_order_acquire);
    }
    [[nodiscard]] float editor_mode_param(
        pulp::state::ParamID id) const noexcept;
    [[nodiscard]] ModulationSettings modulation_settings() const noexcept;
    bool set_modulation_target_mask(std::uint8_t mask) noexcept;

    /// The slots @p macro drives. Out-of-range reads as empty.
    [[nodiscard]] MacroMembership<kMaxBands> macro_members(
        std::size_t macro) const noexcept;
    /// Replace one macro's membership. Returns false for an out-of-range
    /// index. Bits at or above the visible count are KEPT, not dropped: the
    /// layout is a projection, so narrowing it must not destroy a membership
    /// widening it would restore.
    bool set_macro_members(std::size_t macro,
                           const MacroMembership<kMaxBands>& members) noexcept;
    /// Membership plus the live parameter values, as one bank for
    /// `apply_macro_offsets`. Control-thread reader (the editor projection and
    /// `make_mask_layout_`); the audio thread assembles its own from the
    /// publication and its cursor so it never reads the store off-block.
    [[nodiscard]] BandMacroBank macro_bank() const noexcept;
    /// Write one macro's VALUE, the way a drag should.
    ///
    /// This is the whole point of the feature's gesture story. A group drag
    /// used to commit one band-gain write per selected band, each with its
    /// own host gesture bracket — so Logic's Learn latched onto whichever
    /// `Band NN Gain` happened to be written first, and the user ended up
    /// automating one arbitrary band instead of the group. Driving the macro
    /// instead means ONE parameter and ONE bracket, which is the only shape
    /// a host modulator can usefully grab.
    ///
    /// Inside an open gesture epoch (`begin_param_gesture_epoch`) the bracket
    /// opens once and closes when the epoch ends, so a drag of any length is
    /// still one bracket. Outside one, each call is its own complete
    /// bracket, which is what a discrete command (a menu item, a typed
    /// value) needs.
    ///
    /// Returns false for an out-of-range index or before the store exists.
    bool set_macro_value(std::size_t macro, float value_db) noexcept;
    void sync_params_from_field(bool emit_gestures = true) noexcept;

    /// Paint-drag gesture epochs (EditorAuthority drives these from
    /// begin/end/cancel_band_edit on the UI thread). While an epoch is open,
    /// the first sync push of each parameter also opens a host gesture, and
    /// ending the epoch closes them all — one begin/end bracket per touched
    /// parameter per drag, at gesture rate, instead of per-event brackets.
    void begin_param_gesture_epoch() noexcept;
    void end_param_gesture_epoch() noexcept;

    // ── Editor edits of plain host parameters ──────────────────────────────
    //
    // The controls whose parameter IS their whole state -- Mix, Output trim,
    // and every internal-LFO lane (on/off, shape, rate, depth, target, for
    // both LFOs) -- are edited by the editor through these, never through a
    // bare value write. A host recording in Touch, Latch or Write keys on the
    // edit gesture (begin, value, end); a bare write moves the parameter and
    // the DSP but leaves such a host nothing to record.
    //
    // A press that is one complete act (a toggle, a shape, a target, a step
    // of the keyboard) calls only `edit_param_from_editor`, which emits a
    // complete bracket. A drag opens the bracket on press with
    // `begin_editor_param_gesture`, writes through `edit_param_from_editor`
    // inside it, and closes it on release -- one bracket per drag. UI thread.
    //
    // Other parameters have their own editor routes (bands, morph and
    // viewport through the field publication; modes through mode_set; Freeze
    // through freeze_set; macros through macro_set), each already gestured,
    // so these refuse them rather than offer a second, cache-bypassing path.

    /// True when @p id is edited through the three calls below.
    [[nodiscard]] static bool is_editor_plain_param(
        pulp::state::ParamID id) noexcept;
    /// Write @p value. Inside an open drag gesture for @p id this is the
    /// value alone; otherwise it is a complete begin/value/end bracket.
    /// Returns false for a parameter outside the set above or before the
    /// store exists.
    bool edit_param_from_editor(pulp::state::ParamID id, float value) noexcept;
    /// Open a drag gesture on @p id. A second begin on an open id is a no-op.
    bool begin_editor_param_gesture(pulp::state::ParamID id) noexcept;
    /// Close the drag gesture on @p id. Closing one that is not open is a
    /// no-op, so a release the editor reports twice cannot unbalance a host.
    bool end_editor_param_gesture(pulp::state::ParamID id) noexcept;
    /// Close every editor drag gesture still open (the editor went away
    /// mid-drag).
    void end_editor_param_gestures() noexcept;

    // ── Pattern library ────────────────────────────────────────────────
    //
    // Each Spectr owns a PatternLibrary pre-populated with the factory
    // presets. User patterns come and go through the editor bridge's
    // load_pattern / (future) save_current paths. Persistence for
    // user patterns is a M9 follow-up — the library lives in memory
    // only for now; serialize_plugin_state doesn't yet pack it into
    // the supplemental blob.
    PatternLibrary&       patterns()       noexcept { return patterns_; }
    const PatternLibrary& patterns() const noexcept { return patterns_; }

    // ── Analyzer bridge — UI-thread read path ───────────────────────────
    //
    // Spectr publishes STFT + meter + waveform snapshots from the audio
    // thread through VisualizationBridge's TripleBuffers. UI/tests read
    // via these accessors; the reads are lock-free and always see the
    // latest complete frame.
    pulp::view::VisualizationBridge& bridge() noexcept { return bridge_; }
    const pulp::view::SpectrumData& read_spectrum() { return bridge_.read_spectrum(); }
    /// Analyze everything captured since the last call. One poll suffices:
    /// the bridge runs the latest_window backlog policy (see analyzer_config).
    /// UI thread only, like every other bridge read.
    void drain_analyzer() { (void)bridge_.poll(); }
    const pulp::view::WaveformData& read_waveform() { return bridge_.read_waveform(); }
    const pulp::signal::MultiChannelMeterData& read_meter() { return bridge_.read_meter(); }

    /// One UI-thread reading of the level Spectr handed the host.
    ///
    /// `bridge_.process()` is fed the buffer AFTER the output trim has been
    /// applied, in both the shared-processor and the direct render paths, so
    /// this is the level downstream sees and not an internal one. Spectr is
    /// float end to end and clips nothing itself: `over` means the signal
    /// leaving the plugin reached or passed full scale, which is the exact
    /// condition under which anything fixed-point downstream distorts. That
    /// is why the editor labels it OVER and not CLIP.
    ///
    /// Lock-free; reads the latest complete meter frame. Hold and reset
    /// policy deliberately live in the UI, which knows how long a person
    /// needs to see a number.
    struct OutputLevelReading {
        /// Highest sample peak across channels, dBFS. -inf for digital silence.
        float peak_db = -std::numeric_limits<float>::infinity();
        /// Any channel reached full scale in the latest frame.
        bool  over = false;
        /// The Output trim in force, dB, so the editor's control and its
        /// meter cannot disagree about which gain produced the reading.
        float trim_db = 0.0f;
        /// Intensity (param 5000), percent, as the store holds it.
        float intensity_percent = kIntensityDefaultPercent;
        /// Auto Gain (param 5001) switch, and the compensation the audio
        /// owner is applying right now (dB; 0 when off and settled).
        bool  auto_gain = false;
        float auto_gain_db = 0.0f;
        /// Mix (param 1), percent, for the editor's MIX knob.
        float mix_percent = 100.0f;
    };
    OutputLevelReading read_output_level();

    /// Latest post-LFO band field from the audio owner, for drawing only.
    /// Lock-free; always a complete frame. `active == false` means no
    /// modulator is running and the editor should draw canonical state.
    /// Whether the audio owner is asking for a freeze right now, Freeze
    /// target included, and whether an LFO's Freeze target drives it.
    [[nodiscard]] bool freeze_effective() const noexcept {
        return freeze_effective_.load(std::memory_order_relaxed);
    }
    [[nodiscard]] bool freeze_gate_driven() const noexcept {
        return freeze_gate_driven_.load(std::memory_order_relaxed);
    }
    /// The LENGTH-list index the next freeze takes under the Length target,
    /// or -1 when no LFO drives Length.
    [[nodiscard]] int freeze_modulated_length_index() const noexcept {
        return freeze_modulated_length_index_.load(std::memory_order_relaxed);
    }
    /// The LENGTH-list index the playing freeze took at its engage while the
    /// Length target drove it, or -1.
    [[nodiscard]] int freeze_engaged_length_index() const noexcept {
        return freeze_engaged_length_index_.load(std::memory_order_relaxed);
    }
    /// The LENGTH index the closed LENGTH dropdown shows while the Length
    /// target drives it -- the length the current freeze took while frozen,
    /// else the length the next engage would take -- or -1 when no LFO
    /// drives Length.
    [[nodiscard]] int freeze_shown_length_index() const noexcept;
    /// Freeze "Hold for Length" (kParamFreezeHoldForLength).
    [[nodiscard]] bool freeze_hold_for_length() const noexcept;
    /// Hold for Length latches since prepare (each one a fresh freeze held
    /// for its own Length), and the Length in seconds the last one took
    /// (diagnostics and tests).
    [[nodiscard]] std::uint32_t freeze_hold_latch_count() const noexcept {
        return freeze_hold_latch_count_.load(std::memory_order_relaxed);
    }
    [[nodiscard]] double freeze_hold_latched_seconds() const noexcept {
        return freeze_hold_latched_seconds_.load(std::memory_order_relaxed);
    }
    /// The band count the Bands destination plays, or 0 when no LFO drives it.
    [[nodiscard]] int modulated_band_count_shown() const noexcept {
        return audio_bands_shown_.load(std::memory_order_relaxed);
    }
    /// Whether an LFO drives the Preset destination, and the whole step from
    /// the current preset it is nearest (0 = the current preset).
    [[nodiscard]] bool preset_modulation_driven() const noexcept {
        return audio_preset_driven_.load(std::memory_order_relaxed);
    }
    [[nodiscard]] int preset_modulation_step_shown() const noexcept {
        return audio_preset_step_.load(std::memory_order_relaxed);
    }
    /// The Preset destination's neighbourhood, as the editor resolved it:
    /// the current preset's id, the names and band gains of the presets
    /// around it in menu order (index kPresetModulationSteps is the current
    /// one), and how many exist each way. Persisted in the plugin state, so
    /// the target keeps working when a session reopens.
    bool set_preset_modulation(std::string centre_id,
                               const std::array<std::string, kPresetNeighbourCount>& names,
                               const PresetModulationNeighbours& neighbours);
    /// The preset name @p step from the current one, or "" when unknown.
    [[nodiscard]] std::string preset_modulation_name(int step) const;
    [[nodiscard]] std::string preset_modulation_centre_id() const;
    const ModulatedFieldSnapshot& read_modulated_field() {
        return modulated_field_publication_.read();
    }

private:
    double sample_rate_ = 48000.0;
    int    max_block_   = 512;
    int    channels_    = 1;

    static constexpr std::size_t kMaximumChannels = 64;

    BandField                              field_{};
    Viewport                               viewport_{};
    Layout                                 layout_ = Layout::Bands32;
    // The mask renderer behind the mode. Owned through the seam rather than
    // held concretely, so which realisation is live is a value this class
    // stores instead of a type it is compiled against.
#if defined(SPECTR_SHARED_PRODUCT_ACCEPTANCE)
    bool shared_product_force_cpu_ = false;
#endif
    // Only pointer publication/removal and the public observer take this lock.
    // Build/join/destruction happen outside it; process() never acquires it.
    // When nested, processing_state_mutex_ precedes this observation mutex.
    // Nothing has refused the source until a renderer is built.
    std::atomic<bool> freeze_source_wired_{true};
    mutable std::mutex renderer_observation_mutex_;
    std::unique_ptr<MaskRenderer>          renderer_{};
    // The mode `renderer_` was built for. Authoritative for what this instance
    // sounds like and what latency it reports; written by the control thread
    // only, read by process() to notice a pending rebuild.
    MaskRenderMode                         render_mode_ = kDefaultRenderMode;
    bool                                   gpu_processing_ = false;
    // Set when a restore could not build the renderer the project asked for.
    // The instance keeps the mode it has and says so rather than pretending
    // the project opened cleanly.
    bool                                   render_mode_unknown_on_load_ = false;
    // What process() actually renders through. Separate from `renderer_`
    // because a mode switch replaces the object underneath a possibly-running
    // audio thread, and the audio thread must never chase a freed pointer.
    std::atomic<MaskRenderer*>             active_renderer_{nullptr};
    // Even outside process(), odd inside it. A control thread that sees this
    // change, or sees it even, knows the audio thread is not holding a
    // renderer pointer it read earlier. Cheaper than a lock and never blocks
    // the audio thread, which is the point.
    std::atomic<std::uint64_t>             render_epoch_{0};
    // Renderers replaced by a mode switch, held until the audio thread has
    // demonstrably let go of them. Drained on the control thread; never freed
    // from process().
    std::vector<std::unique_ptr<MaskRenderer>> retired_renderers_{};

    // The last layout the audio thread staged into the renderer, and whether
    // it holds one. Owned by process() alone -- never read or written by any
    // other thread -- so comparing against it needs no synchronisation.
    //
    // It exists to stop the audio thread re-staging a mask that is already
    // live. Re-staging one is not free: it spawns a redesign whose result is
    // swapped in with a crossfade at whatever block the worker happens to
    // finish on, and crossfading an impulse response with an identical copy
    // of itself is not an exact identity in floating point. That made two
    // renders of the same input differ by about one ULP at a position that
    // moved with thread scheduling -- measured before this gate, and zero
    // after it.
    pulp::signal::SpectralBandLayout       last_staged_layout_{};
    bool                                   last_staged_layout_valid_ = false;

    // The last layout handed to the renderer from a CONTROL thread, guarded by
    // processing_state_mutex_ like every other control-side field here.
    //
    // The parameter-sync worker republishes on every drift it observes, and
    // most of those carry a mask the renderer is already realising. Each one
    // still queues a redesign and a crossfade, landing at whichever block that
    // worker finishes on -- which is why two identical offline renders
    // differed. Skipping a republication that changes nothing is what makes an
    // offline bounce reproducible.
    pulp::signal::SpectralBandLayout       last_published_layout_{};
    bool                                   last_published_layout_valid_ = false;

    // The design grid the live renderer samples the drawn magnitude on, held
    // as a value under processing_state_mutex_. The resolution disclosure
    // reads this rather than dereferencing renderer_, which a mode switch can
    // replace from a different control thread than the editor reads on.
    int                                    active_design_grid_ = kSpectralFftSize;

    /// Build and fully prepare a renderer for `mode` against the current
    /// geometry, including its initial layout and mix. Returns null when the
    /// mode cannot be prepared; the caller keeps whatever was already live.
    std::unique_ptr<MaskRenderer> build_renderer_(MaskRenderMode mode, bool gpu);
    bool switch_renderer_(MaskRenderMode mode, bool gpu);
    /// Free retired renderers the audio thread can no longer reach. Control
    /// thread only.
    void drain_retired_renderers_() noexcept;
    /// The geometry any renderer for this instance is prepared against.
    MaskRendererConfig renderer_config_() const noexcept;
    /// Just the geometry latency depends on -- no mix, and therefore no
    /// parameter store. An editor may ask what a mode costs before the store
    /// is wired, and answering that question has nothing to do with the mix.
    [[nodiscard]] MaskRendererConfig latency_geometry_() const noexcept {
        MaskRendererConfig config;
        config.design_grid_size = kSpectralFftSize;
        config.analysis_hop     = kSpectralAnalysisHop;
        config.sample_rate      = sample_rate_;
        return config;
    }
    pulp::signal::SmoothedValue<float>     output_gain_{1.0f};
    // ── Level controls (level_controls.hpp) ──────────────────────────────
    // Audio thread only, except the reference (built in prepare) and the
    // published reading.
    AutoGainReference                      auto_gain_reference_{};
    // Linear Auto Gain multiplier, ramped over kAutoGainRampSeconds. Exactly
    // 1.0f once Auto Gain is off and settled, so the multiply is an identity.
    pulp::signal::SmoothedValue<float>     auto_gain_{1.0f};
    float                                  auto_gain_target_db_ = 0.0f;
    bool                                   auto_gain_primed_ = false;
    // Slewed Intensity factor (0..1) and whether it has adopted its first
    // value; plus the cursor baselines, like audio_mix_percent_.
    float                                  audio_intensity_ = 1.0f;
    /// The Output LFO destination's dB at the end of the last slice: the
    /// start of the next slice's ramp (modulation.hpp, level destinations).
    float                                  audio_output_mod_db_ = 0.0f;
    bool                                   audio_output_mod_primed_ = false;
    bool                                   audio_intensity_primed_ = false;
    float                                  audio_intensity_percent_ = kIntensityDefaultPercent;
    float                                  audio_auto_gain_param_ =
        kAutoGainDefaultForNewInstances ? 1.0f : 0.0f;
    std::atomic<float>                     auto_gain_applied_db_{0.0f};
    // Editor Range, dB. Guarded by processing_state_mutex_.
    int                                    editor_range_db_ = kEditorRangeDefaultDb;
    bool                                   processor_prepared_ = false;
    // Owned here, not by a renderer, so a Latency switch hands the running
    // hold to the new realisation instead of dropping it. Prepared with the
    // processor; its members belong to the audio thread afterwards.
    FreezeSource                           freeze_source_{};
    // Auto Gain v2: the material estimator and the wet-source stage the
    // renderers call -- it runs freeze_source_, then feeds the estimator what
    // the mask is about to shape. Audio-thread state after prepare().
    AutoGainMaterial                       auto_gain_material_{};
    AutoGainWetTap                         auto_gain_tap_{};
    std::atomic<int>                       auto_gain_model_{
        static_cast<int>(kAutoGainShippingModel)};
    // 0: no; 1: a pre-v2 session runs v1 until AUTO is toggled; 2: the same,
    // just loaded (the audio thread forgets AUTO's previous state).
    std::atomic<int>                       auto_gain_legacy_v1_{0};
    // AUTO as the audio thread last saw it: -1 not yet, 0 off, 1 on.
    int                                    auto_gain_seen_ = -1;
    // The last slice's Auto Gain inputs, to tell a shape edit (retarget now)
    // from material movement (slew-limited, on the estimator's frame grid).
    pulp::signal::SpectralBandLayout       auto_gain_last_shape_{};
    float                                  auto_gain_last_mix_ = -1.0f;
    bool                                   auto_gain_last_enabled_ = false;
    int                                    auto_gain_last_model_ = 0;
    const MaskRenderer*                    auto_gain_last_renderer_ = nullptr;
    bool                                   auto_gain_last_valid_ = false;
    std::atomic<std::uint32_t> freeze_custom_length_{pack_length(kDefaultFreezeLength)};
    std::atomic<double> transport_tempo_bpm_{kFallbackTempoBpm};
    std::atomic<int> transport_time_sig_numerator_{4};
    std::atomic<int> transport_time_sig_denominator_{4};
    std::atomic<double> freeze_seconds_override_{-1.0};
    // Audio -> worker: build bigger loop rings off the audio thread, and free
    // the ones the source let go of (FreezeSource LOOP MEMORY). Declared
    // after freeze_source_ so it is joined before the source is destroyed.
    struct FreezeStorageTask { double seconds = 0.0; };
    pulp::format::BackgroundTaskLane<FreezeStorageTask, 8> freeze_storage_lane_;
    bool freeze_storage_collect_sent_ = false;   // audio thread
    static void freeze_storage_trampoline_(void* ctx, const FreezeStorageTask& task) noexcept;
    void start_freeze_storage_lane_();
    /// The seconds the next freeze takes in at a transport (or the override).
    /// The next freeze's hold length with the Length target applied: the
    /// LENGTH-list index the LFOs reach at this moment. Audio thread.
    [[nodiscard]] double modulated_freeze_seconds_(double tempo_bpm, int numerator,
                                                   int denominator) noexcept;
    [[nodiscard]] double freeze_hold_seconds_at_(double tempo_bpm, int numerator,
                                                 int denominator) const noexcept;
    /// The Length a freeze takes with the LFOs at @p phases (LFO 1, LFO 2):
    /// seconds, its LENGTH-list index in @p index (-1 when no LFO drives
    /// Length), and the longest length the routes reach in @p reach_seconds.
    [[nodiscard]] double freeze_length_at_phases_(double tempo_bpm, int numerator,
                                                  int denominator, const double phases[2],
                                                  int* index,
                                                  double* reach_seconds) const noexcept;
    void preroll_surviving_hold_();
    std::array<const float*, kMaximumChannels> input_channels_{};
    std::array<float*, kMaximumChannels>       output_channels_{};

    pulp::view::VisualizationBridge       bridge_{};
    SnapshotBank                          snapshots_{};
    PatternLibrary                        patterns_{};
    std::unique_ptr<pulp::view::ABCompare> ab_{};
    EditorAuthority                       editor_authority_;

    // ── spectr#34 parameter-surface sync state ───────────────────────────
    // Non-owning store handle so const paths (serialize) can read param
    // values and control paths can push without going through the const
    // state() accessor. Set in define_parameters.
    pulp::state::StateStore* param_store_ = nullptr;
    // Per-slot mirror of the store value at the last reconciliation, read on
    // the audio thread by the process() drift sweep and written by both sync
    // directions. Slot layout: 0..63 gains, 64..127 mutes, 128 morph,
    // 129 viewport center, 130 viewport width, 131 band count, then motion,
    // analyzer, edit, and visualization at 132..135, then internal LFO
    // enabled/shape/rate/depth/target at 136..140, LFO 2
    // enabled/shape/rate/depth at 141..144, Macro 1..4 at 145..148,
    // Freeze at 149 and Freeze Length at 150.
    static constexpr std::size_t kSurfaceCacheSlots = detail::kSurfaceSlots;
    static_assert(kSurfaceCacheSlots == detail::kSurfaceSlots);
    std::array<std::atomic<float>, kSurfaceCacheSlots> applied_param_cache_{};
    // The audio thread's OWN record of the surface values it last pushed into
    // the mask processor, plus the scratch it samples the store into each
    // block. `applied_param_cache_` above is the SYNC WORKER's record: gating
    // the audio path on it makes what the plugin sounds like depend on that
    // worker having been scheduled. These two are touched only from the audio
    // thread, so a store write is observed on the block that follows it no
    // matter what any other thread is doing.
    std::array<float, kSurfaceCacheSlots> audio_applied_surface_{};
    std::array<float, kSurfaceCacheSlots> audio_surface_scratch_{};
    bool audio_applied_surface_valid_ = false;
    // Audio-owner baseline used when an adapter supplies an event queue after
    // already committing its end-of-block values to StateStore. ParamCursor
    // must begin from the values audible at the end of the previous block,
    // otherwise the pre-event slice would incorrectly jump to the final value.
    float audio_mix_percent_ = 100.0f;
    float audio_output_trim_db_ = 0.0f;
    // Settings/snapshots cross from serialized control writers to the audio
    // owner through a latest-value publication. Derived LFO values never flow
    // back into either canonical state or host parameter lanes.
    pulp::runtime::TripleBuffer<AudioModulationState>
        audio_modulation_publication_{};
    double audio_modulation_phase_ = 0.0;
    double audio_modulation_phase_2_ = 0.0;
    // Shape crossfades, advanced with the phases. Unprimed until the first
    // block so the initial shape is adopted without a fade.
    LfoShapeFade audio_lfo_shape_fade_{};
    LfoShapeFade audio_lfo_2_shape_fade_{};
    bool         audio_lfo_shape_fade_primed_ = false;
    // The legacy LFO command lanes (4003/4013 Depth, 4004 Target) as the
    // audio owner last saw them, with the routing lanes beside them. A
    // command is latched on the slice its lane moves -- unless that slice
    // also moved the LFO's routing lanes, which then win -- and released when
    // the routing lanes next move (which is how the worker's own write of the
    // command lands). Audio-thread only; primed from the published state.
    struct AudioLegacyLanes {
        bool  primed = false;
        float depth[2] = {0.0f, 0.0f};
        int   target = 0;
        std::array<LfoRoutes, 2> routes{};
        bool  depth_command[2] = {false, false};
        bool  target_command[2] = {false, false};
    };
    AudioLegacyLanes audio_legacy_lanes_{};
    // Each LFO's slewed audible level (enabled ? depth : 0); see
    // slew_lfo_level. Adopted without a ramp on the first block, like the
    // shape, so a session that opens with an LFO running starts on it.
    std::array<float, 2> audio_lfo_level_{};
    bool                 audio_lfo_level_primed_ = false;
    // Each route's slewed level (enabled ? amount : 0), per LFO and
    // destination, at the same rate as the LFO level: a destination toggled on
    // or off, or an amount automated, fades its contribution rather than
    // stepping it. Primed with the LFO level above.
    std::array<std::array<float, kModulationTargetCount>, 2> audio_route_level_{};
    // The Freeze target. `freeze_gate_last_` / `freeze_param_last_` are the
    // previous block's gate and Freeze-parameter values; a change of the
    // parameter while a gate drives the freeze (the user's press, or host
    // automation) takes effect until the gate's next transition.
    bool freeze_gate_last_ = false;
    bool freeze_param_last_ = false;
    bool freeze_user_override_ = false;
    bool freeze_user_value_ = false;
    // "Hold for Length" (kParamFreezeHoldForLength): samples of the current
    // latch still to play, the raw LFO gate of the previous block (a latch
    // needs its rising edge), and the effective Length this callback's
    // freeze takes, in seconds -- the length a latch holds for.
    std::int64_t freeze_hold_remaining_ = 0;
    // Display of the modulated LENGTH, BANDS and preset (freeze_display).
    std::atomic<int> freeze_engaged_length_index_{-1};
    bool freeze_engage_last_ = false;
    std::atomic<int> audio_bands_shown_{0};
    // The Bands destination's crossfade through flat: the count playing and
    // how much of the shape is applied (1 = all of it).
    int audio_bands_playing_ = 0;
    float audio_bands_fade_ = 1.0f;
    bool audio_bands_modulated_ = false;
    std::atomic<bool> audio_preset_driven_{false};
    std::atomic<int> audio_preset_step_{0};
    // Guarded by processing_state_mutex_: the Preset destination's
    // neighbourhood, published through AudioModulationState.
    PresetModulationNeighbours preset_neighbours_{};
    std::array<std::string, kPresetNeighbourCount> preset_names_{};
    std::string preset_centre_id_;
    bool freeze_hold_gate_last_ = false;
    double audio_freeze_length_seconds_ = 0.0;
    // The length the playing Hold for Length hold took at its trigger, in
    // seconds (0 between holds): the source's hold length while it plays.
    double freeze_hold_seconds_ = 0.0;
    // The longest Length the Length target can step to now, in seconds (the
    // loop rings are grown for it ahead of the trigger that needs it).
    double audio_freeze_reach_seconds_ = 0.0;
    // ONE LFO ON BOTH (see the Hold for Length latch): how many eighths of a
    // cycle along an LFO driving Freeze and Length the next hold reads its
    // Length, the transport state it last saw, and a request from a state
    // load to start the walk again.
    static constexpr double kFreezeHoldWalkStep = 0.125;
    static constexpr int kFreezeHoldWalkCycle = 8;
    int freeze_hold_walk_ = 0;
    bool freeze_hold_playing_last_ = false;
    std::atomic<bool> freeze_hold_walk_reset_{false};
    std::atomic<std::uint32_t> freeze_hold_latch_count_{0};
    std::atomic<double> freeze_hold_latched_seconds_{0.0};
    // An editor press (button, key, chord) while the Freeze target drives the
    // freeze: the value it asks for, or -1. Taken on the audio thread, where
    // it holds the freeze there until the gate's next change -- even when the
    // parameter already had that value, which is the usual case while a gate
    // is showing the opposite state.
    std::atomic<int> freeze_press_request_{-1};
    // What the audio owner actually asked the freeze source for, and whether
    // an LFO was driving it: the editor's LIVE/FROZEN face shows this.
    std::atomic<bool> freeze_effective_{false};
    std::atomic<bool> freeze_gate_driven_{false};
    // The LENGTH-list index the next freeze takes while the Length target
    // drives it, else -1 (diagnostics and tests).
    std::atomic<int> freeze_modulated_length_index_{-1};
    // Audio owner -> UI publication of the post-LFO band field, so the editor
    // can draw the modulation it is playing. Write-only on the audio thread,
    // read-only through read_modulated_field().
    pulp::runtime::TripleBuffer<ModulatedFieldSnapshot>
        modulated_field_publication_{};
    std::uint64_t modulated_field_sequence_ = 0;
    bool          modulated_field_was_active_ = false;
    // The most recent EditorAuthority revision caused specifically by host
    // parameter adoption. Views use this as a coalescing publication key so
    // automation redraws immediately without echoing every editor-originated
    // paint receipt back through a full hydration.
    std::atomic<EditorRevision> host_automation_revision_{0};
    // The canonical state as last pushed to the parameters — the sync delta
    // base. Morph updates it silently (morph moves the morph parameter only;
    // pushing 64 resulting band lanes per morph move would double-drive the
    // field on automation playback).
    BandField synced_field_{};
    Viewport  synced_viewport_{};
    Layout    synced_layout_ = Layout::Bands32;
    // Serializes every mutation (and the matching mask publish) of field_/
    // viewport_/layout_ across the control threads that now write them:
    // UI (EditorAuthority), the sync worker, and host state restore. The
    // audio thread never takes it — process() reads params and the applied
    // cache only. Param pushes happen OUTSIDE it: set_value fires listeners
    // that may read processor state back, and holding the lock there would
    // self-deadlock. Mutable so const readers (spectral_resolution) can
    // take a coherent snapshot.
    mutable std::mutex processing_state_mutex_;
    // Audio→worker lane: process() spawns on parameter drift, the worker
    // applies params → canonical state and republishes the mask (table
    // compilation is a control-thread operation).
    // `ordinal` is the renderer request ordinal reserved when the audio thread
    // asked for this sync (MaskRenderer::reserve_request_ordinal), so the
    // mask it publishes is ordered against the layouts the audio thread
    // stages by when each was asked for; `renderer` is the renderer it was
    // reserved from.
    struct ParamSyncTask {
        std::uint64_t tag = 0;
        std::uint64_t ordinal = 0;
        const MaskRenderer* renderer = nullptr;
    };
    pulp::format::BackgroundTaskLane<ParamSyncTask, 8> param_sync_lane_;
    // Offline pacing: the last param-sync task the audio thread handed the
    // worker, and the last one the worker finished (Latest coalesces, so a
    // finished task retires every one it superseded).
    std::atomic<std::uint64_t> param_sync_requested_{0};
    std::atomic<std::uint64_t> param_sync_done_{0};
    std::atomic<bool> host_offline_render_{false};
    std::atomic<std::uint64_t> offline_wait_budget_exhausted_{0};
    std::atomic<std::uint64_t> param_sync_superseded_{0};
    std::atomic<bool> offline_wait_budget_logged_{false};
    // Offline blocks only: wait for the worker results a paced host would
    // already have adopted by now, for at most kOfflineBlockWaitBudget.
    // Sleeps; never called on a realtime block.
    void await_offline_work_(MaskRenderer* renderer) noexcept;
    void retire_param_sync_through_(std::uint64_t tag) noexcept;
    void stop_param_sync_lane_() noexcept;
    // Audio thread, lock-free: hand the param-sync worker one task.
    void spawn_param_sync_(MaskRenderer* renderer) noexcept;
    // Offline blocks only: sleep until the param-sync worker is idle or the
    // deadline passes. False when it gave up with work outstanding.
    bool await_param_sync_(std::chrono::steady_clock::time_point deadline) noexcept;
    // Set by the param-sync worker around its apply, so the mask it publishes
    // carries the request ordinal reserved when it was asked for.
    struct SyncPublishOrder {
        std::uint64_t ordinal = 0;
        const MaskRenderer* renderer = nullptr;
    };
    static thread_local SyncPublishOrder t_sync_publish;
    ModulationSettings modulation_{};
    // Guarded by processing_state_mutex_ and published to the audio thread in
    // AudioModulationState, so both sides of a morph agree on what moves.
    bool morph_applies_viewport_ = true;
    // Guarded by processing_state_mutex_. Editor-only: the audio thread
    // never reads it.
    bool keyboard_shortcuts_in_daw_ = false;
    // Guarded by processing_state_mutex_. Editor-only.
    bool show_tooltips_ = true;
    bool ask_before_override_ = true;
    // Which canonical slots each macro drives. Guarded by
    // processing_state_mutex_ and published in AudioModulationState.
    //
    // Editor state, not a parameter: a SET OF SLOTS is not a number a host
    // can automate, and exposing one lane per member would put the 64 echoing
    // lanes back that macros exist to remove. It persists in the supplemental
    // blob instead (`macro_members`).
    //
    // Membership survives a band-count change untouched. The C++ layout
    // change keeps slot identity — slot 7 is slot 7 at every count — so a
    // member that scrolls out of the visible range is merely inert and
    // returns to its macro when the count comes back up.
    std::array<MacroMembership<kMaxBands>, kMacroCount> macro_members_{};
    // Open paint-drag epoch (UI thread only): params already begin-gestured.
    std::vector<pulp::state::ParamID> epoch_gesture_params_{};
    bool param_gesture_epoch_open_ = false;
    // Editor drag gestures open on plain parameters (UI thread only).
    std::vector<pulp::state::ParamID> editor_param_gestures_{};

    // A morph derives non-overridden bands from the snapshot bank while the
    // sparse override mask identifies later band edits whose values live in
    // StateStore. This makes the v3 supplemental blob non-duplicating: it
    // stores only the derivation shape, never a second copy of host params.
    bool morph_derived_ = false;
    std::bitset<kMaxBands> morph_overrides_{};

    /// What one store sweep found, against each of the two records that care.
    struct SurfaceDrift {
        bool worker = false;  ///< differs from the sync worker's record
        bool audio  = false;  ///< differs from what this thread last applied
    };
    /// Sample every surface parameter into `audio_surface_scratch_` and report
    /// which records disagree with it. Audio thread only.
    SurfaceDrift sample_surface_drift_() noexcept;
    /// The modulation settings described by the current host parameter lanes.
    /// `target_mask` is left at the unset sentinel: the caller owns whatever
    /// explicit destination selection should ride along.
    ModulationSettings modulation_from_store_() const noexcept;
    void push_surface_param_(pulp::state::ParamID id, std::size_t slot,
                             float value, bool emit_gesture = true);
    static void param_sync_trampoline_(void* ctx, const ParamSyncTask&) noexcept;

#if defined(SPECTR_NATIVE_EDITOR)
    // Matches Pulp's framework-reserved `PLST` command in pending pulp#7712.
    // Keeping the consumer on the existing registry surface lets the current
    // SDK prove Spectr's handler before the host-side Cmd/Ctrl+, fallback lands.
    static constexpr pulp::view::CommandID kOpenSettingsCommand = 0x504C5354;
    // Spectr owns document history, so these chords must be claimed by the
    // plugin before a DAW interprets Cmd/Ctrl+Z as its own project command.
    static constexpr pulp::view::CommandID kUndoCommand = 0x5350554E; // 'SPUN'
    static constexpr pulp::view::CommandID kRedoCommand = 0x53505244; // 'SPRD'
    std::vector<pulp::view::CommandID> commands() const override;
    bool perform_command(pulp::view::CommandID id) override;

    pulp::view::CommandRegistry native_command_registry_{};
    pulp::view::EditorBridge native_editor_bridge_{};
    bool native_editor_handlers_registered_ = false;
    std::unique_ptr<pulp::view::ScriptedUiSession> native_scripted_ui_{};
    std::int64_t scenario_mod_sequence_ = 1000000;  // menu-scenario modframe steps
    bool scenario_drag_held_ = false;               // menu-scenario drag:...!hold
    pulp::view::Point scenario_held_point_{};
    std::uint16_t scenario_held_mods_ = 0;
    std::filesystem::path native_package_path_{};
    pulp::view::View* native_editor_root_ = nullptr;
    // Latches once the SPECTR_SETTINGS_SCROLL fixture has positioned the
    // settings body, so the capture is not re-scrolled every frame.
    bool settings_fixture_scrolled_ = false;
    bool settings_fixture_dumped_ = false;
    bool settings_fixture_key_sent_ = false;
    bool live_capture_done_ = false;
    // Pointer fixtures. A drag is the only way to reach the states the status
    // overlay is judged in, and a screenshot cannot be taken mid-gesture, so
    // the fixture probes the banner's live text between the press and the
    // release rather than inferring "during" from a picture taken after it.
    bool drag_fixture_done_ = false;
    double drag_fixture_finished_ms_ = -1.0;
    std::size_t status_probe_next_ = 0;
    bool cursor_probe_done_ = false;
    // Stepped-drag fixture. A "fast drag" row is a claim about the samples
    // BETWEEN press and release, so this probe reads processing state after
    // every delivered move rather than only before and after the gesture: a
    // before/after pair cannot tell a drag that tracked the pointer from one
    // that jumped straight to its end point.
    bool gesture_probe_done_ = false;
    // Band-menu scenario runner (SPECTR_MENU_SCENARIO), one step per N ticks.
    std::vector<std::string> menu_scenario_steps_;
    std::string menu_scenario_json_;
    std::size_t menu_scenario_index_ = 0;
    int menu_scenario_tick_ = 0;
    int menu_scenario_delay_ = 20;
    bool menu_scenario_done_ = false;
#if defined(SPECTR_ENABLE_PERF_FIXTURES)
    // Per-frame gesture-perf fixture (SPECTR_GESTURE_PERF). The AppKit drive in
    // the window host synthesises every NSEvent with `modifierFlags:0`, so it
    // can express a band drag but NOT a Command-held marquee — and the marquee
    // is a distinct code path in the editor, not a variant of the drag. This
    // fixture delivers one pointer sample per frame through the same
    // pointer_dispatch verbs a host calls, with the modifiers the gesture
    // actually carries, so a marquee and a drag can be compared sample for
    // sample. It does not reach the window-space -> design-space pointer
    // transform, which is a per-event constant and identical in both arms.
    int gesture_perf_tick_ = -1;
    bool gesture_perf_done_ = false;
    pulp::view::View* gesture_perf_target_ = nullptr;
#endif
    bool resize_fixture_applied_ = false;
    bool resize_request_sent_ = false;
    // Per-tick state trace for externally driven gestures. The AppKit drag
    // fixture runs over hundreds of frames and only its END state is visible
    // in a dump, but "an edge drag never moves the opposite trim" is a claim
    // about every frame in between -- an excursion that returns before the
    // last frame is exactly the defect and is invisible to a before/after
    // pair. Accumulated in memory and rewritten whole each tick so the file is
    // complete even if the process is killed rather than closed.
    std::string native_state_trace_{};
    int native_state_trace_tick_ = 0;
    pulp::view::View* native_resize_grip_ = nullptr;
    // Last host size reported to on_view_resized. Under a pinned viewport the
    // ROOT is constant at the authored box, so root bounds are useless as a
    // resize base — every drag would measure from 1320x860 and the grip could
    // only ever take one step. The host size is the thing that actually moves.
    std::uint32_t native_host_width_ = 0;
    std::uint32_t native_host_height_ = 0;
    // Editor size latched at grip mouse-down. The grip resolves a size from a
    // cumulative delta against the drag start, so the base must be sampled once
    // per drag rather than read live (reading live would compound).
    std::uint32_t native_resize_base_width_ = 0;
    std::uint32_t native_resize_base_height_ = 0;
    // Set when the host refuses a request mid-drag, so one refusal doesn't turn
    // into a rejected transaction per mouse-move for the rest of the gesture.
    bool native_resize_refused_ = false;
    // Last (w, h) handed to publish_native_layout_. Under a pinned viewport
    // every resize publishes the same authored box, so without this the whole
    // materialized restore + re-place pass re-runs per pointer event for a
    // result that cannot differ. 0 means "nothing published yet", which is the
    // state a freshly created editor must be in so the first pass still runs.
    std::uint32_t native_published_width_ = 0;
    std::uint32_t native_published_height_ = 0;
    pulp::view::FrameClock* native_frame_clock_ = nullptr;
    int native_frame_subscription_ = -1;
    float native_analyzer_elapsed_ = 0.0f;
    // Last PUBLISHED output-level reading, so an unchanged one costs no
    // script evaluation. peak is held at the 0.1 dB the editor prints.
    float native_output_level_peak_ = std::numeric_limits<float>::max();
    bool  native_output_level_over_ = false;
    float native_output_level_trim_db_ = std::numeric_limits<float>::max();
    // The level controls ride the same publication (Intensity, Mix, Auto
    // Gain and the gain it applies, held at 0.1 dB).
    float native_output_level_intensity_ = std::numeric_limits<float>::max();
    float native_output_level_mix_ = std::numeric_limits<float>::max();
    int   native_output_level_auto_gain_ = -1;
    float native_output_level_auto_gain_db_ = std::numeric_limits<float>::max();
    std::uint64_t native_analyzer_sequence_ = 0;
    // Last modulated-field sequence projected to the editor, so a UI tick
    // that finds no new audio frame does not re-dispatch the same overlay.
    std::uint64_t native_modulation_sequence_ = 0;
    // Last freeze display sent: bit 0 frozen, 1 driven, 2-3 Freeze LFOs,
    // 4-5 Length LFOs; -1 before the first.
    std::int64_t native_freeze_display_ = -1;
    // Scratch for the display-time LFO reconstruction. A member rather than a
    // local so a BandField is not built on the stack every frame.
    BandField     native_modulation_drawn_{};
    // Phases last painted, and how many consecutive ticks have seen no new
    // publication. Together they recognise the two no-ops: nothing new to
    // draw, and a producer that has gone quiet.
    double        native_modulation_drawn_phase_ = -1.0;
    double        native_modulation_drawn_phase_2_ = -1.0;
    int           native_modulation_stale_ticks_ = 0;
    EditorRevision native_host_automation_revision_ = 0;

    std::unique_ptr<pulp::view::View> create_native_editor_();

    // Hands the materialized document to the constructed session. Pulp
    // evaluates it at once in-process, or from the session's second idle poll
    // inside a host's view-creation call (view-first open).
    void load_native_document_();
    // Runs the post-load scripts once the session has (or has not) mounted
    // the document; from the session's document-loaded callback, or directly
    // when the script could not even be read.
    void finish_native_document_load_(bool session_loaded, const std::string& error,
                                      bool from_session);
    // Destroys a session marked failed from inside its own callback.
    void retire_failed_native_session_();
    bool native_session_failed_ = false;
    bool native_document_load_reported_ = false;
    void publish_native_layout_(std::uint32_t w, std::uint32_t h);
    void open_native_editor_(pulp::view::View& view);
    void close_native_editor_();
    bool tick_native_analyzer_(float dt);
    /// Draw what the modulators are playing: reconstruct the post-LFO field at
    /// this frame's time from the audio owner's published inputs and hand it
    /// to the editor. Display only -- it never re-enters canonical state.
    void publish_modulation_frame_();
    /// Tell the editor when the LFOs drive Freeze or Length, and the
    /// LIVE/FROZEN state the audio owner is playing. Sent on change only.
    void publish_freeze_display_();
    // Fixture-only. Writes the laid-out tree plus its depth sidecar under
    // SPECTR_DRAG_DUMP_PREFIX for one named stage of a gesture, so "during"
    // and "after" are two artifacts rather than one interpretation.
    void dump_fixture_stage_(const std::string& stage);
    // Pump the scripted runtime so a React state change queued by the event
    // just delivered has actually landed before the next stage is written.
    void settle_native_runtime_(int frames);
    static double fixture_now_ms_();
#endif

    [[nodiscard]] pulp::signal::SpectralBandLayout
        make_mask_layout_() const noexcept;
    /// `macro_bank()` without the lock, for callers that already hold
    /// processing_state_mutex_ (make_mask_layout_ and the publish path).
    [[nodiscard]] BandMacroBank macro_bank_locked_() const noexcept;
    void publish_audio_modulation_state_() noexcept;
    void publish_processing_state_() noexcept;
    void configure_bridge_(int num_channels);
};

inline std::unique_ptr<pulp::format::Processor> create_spectr() {
    return std::make_unique<Spectr>();
}

} // namespace spectr

/// Host-parameter drift the audio thread handed the parameter-sync worker that
/// it has not applied yet, process-wide. The companion of
/// `spectr_mask_design_backlog_v1()`: a harness that renders back to back waits
/// for both to read zero after each block to render as a paced host would.
/// Read-only and lock-free; exported from the AU bundle.
extern "C" std::uint64_t spectr_param_sync_backlog_v1() noexcept;
