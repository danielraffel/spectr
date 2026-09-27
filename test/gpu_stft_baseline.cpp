// Offline staged-GPU baseline. This deliberately does not run on an audio callback.
#include <pulp/gpu_audio/gpu_stft.hpp>
#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include <string_view>
#include <vector>

namespace {
bool compare(const std::vector<double>& actual, const std::vector<double>& oracle,
             const std::vector<double>& weights, unsigned edge, double& maximum) {
    if (actual.size() != oracle.size() || actual.size() != weights.size() ||
        edge >= actual.size() || edge >= actual.size() - edge) return false;
    maximum = 0;
    for (unsigned i = 0; i < actual.size(); ++i) {
        // Every output, including startup/tail, must be finite. Numerical
        // reconstruction is compared only where complete overlap exists.
        if (!std::isfinite(actual[i]) || !std::isfinite(oracle[i]) ||
            !std::isfinite(weights[i]) || weights[i] < 0) return false;
        if (i < edge || i >= actual.size() - edge) continue;
        if (weights[i] <= 0) return false;
        const double residual = std::abs(actual[i] - oracle[i]) / weights[i];
        if (!std::isfinite(residual)) return false;
        maximum = std::max(maximum, residual);
    }
    return maximum <= 1e-4;
}

int rejection_controls() {
    std::vector<double> actual(8, 1), oracle(8, 1), weights(8, 1);
    double maximum = 0;
    if (!compare(actual, oracle, weights, 2, maximum)) return 10;
    const double nan = std::numeric_limits<double>::quiet_NaN();
    for (auto* values : {&actual, &oracle, &weights}) {
        for (unsigned index : {0u, 3u, 7u}) {
            (*values)[index] = nan;
            if (compare(actual, oracle, weights, 2, maximum)) return 11;
            (*values)[index] = 1;
        }
    }
    actual[3] = std::numeric_limits<double>::max();
    oracle[3] = -std::numeric_limits<double>::max();
    if (compare(actual, oracle, weights, 2, maximum)) return 12;
    actual[3] = oracle[3] = 1;
    weights[3] = 0;
    if (compare(actual, oracle, weights, 2, maximum)) return 13;
    weights[3] = 1;
    actual[3] = 1.1;
    if (compare(actual, oracle, weights, 2, maximum)) return 14;
    return 0;
}
}

int main(int argc, char** argv) {
    if (argc == 2 && std::string_view(argv[1]) == "--rejection-controls")
        return rejection_controls();
    if (argc != 1) return 64;
    constexpr unsigned fft = 1024, hop = 256, frames = 16;
    constexpr unsigned length = fft + (frames - 1) * hop;
    pulp::gpu_audio::GpuStft stft;
    if (!stft.prepare(fft)) return 1;
    if (!stft.gpu_available()) {
        std::cerr << "GPU unavailable; no GPU baseline measured\n";
        return 77;
    }
    const auto capability = stft.compute()->capabilities();
    if (!capability.adapter_info_authentic || capability.backend != "Metal") {
        std::cerr << "Authentic Metal adapter required\n";
        return 1;
    }
    std::vector<float> input(length), spectrum(2 * fft), frame(fft);
    std::vector<double> actual(length), oracle(length), weights(length);
    unsigned state = 0x12487643u;
    for (auto& sample : input) {
        state = state * 1664525u + 1013904223u;
        sample = static_cast<float>(state >> 8) / 16777216.0f - 0.5f;
    }
    // The independent identity oracle needs no FFT: analysis and synthesis
    // windows multiply the original input by w*w before overlap-add.
    for (unsigned f = 0; f < frames; ++f) {
        const unsigned offset = f * hop;
        if (!stft.analyze(input.data() + offset, spectrum.data()) ||
            !stft.synthesize(spectrum.data(), frame.data())) return 2;
        for (unsigned i = 0; i < fft; ++i) {
            const double w = stft.window()[i];
            if (!std::isfinite(frame[i]) || !std::isfinite(w)) return 3;
            actual[offset + i] += frame[i] * w;
            oracle[offset + i] += input[offset + i] * w * w;
            weights[offset + i] += w * w;
        }
    }
    double max_error = 0;
    if (!compare(actual, oracle, weights, fft, max_error)) return 3;
    // oracle also supplies the prepared identity CPU substitute. This baseline
    // does not schedule callbacks or exercise a deadline-triggered fallback.
    std::cout << "{\"schema\":\"spectr.gpu-stft-baseline.v1\","
              << "\"path\":\"staged_readback\",\"backend\":\"Metal\","
              << "\"adapter_info_authentic\":true,\"fft_size\":" << fft
              << ",\"hop\":" << hop << ",\"frames\":" << frames
              << ",\"max_absolute_error\":" << max_error
              << ",\"all_output_finite\":true,\"comparison_begin_sample\":" << fft
              << ",\"comparison_end_sample_exclusive\":" << length - fft
              << ",\"requested_lead_blocks\":[1,2,4,8],\"lead_tested\":false,"
              << "\"fallback_exercised\":false,\"shared_memory_proven\":false}\n";
    return max_error <= 1e-4 ? 0 : 4;
}
