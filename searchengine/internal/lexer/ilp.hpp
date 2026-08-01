#pragma once
#include <string>

// Both fields own their bytes. These travel through a queue, so the producer's
// buffers (a reused fgets stack buffer, a reassigned local string) are long gone
// by the time a worker pops the item — a view here would always dangle.
struct ILP {
    std::string filepath;
    std::string line;
};
