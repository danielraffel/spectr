#pragma once

#include <pulp/format/processor.hpp>
#include <cmath>

// Spectr — A Pulp audio effect

namespace spectr {

enum ParamIDs : pulp::state::ParamID {
    kMix = 1,
};

inline pulp::format::PluginDescriptor make_descriptor() {
    return {
        .name = "Spectr",
        .manufacturer = "Pulp",
        .bundle_id = "com.pulp.spectr",
        .version = "1.0.0",
        .category = pulp::format::PluginCategory::Effect,
    };
}

class Spectr : public pulp::format::Processor {
public:
    pulp::format::PluginDescriptor descriptor() const override {
        return make_descriptor();
    }

    void define_parameters(pulp::state::StateStore& store) override {
        store.add_parameter({
            .id = kMix,
            .name = "Mix",
            .unit = "%",
            .range = {0.0f, 100.0f, 100.0f},
        });
    }

    void prepare(const pulp::format::PrepareContext& ctx) override {
        sample_rate_ = ctx.sample_rate;
    }

    void process(
        pulp::audio::BufferView<float>& output,
        const pulp::audio::BufferView<const float>& input,
        pulp::midi::MidiBuffer& /*midi_in*/,
        pulp::midi::MidiBuffer& /*midi_out*/,
        const pulp::format::ProcessContext& /*ctx*/) override
    {
        float mix = state().get_value(kMix) / 100.0f;

        for (std::size_t ch = 0; ch < output.num_channels(); ++ch) {
            for (std::size_t i = 0; i < output.num_samples(); ++i) {
                float dry = input.channel(ch)[i];
                float wet = dry; // TODO: replace with your effect processing
                output.channel(ch)[i] = dry * (1.0f - mix) + wet * mix;
            }
        }
    }

private:
    double sample_rate_ = 48000.0;
};

inline std::unique_ptr<pulp::format::Processor> create_spectr() {
    return std::make_unique<Spectr>();
}

} // namespace spectr
