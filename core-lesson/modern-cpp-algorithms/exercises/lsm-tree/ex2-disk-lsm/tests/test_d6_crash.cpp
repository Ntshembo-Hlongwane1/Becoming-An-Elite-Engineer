// D6 — Crash safety with real processes: fork a writer, SIGKILL it at a random moment, reopen.
// A SIGKILL kills the PROCESS; the kernel's page cache survives (notes Part 1 §6), so this tests your
// ordering of write()/rename()/MANIFEST commits and recovery logic. It does NOT simulate power loss
// (that needs fault injection below the page cache — see README "Challenge D7").
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>

#include <random>

#include "dhelpers.hpp"
#include "dlsm/db.hpp"
#include "minitest.hpp"

using namespace dlsm;

namespace {
std::string ValueFor(std::uint64_t i) { return "value-" + std::to_string(i) + std::string(i % 50, '#'); }

// Child: open, write keys start..∞, report each acknowledged index through the pipe. Never returns.
[[noreturn]] void ChildWriter(const std::string& dir, std::uint64_t start, int ack_fd, bool sync) {
    DiskOptions o;
    o.memtable_bytes = 8 * 1024;  // flush constantly => crashes land inside flushes and compactions
    o.l0_compaction_trigger = 2;
    o.level_ratio = 3;
    o.level1_bytes = 32 * 1024;
    o.target_file_bytes = 16 * 1024;
    o.num_levels = 4;
    o.table.block_size = 512;
    std::unique_ptr<DiskDB> db;
    if (!DiskDB::Open(o, dir, &db).ok()) _exit(3);
    for (std::uint64_t i = start;; ++i) {
        WriteOptions wo;
        wo.sync = sync;
        if (!db->Put(wo, testutil::KeyN(i), ValueFor(i)).ok()) _exit(4);
        if (::write(ack_fd, &i, sizeof i) != sizeof i) _exit(5);
    }
}

void CrashRounds(bool sync) {
    dtest::TempDir d;
    std::mt19937_64 rng(sync ? 1 : 2);
    std::uint64_t next = 0;  // first key the next child writes
    std::vector<std::pair<std::uint64_t, std::uint64_t>> acked;  // [begin, end) ranges acknowledged so far
    for (int round = 0; round < 8; ++round) {
        int fds[2];
        CHECK(::pipe(fds) == 0);
        pid_t pid = ::fork();
        CHECK(pid >= 0);
        if (pid == 0) {
            ::close(fds[0]);
            try {
                ChildWriter(d.path(), next, fds[1], sync);
            } catch (const lsm::NotImplemented&) {
                _exit(42);  // the child must NEVER return into the test runner
            } catch (...) {
                _exit(6);
            }
        }
        ::close(fds[1]);
        const std::uint64_t target = 200 + rng() % 1500;
        std::uint64_t acked_upto = next, got = 0, value = 0;
        while (got < target && ::read(fds[0], &value, sizeof value) == sizeof value) {
            acked_upto = value + 1;
            ++got;
        }
        ::kill(pid, SIGKILL);
        int status = 0;
        ::waitpid(pid, &status, 0);
        ::close(fds[0]);
        if (WIFEXITED(status) && WEXITSTATUS(status) == 42) lsm::Todo("DiskDB (crash test child)");
        CHECK(WIFSIGNALED(status));  // the child must not have died on its own (exit codes 3/4/5/6)

        DiskOptions o;
        o.memtable_bytes = 8 * 1024;
        std::unique_ptr<DiskDB> db;
        Status s = DiskDB::Open(o, d.path(), &db);
        if (!s.ok()) std::printf("         reopen after crash failed: %s\n", s.ToString().c_str());
        CHECK(s.ok());
        acked.emplace_back(next, acked_upto);
        std::string v;
        for (auto [b, e] : acked) {  // every acknowledged write, from every round, survives
            for (std::uint64_t i = b; i < e; ++i) {
                Status g = db->Get({}, testutil::KeyN(i), &v);
                if (!g.ok()) std::printf("         round %d: acked key %llu lost (%s)\n", round,
                                         static_cast<unsigned long long>(i), g.ToString().c_str());
                CHECK(g.ok());
                CHECK(v == ValueFor(i));
            }
        }
        for (std::uint64_t i = acked_upto; i < acked_upto + 50; ++i) {  // unacked: absent or correct
            Status g = db->Get({}, testutil::KeyN(i), &v);
            if (g.ok()) CHECK(v == ValueFor(i));
            else CHECK(g.IsNotFound());
        }
        next = acked_upto + 50;  // never rewrite a key whose fate is unknown
    }
}
}  // namespace

TEST(crash_sigkill_with_sync_writes) { CrashRounds(true); }
TEST(crash_sigkill_with_async_writes) { CrashRounds(false); }  // LevelDB doc: process crash loses nothing
