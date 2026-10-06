#include <cstdio>
#include <string>
#include <vector>
#include "byte_stream.hpp"
#include "file_manager.hpp"
#include <iostream>

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: %s <filename> [size=s|m|l]\n", argv[0]);
        return 1;
    }
    std::string filename = argv[1];
    std::string size = argc > 2 ? argv[2] : "size=s";

    const std::size_t MB = 1024 * 1024;
    std::size_t chunk_size;
    if (size == "size=s")      chunk_size = 5 * MB;
    else if (size == "size=m") chunk_size = 25 * MB;
    else if (size == "size=l") chunk_size = 1024 * MB;
    else {
        std::fprintf(stderr, "invalid size '%s', expected size=s|m|l\n", size.c_str());
        return 1;
    }

    FileManager fm{filename};
    stream_bytes([&](const std::vector<char>& chunk) { fm.Put(chunk); }, chunk_size);

    std::cout << "Read bytes" << std::endl;

    std::vector<char> content;
    fm.Read(content);

    std::cout << content.size() << std::endl;
}
