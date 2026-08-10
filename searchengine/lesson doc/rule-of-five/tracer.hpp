#pragma once
#include <cstdio>
#include <string>
struct Tracer {
    std::string name;
    int id;
    static inline int counter = 0;

    Tracer(std::string n = "?") : name(std::move(n)), id(++counter) {
        printf("  [%d] ctor          %s\n", id, name.c_str());
    }
    ~Tracer() { printf("  [%d] DTOR          %s\n", id, name.c_str()); }

    Tracer(const Tracer& o) : name(o.name + "-copy"), id(++counter) {
        printf("  [%d] COPY ctor  <-  [%d] %s\n", id, o.id, o.name.c_str());
    }
    Tracer& operator=(const Tracer& o) {
        printf("  [%d] COPY assign <- [%d] %s\n", id, o.id, o.name.c_str());
        name = o.name + "-copyassigned";
        return *this;
    }
    Tracer(Tracer&& o) noexcept : name(std::move(o.name)), id(++counter) {
        printf("  [%d] MOVE ctor  <-  [%d] (source now empty)\n", id, o.id);
        o.name = "<moved-from>";
    }
    Tracer& operator=(Tracer&& o) noexcept {
        printf("  [%d] MOVE assign <- [%d]\n", id, o.id);
        name = std::move(o.name);
        o.name = "<moved-from>";
        return *this;
    }
};
