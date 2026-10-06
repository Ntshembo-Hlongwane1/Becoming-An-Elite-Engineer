// D1 — Env. Notes Part 1 §7–§8, Part 2 §10.
#include <unistd.h>

#include <random>

#include "dhelpers.hpp"
#include "dlsm/env.hpp"
#include "minitest.hpp"

using namespace dlsm;

TEST(env_writable_file_buffering_and_size) {
    dtest::TempDir d;
    std::unique_ptr<WritableFile> f;
    CHECK(WritableFile::Open(d / "a", OpenMode::kCreateNew, &f).ok());
    std::mt19937_64 rng(1);
    std::string expect;
    for (int i = 0; i < 500; ++i) {  // ~250 KB: crosses the 64 KiB buffer several times
        std::string chunk = testutil::RandomBytes(rng, rng() % 1000);
        CHECK(f->Append(chunk).ok());
        expect += chunk;
    }
    CHECK_EQ(f->Size(), expect.size());
    CHECK(f->Close().ok());
    CHECK(f->Close().ok());  // idempotent
    CHECK(dtest::Slurp(d / "a") == expect);
    std::unique_ptr<WritableFile> g;
    CHECK(!WritableFile::Open(d / "a", OpenMode::kCreateNew, &g).ok());  // O_EXCL: never clobber
    CHECK(WritableFile::Open(d / "a", OpenMode::kAppend, &g).ok());
    CHECK_EQ(g->Size(), expect.size());
    CHECK(g->Append("tail").ok());
    CHECK(g->Sync().ok());
    CHECK(dtest::Slurp(d / "a") == expect + "tail");  // Sync implies Flush
}

TEST(env_flush_makes_data_visible_to_other_readers) {
    dtest::TempDir d;
    std::unique_ptr<WritableFile> f;
    CHECK(WritableFile::Open(d / "f", OpenMode::kTruncate, &f).ok());
    CHECK(f->Append("hello").ok());
    CHECK(f->Flush().ok());
    CHECK(dtest::Slurp(d / "f") == "hello");  // in the page cache now, not just our buffer
}

TEST(env_sync_counts_fsyncs) {
    dtest::TempDir d;
    std::unique_ptr<WritableFile> f;
    CHECK(WritableFile::Open(d / "s", OpenMode::kTruncate, &f).ok());
    const auto before = GlobalIoStats().fsyncs.load();
    CHECK(f->Append("x").ok());
    CHECK(f->Sync().ok());
    CHECK(SyncDir(d.path()).ok());
    CHECK(GlobalIoStats().fsyncs.load() - before >= 2);
    CHECK(!SyncDir(d / "does-not-exist").ok());
}

TEST(env_random_access_pread) {
    dtest::TempDir d;
    std::string data;
    for (int i = 0; i < 10000; ++i) data += static_cast<char>('a' + i % 26);
    dtest::Spit(d / "r", data);
    std::unique_ptr<RandomAccessFile> r;
    CHECK(RandomAccessFile::Open(d / "r", &r).ok());
    CHECK_EQ(r->Size(), data.size());
    char scratch[200];
    std::string_view out;
    const auto before = GlobalIoStats().preads.load();
    CHECK(r->Read(4096, 100, scratch, &out).ok());
    CHECK(out == std::string_view(data).substr(4096, 100));
    CHECK(r->Read(9950, 200, scratch, &out).ok());  // short read at EOF
    CHECK_EQ(out.size(), 50u);
    CHECK(r->Read(20000, 10, scratch, &out).ok());
    CHECK(out.empty());
    CHECK(GlobalIoStats().preads.load() - before >= 2);
}

TEST(env_sequential_read_and_skip) {
    dtest::TempDir d;
    dtest::Spit(d / "s", "0123456789abcdef");
    std::unique_ptr<SequentialFile> s;
    CHECK(SequentialFile::Open(d / "s", &s).ok());
    char buf[8];
    std::string_view out;
    CHECK(s->Read(4, buf, &out).ok());
    CHECK(out == "0123");
    CHECK(s->Skip(6).ok());
    CHECK(s->Read(8, buf, &out).ok());
    CHECK(out == "abcdef");
    CHECK(s->Read(8, buf, &out).ok());
    CHECK(out.empty());
}

TEST(env_rename_atomically_replaces) {
    dtest::TempDir d;
    CHECK(WriteStringToFileSync(d / "CURRENT", "old\n").ok());
    CHECK(WriteStringToFileSync(d / "tmp", "new\n").ok());
    CHECK(RenameFile(d / "tmp", d / "CURRENT").ok());
    std::string s;
    CHECK(ReadFileToString(d / "CURRENT", &s).ok());
    CHECK(s == "new\n");
    CHECK(!FileExists(d / "tmp"));
    std::vector<std::string> kids;
    CHECK(GetChildren(d.path(), &kids).ok());
    CHECK(kids == std::vector<std::string>{"CURRENT"});
    std::uint64_t size = 0;
    CHECK(GetFileSize(d / "CURRENT", &size).ok());
    CHECK_EQ(size, 4u);
    CHECK(RemoveFile(d / "CURRENT").ok());
    CHECK(!FileExists(d / "CURRENT"));
    CHECK(CreateDirIfMissing(d / "sub").ok());
    CHECK(CreateDirIfMissing(d / "sub").ok());
}

TEST(env_file_lock_is_exclusive) {
    dtest::TempDir d;
    std::unique_ptr<FileLock> a, b;
    CHECK(FileLock::Acquire(d / "LOCK", &a).ok());
    CHECK(!FileLock::Acquire(d / "LOCK", &b).ok());  // flock conflicts across open file descriptions
    a.reset();
    CHECK(FileLock::Acquire(d / "LOCK", &b).ok());
}

TEST(env_no_fd_leaks) {
    dtest::TempDir d;
    dtest::Spit(d / "x", "data");
    const auto before = dtest::OpenFdCount();
    for (int i = 0; i < 300; ++i) {
        std::unique_ptr<RandomAccessFile> r;
        CHECK(RandomAccessFile::Open(d / "x", &r).ok());
        std::unique_ptr<WritableFile> w;
        CHECK(WritableFile::Open(d / "w", OpenMode::kTruncate, &w).ok());
        std::unique_ptr<SequentialFile> s;
        CHECK(SequentialFile::Open(d / "x", &s).ok());
        Fd moved(::dup(0));
        Fd other = std::move(moved);
    }
    CHECK_EQ(dtest::OpenFdCount(), before);  // your master-file-manager bug, made impossible
}
