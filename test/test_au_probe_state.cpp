#include "../tools/au_probe_state.hpp"

#include <catch2/catch_test_macros.hpp>
#include <pulp/format/plugin_state_io.hpp>
#include <pulp/format/processor.hpp>
#include <pulp/state/store.hpp>

#ifdef SPECTR_HAS_PULP_COMPACT_STATE
#include <pulp/format/au_v2_common.hpp>
#endif

#include <array>
#include <memory>
#include <span>
#include <type_traits>
#include <vector>

namespace {

constexpr std::string_view original = R"({"freeze_length": {"bars": 1, "fraction": "0"}})";
const std::string edited = R"({"freeze_length": {"bars": 0, "fraction": "1/16"}, "notes": ")"
    + std::string(140, 'x') + R"("})";

struct FixtureProcessor : pulp::format::Processor {
    std::string_view json = original;

    pulp::format::PluginDescriptor descriptor() const override { return {}; }
    void define_parameters(pulp::state::StateStore&) override {}
    void prepare(const pulp::format::PrepareContext&) override {}
    void process(pulp::audio::BufferView<float>&,
                 const pulp::audio::BufferView<const float>&,
                 pulp::midi::MidiBuffer&, pulp::midi::MidiBuffer&,
                 const pulp::format::ProcessContext&) override {}
    std::vector<std::uint8_t> serialize_plugin_state() const override {
        return {json.begin(), json.end()};
    }
};

struct ClassInfo {
    std::unique_ptr<std::remove_pointer_t<CFMutableDictionaryRef>, decltype(&CFRelease)> dict{
        CFDictionaryCreateMutable(nullptr, 0, &kCFTypeDictionaryKeyCallBacks,
                                  &kCFTypeDictionaryValueCallBacks), &CFRelease};

    void set(CFStringRef key, std::span<const std::uint8_t> bytes) {
        auto* data = CFDataCreate(nullptr, bytes.data(), CFIndex(bytes.size()));
        CFDictionarySetValue(dict.get(), key, data);
        CFRelease(data);
    }

    std::vector<std::uint8_t> get(CFStringRef key) const {
        const auto data = static_cast<CFDataRef>(CFDictionaryGetValue(dict.get(), key));
        REQUIRE(data);
        const auto* bytes = CFDataGetBytePtr(data);
        return {bytes, bytes + CFDataGetLength(data)};
    }

    bool rewrite() {
        return spectr::au_probe_state::edit_class_info(dict.get(), [](std::string& json) {
            REQUIRE(json == original);
            json = edited;
            return true;
        });
    }
};

} // namespace

TEST_CASE("AU probe rewrites legacy PLST payloads under either bridge key") {
    pulp::state::StateStore store;
    FixtureProcessor processor;
    const auto initial = pulp::format::plugin_state_io::serialize(store, processor);
    processor.json = edited;
    const auto expected = pulp::format::plugin_state_io::serialize(store, processor);

    for (const auto key : {CFSTR("pulp-state"), CFSTR("pulpState")}) {
        ClassInfo info;
        info.set(key, initial);
        REQUIRE(info.rewrite());
        CHECK(info.get(key) == expected);
    }
}

#ifdef SPECTR_HAS_PULP_COMPACT_STATE
TEST_CASE("AU probe rewrites compact state produced by Pulp") {
    namespace compact = pulp::format::au::compact_state;
    pulp::state::StateStore store;
    store.set_state_version(7);
    REQUIRE(store.add_parameter({.id = 257, .name = "Gain", .range = {0, 1, 0}}));
    store.set_value(257, 0.75f);
    FixtureProcessor processor;
    const auto initial = compact::encode(store, processor.serialize_plugin_state());
    const auto legacy = pulp::format::plugin_state_io::serialize(store, processor);
    ClassInfo info;
    const auto key = CFSTR("pulp-state-v2");
    info.set(CFSTR("pulp-state"), legacy);

    SECTION("compact state takes precedence and the rewrite matches the SDK byte for byte") {
        info.set(key, initial);
        REQUIRE(info.rewrite());
        processor.json = edited;
        CHECK(info.get(key) == compact::encode(store, processor.serialize_plugin_state()));
        CHECK(info.get(CFSTR("pulp-state")) == legacy);
    }

    SECTION("a bad checksum or omitted payload never invokes the edit or updates the dictionary") {
        auto rejected = std::array{initial, compact::encode(store, processor.serialize_plugin_state(), true)};
        rejected.front().back() ^= 1;
        for (const auto& blob : rejected) {
            info.set(key, blob);
            CHECK_FALSE(spectr::au_probe_state::edit_class_info(info.dict.get(), [](std::string&) {
                FAIL("invalid or omitted payload passed to the editor");
                return true;
            }));
            CHECK(info.get(key) == blob);
            CHECK(info.get(CFSTR("pulp-state")) == legacy);
        }
    }
}
#endif
