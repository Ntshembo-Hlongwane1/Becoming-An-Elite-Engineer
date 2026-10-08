# Lesson 10 — Glossary

| Term | One line | § |
|---|---|---|
| custom allocator | allocator specialised to a workload's pattern; faster/simpler than malloc | 10.1 |
| allocation pattern | same-size-many / same-lifetime-freed-together; what you exploit | 10.1 |
| arena / bump / monotonic | one pointer bumped forward; O(1) alloc, no per-object free | 10.2 |
| `reset()` | arena op that frees everything at once (offset ← 0); runs NO destructors | 10.2 |
| backing buffer | the one big block an allocator carves; it owns + frees it once | 10.2 |
| upstream resource | where an allocator gets more memory when its buffer is exhausted | 10.2/10.4 |
| pool / fixed-block | free list of equal-sized blocks; O(1) alloc AND free | 10.3 |
| intrusive free list | free-block `next` pointer stored inside the free block itself | 10.3 |
| block size lower bounds | ≥ sizeof(T) and ≥ sizeof(FreeNode), aligned to the object | 10.3 |
| address recycling | freeing then allocating returns the same block (pool/fastbin) | 10.3 |
| external fragmentation (absent) | impossible in a pool: all blocks interchangeable | 10.3 |
| `Allocator` (named req) | classic template allocator: value_type + allocate/deallocate | 10.4 |
| `std::allocator_traits` | adapter supplying defaults + rebind for an allocator | 10.4 |
| rebind | derive an allocator for a different type (e.g. list nodes) | 10.4 |
| template infection | allocator as a template param changes the container's type | 10.4 |
| `std::pmr::memory_resource` | abstract allocator base; do_allocate/do_deallocate/do_is_equal | 10.4 |
| `do_allocate(bytes, align)` | virtual: ≥bytes aligned to power-of-two align; throws on failure | 10.4 |
| `do_is_equal` | can memory from one resource be freed by the other? | 10.4 |
| `polymorphic_allocator` | Allocator holding a memory_resource*; runtime-chosen behaviour | 10.4 |
| `std::pmr::vector<T>` | `vector<T, polymorphic_allocator<T>>`; one type, any resource | 10.4 |
| `monotonic_buffer_resource` | standard arena: deallocate is a no-op, releases on destruction | 10.4 |
| pool_resource (sync/unsync) | standard size-class pools; unsync is faster, not thread-safe | 10.4 |
| `null_memory_resource()` | always throws; as upstream it forbids heap fallback (hard bound) | 10.4 |
| non-propagating allocator | pmr alloc doesn't move on copy/move/swap → don't swap across resources | 10.4 |
| allocator as mitigation | partitioned/isolated heaps, wipe-on-free, red-zones, guard pages | 10.5 |
| ASan blindness | arena/pool reuse inside one malloc'd buffer → ASan can't see it | 10.5 |
