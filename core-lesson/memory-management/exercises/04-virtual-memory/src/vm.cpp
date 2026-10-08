#include "mm/vm.hpp"
#include "mm/todo.hpp"
namespace mm {
std::size_t page_size() { Todo("page_size"); }
std::uintptr_t page_base(std::uintptr_t, std::size_t) { Todo("page_base"); }
std::size_t page_offset(std::uintptr_t, std::size_t) { Todo("page_offset"); }
std::size_t pages_spanned(std::uintptr_t, std::size_t, std::size_t) { Todo("pages_spanned"); }
std::optional<long> parse_status_kb(std::string_view, std::string_view) { Todo("parse_status_kb"); }
PageProbe::PageProbe(std::size_t) { Todo("PageProbe::PageProbe"); }
PageProbe::~PageProbe() { /* unmap if mapped; empty stub is fine until ctor works */ }
void PageProbe::touch(std::size_t) { Todo("PageProbe::touch"); }
bool PageProbe::is_resident(std::size_t) const { Todo("PageProbe::is_resident"); }
std::size_t PageProbe::resident_count() const { Todo("PageProbe::resident_count"); }
}
