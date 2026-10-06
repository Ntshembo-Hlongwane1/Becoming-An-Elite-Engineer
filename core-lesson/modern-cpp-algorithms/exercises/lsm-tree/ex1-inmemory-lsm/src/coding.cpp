#include "lsm/coding.hpp"
#include "lsm/todo.hpp"

namespace lsm {

void EncodeFixed32(char*, std::uint32_t) { Todo("EncodeFixed32"); }
void EncodeFixed64(char*, std::uint64_t) { Todo("EncodeFixed64"); }
void PutFixed32(std::string&, std::uint32_t) { Todo("PutFixed32"); }
void PutFixed64(std::string&, std::uint64_t) { Todo("PutFixed64"); }
std::uint32_t DecodeFixed32(const char*) { Todo("DecodeFixed32"); }
std::uint64_t DecodeFixed64(const char*) { Todo("DecodeFixed64"); }

void PutVarint32(std::string&, std::uint32_t) { Todo("PutVarint32"); }
void PutVarint64(std::string&, std::uint64_t) { Todo("PutVarint64"); }
int VarintLength(std::uint64_t) { Todo("VarintLength"); }

const char* GetVarint32Ptr(const char*, const char*, std::uint32_t*) { Todo("GetVarint32Ptr"); }
const char* GetVarint64Ptr(const char*, const char*, std::uint64_t*) { Todo("GetVarint64Ptr"); }
bool GetVarint32(std::string_view*, std::uint32_t*) { Todo("GetVarint32"); }
bool GetVarint64(std::string_view*, std::uint64_t*) { Todo("GetVarint64"); }

void PutLengthPrefixed(std::string&, std::string_view) { Todo("PutLengthPrefixed"); }
bool GetLengthPrefixed(std::string_view*, std::string_view*) { Todo("GetLengthPrefixed"); }

}  // namespace lsm
