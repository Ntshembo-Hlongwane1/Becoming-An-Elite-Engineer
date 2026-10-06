// D6 — lsmdump: your X-ray for every file the engine writes. You'll use it to SEE page ordering,
// headers, trailers and footers — and later, as a red-teamer, to read files without your source.
//
// Usage:  lsmdump <file>        detect type from the name (FORMAT.md §1) and dump it
//         lsmdump --hex <file>  also hexdump each structure
//
// Required output (design the exact format yourself, document it in DECISIONS.md):
//   .sst      header fields + crc status; for each data block: offset, size, end, crc OK/BAD,
//             first/last user key, and whether it CROSSES a 4096-byte page boundary;
//             filter/metaindex/index handles; footer; summary "N blocks, M cross a page boundary,
//             K bytes of alignment padding"
//   .log      every physical record: file offset, block#, type, length, crc OK/BAD; then decoded
//             WriteBatches (seq, count, ops)
//   MANIFEST  every VersionEdit decoded
//   CURRENT   contents + whether the named MANIFEST exists
#include <cstdio>

int main(int argc, char** argv) {
    (void)argv;
    if (argc < 2) {
        std::fprintf(stderr, "usage: lsmdump [--hex] <file>\n");
        return 2;
    }
    std::fprintf(stderr, "TODO(D6): implement lsmdump\n");
    return 1;
}
