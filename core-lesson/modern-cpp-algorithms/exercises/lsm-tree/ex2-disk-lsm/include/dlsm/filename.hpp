#pragma once
// D4 — File naming (FORMAT.md §1) and the atomic CURRENT switch (notes Part 1 §8, Part 6 §9).
#include <cstdint>
#include <string>
#include <string_view>

#include "dlsm/env.hpp"

namespace dlsm {

enum class FileType { kLog, kTable, kManifest, kCurrent, kLock, kTemp };

std::string LogFileName(const std::string& dir, std::uint64_t number);       // dir/000007.log
std::string TableFileName(const std::string& dir, std::uint64_t number);     // dir/000007.sst
std::string ManifestFileName(const std::string& dir, std::uint64_t number);  // dir/MANIFEST-000007
std::string TempFileName(const std::string& dir, std::uint64_t number);      // dir/000007.dbtmp
std::string CurrentFileName(const std::string& dir);                         // dir/CURRENT
std::string LockFileName(const std::string& dir);                            // dir/LOCK

// Parse a bare file name (no directory). Numbers for CURRENT/LOCK are 0. Rejects anything else,
// including "000007.log.bak", "MANIFEST-", "MANIFEST-12x", numbers that overflow uint64.
bool ParseFileName(std::string_view name, std::uint64_t* number, FileType* type);

// temp file "MANIFEST-<n>\n" -> fsync -> rename to CURRENT -> fsync(dir). Removes the temp on failure.
Status SetCurrentFile(const std::string& dir, std::uint64_t manifest_number);

}  // namespace dlsm
