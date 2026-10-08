// Exercise 5 — DirectFile + DirectAppender. Notes Part 5, Part 6, Part 7 §4.
//
// IMPORTANT (Part 5 §6): these tests create files under the CURRENT DIRECTORY. Run them from a real
// disk filesystem (the build dir inside the repo is ext4 on your VM). On tmpfs (/tmp) the kernel
// accepts misaligned O_DIRECT, so the "kernel agrees" test is skipped and the rest prove less.
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <random>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

#include "am/aligned_buffer.hpp"
#include "am/direct_file.hpp"
#include "minitest.hpp"

using namespace am;
namespace fs = std::filesystem;

namespace {

// RAII temp directory under the current directory (NOT /tmp — see header comment).
struct TempDir {
    fs::path path;
    TempDir() {
        std::string tmpl = (fs::current_path() / "am_test_XXXXXX").string();
        if (::mkdtemp(tmpl.data()) == nullptr) throw std::system_error(errno, std::generic_category(), "mkdtemp");
        path = tmpl;
    }
    ~TempDir() {
        std::error_code ec;
        fs::remove_all(path, ec);
    }
    std::string file(const char* name) const { return (path / name).string(); }
};

std::size_t OpenFdCount() {
    std::size_t n = 0;
    for ([[maybe_unused]] auto& e : fs::directory_iterator("/proc/self/fd")) ++n;
    return n;
}

std::string ReadWhole(const std::string& p) {
    std::ifstream in(p, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

std::string RandomBytes(std::size_t n, unsigned seed) {
    std::mt19937 rng(seed);
    std::string s(n, '\0');
    for (auto& c : s) c = static_cast<char>(rng());
    return s;
}

constexpr int kCreate = O_RDWR | O_CREAT | O_TRUNC;

}  // namespace

// ---- discovery -------------------------------------------------------------------------------

TEST(ex5_query_alignment_is_sane) {
    TempDir d;
    auto f = DirectFile::Open(d.file("a.bin"), kCreate, IoMode::kBuffered);
    DioAlignment a = f.alignment();
    CHECK(a.memory > 0 && (a.memory & (a.memory - 1)) == 0);
    CHECK(a.offset > 0 && (a.offset & (a.offset - 1)) == 0);
    if (!a.reported) {
        CHECK_EQ(a.memory, kFallbackDioAlignment);
        CHECK_EQ(a.offset, kFallbackDioAlignment);
    }
    std::printf("         (this filesystem: mem_align=%zu offset_align=%zu reported=%d)\n", a.memory, a.offset,
                int(a.reported));
}

TEST(ex5_open_missing_file_throws_system_error_enoent) {
    TempDir d;
    bool threw = false;
    try {
        (void)DirectFile::Open(d.file("does_not_exist.bin"), O_RDONLY, IoMode::kDirect);
    } catch (const std::system_error& e) {
        threw = true;
        CHECK_EQ(e.code().value(), ENOENT);
        CHECK(std::string(e.what()).find("does_not_exist.bin") != std::string::npos);  // say WHICH file
    }
    CHECK(threw);
}

// ---- RAII ------------------------------------------------------------------------------------

TEST(ex5_no_fd_leak_and_move_semantics) {
    TempDir d;
    const std::size_t before = OpenFdCount();
    {
        auto a = DirectFile::Open(d.file("m.bin"), kCreate, IoMode::kDirect);
        CHECK(a.is_open());
        CHECK_EQ(OpenFdCount(), before + 1);
        int raw = a.fd();
        DirectFile b(std::move(a));
        CHECK_EQ(a.fd(), -1);  // NOLINT: moved-from
        CHECK_EQ(b.fd(), raw);
        DirectFile c;
        c = std::move(b);
        CHECK_EQ(c.fd(), raw);
        CHECK_EQ(OpenFdCount(), before + 1);   // still exactly one fd
        auto other = DirectFile::Open(d.file("n.bin"), kCreate, IoMode::kDirect);
        CHECK_EQ(OpenFdCount(), before + 2);
        c = std::move(other);                  // c's old fd must be closed now
        CHECK_EQ(OpenFdCount(), before + 1);
        DirectFile& alias = c;
        c = std::move(alias);                  // self-move must not close it
        CHECK(c.is_open());
        CHECK(::fcntl(c.fd(), F_GETFD) != -1);
    }
    CHECK_EQ(OpenFdCount(), before);           // everything closed, exactly once
}

TEST(ex5_close_is_idempotent) {
    TempDir d;
    auto f = DirectFile::Open(d.file("c.bin"), kCreate, IoMode::kBuffered);
    f.Close();
    CHECK(!f.is_open());
    f.Close();
}

// ---- the three rules (Part 5 §5) ---------------------------------------------------------------

TEST(ex5_validation_names_the_rule_before_the_syscall) {
    TempDir d;
    auto f = DirectFile::Open(d.file("v.bin"), kCreate, IoMode::kDirect);
    const std::size_t A = f.alignment().memory, O = f.alignment().offset;
    auto buf = AlignedBuffer::Allocate(4 * 4096, 4096);

    auto expect_rule = [&](std::span<const char> s, std::uint64_t off, MisalignedIo::Rule rule) {
        bool threw = false;
        try {
            f.WriteAt(s, off);
        } catch (const MisalignedIo& e) {
            threw = true;
            CHECK(e.rule() == rule);
        }
        CHECK(threw);
    };
    // rule 1: address (your drill's bug: 32 bytes past a boundary)
    if (A > 32) expect_rule(buf.span().subspan(32, 4096), 0, MisalignedIo::Rule::kAddress);
    expect_rule(buf.span().subspan(1, 4096), 0, MisalignedIo::Rule::kAddress);
    // rule 2: length
    expect_rule(buf.span().subspan(0, 100), 0, MisalignedIo::Rule::kLength);
    expect_rule(buf.span().subspan(0, O + 1), 0, MisalignedIo::Rule::kLength);
    // rule 3: offset (your drill's i * 5)
    expect_rule(buf.span().subspan(0, 4096), 5, MisalignedIo::Rule::kOffset);
    // address is checked first, then length, then offset
    expect_rule(buf.span().subspan(1, 100), 5, MisalignedIo::Rule::kAddress);
    expect_rule(buf.span().subspan(0, 100), 5, MisalignedIo::Rule::kLength);
    // reads are validated too
    bool threw = false;
    try {
        f.ReadAt(buf.span().subspan(0, 4096), 7);
    } catch (const MisalignedIo& e) {
        threw = (e.rule() == MisalignedIo::Rule::kOffset);
    }
    CHECK(threw);
    // nothing reached the file
    CHECK_EQ(f.Size(), std::uint64_t{0});
}

TEST(ex5_buffered_mode_does_not_validate) {
    TempDir d;
    auto f = DirectFile::Open(d.file("b.bin"), kCreate, IoMode::kBuffered);
    std::string s = "hello, misaligned world";
    CHECK_EQ(f.WriteAt({s.data() + 1, 7}, 5), std::size_t{7});
    CHECK_EQ(f.Size(), std::uint64_t{12});
}

TEST(ex5_kernel_agrees_with_your_rules) {
    // Shows WHY the validation exists: on a filesystem that reports DIOALIGN, a raw misaligned
    // pwrite on the same fd fails with EINVAL — the error your drill got.
    TempDir d;
    auto f = DirectFile::Open(d.file("k.bin"), kCreate, IoMode::kDirect);
    if (!f.alignment().reported) SKIP("filesystem does not report STATX_DIOALIGN (tmpfs?) — run from the repo");
    auto buf = AlignedBuffer::Allocate(2 * 4096, 4096);
    errno = 0;
    CHECK_EQ(::pwrite(f.fd(), buf.data() + 32, 4096, 0), ssize_t{-1});
    CHECK_EQ(errno, EINVAL);
    CHECK_EQ(::pwrite(f.fd(), buf.data(), 4096, 0), ssize_t{4096});
}

// ---- I/O round trips -----------------------------------------------------------------------------

TEST(ex5_write_read_roundtrip_direct) {
    TempDir d;
    auto f = DirectFile::Open(d.file("rt.bin"), kCreate, IoMode::kDirect);
    const std::size_t B = std::max(f.alignment().offset, f.alignment().memory);
    auto w = AlignedBuffer::Allocate(8 * B, f.alignment().memory);
    std::string data = RandomBytes(8 * B, 1);
    std::memcpy(w.data(), data.data(), data.size());
    CHECK_EQ(f.WriteAt(w.span(), 3 * B), 8 * B);          // a hole of 3 blocks before it
    CHECK_EQ(f.Size(), std::uint64_t{11 * B});
    auto r = AlignedBuffer::Allocate(8 * B, f.alignment().memory);
    CHECK_EQ(f.ReadAt(r.span(), 3 * B), 8 * B);
    CHECK(std::memcmp(r.data(), data.data(), data.size()) == 0);
    // the hole reads as zeros
    auto z = AlignedBuffer::Allocate(B, f.alignment().memory);
    std::memset(z.data(), 1, B);
    CHECK_EQ(f.ReadAt(z.span(), 0), B);
    for (char c : z.span()) CHECK_EQ(c, '\0');
    f.Sync();
}

TEST(ex5_read_is_short_only_at_eof) {
    TempDir d;
    auto f = DirectFile::Open(d.file("eof.bin"), kCreate, IoMode::kDirect);
    const std::size_t B = std::max(f.alignment().offset, f.alignment().memory);
    auto w = AlignedBuffer::Allocate(B, f.alignment().memory);
    std::memset(w.data(), 'e', B);
    f.WriteAt(w.span(), 0);
    f.Truncate(13);                                   // Part 5 §7/§8
    auto r = AlignedBuffer::Allocate(4 * B, f.alignment().memory);
    CHECK_EQ(f.ReadAt(r.span(), 0), std::size_t{13});
    CHECK_EQ(f.ReadAt(r.span().subspan(0, B), 4 * B), std::size_t{0});   // past EOF
}

// ---- DirectAppender (Part 5 §7) -------------------------------------------------------------------

TEST(ex5_appender_rejects_bad_block_size_and_nonempty_file) {
    TempDir d;
    auto f = DirectFile::Open(d.file("bad.bin"), kCreate, IoMode::kDirect);
    CHECK_THROWS(DirectAppender(f, 0), std::invalid_argument);
    CHECK_THROWS(DirectAppender(f, f.alignment().offset + 1), std::invalid_argument);
    CHECK_THROWS(DirectAppender(f, 100), std::invalid_argument);
    auto g = DirectFile::Open(d.file("nonempty.bin"), kCreate, IoMode::kBuffered);
    std::string s = "x";
    g.WriteAt({s.data(), 1}, 0);
    CHECK_THROWS(DirectAppender(g, 4096), std::invalid_argument);
}

TEST(ex5_appender_exact_bytes_odd_chunks) {
    TempDir d;
    const std::string p = d.file("log.bin");
    std::string expect;
    {
        auto f = DirectFile::Open(p, kCreate, IoMode::kDirect);
        const std::size_t B = std::max<std::size_t>(4096, f.alignment().offset);
        DirectAppender app(f, B);
        std::mt19937 rng(9);
        for (std::size_t n : {1ul, 7ul, 0ul, 513ul, 4095ul, 4096ul, 4097ul, 3ul * 4096ul + 37ul, 1ul}) {
            std::string chunk = RandomBytes(n, static_cast<unsigned>(rng()));
            app.Append(chunk);
            expect += chunk;
        }
        CHECK_EQ(app.logical_size(), std::uint64_t{expect.size()});
        app.Flush();
        CHECK_EQ(f.Size(), std::uint64_t{expect.size()});   // ftruncate removed the padding
        CHECK_EQ(app.device_bytes_written() % B, std::uint64_t{0});   // only whole blocks ever written
    }
    CHECK(ReadWhole(p) == expect);   // read back WITHOUT O_DIRECT: byte-identical
}

TEST(ex5_appender_padding_never_carries_stale_bytes) {
    // Part 7 §4: after a full block goes out, the staging block is reused. If it isn't re-zeroed,
    // the next tail block's padding carries the PREVIOUS block's bytes onto the disk.
    TempDir d;
    auto f = DirectFile::Open(d.file("pad.bin"), kCreate, IoMode::kDirect);
    const std::size_t B = std::max<std::size_t>(4096, f.alignment().offset);
    DirectAppender app(f, B);
    std::string secret(B, 'S');                  // one full block of "secret" bytes
    app.Append(secret);
    std::string tail = "tail-10by";
    app.Append(tail);
    app.Flush();
    auto blk = app.staging_block();
    CHECK_EQ(blk.size(), B);
    CHECK(std::memcmp(blk.data(), tail.data(), tail.size()) == 0);
    for (std::size_t i = tail.size(); i < B; ++i) CHECK_EQ(blk[i], '\0');
}

TEST(ex5_appender_flush_then_continue_rewrites_tail) {
    TempDir d;
    const std::string p = d.file("tail.bin");
    std::string expect;
    auto f = DirectFile::Open(p, kCreate, IoMode::kDirect);
    const std::size_t B = std::max<std::size_t>(4096, f.alignment().offset);
    DirectAppender app(f, B);
    for (int round = 0; round < 50; ++round) {
        std::string chunk = RandomBytes(1 + std::size_t(round) * 37, unsigned(round));
        app.Append(chunk);
        expect += chunk;
        app.Flush();                                   // tail written, padded, truncated — every round
        CHECK_EQ(f.Size(), std::uint64_t{expect.size()});
        CHECK(ReadWhole(p) == expect);
    }
    // Flushing every round rewrites the same tail block many times: device bytes > logical bytes.
    CHECK(app.device_bytes_written() > app.logical_size());
    std::printf("         (logical=%llu device=%llu -> write amplification %.2f)\n",
                (unsigned long long)app.logical_size(), (unsigned long long)app.device_bytes_written(),
                double(app.device_bytes_written()) / double(app.logical_size()));
}

TEST(ex5_appender_large_random_stream_and_destructor_flushes) {
    TempDir d;
    const std::string p = d.file("big.bin");
    std::string expect;
    {
        auto f = DirectFile::Open(p, kCreate, IoMode::kDirect);
        DirectAppender app(f, std::max<std::size_t>(64 * 1024, f.alignment().offset));
        std::mt19937 rng(123);
        while (expect.size() < (3u << 20)) {
            std::string chunk = RandomBytes(rng() % 20000, static_cast<unsigned>(rng()));
            app.Append(chunk);
            expect += chunk;
        }
    }   // no explicit Flush: the destructor must do it (best effort)
    CHECK_EQ(fs::file_size(p), std::uintmax_t{expect.size()});
    CHECK(ReadWhole(p) == expect);
}

TEST(ex5_appender_works_in_buffered_mode_too) {
    TempDir d;
    const std::string p = d.file("buf.bin");
    std::string expect = RandomBytes(10000, 5);
    {
        auto f = DirectFile::Open(p, kCreate, IoMode::kBuffered);
        DirectAppender app(f, 4096);
        app.Append(expect);
        app.Flush();
    }
    CHECK(ReadWhole(p) == expect);
}
