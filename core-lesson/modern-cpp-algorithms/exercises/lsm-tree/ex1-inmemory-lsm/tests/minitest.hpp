#pragma once
// Provided: a ~60-line test runner (no external dependency).
//   TEST(name) { CHECK(cond); CHECK_EQ(a, b); }
//   ./tests            run all       ./tests foo bar     run tests whose name contains "foo" or "bar"
#include <cstdio>
#include <exception>
#include <sstream>
#include <string>
#include <vector>

#include "lsm/todo.hpp"

namespace minitest {

struct Case {
    const char* name;
    void (*fn)();
};
inline std::vector<Case>& Registry() {
    static std::vector<Case> r;
    return r;
}
struct Registrar {
    Registrar(const char* n, void (*f)()) { Registry().push_back({n, f}); }
};
struct Failure {
    std::string msg;
};

template <class A, class B>
std::string Describe(const A& a, const B& b) {
    std::ostringstream os;
    if constexpr (requires { os << a; os << b; }) {
        os << "  lhs: " << a << "\n  rhs: " << b;
    }
    return os.str();
}

inline int RunAll(int argc, char** argv) {
    int passed = 0, failed = 0, todo = 0;
    for (const auto& c : Registry()) {
        bool selected = argc <= 1;
        for (int i = 1; i < argc; ++i) selected |= std::string(c.name).find(argv[i]) != std::string::npos;
        if (!selected) continue;
        try {
            c.fn();
            std::printf("[ PASS ] %s\n", c.name);
            ++passed;
        } catch (const Failure& f) {
            std::printf("[ FAIL ] %s\n%s\n", c.name, f.msg.c_str());
            ++failed;
        } catch (const lsm::NotImplemented& e) {
            std::printf("[ TODO ] %s  (%s)\n", c.name, e.what());
            ++todo;
        } catch (const std::exception& e) {
            std::printf("[ FAIL ] %s  (exception: %s)\n", c.name, e.what());
            ++failed;
        }
    }
    std::printf("\n%d passed, %d failed, %d not implemented\n", passed, failed, todo);
    return (failed + todo) == 0 ? 0 : 1;
}

}  // namespace minitest

#define MT_CAT2(a, b) a##b
#define MT_CAT(a, b) MT_CAT2(a, b)
#define TEST(name)                                                              \
    static void name();                                                         \
    static ::minitest::Registrar MT_CAT(mt_reg_, name)(#name, &name);           \
    static void name()

#define CHECK(cond)                                                                       \
    do {                                                                                  \
        if (!(cond))                                                                      \
            throw ::minitest::Failure{std::string(__FILE__) + ":" + std::to_string(__LINE__) + \
                                      ": CHECK(" #cond ") failed"};                       \
    } while (0)

#define CHECK_EQ(a, b)                                                                    \
    do {                                                                                  \
        const auto mt_a = (a);                                                            \
        const auto mt_b = (b);                                                            \
        if (!(mt_a == mt_b))                                                              \
            throw ::minitest::Failure{std::string(__FILE__) + ":" + std::to_string(__LINE__) + \
                                      ": CHECK_EQ(" #a ", " #b ") failed\n" +             \
                                      ::minitest::Describe(mt_a, mt_b)};                  \
    } while (0)
