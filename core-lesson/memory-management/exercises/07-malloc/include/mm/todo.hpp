#pragma once
// Provided. Unimplemented functions call Todo(); the runner reports "NOT IMPLEMENTED" instead of
// crashing. Delete the Todo() call when you implement a function.
#include <stdexcept>
#include <string>
namespace mm {
struct NotImplemented : std::logic_error { using std::logic_error::logic_error; };
[[noreturn]] inline void Todo(const char* what) { throw NotImplemented(std::string("TODO: ") + what); }
}  // namespace mm
