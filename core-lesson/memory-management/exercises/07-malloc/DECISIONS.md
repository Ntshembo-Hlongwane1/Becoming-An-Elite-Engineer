# Decisions (one line each: decision — why — source)

## align_up / rounding to 16 (why 16, and why it frees the size field's low bits)

## find_fit policy (first-fit vs best-fit — which, and the fragmentation trade-off you accept)

## split: the remainder >= kMinBlock rule (why a smaller remainder can't become a free block)

## coalesce: the four neighbour cases, and the order you merge (forward/backward) — why it's safe

## boundary tag: why you write BOTH header and footer (set_block) on every size change

## free list membership: when a block is on the list vs off it during allocate/split/coalesce

## double-free handling (what deallocate does if the block is already free — assert vs ignore)

## RESULTS.md: your bench/heap_probe numbers vs the notes (break growth, mmap threshold, stride)
