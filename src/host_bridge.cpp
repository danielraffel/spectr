#include "spectr/host_bridge.hpp"

#include <choc/text/choc_JSON.h>

namespace spectr {

// ── Response helpers ───────────────────────────────────────────────────

std::string HostBridge::ok_response() noexcept {
    try {
        auto obj = choc::value::createObject("BridgeOk");
        obj.addMember("ok", true);
        return choc::json::toString(obj, /*useLineBreaks=*/false);
    } catch (...) {
        return R"({"ok": true})";
    }
}

std::string HostBridge::ok_response(const choc::value::ValueView& extras) noexcept {
    if (!extras.isObject()) return ok_response();
    try {
        auto obj = choc::value::createObject("BridgeOk");
        obj.addMember("ok", true);
        for (uint32_t i = 0, n = extras.size(); i < n; ++i) {
            const auto m = extras.getObjectMemberAt(i);
            obj.addMember(std::string(m.name), m.value);
        }
        return choc::json::toString(obj, /*useLineBreaks=*/false);
    } catch (...) {
        return R"({"ok": true})";
    }
}

std::string HostBridge::err_response(std::string_view msg) noexcept {
    try {
        auto obj = choc::value::createObject("BridgeError");
        obj.addMember("ok",    false);
        obj.addMember("error", std::string(msg));
        return choc::json::toString(obj, /*useLineBreaks=*/false);
    } catch (...) {
        return R"({"ok": false, "error": "internal error"})";
    }
}

// ── Coercion helpers ───────────────────────────────────────────────────

float HostBridge::get_float(const choc::value::ValueView& v,
                            const char* key, float dflt) noexcept {
    try {
        if (!v.isObject() || !v.hasObjectMember(key)) return dflt;
        const auto e = v[key];
        if (e.isFloat64()) return static_cast<float>(e.getFloat64());
        if (e.isInt64())   return static_cast<float>(e.getInt64());
        if (e.isInt32())   return static_cast<float>(e.getInt32());
    } catch (...) {}
    return dflt;
}

std::size_t HostBridge::get_uint(const choc::value::ValueView& v,
                                 const char* key, std::size_t dflt) noexcept {
    try {
        if (!v.isObject() || !v.hasObjectMember(key)) return dflt;
        const auto e = v[key];
        if (e.isInt32())   { const auto x = e.getInt32();   return x < 0 ? 0 : static_cast<std::size_t>(x); }
        if (e.isInt64())   { const auto x = e.getInt64();   return x < 0 ? 0 : static_cast<std::size_t>(x); }
        if (e.isFloat64()) { const auto x = e.getFloat64(); return x < 0 ? 0 : static_cast<std::size_t>(x); }
    } catch (...) {}
    return dflt;
}

std::string HostBridge::get_string(const choc::value::ValueView& v,
                                   const char* key) noexcept {
    try {
        if (!v.isObject() || !v.hasObjectMember(key)) return {};
        const auto e = v[key];
        if (!e.isString()) return {};
        return std::string(e.getString());
    } catch (...) {}
    return {};
}

// ── Handler registration + dispatch ────────────────────────────────────

void HostBridge::add_handler(std::string type, Handler fn) {
    handlers_[std::move(type)] = std::move(fn);
}

std::string HostBridge::dispatch_json(std::string_view json) const noexcept {
    choc::value::Value root;
    try {
        root = choc::json::parse(json);
    } catch (...) {
        return err_response("malformed JSON");
    }
    if (!root.isObject()) return err_response("envelope must be an object");

    const auto type = get_string(root, "type");
    if (type.empty()) return err_response("envelope missing 'type'");

    const auto it = handlers_.find(type);
    if (it == handlers_.end()) return err_response("unknown message type");

    // Payload is optional — handlers that don't need one still get a
    // well-formed ValueView via a stand-in empty object.
    auto empty = choc::value::createObject("Empty");
    if (!root.hasObjectMember("payload")) {
        try {
            return it->second(empty);
        } catch (...) {
            return err_response("internal error");
        }
    }

    try {
        return it->second(root["payload"]);
    } catch (...) {
        return err_response("internal error");
    }
}

} // namespace spectr
