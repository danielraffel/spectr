#pragma once

#include "spectr/spectr.hpp"
#include "spectr/vellum_editor.hpp"

#include <pulp-vellum/editor-packages/SpectrVellumEditor.hpp>
#include <catch2/catch_test_macros.hpp>

namespace spectr_test {

class VellumContext final : public pulp::format::EditorContext {
public:
    explicit VellumContext(pulp::format::ParamPort& port) : port_(port) {}
    pulp::format::ParamPort& params() override { return port_; }
    pulp::format::VisualPort* visuals() override { return nullptr; }
    bool request_resize(const pulp::format::EditorSize&) override { return false; }
    bool host_action(const pulp::format::HostActionRequest&) override { return false; }
    pulp::format::EditorHostKind host_kind() const override { return kind; }

    pulp::format::EditorHostKind kind = pulp::format::EditorHostKind::plugin;

private:
    pulp::format::ParamPort& port_;
};

inline std::string evaluate(vellum::view::WidgetBridge& bridge, const std::string& expr) {
    try {
        bridge.load_script("throw new Error('SPECTR_VALUE:' + (" + expr + ") + ':END');",
                           "spectr-test-observe");
    } catch (const std::exception& e) {
        const std::string message = e.what();
        constexpr std::string_view marker = "SPECTR_VALUE:";
        const auto start = message.find(marker);
        const auto end = message.find(":END", start);
        if (start != std::string::npos && end != std::string::npos)
            return message.substr(start + marker.size(), end - start - marker.size());
        throw;
    }
    FAIL("script evaluation returned without a result");
    return {};
}

} // namespace spectr_test
