#include "spectr/spectr.hpp"

namespace spectr {

Spectr::Spectr()  = default;
Spectr::~Spectr() = default;

pulp::format::PluginDescriptor Spectr::descriptor() const {
    return make_descriptor();
}

void Spectr::define_parameters(pulp::state::StateStore& store) {
    // Milestone 4 will expand this once #625 lands. For now we keep the
    // single scaffold parameter so the existing tests stay green.
    store.add_parameter({
        .id    = kMix,
        .name  = "Mix",
        .unit  = "%",
        .range = {0.0f, 100.0f, 100.0f},
    });
}

void Spectr::prepare(const pulp::format::PrepareContext& ctx) {
    sample_rate_ = ctx.sample_rate;
    max_block_   = ctx.max_buffer_size;
    rebuild_engine_();
    configure_bridge_(ctx.output_channels);
}

void Spectr::configure_bridge_(int num_channels) {
    pulp::view::VisualizationConfig c;
    c.fft_size        = 1024;
    c.hop_size        = 256;
    c.window          = pulp::signal::WindowFunction::Type::hann;
    c.num_channels    = std::max(1, num_channels);
    c.sample_rate     = static_cast<float>(sample_rate_);
    c.capture_waveform = true;
    c.waveform_length = 1024;
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
    const float mix = state().get_value(kMix) / 100.0f;

    if (engine_) {
        // Engines render dry→wet through the band mask. We then mix wet
        // against dry so the UI Mix control survives engine changes.
        engine_->process(output, input, field_, viewport_, layout_, response_mode_);

        if (mix < 1.0f) {
            const float dry_gain = 1.0f - mix;
            for (std::size_t ch = 0; ch < output.num_channels(); ++ch) {
                auto dst = output.channel(ch);
                auto src = input.channel(ch);
                for (std::size_t i = 0; i < dst.size(); ++i) {
                    dst[i] = dry_gain * src[i] + mix * dst[i];
                }
            }
        }

        // Analyzer bridge: publish post-engine audio to the UI thread.
        // VisualizationBridge owns its own STFT/meter/waveform state and
        // uses TripleBuffers internally, so this is lock-free.
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

    // Fallback: straight copy + mix.
    for (std::size_t ch = 0; ch < output.num_channels(); ++ch) {
        auto dst = output.channel(ch);
        auto src = input.channel(ch);
        for (std::size_t i = 0; i < dst.size(); ++i) {
            dst[i] = src[i];
        }
    }
}

} // namespace spectr
