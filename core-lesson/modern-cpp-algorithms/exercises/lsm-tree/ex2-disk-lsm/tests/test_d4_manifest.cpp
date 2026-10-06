// D4 — File names, CURRENT, VersionEdit, MANIFEST replay. FORMAT.md §1, §5–§7; notes Part 6 §9–§11.
#include "dhelpers.hpp"
#include "dlsm/filename.hpp"
#include "dlsm/version.hpp"
#include "lsm/internal_key.hpp"
#include "minitest.hpp"

using namespace dlsm;
using lsm::MakeInternalKey;
using lsm::ValueType;

namespace {
FileMetaData Meta(std::uint64_t n, const std::string& lo, const std::string& hi) {
    return FileMetaData{n, 1000 + n, MakeInternalKey(lo, 1, ValueType::kValue), MakeInternalKey(hi, 1, ValueType::kValue)};
}
bool SameState(const DbState& a, const DbState& b) {
    if (a.comparator != b.comparator || a.log_number != b.log_number || a.next_file_number != b.next_file_number ||
        a.last_sequence != b.last_sequence || a.levels.size() != b.levels.size())
        return false;
    for (std::size_t l = 0; l < a.levels.size(); ++l) {
        if (a.levels[l].size() != b.levels[l].size()) return false;
        for (std::size_t i = 0; i < a.levels[l].size(); ++i) {
            const auto &x = a.levels[l][i], &y = b.levels[l][i];
            if (x.number != y.number || x.file_size != y.file_size || x.smallest != y.smallest || x.largest != y.largest) return false;
        }
    }
    return a.compact_pointers == b.compact_pointers;
}
}  // namespace

TEST(filenames) {
    CHECK(LogFileName("/db", 7) == "/db/000007.log");
    CHECK(TableFileName("/db", 123456) == "/db/123456.sst");
    CHECK(TableFileName("/db", 1234567) == "/db/1234567.sst");
    CHECK(ManifestFileName("/db", 5) == "/db/MANIFEST-000005");
    CHECK(TempFileName("/db", 5) == "/db/000005.dbtmp");
    CHECK(CurrentFileName("/db") == "/db/CURRENT");
    CHECK(LockFileName("/db") == "/db/LOCK");
    std::uint64_t n = 0;
    FileType t;
    CHECK(ParseFileName("000007.log", &n, &t) && n == 7 && t == FileType::kLog);
    CHECK(ParseFileName("123.sst", &n, &t) && n == 123 && t == FileType::kTable);
    CHECK(ParseFileName("MANIFEST-000005", &n, &t) && n == 5 && t == FileType::kManifest);
    CHECK(ParseFileName("000005.dbtmp", &n, &t) && n == 5 && t == FileType::kTemp);
    CHECK(ParseFileName("CURRENT", &n, &t) && t == FileType::kCurrent);
    CHECK(ParseFileName("LOCK", &n, &t) && t == FileType::kLock);
    for (const char* bad : {"", "foo", "000007.log.bak", "MANIFEST-", "MANIFEST-12x", ".log", "12.sstx",
                            "99999999999999999999999.log", "CURRENT.tmp", "-1.log"})
        CHECK(!ParseFileName(bad, &n, &t));
}

TEST(set_current_file_is_atomic_and_clean) {
    dtest::TempDir d;
    CHECK(SetCurrentFile(d.path(), 5).ok());
    CHECK(dtest::Slurp(d / "CURRENT") == "MANIFEST-000005\n");
    CHECK(SetCurrentFile(d.path(), 6).ok());
    CHECK(dtest::Slurp(d / "CURRENT") == "MANIFEST-000006\n");
    CHECK(dtest::ListDir(d.path()) == std::vector<std::string>{"CURRENT"});  // no temp left behind
}

TEST(version_edit_roundtrip_and_rejects_garbage) {
    VersionEdit e;
    e.comparator = kComparatorName;
    e.log_number = 12;
    e.next_file_number = 40;
    e.last_sequence = 999999;
    e.compact_pointers.emplace_back(2, MakeInternalKey("m", 5, ValueType::kValue));
    e.deleted_files.emplace_back(1, 17);
    e.new_files.emplace_back(2, Meta(33, "a", "f"));
    std::string s;
    e.EncodeTo(&s);
    CHECK(static_cast<unsigned char>(s[0]) == 1);  // first field is the comparator tag
    VersionEdit g;
    CHECK(g.DecodeFrom(s).ok());
    CHECK(g.comparator == e.comparator && g.log_number == e.log_number && g.next_file_number == e.next_file_number);
    CHECK(g.last_sequence == e.last_sequence && g.compact_pointers == e.compact_pointers && g.deleted_files == e.deleted_files);
    CHECK_EQ(g.new_files.size(), 1u);
    CHECK(g.new_files[0].second.smallest == e.new_files[0].second.smallest);
    CHECK_EQ(g.new_files[0].second.number, 33u);
    for (std::size_t cut = 1; cut < s.size(); ++cut) {
        VersionEdit h;
        Status st = h.DecodeFrom(std::string_view(s).substr(0, cut));
        (void)st;  // some prefixes are valid edits; none may crash (ASan)
    }
    std::string unknown;
    unknown.push_back(static_cast<char>(99));
    VersionEdit h;
    CHECK(h.DecodeFrom(unknown).IsCorruption());
    std::string trunc = s.substr(0, s.size() - 3);  // inside the last length-prefixed key
    CHECK(h.DecodeFrom(trunc).IsCorruption());
}

TEST(db_state_apply_and_snapshot) {
    DbState st(4);
    VersionEdit e1;
    e1.comparator = kComparatorName;
    e1.new_files.emplace_back(0, Meta(5, "a", "z"));
    e1.new_files.emplace_back(0, Meta(9, "b", "y"));
    e1.new_files.emplace_back(1, Meta(7, "m", "p"));
    e1.new_files.emplace_back(1, Meta(6, "a", "f"));
    e1.next_file_number = 10;
    CHECK(st.Apply(e1).ok());
    CHECK_EQ(st.levels[0].front().number, 9u);  // L0 newest first
    CHECK_EQ(st.levels[1].front().number, 6u);  // L1 by smallest key
    VersionEdit bad;
    bad.deleted_files.emplace_back(2, 123);
    CHECK(st.Apply(bad).IsCorruption());
    VersionEdit dup;
    dup.new_files.emplace_back(2, Meta(5, "q", "r"));
    CHECK(st.Apply(dup).IsCorruption());
    VersionEdit out_of_range;
    out_of_range.new_files.emplace_back(9, Meta(11, "q", "r"));
    CHECK(st.Apply(out_of_range).IsCorruption());
    DbState fresh(4);
    CHECK(fresh.Apply(st.Snapshot()).ok());
    CHECK(SameState(st, fresh));
}

TEST(manifest_create_append_recover) {
    dtest::TempDir d;
    DbState st(4);
    st.comparator = kComparatorName;
    st.next_file_number = 3;
    std::unique_ptr<ManifestWriter> mw;
    CHECK(ManifestWriter::Create(d.path(), 2, st, &mw).ok());
    CHECK(dtest::Slurp(d / "CURRENT") == "MANIFEST-000002\n");
    for (std::uint64_t n = 3; n < 30; ++n) {
        VersionEdit e;
        e.new_files.emplace_back(static_cast<int>(n % 3), Meta(n, "k" + std::to_string(n), "k" + std::to_string(n) + "z"));
        e.next_file_number = n + 1;
        e.last_sequence = n * 10;
        e.log_number = n;
        if (n % 5 == 0) e.deleted_files.emplace_back(static_cast<int>((n - 1) % 3), n - 1);
        CHECK(mw->Append(e).ok());
        CHECK(st.Apply(e).ok());
    }
    mw.reset();
    DbState rec(4);
    std::uint64_t mnum = 0;
    CHECK(RecoverDbState(d.path(), 4, &rec, &mnum).ok());
    CHECK_EQ(mnum, 2u);
    CHECK(SameState(st, rec));
}

TEST(manifest_torn_tail_and_bad_current) {
    dtest::TempDir d;
    DbState st(4);
    st.comparator = kComparatorName;
    std::unique_ptr<ManifestWriter> mw;
    CHECK(ManifestWriter::Create(d.path(), 2, st, &mw).ok());
    VersionEdit e1;
    e1.new_files.emplace_back(0, Meta(3, "a", "b"));
    CHECK(mw->Append(e1).ok());
    VersionEdit e2;
    e2.new_files.emplace_back(0, Meta(4, "c", "d"));
    CHECK(mw->Append(e2).ok());
    mw.reset();
    std::string m = dtest::Slurp(d / "MANIFEST-000002");
    dtest::Spit(d / "MANIFEST-000002", m.substr(0, m.size() - 3));  // crash while appending e2
    DbState rec(4);
    std::uint64_t mnum = 0;
    CHECK(RecoverDbState(d.path(), 4, &rec, &mnum).ok());
    CHECK_EQ(rec.levels[0].size(), 1u);
    CHECK_EQ(rec.levels[0][0].number, 3u);
    dtest::Spit(d / "CURRENT", "MANIFEST-000077\n");                 // points nowhere
    CHECK(!RecoverDbState(d.path(), 4, &rec, &mnum).ok());
    dtest::Spit(d / "CURRENT", "MANIFEST-000002");                   // missing newline
    CHECK(!RecoverDbState(d.path(), 4, &rec, &mnum).ok());
    dtest::Spit(d / "CURRENT", "../etc/passwd\n");                   // not a manifest name
    CHECK(!RecoverDbState(d.path(), 4, &rec, &mnum).ok());
}

TEST(manifest_comparator_mismatch_refused) {
    dtest::TempDir d;
    DbState st(4);
    st.comparator = "someone.elses.comparator";
    std::unique_ptr<ManifestWriter> mw;
    CHECK(ManifestWriter::Create(d.path(), 2, st, &mw).ok());
    mw.reset();
    DbState rec(4);
    std::uint64_t mnum = 0;
    CHECK(!RecoverDbState(d.path(), 4, &rec, &mnum).ok());
}
