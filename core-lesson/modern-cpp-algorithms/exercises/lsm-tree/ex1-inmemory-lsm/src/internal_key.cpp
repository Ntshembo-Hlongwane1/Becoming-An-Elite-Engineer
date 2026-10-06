#include "lsm/internal_key.hpp"
#include "lsm/todo.hpp"

namespace lsm {

std::uint64_t PackSequenceAndType(SequenceNumber, ValueType) { Todo("PackSequenceAndType"); }
void AppendInternalKey(std::string&, std::string_view, SequenceNumber, ValueType) { Todo("AppendInternalKey"); }
std::string MakeInternalKey(std::string_view, SequenceNumber, ValueType) { Todo("MakeInternalKey"); }
bool ParseInternalKey(std::string_view, ParsedInternalKey*) { Todo("ParseInternalKey"); }
std::string_view ExtractUserKey(std::string_view) { Todo("ExtractUserKey"); }
int BytewiseCompare(std::string_view, std::string_view) { Todo("BytewiseCompare"); }
int InternalKeyCompare(std::string_view, std::string_view) { Todo("InternalKeyCompare"); }
void FindShortestSeparator(std::string*, std::string_view) { Todo("FindShortestSeparator"); }
void FindShortSuccessor(std::string*) { Todo("FindShortSuccessor"); }

}  // namespace lsm
