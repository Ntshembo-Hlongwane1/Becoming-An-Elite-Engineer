#pragma once
// Exercise 3 — parse /proc/<pid>/maps and classify any address. Notes: Lesson 3 §3.2.
//
// This is the lookup a debugger, the kernel's fault handler, and your capstone all do: given an
// address, find the VMA that contains it and say what kind of region it is.
//
// Format of one maps line (Lesson 3 §3.2, man proc_pid_maps):
//   start-end perms offset dev inode pathname
//   7b21d2c28000-7b21d2dc0000 r-xp 00028000 08:02 787315 /usr/lib/.../libc.so.6
//   60f12494f000-60f124991000 rw-p 00000000 00:00 0      [heap]
//   7b21d3072000-7b21d3074000 rw-p 00000000 00:00 0
// perms: 4 chars r/w/x/- then p (private/COW) or s (shared).
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace mm {

struct Perms {
    bool r = false, w = false, x = false, shared = false;
    bool operator==(const Perms&) const = default;
};

struct Vma {
    std::uintptr_t start = 0;
    std::uintptr_t end = 0;          // half-open [start, end)
    Perms perms{};
    std::uint64_t offset = 0;
    std::string dev;                 // "major:minor" as written, e.g. "08:02"
    std::uint64_t inode = 0;
    std::string path;                // backing file, "[heap]"/"[stack]"/..., or "" if anonymous
    std::size_t size() const noexcept { return static_cast<std::size_t>(end - start); }
    bool contains(std::uintptr_t a) const noexcept { return a >= start && a < end; }
};

// Region kinds are fully determined by (perms, path) — no heuristics, so results are deterministic.
enum class Region {
    kUnmapped,       // no VMA contains the address
    kFileExec,       // file-backed, executable  (program/library .text)
    kFileReadOnly,   // file-backed, r-- , not writable/executable (.rodata)
    kFileWritable,   // file-backed, writable (.data)
    kHeap,           // path == "[heap]"
    kStack,          // path == "[stack]"
    kVdsoVvar,       // path in {"[vdso]","[vvar]","[vvar_vclock]"}
    kVsyscall,       // path == "[vsyscall]"
    kAnon,           // anonymous (no path, inode 0): bss tail, anon mmap, large malloc, thread stacks
    kOther,          // any other bracketed pseudo-path (e.g. "[anon:name]")
};
const char* region_name(Region r) noexcept;

// Parse ONE line. Returns nullopt if the line is malformed (fewer than the 5 mandatory fields, or a
// field doesn't parse). The pathname is optional and may itself contain spaces: parse the first five
// whitespace-separated fields, then take the remainder (trimmed) as the path.
std::optional<Vma> parse_maps_line(std::string_view line);

// Parse a whole maps file (newline-separated). Skips blank lines; a malformed non-blank line makes
// the whole parse fail (returns nullopt) — be strict. On success the result is sorted by start.
std::optional<std::vector<Vma>> parse_maps(std::string_view text);

// Binary search for the VMA containing addr; nullptr if none. Precondition: vmas sorted by start and
// non-overlapping (as /proc guarantees and parse_maps returns).
const Vma* find_vma(const std::vector<Vma>& vmas, std::uintptr_t addr);

// Classify addr using the containing VMA (or kUnmapped if none).
Region classify(const std::vector<Vma>& vmas, std::uintptr_t addr);

// Convenience: read /proc/self/maps into a string (provided in the exercise's test helper, not here).

}  // namespace mm
