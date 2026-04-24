#pragma once

// HostBridge — renderer-agnostic message-dispatch framework.
//
// Stand-in for what Pulp will eventually ship as
// `pulp::view::EditorBridge`. See pulp#709 for the design discussion
// and acceptance criteria. When that framework lands and a Pulp SDK
// release includes it, this file + host_bridge.cpp get deleted and
// the `spectr::HostBridge` uses in editor_bridge.cpp get renamed
// `pulp::view::EditorBridge`. Public API is deliberately matched so
// that's a mechanical search-and-replace.
//
// Why it's here in the meantime:
// 1. Proves the proposed Pulp API works as a real consumer uses it.
//    Any design issue we surface now gets folded into pulp#709
//    before the upstream implementation starts.
// 2. Shrinks Spectr's in-repo bridge from ~280 lines to ~100 lines
//    of handler registrations, improving readability today.
// 3. Makes the Phase-2 cutover (WebView → native import via
//    pulp#468) a smaller, less risky diff on the Spectr side.
//
// This class is deliberately renderer-agnostic:
//
//   - `dispatch_json(envelope)` takes a JSON string and returns a
//     JSON response. Never throws. Always returns a well-formed
//     `{"ok": true|false, ...}` envelope, even on malformed input.
//   - `add_handler("type", fn)` registers a handler for a message
//     type. Handlers receive the parsed `payload` ValueView and
//     return a JSON response string (use `ok_response()` or
//     `err_response()` helpers for the common cases).
//   - Attachment to a concrete source (WebViewPanel today,
//     pulp#468's native JS runtime tomorrow, WASM or WebCLAP in the
//     future) is done by the consumer — the bridge itself doesn't
//     know or care where envelopes come from.
//
// The `get_float` / `get_uint` / `get_string` statics are safe
// wrappers around choc::value::ValueView — each handler uses them
// to pull payload fields without the boilerplate of type-checking
// every getter.

#include <choc/containers/choc_Value.h>

#include <functional>
#include <string>
#include <string_view>
#include <unordered_map>

namespace spectr {

class HostBridge {
public:
    /// Handler signature: takes the parsed payload, returns a JSON
    /// response string. Should not throw — if an exception escapes,
    /// the dispatcher catches it and returns `err_response("internal error")`.
    using Handler =
        std::function<std::string(const choc::value::ValueView& payload)>;

    HostBridge() = default;
    HostBridge(const HostBridge&) = delete;
    HostBridge& operator=(const HostBridge&) = delete;

    /// Register a handler for a message type. Overwrites any existing
    /// handler for that type.
    void add_handler(std::string type, Handler fn);

    /// Dispatch a JSON envelope. Returns a JSON response envelope.
    /// Malformed JSON, missing "type", unknown type, and handler
    /// exceptions all return a well-formed error envelope. Never
    /// throws.
    std::string dispatch_json(std::string_view json) const noexcept;

    // ── Payload coercion helpers ────────────────────────────────────────
    //
    // choc::value::ValueView throws on wrong-type getters. These map
    // missing or wrong-type fields to the caller-supplied default
    // without throwing, so handlers stay defensive with minimal
    // boilerplate.

    static float       get_float (const choc::value::ValueView&, const char* key, float dflt) noexcept;
    static std::size_t get_uint  (const choc::value::ValueView&, const char* key, std::size_t dflt) noexcept;
    static std::string get_string(const choc::value::ValueView&, const char* key) noexcept;

    // ── Response builders ───────────────────────────────────────────────
    //
    // Both emit choc-style JSON (`{"ok": true}` / `{"ok": false, "error": "…"}`
    // — note the space after the colon; choc always inserts one).

    /// Success with no extras.
    static std::string ok_response() noexcept;

    /// Success with arbitrary extra object members (e.g. echoing
    /// metadata back to the caller). The `extras` value must be an
    /// object; other types are ignored and the result falls back to
    /// plain `ok_response()`.
    static std::string ok_response(const choc::value::ValueView& extras) noexcept;

    /// Failure with a human-readable message.
    static std::string err_response(std::string_view msg) noexcept;

private:
    std::unordered_map<std::string, Handler> handlers_;
};

} // namespace spectr
