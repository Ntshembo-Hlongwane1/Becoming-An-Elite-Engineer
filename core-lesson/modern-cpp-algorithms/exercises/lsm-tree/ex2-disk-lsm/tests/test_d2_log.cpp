// D2 — Log format. FORMAT.md §2, notes Part 6 §3.
#include <random>

#include "dhelpers.hpp"
#include "dlsm/log.hpp"
#include "minitest.hpp"

using namespace dlsm;

namespace {
struct CountingReporter : log::Reader::Reporter {
    std::size_t dropped = 0;
    int reports = 0;
    void Corruption(std::size_t bytes, const Status&) override {
        dropped += bytes;
        ++reports;
    }
};
void WriteRecords(const std::string& path, const std::vector<std::string>& recs) {
    std::unique_ptr<WritableFile> f;
    CHECK(WritableFile::Open(path, OpenMode::kTruncate, &f).ok());
    log::Writer w(f.get());
    for (auto& r : recs) CHECK(w.AddRecord(r).ok());
    CHECK(f->Close().ok());
}
std::vector<std::string> ReadAll(const std::string& path, CountingReporter* rep) {
    std::unique_ptr<SequentialFile> f;
    CHECK(SequentialFile::Open(path, &f).ok());
    log::Reader r(f.get(), rep, true);
    std::vector<std::string> out;
    std::string rec;
    while (r.ReadRecord(&rec)) out.push_back(rec);
    return out;
}
}  // namespace

TEST(log_worked_example_layout) {
    // notes Part 6 §3: A=1000, B=97270, C=8000.
    dtest::TempDir d;
    std::vector<std::string> recs{std::string(1000, 'A'), std::string(97270, 'B'), std::string(8000, 'C')};
    WriteRecords(d / "x.log", recs);
    const std::string f = dtest::Slurp(d / "x.log");
    CHECK_EQ(f.size(), 3u * 32768u + 8007u);
    auto type_at = [&](std::size_t off) { return static_cast<int>(static_cast<unsigned char>(f[off + 6])); };
    auto len_at = [&](std::size_t off) {
        return static_cast<unsigned>(static_cast<unsigned char>(f[off + 4])) |
               (static_cast<unsigned>(static_cast<unsigned char>(f[off + 5])) << 8);
    };
    CHECK_EQ(type_at(0), 1);        // A FULL
    CHECK_EQ(len_at(0), 1000u);
    CHECK_EQ(type_at(1007), 2);     // B FIRST
    CHECK_EQ(len_at(1007), 31754u);
    CHECK_EQ(type_at(32768), 3);    // B MIDDLE
    CHECK_EQ(len_at(32768), 32761u);
    CHECK_EQ(type_at(65536), 4);    // B LAST
    CHECK_EQ(len_at(65536), 32755u);
    for (std::size_t i = 98298; i < 98304; ++i) CHECK_EQ(f[i], '\0');  // 6-byte zero trailer
    CHECK_EQ(type_at(98304), 1);    // C FULL in the fourth block
    CountingReporter rep;
    CHECK(ReadAll(d / "x.log", &rep) == recs);
    CHECK_EQ(rep.reports, 0);
}

TEST(log_edge_sizes_roundtrip) {
    dtest::TempDir d;
    std::vector<std::string> recs{"", "x", std::string(32768 - 7, 'a'), std::string(32768 - 7 - 7 + 1, 'b'),
                                  std::string(32768 * 3, 'c'), "", std::string(6, 'd'), std::string(100000, 'e')};
    std::mt19937_64 rng(3);
    for (int i = 0; i < 300; ++i) recs.push_back(testutil::RandomBytes(rng, rng() % 3000));
    WriteRecords(d / "y.log", recs);
    CountingReporter rep;
    CHECK(ReadAll(d / "y.log", &rep) == recs);
    CHECK_EQ(rep.reports, 0);
}

TEST(log_torn_tail_is_silent_eof) {
    dtest::TempDir d;
    std::vector<std::string> recs;
    for (int i = 0; i < 5; ++i) recs.push_back(std::string(900 + i, static_cast<char>('a' + i)));
    WriteRecords(d / "t.log", recs);
    const std::string full = dtest::Slurp(d / "t.log");
    std::size_t end4 = 0;
    for (int i = 0; i < 4; ++i) end4 += 7 + recs[static_cast<std::size_t>(i)].size();
    std::vector<std::string> first4(recs.begin(), recs.begin() + 4);
    for (std::size_t cut = end4; cut < full.size(); ++cut) {  // every possible torn tail of record 5
        dtest::Spit(d / "t2.log", full.substr(0, cut));
        CountingReporter rep;
        CHECK(ReadAll(d / "t2.log", &rep) == first4);
        CHECK_EQ(rep.reports, 0);  // a crash mid-append is not corruption
    }
}

TEST(log_corruption_reported_and_resyncs_at_next_block) {
    dtest::TempDir d;
    std::vector<std::string> recs;
    for (int i = 0; i < 1000; ++i) recs.push_back("record-" + std::to_string(i) + std::string(90, '.'));
    WriteRecords(d / "c.log", recs);
    std::string f = dtest::Slurp(d / "c.log");
    f[7 + 3 * 107 + 50] ^= 0x01;  // inside record #3's payload (each physical record is 7 + ~100 bytes)
    dtest::Spit(d / "c.log", f);
    CountingReporter rep;
    auto got = ReadAll(d / "c.log", &rep);
    CHECK(rep.reports >= 1);
    CHECK(rep.dropped > 0);
    CHECK(!got.empty());
    CHECK(got.back() == recs.back());  // recovered after the corrupt block
    std::size_t j = 0;                 // every returned record is a genuine original, in order
    for (auto& g : got) {
        while (j < recs.size() && recs[j] != g) ++j;
        CHECK(j < recs.size());
    }
    for (auto& g : got) CHECK(g != recs[3]);
}

TEST(log_writer_continues_existing_file) {
    dtest::TempDir d;
    WriteRecords(d / "a.log", {std::string(30000, 'x')});
    std::unique_ptr<WritableFile> f;
    CHECK(WritableFile::Open(d / "a.log", OpenMode::kAppend, &f).ok());
    log::Writer w(f.get(), f->Size());
    CHECK(w.AddRecord(std::string(5000, 'y')).ok());  // must respect the existing block offset
    CHECK(f->Close().ok());
    CountingReporter rep;
    auto got = ReadAll(d / "a.log", &rep);
    CHECK_EQ(got.size(), 2u);
    CHECK(got[1] == std::string(5000, 'y'));
    CHECK_EQ(rep.reports, 0);
}
