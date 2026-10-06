// M9/M10 — The LSM. Notes Part 5.
#include <map>
#include <memory>
#include <random>
#include <string>
#include <vector>

#include "helpers.hpp"
#include "lsm/memdb.hpp"
#include "minitest.hpp"

using namespace lsm;

namespace {
Options SmallOptions() {
    Options o;
    o.memtable_bytes = 4 * 1024;  // tiny => many flushes and compactions
    o.l0_compaction_trigger = 3;
    o.level_ratio = 4;
    o.level1_bytes = 16 * 1024;
    o.target_run_bytes = 8 * 1024;
    o.num_levels = 5;
    o.run = RunOptions{512, 8, 10};
    return o;
}
std::map<std::string, std::string> Scan(MemDB& db, const ReadOptions& ro) {
    std::map<std::string, std::string> out;
    auto it = db.NewIterator(ro);
    for (it->SeekToFirst(); it->Valid(); it->Next()) out.emplace(std::string(it->key()), std::string(it->value()));
    CHECK(it->status().ok());
    return out;
}
void CheckLevelInvariants(MemDB& db, const Options& o) {
    auto levels = db.DebugLevelUserRanges();
    CHECK_EQ(levels.size(), static_cast<std::size_t>(o.num_levels));
    for (std::size_t l = 1; l < levels.size(); ++l) {
        for (std::size_t i = 0; i < levels[l].size(); ++i) {
            CHECK(levels[l][i].first <= levels[l][i].second);
            if (i > 0) CHECK(levels[l][i - 1].second < levels[l][i].first);  // L>=1: no overlap
        }
    }
}
}  // namespace

TEST(memdb_basic) {
    MemDB db;
    std::string v;
    CHECK(db.Get({}, "a", &v).IsNotFound());
    db.Put("a", "1");
    db.Put("b", "2");
    db.Put("a", "3");
    CHECK(db.Get({}, "a", &v).ok());
    CHECK_EQ(v, std::string("3"));
    db.Delete("b");
    CHECK(db.Get({}, "b", &v).IsNotFound());
    CHECK_EQ(db.LastSequence(), 4u);
    WriteBatch wb;
    wb.Put("x", "1");
    wb.Delete("a");
    wb.Put("y", "2");
    db.Write(wb);
    CHECK_EQ(db.LastSequence(), 7u);  // one sequence number per op
    CHECK(db.Get({}, "a", &v).IsNotFound());
    CHECK(db.Get({}, "y", &v).ok());
}

TEST(memdb_survives_flush_and_compaction) {
    Options o = SmallOptions();
    MemDB db(o);
    for (int i = 0; i < 3000; ++i) db.Put(testutil::KeyN(i % 1000), "v" + std::to_string(i));
    db.FlushMemTable();
    CheckLevelInvariants(db, o);
    std::string v;
    for (int k = 0; k < 1000; ++k) {
        CHECK(db.Get({}, testutil::KeyN(k), &v).ok());
        CHECK_EQ(v, "v" + std::to_string(2000 + k));
    }
    db.CompactAll();
    CheckLevelInvariants(db, o);
    CHECK(db.Get({}, testutil::KeyN(999), &v).ok());
    CHECK_EQ(v, std::string("v2999"));
}

TEST(memdb_no_tombstone_resurrection) {
    Options o = SmallOptions();
    MemDB db(o);
    db.Put("victim", "secret");
    for (int i = 0; i < 2000; ++i) db.Put(testutil::KeyN(i), std::string(20, 'x'));
    db.CompactAll();               // "victim" now lives in a deep level
    db.Delete("victim");
    for (int i = 0; i < 2000; ++i) db.Put(testutil::KeyN(i), std::string(20, 'y'));  // push the tombstone down
    db.FlushMemTable();
    std::string v;
    CHECK(db.Get({}, "victim", &v).IsNotFound());
    db.CompactAll();
    CHECK(db.Get({}, "victim", &v).IsNotFound());
    CHECK(Scan(db, {}).count("victim") == 0);
}

TEST(memdb_snapshots_across_compaction) {
    Options o = SmallOptions();
    MemDB db(o);
    db.Put("k", "old");
    auto snap = db.GetSnapshot();
    db.Put("k", "new");
    db.Delete("gone");
    for (int i = 0; i < 3000; ++i) db.Put(testutil::KeyN(i), "filler");
    db.CompactAll();
    std::string v;
    CHECK(db.Get(ReadOptions{snap}, "k", &v).ok());
    CHECK_EQ(v, std::string("old"));
    CHECK(db.Get({}, "k", &v).ok());
    CHECK_EQ(v, std::string("new"));
    CHECK(db.Get(ReadOptions{snap}, testutil::KeyN(5), &v).IsNotFound());  // written after snapshot
    snap.reset();                                                          // release
    db.CompactAll();                                                       // "old" may now be dropped
    CHECK(db.Get({}, "k", &v).ok());
    CHECK_EQ(v, std::string("new"));
}

// The big one: random operations vs std::map oracles (latest + snapshots).
TEST(memdb_oracle_stress) {
    Options o = SmallOptions();
    MemDB db(o);
    std::map<std::string, std::string> latest;
    std::vector<std::pair<std::shared_ptr<const Snapshot>, std::map<std::string, std::string>>> snaps;
    std::mt19937_64 rng(2024);
    auto rkey = [&] { return testutil::KeyN(rng() % 1500, 5); };
    for (int op = 0; op < 40000; ++op) {
        const auto r = rng() % 1000;
        if (r < 600) {
            std::string k = rkey(), v = testutil::RandomBytes(rng, rng() % 64);
            db.Put(k, v);
            latest[k] = v;
        } else if (r < 800) {
            std::string k = rkey();
            db.Delete(k);
            latest.erase(k);
        } else if (r < 980) {
            std::string k = rkey(), v;
            Status s = db.Get({}, k, &v);
            auto it = latest.find(k);
            if (it == latest.end()) {
                CHECK(s.IsNotFound());
            } else {
                CHECK(s.ok());
                CHECK_EQ(v, it->second);
            }
            if (!snaps.empty()) {
                auto& [sp, smap] = snaps[rng() % snaps.size()];
                Status s2 = db.Get(ReadOptions{sp}, k, &v);
                auto it2 = smap.find(k);
                if (it2 == smap.end()) CHECK(s2.IsNotFound());
                else { CHECK(s2.ok()); CHECK_EQ(v, it2->second); }
            }
        } else if (r < 990) {
            snaps.emplace_back(db.GetSnapshot(), latest);
            if (snaps.size() > 4) snaps.erase(snaps.begin());  // release the oldest
        } else if (r < 997) {
            db.FlushMemTable();
        } else {
            db.CompactAll();
        }
    }
    CheckLevelInvariants(db, o);
    CHECK(Scan(db, {}) == latest);
    for (auto& [sp, smap] : snaps) CHECK(Scan(db, ReadOptions{sp}) == smap);
}

TEST(memdb_stats) {
    Options o = SmallOptions();
    MemDB db(o);
    for (int i = 0; i < 20000; ++i) db.Put(testutil::KeyN(i * 7919 % 20000), std::string(50, 'v'));
    db.FlushMemTable();
    Stats s = db.GetStats();
    std::printf("         WA=%.2f  flush=%llu  compaction_written=%llu  %s\n", s.WriteAmplification(),
                static_cast<unsigned long long>(s.flush_bytes_written),
                static_cast<unsigned long long>(s.compaction_bytes_written), db.DebugString().c_str());
    CHECK(s.user_bytes_written >= 20000ull * 58);
    CHECK(s.WriteAmplification() > 1.0);
    CHECK_EQ(s.runs_per_level.size(), static_cast<std::size_t>(o.num_levels));
    CHECK(s.runs_per_level[0] < static_cast<std::size_t>(o.l0_compaction_trigger));
    std::string v;
    for (int i = 0; i < 1000; ++i) (void)db.Get({}, testutil::KeyN(i), &v);
    Stats s2 = db.GetStats();
    CHECK_EQ(s2.gets - s.gets, 1000u);
    CHECK(s2.runs_probed > s.runs_probed);
}
