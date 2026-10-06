// M7 — In-memory SSTable. Notes Part 6 §4–§8.
#include <map>
#include <memory>
#include <random>
#include <string>
#include <vector>

#include "helpers.hpp"
#include "lsm/sorted_run.hpp"
#include "minitest.hpp"

using namespace lsm;

namespace {
// Builds entries for keys k00000000..k(n-1): up to 3 versions each, some deletions.
std::vector<std::pair<std::string, std::string>> MakeEntries(int n, std::mt19937_64& rng) {
    std::vector<std::pair<std::string, std::string>> out;
    SequenceNumber seq = 1000000;
    for (int i = 0; i < n; ++i) {
        int versions = 1 + static_cast<int>(rng() % 3);
        for (int v = 0; v < versions; ++v) {  // newest (largest seq) first
            ValueType t = (rng() % 6 == 0) ? ValueType::kDeletion : ValueType::kValue;
            std::string val = t == ValueType::kValue ? "val-" + std::to_string(i) + "-" + std::to_string(v) : "";
            out.emplace_back(MakeInternalKey(testutil::KeyN(static_cast<std::uint64_t>(i) * 3), seq - static_cast<SequenceNumber>(v), t), val);
        }
        seq -= 10;
    }
    return out;
}

std::shared_ptr<const SortedRun> Build(const std::vector<std::pair<std::string, std::string>>& entries,
                                       RunOptions opt, std::shared_ptr<const std::string>* bytes_out = nullptr) {
    SortedRunBuilder b(opt);
    for (auto& [k, v] : entries) b.Add(k, v);
    auto bytes = std::make_shared<std::string>();
    CHECK(b.Finish(bytes.get()).ok());
    CHECK_EQ(b.NumEntries(), entries.size());
    std::shared_ptr<const SortedRun> run;
    CHECK(SortedRun::Open(bytes, 7, &run).ok());
    if (bytes_out) *bytes_out = bytes;
    return run;
}
}  // namespace

TEST(sorted_run_roundtrip_iteration) {
    std::mt19937_64 rng(3);
    auto entries = MakeEntries(3000, rng);
    auto run = Build(entries, RunOptions{1024, 16, 10});
    CHECK_EQ(run->number(), 7u);
    CHECK_EQ(run->smallest(), std::string_view(entries.front().first));
    CHECK_EQ(run->largest(), std::string_view(entries.back().first));
    auto it = run->NewIterator();
    it->SeekToFirst();
    for (auto& [k, v] : entries) {
        CHECK(it->Valid());
        CHECK_EQ(it->key(), std::string_view(k));
        CHECK_EQ(it->value(), std::string_view(v));
        it->Next();
    }
    CHECK(!it->Valid());
    CHECK(it->status().ok());
    // Seek with an internal key lands on the first entry >= it
    it->Seek(MakeInternalKey(testutil::KeyN(31), kMaxSequenceNumber, kValueTypeForSeek));
    CHECK(it->Valid());
    CHECK_EQ(std::string(ExtractUserKey(it->key())), testutil::KeyN(33));
}

TEST(sorted_run_point_lookups_with_snapshots) {
    std::mt19937_64 rng(4);
    auto entries = MakeEntries(2000, rng);
    auto run = Build(entries, RunOptions{4096, 16, 10});
    // Oracle: for each user key, versions newest-first.
    std::map<std::string, std::vector<ParsedInternalKey>> versions;
    std::map<std::string, std::string> values;  // internal key -> value
    for (auto& [k, v] : entries) {
        ParsedInternalKey p;
        CHECK(ParseInternalKey(k, &p));
        versions[std::string(p.user_key)].push_back(p);
        values[k] = v;
    }
    for (auto& [ukey, vs] : versions) {
        for (SequenceNumber snap : {kMaxSequenceNumber, vs.back().sequence, vs.front().sequence - 1}) {
            LookupResult want = LookupResult::kNotPresent;
            std::string want_val;
            for (auto& p : vs) {
                if (p.sequence <= snap) {
                    want = p.type == ValueType::kValue ? LookupResult::kFound : LookupResult::kDeleted;
                    want_val = values[MakeInternalKey(ukey, p.sequence, p.type)];
                    break;
                }
            }
            LookupResult got;
            std::string val;
            CHECK(run->Get(ukey, snap, &got, &val).ok());
            CHECK(got == want);
            if (want == LookupResult::kFound) CHECK_EQ(val, want_val);
        }
    }
}

TEST(sorted_run_bloom_avoids_block_reads) {
    std::mt19937_64 rng(5);
    auto entries = MakeEntries(2000, rng);
    auto run = Build(entries, RunOptions{4096, 16, 10});
    const auto blocks_before = run->data_blocks_read();
    int absent = 0;
    for (int i = 0; i < 2000; ++i) {
        LookupResult r;
        std::string v;
        // KeyN(3i+1) never exists (keys are multiples of 3)
        CHECK(run->Get(testutil::KeyN(static_cast<std::uint64_t>(i) * 3 + 1), kMaxSequenceNumber, &r, &v).ok());
        CHECK(r == LookupResult::kNotPresent);
        ++absent;
    }
    const auto reads = run->data_blocks_read() - blocks_before;
    std::printf("         absent lookups: %d, data blocks read: %llu, bloom negatives: %llu\n", absent,
                static_cast<unsigned long long>(reads), static_cast<unsigned long long>(run->bloom_negatives()));
    CHECK(reads < static_cast<std::uint64_t>(absent) / 20);  // filter must stop >95% of them
    // and a present key costs at most one data block
    const auto before = run->data_blocks_read();
    LookupResult r;
    std::string v;
    CHECK(run->Get(testutil::KeyN(300), kMaxSequenceNumber, &r, &v).ok());
    CHECK(run->data_blocks_read() - before <= 1);
}

TEST(sorted_run_detects_every_single_byte_corruption) {
    std::mt19937_64 rng(6);
    auto entries = MakeEntries(60, rng);
    std::shared_ptr<const std::string> good;
    Build(entries, RunOptions{256, 4, 10}, &good);
    for (std::size_t pos = 0; pos < good->size(); ++pos) {
        auto bad = std::make_shared<std::string>(*good);
        (*bad)[pos] = static_cast<char>((*bad)[pos] ^ 0x40);
        std::shared_ptr<const SortedRun> run;
        if (!SortedRun::Open(bad, 1, &run).ok()) continue;  // detected at open: fine
        // Otherwise a full scan must either report corruption or return exactly the original data
        // (e.g. the flip hit footer padding). It must never return different data with OK status.
        auto it = run->NewIterator();
        std::size_t i = 0;
        bool same = true;
        for (it->SeekToFirst(); it->Valid(); it->Next(), ++i) {
            if (i >= entries.size() || it->key() != entries[i].first || it->value() != entries[i].second) {
                same = false;
                break;
            }
        }
        if (it->status().ok() && same) CHECK_EQ(i, entries.size());
        else if (it->status().ok()) CHECK(false && "silent corruption: different data with OK status");
    }
}

TEST(sorted_run_rejects_non_tables) {
    std::shared_ptr<const SortedRun> run;
    CHECK(!SortedRun::Open(std::make_shared<std::string>(""), 1, &run).ok());
    CHECK(!SortedRun::Open(std::make_shared<std::string>(std::string(100, 'x')), 1, &run).ok());
}
