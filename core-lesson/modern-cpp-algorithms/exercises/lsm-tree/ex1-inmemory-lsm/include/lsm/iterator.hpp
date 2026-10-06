#pragma once
// M8 — Iterator interface and k-way merging. Notes: Part 3 §6, Part 5 §6.
#include <memory>
#include <string_view>
#include <vector>

#include "lsm/internal_key.hpp"
#include "lsm/status.hpp"

namespace lsm {

// A forward iterator over (key, value) byte strings in some sorted order.
// key()/value() views stay valid until the iterator is moved or destroyed.
class Iterator {
public:
    virtual ~Iterator() = default;
    virtual bool Valid() const = 0;
    virtual void SeekToFirst() = 0;
    virtual void Seek(std::string_view target) = 0;  // first entry with key >= target
    virtual void Next() = 0;                         // requires Valid()
    virtual std::string_view key() const = 0;        // requires Valid()
    virtual std::string_view value() const = 0;      // requires Valid()
    virtual Status status() const = 0;               // non-OK if corruption was hit
};

// An iterator over nothing (optionally carrying an error status).
std::unique_ptr<Iterator> NewEmptyIterator(Status s = Status::OK());

// Iterator over a std::vector of (key, value) pairs that is already sorted by `cmp`.
// Provided as a test helper; implement it, it's 30 lines.
std::unique_ptr<Iterator> NewVectorIterator(
    std::vector<std::pair<std::string, std::string>> sorted_entries, CompareFn cmp);

// K-way merge of `children` (each sorted by `cmp`) into one sorted stream. Equal keys from different
// children are all emitted; ties are broken by child index (lower index first) so that callers can
// put NEWER sources at lower indexes. `use_heap` selects a binary heap; otherwise a linear scan.
std::unique_ptr<Iterator> NewMergingIterator(CompareFn cmp,
                                             std::vector<std::unique_ptr<Iterator>> children,
                                             bool use_heap);

// Turns a merged stream of INTERNAL keys into the user-visible view at `snapshot`:
// for each user key, skip versions with seq > snapshot, emit the newest remaining version if it is a
// value, emit nothing if it is a deletion. key() returns the USER key.
std::unique_ptr<Iterator> NewDBIterator(std::unique_ptr<Iterator> internal_iter,
                                        SequenceNumber snapshot);

}  // namespace lsm
