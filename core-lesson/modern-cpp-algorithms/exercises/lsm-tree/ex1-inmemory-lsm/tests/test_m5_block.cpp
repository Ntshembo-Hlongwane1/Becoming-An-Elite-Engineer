// M5 — Blocks and M7 framing (format.hpp). Notes Part 6 §5–§8.
#include <random>
#include <string>
#include <vector>

#include "helpers.hpp"
#include "lsm/block.hpp"
#include "lsm/coding.hpp"
#include "lsm/format.hpp"
#include "minitest.hpp"

using namespace lsm;
using testutil::Hex;

TEST(block_worked_example_bytes) {
    // Part 6 §5: exactly these 27 bytes.
    BlockBuilder b;
    b.Add("apple", "1");
    b.Add("applet", "2");
    b.Add("apply", "3");
    CHECK_EQ(b.CurrentSizeEstimate(), 27u);
    std::string_view out = b.Finish();
    CHECK_EQ(Hex(out), std::string("00 05 01 61 70 70 6c 65 31 05 01 01 74 32 04 01 01 79 33 "
                                   "00 00 00 00 01 00 00 00"));
}

TEST(block_restart_points) {
    BlockBuilder b(BlockOptions{2, BytewiseCompare});
    b.Add("a1", "x");
    b.Add("a2", "x");
    b.Add("a3", "x");  // third entry starts a new restart run with shared == 0
    std::string bytes(b.Finish());
    std::unique_ptr<BlockReader> r;
    CHECK(BlockReader::Open(bytes, BytewiseCompare, &r).ok());
    CHECK_EQ(r->NumRestarts(), 2u);
    // restart[1] points at the third entry, which must be stored with shared == 0
    std::uint32_t num = DecodeFixed32(bytes.data() + bytes.size() - 4);
    std::uint32_t r1 = DecodeFixed32(bytes.data() + bytes.size() - 4 - 4 * num + 4);
    CHECK_EQ(static_cast<unsigned char>(bytes[r1]), 0u);
}

TEST(block_iterate_and_seek) {
    std::vector<std::pair<std::string, std::string>> kv;
    for (int i = 0; i < 1000; ++i) kv.emplace_back(testutil::KeyN(i * 2), "v" + std::to_string(i));
    BlockBuilder b;
    for (auto& [k, v] : kv) b.Add(k, v);
    std::string bytes(b.Finish());
    std::unique_ptr<BlockReader> r;
    CHECK(BlockReader::Open(bytes, BytewiseCompare, &r).ok());
    auto it = r->NewIterator();
    it->SeekToFirst();
    for (auto& [k, v] : kv) {
        CHECK(it->Valid());
        CHECK_EQ(it->key(), std::string_view(k));
        CHECK_EQ(it->value(), std::string_view(v));
        it->Next();
    }
    CHECK(!it->Valid());
    CHECK(it->status().ok());
    it->Seek(testutil::KeyN(501));  // between 500 and 502
    CHECK(it->Valid());
    CHECK_EQ(std::string(it->key()), testutil::KeyN(502));
    it->Seek("");
    CHECK_EQ(it->key(), std::string_view(kv.front().first));
    it->Seek("z");
    CHECK(!it->Valid());
}

TEST(block_reader_rejects_garbage_without_crashing) {
    std::unique_ptr<BlockReader> r;
    CHECK(BlockReader::Open("", BytewiseCompare, &r).IsCorruption());
    CHECK(BlockReader::Open("\x01\x00", BytewiseCompare, &r).IsCorruption());
    std::string huge;
    PutFixed32(huge, 0);
    PutFixed32(huge, 0x7fffffff);  // claims 2^31 restarts in an 8-byte block
    CHECK(BlockReader::Open(huge, BytewiseCompare, &r).IsCorruption());

    // Fuzz: mutate a valid block; Open may fail, but iteration must terminate without UB.
    BlockBuilder b;
    for (int i = 0; i < 100; ++i) b.Add(testutil::KeyN(i), std::string(i % 7, 'v'));
    const std::string good(b.Finish());
    std::mt19937_64 rng(77);
    for (int round = 0; round < 3000; ++round) {
        std::string bad = good;
        int flips = 1 + static_cast<int>(rng() % 4);
        for (int f = 0; f < flips; ++f) bad[rng() % bad.size()] = static_cast<char>(rng());
        if (rng() % 4 == 0) bad.resize(rng() % bad.size());
        if (!BlockReader::Open(bad, BytewiseCompare, &r).ok()) continue;
        auto it = r->NewIterator();
        int steps = 0;
        for (it->SeekToFirst(); it->Valid() && steps < 10000; it->Next()) ++steps;
        CHECK(steps < 10000);
        it->Seek(testutil::KeyN(50));
    }
}

TEST(format_trailer_worked_example) {
    // Part 6 §6: the 27-byte block gets trailer 00 6e 66 8d 18.
    BlockBuilder b;
    b.Add("apple", "1");
    b.Add("applet", "2");
    b.Add("apply", "3");
    std::string file = "PREFIX";
    BlockHandle h = AppendBlockWithTrailer(file, b.Finish());
    CHECK_EQ(h.offset, 6u);
    CHECK_EQ(h.size, 27u);
    CHECK_EQ(file.size(), 6u + 27u + kBlockTrailerSize);
    CHECK_EQ(Hex(std::string_view(file).substr(6 + 27)), std::string("00 6e 66 8d 18"));
    std::string_view contents;
    CHECK(VerifyBlock(std::string_view(file).substr(6), &contents).ok());
    CHECK_EQ(contents.size(), 27u);
    std::string bad = file;
    bad[10] ^= 0x20;
    CHECK(VerifyBlock(std::string_view(bad).substr(6), &contents).IsCorruption());
    CHECK(VerifyBlock(std::string_view(file).substr(6, 20), &contents).IsCorruption());  // truncated
}

TEST(format_handles_and_footer) {
    BlockHandle h{123456789012ull, 4096};
    std::string s;
    h.EncodeTo(s);
    std::string_view in(s);
    BlockHandle d;
    CHECK(d.DecodeFrom(&in).ok());
    CHECK_EQ(d.offset, h.offset);
    CHECK_EQ(d.size, h.size);

    Footer f;
    f.metaindex_handle = {1000, 20};
    f.index_handle = {1025, 300};
    std::string fb;
    f.EncodeTo(fb);
    CHECK_EQ(fb.size(), Footer::kEncodedLength);
    CHECK_EQ(fb.size(), 48u);
    CHECK_EQ(fb.substr(40), std::string("MINILSM1"));  // little-endian magic reads as ASCII
    Footer g;
    CHECK(g.DecodeFrom(fb).ok());
    CHECK_EQ(g.index_handle.offset, 1025u);
    CHECK_EQ(g.metaindex_handle.size, 20u);
    std::string wrong = fb;
    wrong[47] = 'X';
    CHECK(g.DecodeFrom(wrong).IsCorruption());
    CHECK(g.DecodeFrom(std::string_view(fb).substr(1)).IsCorruption());
}
