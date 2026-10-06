#include "lsm/arena.hpp"
#include "lsm/todo.hpp"

namespace lsm {

Arena::~Arena() {
    // TODO(M2): delete[] every block. (Left empty so the stub doesn't crash; the arena tests will
    // still fail until Allocate* work, and ASan will report the leak once they do.)
}

char* Arena::Allocate(std::size_t) { Todo("Arena::Allocate"); }
char* Arena::AllocateAligned(std::size_t) { Todo("Arena::AllocateAligned"); }
std::size_t Arena::MemoryUsage() const { Todo("Arena::MemoryUsage"); }

}  // namespace lsm
