#include "dlsm/version.hpp"
#include "lsm/todo.hpp"

namespace dlsm {
using lsm::Todo;

void VersionEdit::EncodeTo(std::string*) const { Todo("VersionEdit::EncodeTo"); }
Status VersionEdit::DecodeFrom(std::string_view) { Todo("VersionEdit::DecodeFrom"); }

DbState::DbState(int num_levels)
    : levels(static_cast<std::size_t>(num_levels)), compact_pointers(static_cast<std::size_t>(num_levels)) {}
Status DbState::Apply(const VersionEdit&) { Todo("DbState::Apply"); }
VersionEdit DbState::Snapshot() const { Todo("DbState::Snapshot"); }

Status ManifestWriter::Create(const std::string&, std::uint64_t, const DbState&, std::unique_ptr<ManifestWriter>*) {
    Todo("ManifestWriter::Create");
}
Status ManifestWriter::Append(const VersionEdit&) { Todo("ManifestWriter::Append"); }

Status RecoverDbState(const std::string&, int, DbState*, std::uint64_t*) { Todo("RecoverDbState"); }

}  // namespace dlsm
