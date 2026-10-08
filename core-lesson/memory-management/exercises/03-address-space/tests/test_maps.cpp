// Exercise 3 — maps parsing + classification. Lesson 3 §3.2.
#include <cstdint>
#include <cstdlib>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "mm/maps.hpp"
#include "minitest.hpp"

using namespace mm;

TEST(parse_line_file_backed) {
    auto v = parse_maps_line("7b21d2c28000-7b21d2dc0000 r-xp 00028000 08:02 787315 /usr/lib/libc.so.6");
    CHECK(v.has_value());
    CHECK_EQ(v->start, std::uintptr_t{0x7b21d2c28000});
    CHECK_EQ(v->end,   std::uintptr_t{0x7b21d2dc0000});
    CHECK(v->perms.r && v->perms.x && !v->perms.w && !v->perms.shared);
    CHECK_EQ(v->offset, std::uint64_t{0x28000});
    CHECK_EQ(v->dev, std::string{"08:02"});
    CHECK_EQ(v->inode, std::uint64_t{787315});
    CHECK_EQ(v->path, std::string{"/usr/lib/libc.so.6"});
    CHECK_EQ(v->size(), std::size_t{0x198000});
}

TEST(parse_line_anonymous_no_path) {
    auto v = parse_maps_line("7b21d3072000-7b21d3074000 rw-p 00000000 00:00 0 ");
    CHECK(v.has_value());
    CHECK(v->perms.r && v->perms.w && !v->perms.x);
    CHECK_EQ(v->inode, std::uint64_t{0});
    CHECK(v->path.empty());
    auto v2 = parse_maps_line("7b21d3072000-7b21d3074000 rw-p 00000000 00:00 0");  // no trailing space
    CHECK(v2.has_value());
    CHECK(v2->path.empty());
}

TEST(parse_line_pseudo_paths_and_perms) {
    auto heap = parse_maps_line("60f12494f000-60f124991000 rw-p 00000000 00:00 0 [heap]");
    CHECK(heap.has_value() && heap->path == "[heap]");
    auto guard = parse_maps_line("7b21d3082000-7b21d3083000 ---p 00000000 00:00 0 ");
    CHECK(guard.has_value());
    CHECK(!guard->perms.r && !guard->perms.w && !guard->perms.x);
    auto sh = parse_maps_line("1000-2000 rwxs 00000000 00:00 0 /x");
    CHECK(sh.has_value() && sh->perms.shared && sh->perms.x && sh->perms.w && sh->perms.r);
    auto vsys = parse_maps_line("ffffffffff600000-ffffffffff601000 --xp 00000000 00:00 0 [vsyscall]");
    CHECK(vsys.has_value() && vsys->path == "[vsyscall]");
    CHECK_EQ(vsys->start, std::uintptr_t{0xffffffffff600000});   // high addresses must parse
}

TEST(parse_line_path_with_spaces_and_deleted) {
    auto v = parse_maps_line("1000-2000 r--p 0 00:00 42 /home/u/my dir/app (deleted)");
    CHECK(v.has_value());
    CHECK_EQ(v->path, std::string{"/home/u/my dir/app (deleted)"});
}

TEST(parse_line_rejects_malformed) {
    CHECK(!parse_maps_line("garbage").has_value());
    CHECK(!parse_maps_line("1000 r--p 0 00:00 0").has_value());          // no end
    CHECK(!parse_maps_line("1000-2000 r--p 0 00:00").has_value());        // missing inode
    CHECK(!parse_maps_line("zzzz-2000 r--p 0 00:00 0").has_value());      // bad hex start
    CHECK(!parse_maps_line("2000-1000 r--p 0 00:00 0 /x").has_value());   // end < start
    CHECK(!parse_maps_line("1000-2000 r-p 0 00:00 0 /x").has_value());    // perms too short
    CHECK(!parse_maps_line("1000-2000 rwxpp 0 00:00 0 /x").has_value());  // perms too long (5 chars)
}

TEST(parse_maps_sorts_and_skips_blanks) {
    std::string text =
        "3000-4000 r--p 0 00:00 0 [stack]\n"
        "\n"
        "1000-2000 r-xp 0 08:02 5 /bin/a\n"
        "2000-3000 rw-p 0 00:00 0 [heap]\n";
    auto vs = parse_maps(text);
    CHECK(vs.has_value());
    CHECK_EQ(vs->size(), std::size_t{3});
    CHECK_EQ((*vs)[0].start, std::uintptr_t{0x1000});   // sorted
    CHECK_EQ((*vs)[1].start, std::uintptr_t{0x2000});
    CHECK_EQ((*vs)[2].start, std::uintptr_t{0x3000});
    CHECK(!parse_maps("1000-2000 r--p 0 00:00 0 /x\nBROKEN\n").has_value());  // strict
}

static std::vector<Vma> sample() {
    return *parse_maps(
        "1000-2000 r-xp 00000000 08:02 5 /bin/app\n"       // code
        "2000-3000 r--p 00001000 08:02 5 /bin/app\n"       // rodata
        "3000-4000 rw-p 00002000 08:02 5 /bin/app\n"       // data
        "4000-5000 rw-p 00000000 00:00 0 \n"               // anon (bss tail / mmap)
        "5000-6000 rw-p 00000000 00:00 0 [heap]\n"
        "6000-7000 ---p 00000000 00:00 0 \n"               // guard (anon, no perms)
        "8000-9000 rw-p 00000000 00:00 0 [stack]\n"
        "9000-a000 r-xp 00000000 00:00 0 [vdso]\n"
        "a000-b000 r--p 00000000 00:00 0 [vvar]\n"
        "ffffffffff600000-ffffffffff601000 --xp 0 00:00 0 [vsyscall]\n");
}

TEST(find_vma_binary_search) {
    auto vs = sample();
    CHECK(find_vma(vs, 0x1000) == &vs[0]);     // at start (inclusive)
    CHECK(find_vma(vs, 0x1fff) == &vs[0]);     // last byte
    CHECK(find_vma(vs, 0x2000) == &vs[1]);     // end is exclusive -> next VMA
    CHECK(find_vma(vs, 0x0fff) == nullptr);    // below everything
    CHECK(find_vma(vs, 0x7000) == nullptr);    // in the gap between guard(6000-7000) and stack(8000)
    CHECK(find_vma(vs, 0xffffffffff600500) != nullptr);  // high address
    CHECK(find_vma(vs, 0xffffffffff601000) == nullptr);  // one past the top VMA
}

TEST(classify_all_regions) {
    auto vs = sample();
    CHECK_EQ(classify(vs, 0x1800), Region::kFileExec);
    CHECK_EQ(classify(vs, 0x2800), Region::kFileReadOnly);
    CHECK_EQ(classify(vs, 0x3800), Region::kFileWritable);
    CHECK_EQ(classify(vs, 0x4800), Region::kAnon);
    CHECK_EQ(classify(vs, 0x5800), Region::kHeap);
    CHECK_EQ(classify(vs, 0x6800), Region::kAnon);          // guard: anon, no path -> kAnon
    CHECK_EQ(classify(vs, 0x7800), Region::kUnmapped);      // gap
    CHECK_EQ(classify(vs, 0x8800), Region::kStack);
    CHECK_EQ(classify(vs, 0x9800), Region::kVdsoVvar);
    CHECK_EQ(classify(vs, 0xa800), Region::kVdsoVvar);
    CHECK_EQ(classify(vs, 0xffffffffff600500), Region::kVsyscall);
    auto other = *parse_maps("1000-2000 rw-p 0 00:00 0 [anon:myheap]\n");
    CHECK_EQ(classify(other, 0x1800), Region::kOther);
}

// Integration: classify real addresses in THIS running process against its own /proc/self/maps.
static std::string read_self_maps() {
    std::string out;
    if (FILE* f = std::fopen("/proc/self/maps", "rb")) {
        char buf[4096]; size_t n;
        while ((n = std::fread(buf, 1, sizeof buf, f)) > 0) out.append(buf, n);
        std::fclose(f);
    }
    return out;
}

TEST(classify_real_process_addresses) {
    auto text = read_self_maps();
    if (text.empty()) SKIP("/proc/self/maps unavailable");
    auto vs = parse_maps(text);
    CHECK(vs.has_value());
    int local = 0;
    auto a_of = [](const void* p) { return reinterpret_cast<std::uintptr_t>(p); };

    // Classify everything FIRST, free the heap block, THEN assert — so a failing CHECK can't leak.
    Region r_stack = classify(*vs, a_of(&local));
    Region r_func  = classify(*vs, a_of(reinterpret_cast<const void*>(&std::malloc)));
    auto* heap = static_cast<int*>(std::malloc(16));
    Region r_heap  = classify(*vs, a_of(heap));
    std::free(heap);
    Region r_unmapped = classify(*vs, std::uintptr_t{0x1000});

    // A stack local is normally kStack — but under ASan's use-after-return detection, locals live on
    // a "fake stack" in anonymous mmap memory (kAnon), not [stack]. Both are correct observations of
    // reality (and the fake stack is exactly the mechanism Lesson 16 explains). Accept either.
    CHECK(r_stack == Region::kStack || r_stack == Region::kAnon);
    CHECK_EQ(r_func, Region::kFileExec);                       // a function: file-backed, executable
    CHECK(r_heap == Region::kHeap || r_heap == Region::kAnon); // glibc may use [heap] or an anon arena
    CHECK_EQ(r_unmapped, Region::kUnmapped);                   // low unmapped address
}
