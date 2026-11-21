#pragma once

#include <cstddef>
#include <cstdint>
#include <sstream>
#include <string>

namespace spectr {

// Content identity of a set of embedded files: FNV-1a 64 over each file's
// relative path, size and bytes. Count and total size alone are not an
// identity -- a same-size edit to an asset or script leaves both unchanged and
// would let a stale materialized package be reused. `Files` is any range whose
// elements expose relative_path (C string), data (unsigned char*) and size.
template <typename Files>
std::string embedded_package_identity(const Files& files) {
    std::uint64_t hash = 1469598103934665603ull;
    const auto mix = [&hash](const unsigned char* bytes, std::size_t count) {
        for (std::size_t i = 0; i < count; ++i) {
            hash ^= bytes[i];
            hash *= 1099511628211ull;
        }
    };
    std::size_t total = 0;
    std::size_t count = 0;
    for (const auto& file : files) {
        const char* path = file.relative_path;
        std::size_t path_len = 0;
        while (path[path_len] != '\0') ++path_len;
        const std::uint64_t size = file.size;
        mix(reinterpret_cast<const unsigned char*>(path), path_len + 1);
        mix(reinterpret_cast<const unsigned char*>(&size), sizeof size);
        mix(file.data, file.size);
        total += file.size;
        ++count;
    }
    std::ostringstream id;
    id << std::hex << count << 'x' << total << 'x' << hash;
    return id.str();
}

}  // namespace spectr
