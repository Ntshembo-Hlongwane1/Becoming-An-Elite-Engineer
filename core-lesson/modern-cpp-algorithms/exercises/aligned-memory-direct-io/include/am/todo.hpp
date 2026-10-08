#pragma once
// Provided. Every unimplemented function calls Todo(); the test runner reports it as
// "NOT IMPLEMENTED" instead of crashing. Delete the call when you implement the function.
#include <stdexcept>
#include <string>

namespace am {

struct NotImplemented : std::logic_error {
    using std::logic_error::logic_error;
};

[[noreturn]] inline void Todo(const char* what) {
    throw NotImplemented(std::string("TODO: ") + what);
}

}  // namespace am
