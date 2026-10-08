#include "mm/maps.hpp"
#include "mm/todo.hpp"
namespace mm {

// Provided (boilerplate): the enum-to-string helper.
const char* region_name(Region r) noexcept {
    switch (r) {
        case Region::kUnmapped:     return "unmapped";
        case Region::kFileExec:     return "file-exec";
        case Region::kFileReadOnly: return "file-readonly";
        case Region::kFileWritable: return "file-writable";
        case Region::kHeap:         return "heap";
        case Region::kStack:        return "stack";
        case Region::kVdsoVvar:     return "vdso/vvar";
        case Region::kVsyscall:     return "vsyscall";
        case Region::kAnon:         return "anon";
        case Region::kOther:        return "other";
    }
    return "?";
}

std::optional<Vma> parse_maps_line(std::string_view) { Todo("parse_maps_line"); }
std::optional<std::vector<Vma>> parse_maps(std::string_view) { Todo("parse_maps"); }
const Vma* find_vma(const std::vector<Vma>&, std::uintptr_t) { Todo("find_vma"); }
Region classify(const std::vector<Vma>&, std::uintptr_t) { Todo("classify"); }

}  // namespace mm
