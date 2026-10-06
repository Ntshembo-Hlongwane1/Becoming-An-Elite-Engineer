// D3 — Tables on disk. FORMAT.md §4, notes Part 6 §4–§8, §12.
#include <map>
#include <random>

#include "dhelpers.hpp"
#include "dlsm/table.hpp"
#include "minitest.hpp"

using namespace dlsm;
using lsm::LookupResult;
using lsm::MakeInternalKey;
using lsm::ValueType;

namespace {
std::vector<std::pair<std::string, std::string>> Entries(int n, int value_size) {
    std::vector<std::pair<std::string, std::string>> out;
    for (int i = 0; i < n; ++i)
        out.emplace_back(MakeInternalKey(testutil::KeyN(static_cast<std::uint64_t>(i) * 2), 1000, ValueType::kValue),
                         std::string(static_cast<std::size_t>(value_size), static_cast<char>('a' + i % 26)));
    return out;
}
void BuildFile(const std::string& path, std::uint64_t number, const TableOptions& opt,
               const std::vector<std::pair<std::string, std::string>>& entries) {
    std::unique_ptr<WritableFile> f;
    CHECK(WritableFile::Open(path, OpenMode::kTruncate, &f).ok());
    TableBuilder b(opt, f.get(), number);
    for (auto& [k, v] : entries) b.Add(k, v);
    CHECK(b.Finish().ok());
    CHECK_EQ(b.NumEntries(), entries.size());
    CHECK(b.smallest() == entries.front().first);
    CHECK(b.largest() == entries.back().first);
    CHECK(f->Sync().ok());
    CHECK_EQ(f->Size(), b.FileSize());
    CHECK(f->Close().ok());
}
Status OpenTable(const std::string& path, std::uint64_t number, std::shared_ptr<const Table>* t) {
    std::unique_ptr<RandomAccessFile> raf;
    Status s = RandomAccessFile::Open(path, &raf);
    if (!s.ok()) return s;
    return Table::Open(std::move(raf), number, t);
}
}  // namespace

TEST(table_header_bytes) {
    TableHeader h;
    h.flags = TableHeader::kFlagAligned;
    h.block_align = 4096;
    h.target_block_size = 4091;
    h.file_number = 42;
    std::string s;
    h.EncodeTo(s);
    CHECK_EQ(s.size(), 64u);
    CHECK(s.substr(0, 8) == "DLSMTBL1");
    CHECK_EQ(testutil::Hex(s.substr(8, 4)), std::string("01 00 00 00"));
    CHECK_EQ(testutil::Hex(s.substr(24, 8)), std::string("2a 00 00 00 00 00 00 00"));
    for (std::size_t i = 32; i < 60; ++i) CHECK_EQ(s[i], '\0');
    TableHeader g;
    CHECK(g.DecodeFrom(s).ok());
    CHECK_EQ(g.file_number, 42u);
    CHECK_EQ(g.block_align, 4096u);
    std::string bad = s;
    bad[20] ^= 1;
    CHECK(g.DecodeFrom(bad).IsCorruption());   // header crc
    bad = s;
    bad[0] = 'X';
    CHECK(g.DecodeFrom(bad).IsCorruption());   // magic
}

TEST(table_roundtrip_packed) {
    dtest::TempDir d;
    auto entries = Entries(5000, 40);
    BuildFile(d / "000005.sst", 5, TableOptions{}, entries);
    std::shared_ptr<const Table> t;
    CHECK(OpenTable(d / "000005.sst", 5, &t).ok());
    CHECK_EQ(t->header().file_number, 5u);
    CHECK_EQ(t->header().flags & TableHeader::kFlagAligned, 0u);
    auto it = t->NewIterator();
    std::size_t i = 0;
    for (it->SeekToFirst(); it->Valid(); it->Next(), ++i) {
        CHECK(it->key() == entries[i].first);
        CHECK(it->value() == entries[i].second);
    }
    CHECK_EQ(i, entries.size());
    CHECK(it->status().ok());
    // first data block starts right after the header when packed
    CHECK_EQ(t->DataBlockHandles().front().offset, TableHeader::kSize);
}

TEST(table_get_costs_at_most_one_pread) {
    dtest::TempDir d;
    auto entries = Entries(20000, 30);
    BuildFile(d / "000009.sst", 9, TableOptions{}, entries);
    std::shared_ptr<const Table> t;
    CHECK(OpenTable(d / "000009.sst", 9, &t).ok());
    auto& io = GlobalIoStats();
    for (int i = 0; i < 500; ++i) {
        LookupResult r;
        std::string v;
        auto before = io.preads.load();
        CHECK(t->Get(testutil::KeyN(static_cast<std::uint64_t>(i) * 40), lsm::kMaxSequenceNumber, &r, &v).ok());
        CHECK(io.preads.load() - before <= 1);
        CHECK(r == LookupResult::kFound);
        before = io.preads.load();
        CHECK(t->Get(testutil::KeyN(static_cast<std::uint64_t>(i) * 40 + 1), lsm::kMaxSequenceNumber, &r, &v).ok());
        CHECK(io.preads.load() - before <= 1);
        CHECK(r == LookupResult::kNotPresent);
    }
    std::printf("         bloom negatives: %llu / 500 absent lookups\n", static_cast<unsigned long long>(t->bloom_negatives()));
    CHECK(t->bloom_negatives() > 450);
}

TEST(table_aligned_blocks_start_on_page_boundaries) {
    dtest::TempDir d;
    auto entries = Entries(3000, 50);
    TableOptions aligned;
    aligned.block_align = 4096;
    BuildFile(d / "000007.sst", 7, aligned, entries);
    BuildFile(d / "000008.sst", 8, TableOptions{}, entries);
    std::shared_ptr<const Table> a, p;
    CHECK(OpenTable(d / "000007.sst", 7, &a).ok());
    CHECK(OpenTable(d / "000008.sst", 8, &p).ok());
    CHECK((a->header().flags & TableHeader::kFlagAligned) != 0);
    CHECK_EQ(a->header().block_align, 4096u);
    std::size_t crossing_packed = 0;
    for (auto& h : a->DataBlockHandles()) {
        CHECK_EQ(h.offset % 4096, 0u);
        CHECK(h.offset % 4096 + h.size + lsm::kBlockTrailerSize <= 4096);  // block+trailer in ONE page
    }
    for (auto& h : p->DataBlockHandles())
        crossing_packed += (h.offset / 4096) != ((h.offset + h.size + lsm::kBlockTrailerSize - 1) / 4096);
    std::printf("         packed: %llu bytes, %zu of %zu blocks cross a page; aligned: %llu bytes, 0 cross\n",
                static_cast<unsigned long long>(p->file_size()), crossing_packed, p->DataBlockHandles().size(),
                static_cast<unsigned long long>(a->file_size()));
    CHECK(a->file_size() > p->file_size());
    CHECK(crossing_packed > 0);
    auto it = a->NewIterator();
    std::size_t n = 0;
    for (it->SeekToFirst(); it->Valid(); it->Next()) ++n;
    CHECK_EQ(n, entries.size());
}

TEST(table_rejects_wrong_number_and_truncation) {
    dtest::TempDir d;
    auto entries = Entries(200, 20);
    BuildFile(d / "000003.sst", 3, TableOptions{}, entries);
    std::shared_ptr<const Table> t;
    CHECK(!OpenTable(d / "000003.sst", 4, &t).ok());  // header.file_number mismatch
    const std::string full = dtest::Slurp(d / "000003.sst");
    for (std::size_t cut : {std::size_t{0}, std::size_t{10}, std::size_t{63}, full.size() / 2, full.size() - 1}) {
        dtest::Spit(d / "000003.sst", full.substr(0, cut));
        CHECK(!OpenTable(d / "000003.sst", 3, &t).ok());
    }
}

TEST(table_every_byte_flip_detected_or_harmless) {
    dtest::TempDir d;
    auto entries = Entries(40, 10);
    TableOptions small;
    small.block_size = 256;
    small.restart_interval = 4;
    BuildFile(d / "000002.sst", 2, small, entries);
    const std::string good = dtest::Slurp(d / "000002.sst");
    for (std::size_t pos = 0; pos < good.size(); ++pos) {
        std::string bad = good;
        bad[pos] = static_cast<char>(bad[pos] ^ 0x10);
        dtest::Spit(d / "000002.sst", bad);
        std::shared_ptr<const Table> t;
        if (!OpenTable(d / "000002.sst", 2, &t).ok()) continue;
        auto it = t->NewIterator();
        std::size_t i = 0;
        bool same = true;
        for (it->SeekToFirst(); it->Valid(); it->Next(), ++i)
            if (i >= entries.size() || it->key() != entries[i].first || it->value() != entries[i].second) { same = false; break; }
        if (it->status().ok()) {
            CHECK(same);                    // never different data with an OK status
            CHECK_EQ(i, entries.size());    // never silently fewer entries
        }
    }
}
