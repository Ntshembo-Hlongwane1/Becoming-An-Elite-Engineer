#include "am/align.hpp"
#include "am/todo.hpp"

namespace am {

bool IsPowerOfTwo(std::size_t) { Todo("IsPowerOfTwo"); }
bool IsAligned(std::uintptr_t, std::size_t) { Todo("IsAligned(uintptr_t)"); }
bool IsAligned(const void*, std::size_t) { Todo("IsAligned(const void*)"); }
std::size_t AlignDown(std::size_t, std::size_t) { Todo("AlignDown"); }
std::optional<std::size_t> AlignUp(std::size_t, std::size_t) { Todo("AlignUp"); }
std::size_t PaddingTo(std::uintptr_t, std::size_t) { Todo("PaddingTo"); }
std::size_t NaturalAlignment(std::uintptr_t) { Todo("NaturalAlignment"); }

}  // namespace am
