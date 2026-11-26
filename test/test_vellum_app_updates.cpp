// Settings > UPDATES in the Vellum-hosted editor answers from the same
// process-wide update service the standalone's app menu uses.

#include <catch2/catch_test_macros.hpp>

#include "spectr/editor_bridge.hpp"
#include "spectr/spectr.hpp"

#include <pulp/format/app_updates.hpp>
#include <pulp/format/app_updates_bridge.hpp>
#include <pulp/state/store.hpp>
#include <pulp/view/editor_bridge.hpp>

#include <choc/text/choc_JSON.h>

#include <memory>
#include <string>

namespace {

class FakeUpdater final : public pulp::format::AppUpdateService {
  public:
    pulp::format::AppUpdateStatus status() const override {
        pulp::format::AppUpdateStatus s;
        s.available = true;
        s.can_check_now = true;
        s.automatic_checks = automatic;
        s.app_name = "Spectr";
        s.version = "1.0.13";
        s.build = "1.0.13";
        s.releases_url = "https://github.com/danielraffel/spectr/releases";
        s.installer = pulp::format::AppUpdateInstaller::package;
        return s;
    }
    bool check_for_updates() override { ++checks; return true; }
    bool set_automatic_checks(bool on) override { automatic = on; return true; }
    bool open_releases_page() override { ++opens; return true; }

    int checks = 0;
    int opens = 0;
    bool automatic = true;
};

struct InstalledUpdater {
    std::shared_ptr<FakeUpdater> service = std::make_shared<FakeUpdater>();
    InstalledUpdater() { pulp::format::set_app_update_service(service); }
    ~InstalledUpdater() { pulp::format::set_app_update_service(nullptr); }
};

choc::value::Value send(pulp::view::EditorBridge& bridge, const std::string& envelope) {
    return choc::json::parse(bridge.dispatch_json(envelope));
}

} // namespace

TEST_CASE("Vellum editor answers the Settings update messages from the app updater",
          "[vellum][updates]") {
    InstalledUpdater updater;
    pulp::state::StateStore store;
    auto proc = std::make_unique<spectr::Spectr>();
    proc->set_state_store(&store);
    proc->define_parameters(store);
    pulp::view::EditorBridge bridge;
    spectr::register_spectr_editor_handlers(bridge, *proc, proc->patterns(),
                                            proc->editor_authority(), {});

    const auto status = send(bridge, R"({"type":"pulp_updates_get","payload":{}})");
    REQUIRE(status["ok"].getBool());
    CHECK(status["available"].getBool());
    CHECK(status["version"].get<std::string>() == "1.0.13");
    CHECK(status["automaticChecks"].getBool());

    const auto check = send(bridge, R"({"type":"pulp_updates_check","payload":{}})");
    REQUIRE(check["ok"].getBool());
    CHECK(check["started"].getBool());
    CHECK(updater.service->checks == 1);

    const auto automatic =
        send(bridge, R"({"type":"pulp_updates_set_automatic","payload":{"on":false}})");
    REQUIRE(automatic["ok"].getBool());
    CHECK(automatic["applied"].getBool());
    CHECK_FALSE(automatic["automaticChecks"].getBool());
    CHECK_FALSE(updater.service->automatic);

    const auto refused =
        send(bridge, R"({"type":"pulp_updates_set_automatic","payload":{}})");
    CHECK_FALSE(refused["ok"].getBool());

    const auto opened = send(bridge, R"({"type":"pulp_updates_open_releases","payload":{}})");
    REQUIRE(opened["ok"].getBool());
    CHECK(opened["opened"].getBool());
    CHECK(updater.service->opens == 1);
}

TEST_CASE("Vellum editor reports no updater where none is installed", "[vellum][updates]") {
    pulp::format::set_app_update_service(nullptr);
    pulp::view::EditorBridge bridge;
    pulp::format::add_app_update_handlers(bridge);
    const auto status = send(bridge, R"({"type":"pulp_updates_get","payload":{}})");
    REQUIRE(status["ok"].getBool());
    CHECK_FALSE(status["available"].getBool());
}
