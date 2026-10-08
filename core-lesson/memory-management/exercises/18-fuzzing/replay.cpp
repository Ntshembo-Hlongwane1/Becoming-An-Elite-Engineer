// Replays a reproducer through the harness under ASan to confirm the bug is real & deterministic.
#include "mm/fuzz.hpp"
#include <cstdio>
#include <vector>
int main(int argc, char** argv) {
    if (argc < 2) { std::fprintf(stderr, "usage: replay <file>\n"); return 2; }
    FILE* f = std::fopen(argv[1], "rb"); if (!f) { std::perror("open"); return 2; }
    std::vector<unsigned char> buf; int c; while ((c = std::fgetc(f)) != EOF) buf.push_back((unsigned char)c);
    std::fclose(f);
    std::printf("replaying %zu bytes...\n", buf.size());
    mm::fuzz_one(buf.data(), buf.size());     // crashes here under ASan if the bug reproduces
    std::printf("no crash on replay\n");
    return 0;
}
