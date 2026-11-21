#include <spectr/spectr.hpp>
#include <pulp/format/headless.hpp>
#include <catch2/catch_test_macros.hpp>
#include <choc/text/choc_JSON.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <thread>
#include <vector>

namespace {
constexpr int sample_rate = 48000;
constexpr auto tracking = spectr::MaskRenderMode::zero_latency;
using Host = pulp::format::HeadlessHost;
using Block = std::array<std::array<float,256>,2>;
spectr::Spectr& processor(Host& h) { return *static_cast<spectr::Spectr*>(h.processor()); }

void configure(Host& host, float mix) {
    host.state().set_value(spectr::kMix, mix);
    host.state().set_value(spectr::kOutputTrim, 0.0f);
    host.state().set_value(spectr::kParamAutoGain, 0.0f);
    REQUIRE(processor(host).set_render_mode(tracking));
}

void render(Host& h, const Block& in, Block& out, int count, bool seek = false) {
    const float* inputs[]{in[0].data(), in[1].data()};
    float* outputs[]{out[0].data(), out[1].data()};
    pulp::audio::BufferView<const float> input(inputs,2,count);
    pulp::audio::BufferView<float> output(outputs,2,count);
    pulp::format::ProcessContext ctx;
    ctx.transport_jump = seek;
    ctx.process_mode = pulp::format::ProcessMode::Offline;
    h.process(output,input,ctx);
}

void draw(Host& host, unsigned pattern) {
    auto field = processor(host).field();
    for (std::size_t i=0; i<32; ++i)
        field.bands[i].gain_db = pattern == 2 ? (i % 3 == 0 ? 5.4f : -18.f)
            : (i % 2 == pattern ? 9.6f : -14.4f);
    processor(host).replace_field(field);
}

std::vector<std::uint8_t> legacy_state(const std::vector<std::uint8_t>& bytes) {
    auto root = choc::json::parse(std::string(bytes.begin(),bytes.end()));
    auto stripped = choc::value::createObject("SpectrPluginState");
    for (std::uint32_t i=0; i<root.size(); ++i) {
        const auto member = root.getObjectMemberAt(i);
        if (std::string_view(member.name) != "tracking_gpu_processing") stripped.addMember(member.name,member.value);
    }
    auto json = choc::json::toString(stripped);
    return {json.begin(),json.end()};
}

void check_markers(Host& host, int latency, int block_size) {
    constexpr int position[]{11,29};
    constexpr float value[]{0.5f,-0.3f};
    Block input{}, out{};
    int measured[]{-1,-1};
    for (int offset=0; offset<1024; offset+=block_size) {
        for (int ch=0; ch<2; ++ch) {
            input[ch].fill(0);
            if (offset == 0) input[ch][position[ch]] = value[ch];
        }
        render(host,input,out,block_size);
        for (int ch=0; ch<2; ++ch) for (int i=0; i<block_size; ++i) {
            if (std::abs(out[ch][i]) > 0.1f && measured[ch] < 0) measured[ch] = offset+i-position[ch];
            REQUIRE(std::abs(out[ch][i] - (offset+i == latency+position[ch] ? value[ch] : 0.f)) < 0.0001f);
        }
    }
    CHECK(measured[0] == latency);
    CHECK(measured[1] == latency);
    CHECK(processor(host).latency_samples() == latency);
    CHECK(processor(host).render_mode_latency_samples(tracking) == latency);
}
}

TEST_CASE("Tracking state preserves independent Mixing and Tracking choices") {
    Host source(spectr::create_spectr), restored(spectr::create_spectr);
    auto& p=processor(source);
    auto& r=processor(restored);
    REQUIRE(p.set_gpu_processing(true));
    CHECK_FALSE(p.tracking_gpu_processing());
    CHECK(p.set_tracking_gpu_processing(true) == p.tracking_gpu_built());
    const auto bytes=p.serialize_plugin_state();
    REQUIRE(r.deserialize_plugin_state(bytes));
    CHECK(r.gpu_processing());
    CHECK(r.tracking_gpu_processing());
    REQUIRE(r.deserialize_plugin_state(legacy_state(bytes)));
    CHECK(r.gpu_processing());
    CHECK_FALSE(r.tracking_gpu_processing());
    restored.prepare(sample_rate,64);
    check_markers(restored,64,64);
    restored.release();
}

TEST_CASE("Tracking opt-in updates host PDC only when Tracking is selected") {
    Host host(spectr::create_spectr);
    auto& p=processor(host);
    if (!p.tracking_gpu_built()) SKIP("Live switch requires an enabled native build");
    REQUIRE(p.set_render_mode(spectr::MaskRenderMode::linear_phase));
    host.prepare(sample_rate,128);
    const auto mixing_delay=p.latency_samples();
    const auto builds=p.renderer_build_count();
    (void)p.consume_latency_changed_flag();
    REQUIRE(p.set_tracking_gpu_processing(true));
    CHECK_FALSE(p.gpu_processing());
    CHECK(p.latency_samples() == mixing_delay);
    CHECK(p.renderer_build_count() == builds);
    CHECK_FALSE(p.consume_latency_changed_flag());
    REQUIRE(p.set_render_mode(tracking));
    CHECK(p.latency_samples() == 192);
    CHECK(p.consume_latency_changed_flag());
    const auto tracking_builds=p.renderer_build_count();
    REQUIRE(p.set_gpu_processing(true));
    CHECK(p.latency_samples() == 192);
    CHECK(p.renderer_build_count() == tracking_builds);
    CHECK_FALSE(p.consume_latency_changed_flag());
    REQUIRE(p.set_tracking_gpu_processing(false));
    CHECK(p.latency_samples() == 64);
    CHECK(p.consume_latency_changed_flag());
    CHECK(p.gpu_processing());
    host.release();
}

TEST_CASE("Tracking refusal preserves CPU audio and reports the measured 64 samples","[refusal]") {
    Host host(spectr::create_spectr);
    configure(host,100);
    auto& p=processor(host);
    (void)p.set_tracking_gpu_processing(true);
    if (p.tracking_gpu_built()) REQUIRE(std::getenv("SPECTR_FORCE_TRACKING_GPU_REFUSAL"));
    host.prepare(sample_rate,128);
    REQUIRE(p.tracking_gpu_provider_refused());
    REQUIRE(p.tracking_gpu_processing());
    CHECK(p.gpu_audio_status().availability == spectr::GpuAudioStatus::Availability::ProviderUnavailable);
    CHECK_FALSE(p.gpu_audio_status().delivery);
    check_markers(host,64,128);
    REQUIRE(p.set_tracking_gpu_processing(false));
    CHECK_FALSE(p.tracking_gpu_provider_refused());
    host.release();
}

TEST_CASE("Tracking reports the measured stereo wet and dry delay at fixed host sizes") {
    for (const bool gpu : {false,true}) for (const float mix : {0.0f,100.0f})
        for (const int block_size : {64,128}) {
            Host host(spectr::create_spectr);
            if (gpu && !processor(host).tracking_gpu_built()) continue;
            configure(host,mix);
            REQUIRE(processor(host).set_tracking_gpu_processing(gpu));
            host.prepare(sample_rate,block_size);
            check_markers(host,gpu ? 192 : 64,block_size);
            std::printf("tracking impulse gpu=%d mix=%.0f host=%d delay=%d/%d\n",gpu,mix,block_size,
                        processor(host).latency_samples(),processor(host).render_mode_latency_samples(tracking));
            host.release();
        }
}

TEST_CASE("Tracking delivers the edited minimum-phase mask and aligned fallback through Spectr") {
    Host hardware(spectr::create_spectr), reference(spectr::create_spectr);
    auto& p=processor(hardware);
    if (!p.tracking_gpu_built()) SKIP("Hardware path is disabled in this build");
    for (auto* h : {&hardware,&reference}) {
        configure(*h,100);
        draw(*h,0);
    }
    REQUIRE(p.set_tracking_gpu_processing(true));
    for (auto* h : {&hardware,&reference}) h->prepare(sample_rate,256);
    REQUIRE(p.latency_samples() == 192);
    REQUIRE(processor(reference).latency_samples() == 64);

    std::array<std::array<float,128>,2> cpu_history{};
    std::array<std::array<float,192>,2> dry_history{};
    std::uint32_t rng=19842;
    Block in{}, cpu{}, gpu{};
    std::size_t timeline=0, callback=0;
    auto run=[&](const char* label, int frames, bool paced, bool seek, bool silent, int host_block) {
        auto previous=p.gpu_audio_status().delivery;
        REQUIRE(previous);
        const auto start=std::chrono::steady_clock::now();
        float error=0.0f, nontrivial=0.0f, peak=0.0f;
        constexpr int sizes[]{17,127,31,193,64,80};
        if (seek) { cpu_history={}; dry_history={}; }
        for (int done=0; done<frames;) {
            int n=std::min(host_block == 0 ? sizes[callback % 6] : host_block,frames-done);
            if (paced)
                std::this_thread::sleep_until(start+std::chrono::nanoseconds(std::int64_t(done)*1000000000/sample_rate));
            for (int i=0; i<n; ++i) for (int ch=0; ch<2; ++ch) {
                rng = rng * 1664525u + 1013904223u;
                in[ch][i] = silent ? 0.f : .12f * (float(rng >> 8)/16777216.f - .5f);
            }
            render(reference,in,cpu,n,seek && done==0);
            render(hardware,in,gpu,n,seek && done==0);
            for (int i=0; i<n; ++i,++timeline) for (int ch=0; ch<2; ++ch) {
                auto& aligned=cpu_history[ch][timeline % 128];
                auto& dry=dry_history[ch][timeline % 192];
                if (!std::isfinite(gpu[ch][i]) || !std::isfinite(aligned))
                    FAIL("non-finite processor output");
                error=std::max(error,std::abs(gpu[ch][i]-aligned));
                nontrivial=std::max(nontrivial,std::abs(gpu[ch][i]-dry));
                peak=std::max(peak,std::abs(gpu[ch][i]));
                aligned=cpu[ch][i];
                dry=in[ch][i];
            }
            ++callback;
            done+=n;
        }
        auto after=p.gpu_audio_status().delivery;
        REQUIRE(after);
        const auto gpu_delta=after->gpu_selected-previous->gpu_selected;
        const auto cpu_delta=after->cpu_fallback-previous->cpu_fallback;
        std::printf("tracking stage=%s frames=%d gpu=%llu cpu=%llu error=%.9g dry_delta=%.9g peak=%.9g\n",
                    label,frames,(unsigned long long)gpu_delta,(unsigned long long)cpu_delta,error,nontrivial,peak);
        INFO(label);
        CHECK(error < 0.00008f);
        if (silent) CHECK(peak < 0.00001f);
        else if (frames >= 8192) CHECK(nontrivial > .01f);
        return std::pair{gpu_delta,cpu_delta};
    };
    CHECK(run("paced64",32768,true,false,false,64).first > 0);
    CHECK(run("paced128",16384,true,false,false,128).first > 0);
    draw(reference,1);
    draw(hardware,1);
    auto during=run("first edit fade",128,true,false,false,64);
    CHECK(during.first == 0);
    CHECK(during.second > 0);
    draw(reference,2);
    draw(hardware,2);
    // The most recent design supersedes the generation still crossfading.
    during=run("superseding edit",4096,true,false,false,0);
    CHECK(during.first == 0);
    CHECK(during.second > 0);
    CHECK(run("irregular edit recovery",32768,true,false,false,0).first > 0);
    // Saturate the worker handoff with more changes than its generation capacity.
    // End with a different mask so selecting an older ready node will fail parity.
    for (unsigned edit=0; edit<9; ++edit) {
        const unsigned pattern=(edit+1) % 3;
        draw(reference,pattern);
        draw(hardware,pattern);
        auto replacement=run("rapid edit",64,false,false,false,64);
        CHECK(replacement.first == 0);
    }
    CHECK(run("rapid edit recovery",49152,true,false,false,0).first > 0);
    CHECK(run("render ahead",32768,false,false,false,64).second > 0);
    during=run("transport seek silence",4096,true,true,true,0);
    CHECK(during.first == 0);
    CHECK(during.second > 0);
    CHECK(run("transport recovery",32768,true,false,false,0).first > 0);
    hardware.release();
    reference.release();
}
