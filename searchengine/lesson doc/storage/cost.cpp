// internal/kernal/core/storage/tests/cost_model_lab.cpp
#include <chrono>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iostream>
#include <random>
#include <vector>

using Clock = std::chrono::steady_clock;

// Returns nanoseconds per iteration.
template <typename F>
double timeIt(F&& fn, int iterations) {
    // Warm up: page in code, populate branch predictors, let the CPU clock ramp.
    // Skipping this reports the cost of a cold instruction cache, not of the work.
    for (int i = 0; i < iterations / 10 + 1; ++i) fn();

    auto start = Clock::now();
    for (int i = 0; i < iterations; ++i) fn();
    auto end = Clock::now();

    auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count();
    return static_cast<double>(ns) / iterations;
}

int main() {
    constexpr std::size_t PAGE = 4096;

    // ---------------------------------------------------------------- 1. syscall floor
    // Every ReadPage you write in doc 03 pays this before it moves a single byte.
    {
        std::ofstream sink("sink.tmp", std::ios::binary);
        char byte = 0;
        double ns = timeIt([&]{
            sink.write(&byte, 1);
            sink.flush();              // forces the write() syscall, not just a buffer append
        }, 20000);
        std::cout << "syscall floor (write+flush 1 byte): " << ns << " ns\n";
    }
    std::remove("sink.tmp");

    // ---------------------------------------------------------------- 2. build a big file
    // MUST exceed physical RAM to defeat the OS page cache. Raise this if your box has
    // lots of RAM -- if the whole file fits in cache, section 4 below measures RAM, not disk,
    // and every conclusion you draw from it will be wrong.
    constexpr std::size_t FILE_PAGES = 512 * 1024;          // 512K pages * 4 KB = 2 GB
    const char* path = "cost_lab.tmp";

    {
        std::cout << "building " << (FILE_PAGES * PAGE) / (1024 * 1024) << " MB test file...\n";
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        std::vector<char> buf(PAGE, 'x');

        auto start = Clock::now();
        for (std::size_t i = 0; i < FILE_PAGES; ++i) {
            // Stamp the page id into the first 8 bytes so section 4 can verify it read
            // the page it asked for. An unverified I/O benchmark is a benchmark of nothing:
            // a bug that returns the wrong page looks exactly like a very fast correct one.
            std::memcpy(buf.data(), &i, sizeof(i));
            out.write(buf.data(), PAGE);
        }
        out.flush();
        auto end = Clock::now();

        double sec = std::chrono::duration<double>(end - start).count();
        std::cout << "sequential write: "
                  << (FILE_PAGES * PAGE) / (1024.0 * 1024.0) / sec << " MB/s\n";
    }

    // ---------------------------------------------------------------- 3. warm read (THE LIE)
    // We just wrote this file, so the tail of it is sitting in the OS page cache.
    // This number is RAM speed wearing a disk costume. Print it precisely so you can
    // compare it to the honest number below and never confuse the two again.
    {
        std::ifstream in(path, std::ios::binary);
        std::vector<char> buf(PAGE);
        std::size_t hotPage = FILE_PAGES - 1;

        double ns = timeIt([&]{
            in.seekg(static_cast<std::streamoff>(hotPage * PAGE));
            in.read(buf.data(), PAGE);
        }, 20000);
        std::cout << "WARM read (cached, misleading): " << ns << " ns\n";
    }

    // ---------------------------------------------------------------- 4. cold random read
    // Random offsets across a file larger than RAM: most reads miss the page cache and
    // reach the device. This is the number the whole series is designed around.
    {
        std::ifstream in(path, std::ios::binary);
        std::vector<char> buf(PAGE);
        std::mt19937_64 rng(12345);
        std::uint64_t checksum = 0;

        auto start = Clock::now();
        constexpr int READS = 2000;
        for (int i = 0; i < READS; ++i) {
            std::size_t page = rng() % FILE_PAGES;
            in.seekg(static_cast<std::streamoff>(page * PAGE));
            in.read(buf.data(), PAGE);

            std::uint64_t stamped;
            std::memcpy(&stamped, buf.data(), sizeof(stamped));
            if (stamped != page) { std::cout << "CORRUPT read!\n"; return 1; }
            checksum += stamped;                 // also stops the optimiser deleting the read
        }
        auto end = Clock::now();

        double ns = std::chrono::duration<double, std::nano>(end - start).count() / READS;
        std::cout << "COLD random 4 KB read: " << ns << " ns  ("
                  << ns / 1000.0 << " us)   [checksum " << checksum << "]\n";
    }

    // ---------------------------------------------------------------- 5. size crossover
    // Where does reading more bytes start actually costing more time? Below that size,
    // you are paying pure overhead -- which is the argument for a 4 KB (or larger) page.
    {
        std::ifstream in(path, std::ios::binary);
        std::mt19937_64 rng(999);

        for (std::size_t sz : {64u, 512u, 4096u, 16384u, 65536u, 262144u}) {
            std::vector<char> buf(sz);
            auto start = Clock::now();
            constexpr int N = 500;
            for (int i = 0; i < N; ++i) {
                std::size_t maxOff = FILE_PAGES * PAGE - sz;
                std::size_t off = (rng() % (maxOff / sz)) * sz;
                in.seekg(static_cast<std::streamoff>(off));
                in.read(buf.data(), static_cast<std::streamsize>(sz));
            }
            auto end = Clock::now();
            double ns = std::chrono::duration<double, std::nano>(end - start).count() / N;
            std::cout << "  read " << sz << " B: " << ns << " ns  ("
                      << (sz / 1024.0) / (ns / 1e9) / 1024.0 << " MB/s)\n";
        }
    }

    std::remove(path);
    return 0;
}