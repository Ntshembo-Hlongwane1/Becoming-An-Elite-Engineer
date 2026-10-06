// D5 — The on-disk engine. FORMAT.md §6–§7, notes Part 5 + Part 6 §10–§11.
#include <map>
#include <random>

#include "dhelpers.hpp"
#include "dlsm/db.hpp"
#include "dlsm/filename.hpp"
#include "minitest.hpp"

using namespace dlsm;

namespace {
DiskOptions Small() {
    DiskOptions o;
    o.memtable_bytes = 16 * 1024;
    o.l0_compaction_trigger = 3;
    o.level_ratio = 4;
    o.level1_bytes = 64 * 1024;
    o.target_file_bytes = 32 * 1024;
    o.num_levels = 5;
    o.table.block_size = 1024;
    return o;
}
std::unique_ptr<DiskDB> MustOpen(const DiskOptions& o, const std::string& dir) {
    std::unique_ptr<DiskDB> db;
    Status s = DiskDB::Open(o, dir, &db);
    if (!s.ok()) std::printf("         open failed: %s\n", s.ToString().c_str());
    CHECK(s.ok());
    return db;
}
std::map<std::string, std::string> Scan(DiskDB& db) {
    std::map<std::string, std::string> out;
    auto it = db.NewIterator({});
    for (it->SeekToFirst(); it->Valid(); it->Next()) out.emplace(std::string(it->key()), std::string(it->value()));
    CHECK(it->status().ok());
    return out;
}
// Only the files FORMAT.md §1 allows, and every live table exists.
void CheckDirectory(DiskDB& db, const std::string& dir) {
    auto live = db.DebugLiveFiles();
    int manifests = 0;
    for (auto& name : dtest::ListDir(dir)) {
        std::uint64_t n;
        FileType t;
        CHECK(ParseFileName(name, &n, &t));
        if (t == FileType::kManifest) ++manifests;
        if (t == FileType::kTable) CHECK(std::find(live.begin(), live.end(), name) != live.end());
        CHECK(t != FileType::kTemp);
    }
    CHECK_EQ(manifests, 1);
    for (auto& name : live) CHECK(FileExists(dir + "/" + name));
}
}  // namespace

TEST(db_open_put_reopen_from_wal) {
    dtest::TempDir d;
    {
        auto db = MustOpen(DiskOptions{}, d.path());
        CHECK(db->Put({}, "a", "1").ok());
        CHECK(db->Put({}, "b", "2").ok());
        CHECK(db->Delete({}, "a").ok());
    }  // no flush: data only in the WAL
    auto db = MustOpen(DiskOptions{}, d.path());
    std::string v;
    CHECK(db->Get({}, "a", &v).IsNotFound());
    CHECK(db->Get({}, "b", &v).ok());
    CHECK(v == "2");
    CheckDirectory(*db, d.path());
}

TEST(db_lock_and_open_flags) {
    dtest::TempDir d;
    auto db = MustOpen(DiskOptions{}, d.path());
    std::unique_ptr<DiskDB> second;
    CHECK(!DiskDB::Open(DiskOptions{}, d.path(), &second).ok());  // LOCK held
    db.reset();
    DiskOptions o;
    o.error_if_exists = true;
    CHECK(!DiskDB::Open(o, d.path(), &second).ok());
    o = DiskOptions{};
    o.create_if_missing = false;
    CHECK(!DiskDB::Open(o, d / "nope", &second).ok());
}

TEST(db_flush_compact_reopen) {
    dtest::TempDir d;
    const auto o = Small();
    {
        auto db = MustOpen(o, d.path());
        for (int i = 0; i < 6000; ++i) CHECK(db->Put({}, testutil::KeyN(i % 2000), "v" + std::to_string(i)).ok());
        CHECK(db->FlushMemTable().ok());
        CheckDirectory(*db, d.path());
        auto levels = db->DebugLevelUserRanges();
        for (std::size_t l = 1; l < levels.size(); ++l)
            for (std::size_t i = 1; i < levels[l].size(); ++i) CHECK(levels[l][i - 1].second < levels[l][i].first);
    }
    {
        auto db = MustOpen(o, d.path());
        std::string v;
        for (int k = 0; k < 2000; ++k) {
            CHECK(db->Get({}, testutil::KeyN(k), &v).ok());
            CHECK(v == "v" + std::to_string(4000 + k));
        }
        CHECK(db->CompactAll().ok());
        CheckDirectory(*db, d.path());
    }
    auto db = MustOpen(o, d.path());
    CHECK_EQ(Scan(*db).size(), 2000u);
}

TEST(db_recovery_deletes_orphans) {
    dtest::TempDir d;
    { auto db = MustOpen(Small(), d.path()); CHECK(db->Put({}, "k", "v").ok()); CHECK(db->FlushMemTable().ok()); }
    dtest::Spit(d / "999999.sst", "garbage left by a crashed flush");
    dtest::Spit(d / "999998.dbtmp", "MANIFEST-999998\n");
    auto db = MustOpen(Small(), d.path());
    CHECK(!FileExists(d / "999999.sst"));
    CHECK(!FileExists(d / "999998.dbtmp"));
    std::string v;
    CHECK(db->Get({}, "k", &v).ok());
    CheckDirectory(*db, d.path());
}

TEST(db_torn_wal_tail_is_tolerated) {
    dtest::TempDir d;
    { auto db = MustOpen(DiskOptions{}, d.path()); for (int i = 0; i < 100; ++i) CHECK(db->Put({}, testutil::KeyN(i), "v").ok()); }
    std::string newest;
    std::uint64_t best = 0;
    for (auto& name : dtest::ListDir(d.path())) {
        std::uint64_t n;
        FileType t;
        if (ParseFileName(name, &n, &t) && t == FileType::kLog && n >= best) { best = n; newest = name; }
    }
    CHECK(!newest.empty());
    std::string wal = dtest::Slurp(d / newest);
    wal += std::string("\x12\x34\x56\x78\xff\x00\x01", 7) + "partial";  // header claims 255 bytes; only 7 follow
    dtest::Spit(d / newest, wal);
    auto db = MustOpen(DiskOptions{}, d.path());
    std::string v;
    for (int i = 0; i < 100; ++i) CHECK(db->Get({}, testutil::KeyN(i), &v).ok());
}

TEST(db_corrupt_table_reports_corruption_not_wrong_data) {
    dtest::TempDir d;
    auto o = Small();
    {
        auto db = MustOpen(o, d.path());
        for (int i = 0; i < 500; ++i) CHECK(db->Put({}, testutil::KeyN(i), std::string(30, 'q')).ok());
        CHECK(db->CompactAll().ok());
    }
    std::string table;
    for (auto& name : dtest::ListDir(d.path())) if (name.size() > 4 && name.substr(name.size() - 4) == ".sst") table = name;
    std::string bytes = dtest::Slurp(d / table);
    bytes[100] ^= 0x55;  // inside the first data block (header is 64 bytes)
    dtest::Spit(d / table, bytes);
    auto db = MustOpen(o, d.path());
    int corrupt = 0;
    std::string v;
    for (int i = 0; i < 500; ++i) {
        Status s = db->Get({}, testutil::KeyN(i), &v);
        if (s.IsCorruption()) ++corrupt;
        else { CHECK(s.ok()); CHECK(v == std::string(30, 'q')); }
    }
    CHECK(corrupt > 0);
}

TEST(db_oracle_with_reopens) {
    dtest::TempDir d;
    const auto o = Small();
    std::map<std::string, std::string> oracle;
    std::mt19937_64 rng(11);
    auto db = MustOpen(o, d.path());
    for (int op = 0; op < 20000; ++op) {
        auto r = rng() % 1000;
        std::string k = testutil::KeyN(rng() % 1000, 4);
        if (r < 600) {
            std::string v = testutil::RandomBytes(rng, rng() % 80);
            CHECK(db->Put({}, k, v).ok());
            oracle[k] = v;
        } else if (r < 800) {
            CHECK(db->Delete({}, k).ok());
            oracle.erase(k);
        } else if (r < 995) {
            std::string v;
            Status s = db->Get({}, k, &v);
            auto it = oracle.find(k);
            if (it == oracle.end()) CHECK(s.IsNotFound());
            else { CHECK(s.ok()); CHECK(v == it->second); }
        } else if (r < 998) {
            db.reset();
            db = MustOpen(o, d.path());
        } else {
            CHECK(db->CompactAll().ok());
        }
    }
    CHECK(Scan(*db) == oracle);
    CheckDirectory(*db, d.path());
    db.reset();
    db = MustOpen(o, d.path());
    CHECK(Scan(*db) == oracle);
}
