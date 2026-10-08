#pragma once
// Provided: a ~80-line test runner (no external dependency). Same as the LSM exercises, plus
// CHECK_THROWS and SKIP.
//   TEST(name) { CHECK(cond); CHECK_EQ(a, b); CHECK_THROWS(expr, Type); SKIP("why"); }
//   ./tests            run all       ./tests foo bar     run tests whose name contains "foo" or "bar"
#include <cstdio>
#include <exception>
#include <sstream>
#include <string>
#include <vector>

#include "am/todo.hpp"

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
struct Skipped {
    std::string why;
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
    int passed = 0, failed = 0, todo = 0, skipped = 0;
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
        } catch (const Skipped& s) {
            std::printf("[ SKIP ] %s  (%s)\n", c.name, s.why.c_str());
            ++skipped;
        } catch (const am::NotImplemented& e) {
            std::printf("[ TODO ] %s  (%s)\n", c.name, e.what());
            ++todo;
        } catch (const std::exception& e) {
            std::printf("[ FAIL ] %s  (exception: %s)\n", c.name, e.what());
            ++failed;
        }
    }
    std::printf("\n%d passed, %d failed, %d not implemented, %d skipped\n", passed, failed, todo, skipped);
    return (failed + todo) == 0 ? 0 : 1;
}

}  // namespace minitest

#define MT_CAT2(a, b) a##b
#define MT_CAT(a, b) MT_CAT2(a, b)
#define TEST(name)                                                              \
    static void name();                                                         \
    static ::minitest::Registrar MT_CAT(mt_reg_, name)(#name, &name);           \
    static void name()

#define MT_WHERE (std::string(__FILE__) + ":" + std::to_string(__LINE__))

#define CHECK(cond)                                                                       \
    do {                                                                                  \
        if (!(cond)) throw ::minitest::Failure{MT_WHERE + ": CHECK(" #cond ") failed"};   \
    } while (0)

#define CHECK_EQ(a, b)                                                                    \
    do {                                                                                  \
        const auto mt_a = (a);                                                            \
        const auto mt_b = (b);                                                            \
        if (!(mt_a == mt_b))                                                              \
            throw ::minitest::Failure{MT_WHERE + ": CHECK_EQ(" #a ", " #b ") failed\n" +  \
                                      ::minitest::Describe(mt_a, mt_b)};                  \
    } while (0)

// Passes only if `expr` throws exactly something catchable as `Type`. A Todo() inside `expr` is
// re-thrown so unimplemented code shows up as TODO, not FAIL.
#define CHECK_THROWS(expr, Type)                                                          \
    do {                                                                                  \
        bool mt_threw = false;                                                            \
        try {                                                                             \
            (void)(expr);                                                                 \
        } catch (const ::am::NotImplemented&) {                                           \
            throw;                                                                        \
        } catch (const Type&) {                                                           \
            mt_threw = true;                                                              \
        } catch (const std::exception& mt_e) {                                            \
            throw ::minitest::Failure{MT_WHERE + ": CHECK_THROWS(" #expr ", " #Type       \
                                      ") threw a different exception: " + mt_e.what()};   \
        }                                                                                 \
        if (!mt_threw)                                                                    \
            throw ::minitest::Failure{MT_WHERE + ": CHECK_THROWS(" #expr ", " #Type       \
                                      ") did not throw"};                                 \
    } while (0)

#define SKIP(why) throw ::minitest::Skipped{why}
