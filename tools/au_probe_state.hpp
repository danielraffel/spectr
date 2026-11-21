#pragma once

#include <CoreFoundation/CoreFoundation.h>
#include <zlib.h>

#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <string_view>

namespace spectr::au_probe_state {

inline std::uint32_t read_u32(std::string_view bytes, std::size_t at) {
    std::uint32_t value = 0;
    for (unsigned shift = 0; shift < 32; shift += 8)
        value |= std::uint32_t(std::uint8_t(bytes[at++])) << shift;
    return value;
}

inline void append_u32(std::string& bytes, std::uint32_t value) {
    for (unsigned shift = 0; shift < 32; shift += 8)
        bytes.push_back(char(std::uint8_t(value >> shift)));
}

inline bool read_varint(std::string_view bytes, std::size_t& at, std::uint64_t& value) {
    value = 0;
    for (unsigned shift = 0; shift < 64 && at < bytes.size(); shift += 7) {
        const auto byte = std::uint8_t(bytes[at++]);
        if (shift == 63 && byte > 1) return false;
        value |= std::uint64_t(byte & 0x7F) << shift;
        if ((byte & 0x80) == 0) return true;
    }
    return false;
}

inline void append_varint(std::string& bytes, std::uint32_t value) {
    while (value >= 0x80) {
        bytes.push_back(char((value & 0x7F) | 0x80));
        value >>= 7;
    }
    bytes.push_back(char(value));
}

inline std::uint32_t checksum(std::string_view bytes) {
    return static_cast<std::uint32_t>(
        crc32_z(0, reinterpret_cast<const Bytef*>(bytes.data()), bytes.size()));
}

// Validate framing and locate the JSON without interpreting parameter values.
struct CompactPayload {
    std::size_t length_offset;
    std::size_t json_offset;
};

inline std::optional<CompactPayload> compact_payload(std::string_view body) {
    if (body.size() < 12 || !body.starts_with("PLCS") || body[4] != 1 || body[5] != 0)
        return std::nullopt;
    std::size_t at = 10;
    std::uint64_t count;
    if (!read_varint(body, at, count) || count > (body.size() - at) / 5)
        return std::nullopt;
    for (std::uint64_t i = 0; i < count; ++i) {
        std::uint64_t id;
        if (!read_varint(body, at, id) || id > std::numeric_limits<std::uint32_t>::max()
            || body.size() - at < 4)
            return std::nullopt;
        at += 4;
    }
    const auto length_offset = at;
    std::uint64_t size;
    if (!read_varint(body, at, size) || size != body.size() - at || size == 0)
        return std::nullopt;
    return CompactPayload{length_offset, at};
}

template <typename Edit>
bool edit_class_info(CFMutableDictionaryRef dict, Edit&& edit) {
    // AUv2 gives the compact key precedence. The AUv3 bridge uses pulpState
    // for its separate fullState dictionary, with the same PLST encoding.
    CFStringRef key = CFSTR("pulp-state-v2");
    auto* value = CFDictionaryGetValue(dict, key);
    const bool compact = value != nullptr;
    if (!value) {
        key = CFSTR("pulp-state");
        value = CFDictionaryGetValue(dict, key);
    }
    if (!value) {
        key = CFSTR("pulpState");
        value = CFDictionaryGetValue(dict, key);
    }
    if (!value || CFGetTypeID(value) != CFDataGetTypeID()) return false;
    const auto data = static_cast<CFDataRef>(value);
    const auto size = static_cast<std::size_t>(CFDataGetLength(data));
    if (size < 4) return false;
    const std::string_view blob(reinterpret_cast<const char*>(CFDataGetBytePtr(data)), size);
    const auto body = blob.substr(0, size - 4);
    if (read_u32(blob, body.size()) != checksum(body)) return false;

    std::string out;
    std::string json;
    std::string_view store;
    if (compact) {
        const auto payload = compact_payload(body);
        if (!payload) return false;
        json = body.substr(payload->json_offset);
        out = body.substr(0, payload->length_offset);
    } else {
        if (body.size() < 16 || !body.starts_with("PLST")) return false;
        const std::size_t store_size = read_u32(body, 8);
        const std::size_t payload_size = read_u32(body, 12);
        if (store_size > body.size() - 16 || payload_size != body.size() - 16 - store_size
            || payload_size == 0)
            return false;
        store = body.substr(16, store_size);
        json = body.substr(16 + store_size);
        out = body.substr(0, 12);
    }

    if (!edit(json) || json.size() > std::numeric_limits<std::uint32_t>::max()) return false;
    const auto payload_size = static_cast<std::uint32_t>(json.size());
    if (compact) {
        append_varint(out, payload_size);
    } else {
        append_u32(out, payload_size);
        out += store;
    }
    out += json;
    append_u32(out, checksum(out));

    auto* next = CFDataCreate(nullptr, reinterpret_cast<const UInt8*>(out.data()), CFIndex(out.size()));
    if (!next) return false;
    CFDictionarySetValue(dict, key, next);
    CFRelease(next);
    return true;
}

} // namespace spectr::au_probe_state
