#pragma once
#include <cstdint>
#include <random>
#include <string>

namespace testutil {

inline std::string Hex(std::string_view s) {
    static const char* d = "0123456789abcdef";
    std::string out;
    for (unsigned char c : s) {
        if (!out.empty()) out += ' ';
        out += d[c >> 4];
        out += d[c & 15];
    }
    return out;
}

inline std::string RandomBytes(std::mt19937_64& rng, std::size_t n) {
    std::string s(n, '\0');
    for (auto& c : s) c = static_cast<char>(rng() & 0xff);
    return s;
}

inline std::string KeyN(std::uint64_t n, int width = 8) {
    std::string s = std::to_string(n);
    return "k" + std::string(width > static_cast<int>(s.size()) ? width - s.size() : 0, '0') + s;
}

}  // namespace testutil
