#pragma once
// M4 — MemTable. Notes: Part 5 §4; LevelDB db/memtable.cc (read after you're green).
//
// Each entry is ONE arena allocation laid out exactly like LevelDB:
//     varint32 internal_key_len | user_key | fixed64(seq<<8|type) | varint32 value_len | value
// The skip list stores `const char*` pointing at such entries; the comparator decodes the
// length-prefixed internal keys and compares them with InternalKeyCompare.
#include <memory>
#include <string>
#include <string_view>

#include "lsm/arena.hpp"
#include "lsm/internal_key.hpp"
#include "lsm/iterator.hpp"
#include "lsm/skiplist.hpp"

namespace lsm {

class MemTable {
public:
    MemTable();
    MemTable(const MemTable&) = delete;
    MemTable& operator=(const MemTable&) = delete;

    // Single writer only (external synchronization).
    void Add(SequenceNumber seq, ValueType type, std::string_view user_key, std::string_view value);

    // Newest version of user_key with sequence <= snapshot.
    LookupResult Get(std::string_view user_key, SequenceNumber snapshot, std::string* value) const;

    std::size_t ApproximateMemoryUsage() const;

    // Iterates (internal_key, value) in InternalKeyCompare order. The memtable must outlive it.
    std::unique_ptr<Iterator> NewIterator() const;

    struct EntryComparator {
        int operator()(const char* a, const char* b) const;
    };
    using Table = SkipList<const char*, EntryComparator>;

private:
    Arena arena_;   // declared BEFORE table_: the table allocates from it during construction
    Table table_;
};

}  // namespace lsm
