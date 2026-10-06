#pragma once
#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "helpers.hpp"  // from Exercise 1 tests

namespace dtest {

// A fresh directory under /tmp, removed (recursively) when the object dies.
class TempDir {
public:
    TempDir() {
        std::string tmpl = (std::filesystem::temp_directory_path() / "dlsm-test-XXXXXX").string();
        std::vector<char> buf(tmpl.begin(), tmpl.end());
        buf.push_back('\0');
        if (::mkdtemp(buf.data()) == nullptr) std::abort();
        path_ = buf.data();
    }
    ~TempDir() {
        std::error_code ec;
        std::filesystem::remove_all(path_, ec);
    }
    const std::string& path() const { return path_; }
    std::string operator/(const std::string& name) const { return path_ + "/" + name; }

private:
    std::string path_;
};

inline std::string Slurp(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(in), {});
}
inline void Spit(const std::string& path, const std::string& data) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out.write(data.data(), static_cast<std::streamsize>(data.size()));
}
inline std::vector<std::string> ListDir(const std::string& dir) {
    std::vector<std::string> out;
    for (auto& e : std::filesystem::directory_iterator(dir)) out.push_back(e.path().filename().string());
    std::sort(out.begin(), out.end());
    return out;
}
inline std::size_t OpenFdCount() {
    std::size_t n = 0;
    for ([[maybe_unused]] auto& e : std::filesystem::directory_iterator("/proc/self/fd")) ++n;
    return n;
}

}  // namespace dtest
