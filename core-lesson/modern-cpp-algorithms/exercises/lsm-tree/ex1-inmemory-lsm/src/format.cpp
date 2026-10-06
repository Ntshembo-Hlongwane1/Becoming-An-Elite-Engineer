#include "lsm/format.hpp"
#include "lsm/todo.hpp"

namespace lsm {

void BlockHandle::EncodeTo(std::string&) const { Todo("BlockHandle::EncodeTo"); }
Status BlockHandle::DecodeFrom(std::string_view*) { Todo("BlockHandle::DecodeFrom"); }
void Footer::EncodeTo(std::string&) const { Todo("Footer::EncodeTo"); }
Status Footer::DecodeFrom(std::string_view) { Todo("Footer::DecodeFrom"); }
BlockHandle AppendBlockWithTrailer(std::string&, std::string_view) { Todo("AppendBlockWithTrailer"); }
Status VerifyBlock(std::string_view, std::string_view*) { Todo("VerifyBlock"); }

}  // namespace lsm
