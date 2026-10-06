#include "dlsm/filename.hpp"
#include "lsm/todo.hpp"

namespace dlsm {
using lsm::Todo;

std::string LogFileName(const std::string&, std::uint64_t) { Todo("LogFileName"); }
std::string TableFileName(const std::string&, std::uint64_t) { Todo("TableFileName"); }
std::string ManifestFileName(const std::string&, std::uint64_t) { Todo("ManifestFileName"); }
std::string TempFileName(const std::string&, std::uint64_t) { Todo("TempFileName"); }
std::string CurrentFileName(const std::string&) { Todo("CurrentFileName"); }
std::string LockFileName(const std::string&) { Todo("LockFileName"); }
bool ParseFileName(std::string_view, std::uint64_t*, FileType*) { Todo("ParseFileName"); }
Status SetCurrentFile(const std::string&, std::uint64_t) { Todo("SetCurrentFile"); }

}  // namespace dlsm
