// Offline staged-GPU baseline. This deliberately does not run on an audio callback.
#include <pulp/gpu_audio/gpu_stft.hpp>
#include <algorithm>
#include <cmath>
#include <iostream>
#include <vector>

int main() {
    constexpr unsigned fft = 1024, hop = 256, frames = 16;
    constexpr unsigned length = fft + (frames - 1) * hop;
    pulp::gpu_audio::GpuStft stft;
    stft.prepare(fft);
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
            actual[offset + i] += frame[i] * w;
            oracle[offset + i] += input[offset + i] * w * w;
            weights[offset + i] += w * w;
        }
    }
    double max_error = 0;
    for (unsigned i = fft; i < length - fft; ++i) {
        if (!std::isfinite(actual[i]) || weights[i] <= 0) return 3;
        max_error = std::max(max_error, std::abs(actual[i] - oracle[i]) / weights[i]);
    }
    // oracle also supplies the prepared identity CPU substitute. This baseline
    // does not schedule callbacks or exercise a deadline-triggered fallback.
    std::cout << "{\"schema\":\"spectr.gpu-stft-baseline.v1\","
              << "\"path\":\"staged_readback\",\"backend\":\"Metal\","
              << "\"adapter_info_authentic\":true,\"fft_size\":" << fft
              << ",\"hop\":" << hop << ",\"frames\":" << frames
              << ",\"max_absolute_error\":" << max_error
              << ",\"requested_lead_blocks\":[1,2,4,8],\"lead_tested\":false,"
              << "\"fallback_exercised\":false,\"shared_memory_proven\":false}\n";
    return max_error <= 1e-4 ? 0 : 4;
}
