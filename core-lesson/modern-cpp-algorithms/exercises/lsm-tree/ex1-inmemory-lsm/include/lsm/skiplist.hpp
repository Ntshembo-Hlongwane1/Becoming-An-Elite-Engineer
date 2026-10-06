#pragma once
// M3 — Skip list (Pugh 1990), LevelDB-style. Notes: Part 3 §4, Part 2 §9 (placement new),
// Part 2 §13 (release/acquire).
//
// Contract (same as leveldb/db/skiplist.h — read its "Thread safety" comment AFTER you're green):
//  * Insert() requires external synchronization (one writer at a time).
//  * Readers (Contains, Iterator) take NO lock and may run concurrently with one writer.
//  * Nodes are allocated from the Arena with placement new and are never freed individually.
//  * A node's key is immutable after it is linked; only its next-pointers change.
//  * Publication: initialise the new node fully, then link it with a RELEASE store; readers follow
//    links with ACQUIRE loads.
//  * p = 1/4 (kBranching = 4), kMaxHeight = 12, deterministic RNG with a fixed seed.
//  * Duplicates are not allowed (assert in debug).
//
// Comparator: any type with `int operator()(const Key& a, const Key& b) const` returning <0, 0, >0.
//
// You may change everything in the private sections. Keep the public API.
#include <atomic>
#include <cassert>
#include <cstdint>

#include "lsm/arena.hpp"
#include "lsm/todo.hpp"

namespace lsm {

template <typename Key, class Comparator>
class SkipList {
public:
    static constexpr int kMaxHeight = 12;
    static constexpr unsigned kBranching = 4;

    SkipList(Comparator cmp, Arena* arena);
    SkipList(const SkipList&) = delete;
    SkipList& operator=(const SkipList&) = delete;

    void Insert(const Key& key);
    bool Contains(const Key& key) const;

    class Iterator {
    public:
        explicit Iterator(const SkipList* list);
        bool Valid() const;
        const Key& key() const;      // requires Valid()
        void Next();                 // requires Valid()
        void Prev();                 // requires Valid()
        void Seek(const Key& target);  // first entry with key >= target
        void SeekToFirst();
        void SeekToLast();

    private:
        const SkipList* list_;
        const void* node_ = nullptr;  // replace with your Node* type
    };

private:
    // Suggested design (Part 2 §9):
    //   struct Node {
    //       explicit Node(const Key& k) : key(k) {}
    //       Key const key;
    //       Node* Next(int level);                 // acquire load
    //       void SetNext(int level, Node* x);      // release store
    //       Node* NoBarrierNext(int level);        // relaxed
    //       void NoBarrierSetNext(int level, Node* x);
    //       std::atomic<Node*> next_[1];           // trailing array, really `height` long
    //   };
    //   Node* NewNode(const Key& key, int height); // arena->AllocateAligned + placement new
    //   int RandomHeight();
    //   Node* FindGreaterOrEqual(const Key& key, Node** prev) const;
    //   Node* FindLessThan(const Key& key) const;
    //   Node* FindLast() const;
    Comparator compare_;
    Arena* arena_;
    std::atomic<int> max_height_{1};
    std::uint32_t rng_state_ = 0xdeadbeef;
};

// ---- Stubs: replace each Todo() with your implementation. ----

template <typename Key, class Comparator>
SkipList<Key, Comparator>::SkipList(Comparator cmp, Arena* arena) : compare_(cmp), arena_(arena) {
    Todo("SkipList::SkipList (allocate head node of kMaxHeight)");
}

template <typename Key, class Comparator>
void SkipList<Key, Comparator>::Insert(const Key&) { Todo("SkipList::Insert"); }

template <typename Key, class Comparator>
bool SkipList<Key, Comparator>::Contains(const Key&) const { Todo("SkipList::Contains"); }

template <typename Key, class Comparator>
SkipList<Key, Comparator>::Iterator::Iterator(const SkipList* list) : list_(list) {}

template <typename Key, class Comparator>
bool SkipList<Key, Comparator>::Iterator::Valid() const { Todo("SkipList::Iterator::Valid"); }

template <typename Key, class Comparator>
const Key& SkipList<Key, Comparator>::Iterator::key() const { Todo("SkipList::Iterator::key"); }

template <typename Key, class Comparator>
void SkipList<Key, Comparator>::Iterator::Next() { Todo("SkipList::Iterator::Next"); }

template <typename Key, class Comparator>
void SkipList<Key, Comparator>::Iterator::Prev() { Todo("SkipList::Iterator::Prev"); }

template <typename Key, class Comparator>
void SkipList<Key, Comparator>::Iterator::Seek(const Key&) { Todo("SkipList::Iterator::Seek"); }

template <typename Key, class Comparator>
void SkipList<Key, Comparator>::Iterator::SeekToFirst() { Todo("SkipList::Iterator::SeekToFirst"); }

template <typename Key, class Comparator>
void SkipList<Key, Comparator>::Iterator::SeekToLast() { Todo("SkipList::Iterator::SeekToLast"); }

}  // namespace lsm
