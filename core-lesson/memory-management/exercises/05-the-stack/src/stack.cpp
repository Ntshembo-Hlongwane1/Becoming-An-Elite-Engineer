#include "mm/stack.hpp"
#include "mm/todo.hpp"
namespace mm {
StackDirection stack_direction() { Todo("stack_direction"); }
std::size_t stack_limit_bytes() { Todo("stack_limit_bytes"); }
std::ptrdiff_t adjacent_frame_delta() { Todo("adjacent_frame_delta"); }
std::size_t capture_backtrace(std::span<void*>) { Todo("capture_backtrace"); }
}
