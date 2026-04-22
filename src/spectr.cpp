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
}

void Spectr::release() {
    if (engine_) engine_->release();
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
        // For M1 the engines are stubs that just copy input→output.
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
