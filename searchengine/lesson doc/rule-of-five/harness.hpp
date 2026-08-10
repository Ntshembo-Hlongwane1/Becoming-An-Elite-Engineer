#pragma once
#include <cstdio>
#include <type_traits>
#include <utility>
#include <vector>

// Compile-time + runtime audit of a type's special members.
// Usage:  AuditType<MyType>("MyType", []{ return MyType("seed"); });
template <typename T, typename Factory>
bool AuditType(const char* name, Factory make) {
    int fails = 0;
    auto check = [&](const char* what, bool ok, const char* why){
        printf("  %-38s %s%s%s\n", what, ok?"PASS":"** FAIL **",
               ok?"":"  <- ", ok?"":why);
        if(!ok) ++fails;
    };
    printf("== %s ==\n", name);

    check("destructible",              std::is_destructible_v<T>, "cannot be destroyed");
    check("destructor is noexcept",    std::is_nothrow_destructible_v<T>, "dtor may throw");

    constexpr bool cc = std::is_copy_constructible_v<T>;
    constexpr bool ca = std::is_copy_assignable_v<T>;
    constexpr bool mc = std::is_move_constructible_v<T>;
    constexpr bool ma = std::is_move_assignable_v<T>;
    printf("  copy-ctor=%s copy-assign=%s move-ctor=%s move-assign=%s\n",
           cc?"y":"n", ca?"y":"n", mc?"y":"n", ma?"y":"n");

    // Rule of Five consistency: copy ops should agree with each other, moves likewise.
    check("copy ctor and copy assign agree", cc == ca,
          "one copy op exists without the other");
    check("move ctor and move assign agree", mc == ma,
          "one move op exists without the other");

    if constexpr (mc) {
        check("move ctor is noexcept", std::is_nothrow_move_constructible_v<T>,
              "vector will COPY instead of moving (doc 07)");
    }
    if constexpr (ma) {
        check("move assign is noexcept", std::is_nothrow_move_assignable_v<T>,
              "vector will COPY instead of moving (doc 07)");
    }

    // Runtime: moved-from must be destructible and assignable.
    if constexpr (mc && ma) {
        { T a = make(); T b = std::move(a); T c = make(); a = std::move(c); }
        check("moved-from survives reassignment + destruction", true, "");
    }
    // Runtime: self-assignment must not corrupt.
    if constexpr (ca) {
        T a = make(); a = a;               // NOLINT: intentional
        check("self copy-assignment survives", true, "");
    }
    // Runtime: vector growth must not corrupt.
    if constexpr (mc || cc) {
        std::vector<T> v;
        for (int i=0;i<40;++i) v.push_back(make());
        check("survives vector reallocation", v.size()==40, "corrupted in container");
    }
    printf("  -> %s\n\n", fails? "FAILURES":"all checks passed");
    return fails==0;
}
