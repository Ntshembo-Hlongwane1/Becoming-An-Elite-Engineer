#pragma once
// D4 — MANIFEST: VersionEdits, replay, and the live-file state (FORMAT.md §5–§7; notes Part 6 §9–§11).
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "dlsm/env.hpp"
#include "dlsm/log.hpp"

namespace dlsm {

inline constexpr const char* kComparatorName = "dlsm.InternalKeyComparator.v1";

struct FileMetaData {
    std::uint64_t number = 0;
    std::uint64_t file_size = 0;
    std::string smallest;  // internal key
    std::string largest;   // internal key
};

struct VersionEdit {
    std::optional<std::string> comparator;
    std::optional<std::uint64_t> log_number;
    std::optional<std::uint64_t> next_file_number;
    std::optional<std::uint64_t> last_sequence;
    std::vector<std::pair<int, std::string>> compact_pointers;   // level, internal key
    std::vector<std::pair<int, std::uint64_t>> deleted_files;    // level, number
    std::vector<std::pair<int, FileMetaData>> new_files;         // level, file

    void EncodeTo(std::string* dst) const;            // tags in the order listed in FORMAT.md §5
    Status DecodeFrom(std::string_view src);          // unknown tag / truncation => Corruption
};

// The database structure obtained by replaying edits.
struct DbState {
    explicit DbState(int num_levels = 7);
    std::string comparator;
    std::uint64_t log_number = 0;
    std::uint64_t next_file_number = 2;
    std::uint64_t last_sequence = 0;
    std::vector<std::vector<FileMetaData>> levels;      // L0 newest first; L>=1 sorted by smallest
    std::vector<std::string> compact_pointers;          // per level

    // Applies one edit. Corruption if it deletes a file that isn't live, adds a duplicate number,
    // or names a level out of range.
    Status Apply(const VersionEdit& edit);
    // One edit that recreates this whole state (first record of a new MANIFEST).
    VersionEdit Snapshot() const;
};

// Owns the open MANIFEST and appends edits durably.
class ManifestWriter {
public:
    // Creates MANIFEST-<number> (OpenMode::kCreateNew), writes state.Snapshot(), syncs it, fsyncs the
    // directory, then calls SetCurrentFile(dir, number). After this returns OK, a crash recovers `state`.
    static Status Create(const std::string& dir, std::uint64_t number, const DbState& state,
                         std::unique_ptr<ManifestWriter>* out);
    // AddRecord(edit) + Sync. This is the COMMIT POINT for flushes and compactions.
    Status Append(const VersionEdit& edit);
    std::uint64_t number() const { return number_; }

private:
    ManifestWriter() = default;
    std::uint64_t number_ = 0;
    std::unique_ptr<WritableFile> file_;
    std::unique_ptr<log::Writer> writer_;
};

// CURRENT -> MANIFEST -> replay (FORMAT.md §7 steps 2–4). A torn last record is ignored; corruption
// before the end, a bad CURRENT, or a comparator mismatch is an error.
Status RecoverDbState(const std::string& dir, int num_levels, DbState* state, std::uint64_t* manifest_number);

}  // namespace dlsm
