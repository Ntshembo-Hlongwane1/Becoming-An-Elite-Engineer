#include "am/aligned_malloc.hpp"
#include "am/todo.hpp"

namespace am {

void* AlignedMalloc(std::size_t, std::size_t) { Todo("AlignedMalloc"); }

void AlignedFree(void* p) {
    // TODO(Ex2): recover the raw pointer from the header and std::free it.
    // (Left as a no-op so the stub never crashes; the tests can't pass until AlignedMalloc works.)
    (void)p;
}

}  // namespace am
