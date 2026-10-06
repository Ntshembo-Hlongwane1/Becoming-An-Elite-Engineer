#pragma once

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <functional>
#include <random>
#include <vector>

// Generates `chunks` chunks of `chunk_size` random bytes and hands each to `sink`.
// One buffer is reused for every chunk, so memory stays at `chunk_size`.
inline void stream_bytes(const std::function<void(const std::vector<char>&)>& sink,
                         std::size_t chunk_size, std::size_t chunks = 10) {
    std::mt19937_64 rng(std::random_device{}());
    std::vector<char> chunk(chunk_size);

    for (std::size_t c = 0; c < chunks; ++c) {
        for (std::size_t i = 0; i < chunk_size; i += 8) {
            auto word = rng();
            std::memcpy(chunk.data() + i, &word, std::min<std::size_t>(8, chunk_size - i));
        }
        sink(chunk);
    }
}
