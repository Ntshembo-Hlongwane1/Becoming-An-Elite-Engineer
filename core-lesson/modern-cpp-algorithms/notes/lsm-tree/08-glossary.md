# Glossary

One line each; the part where it's taught is in brackets.

| Term | Meaning |
|---|---|
| **acquire load** | atomic load after which all writes released by the matching store are visible [P2 §13] |
| **alignment** | power-of-two address multiple a type must sit at; misaligned object = UB [P2 §2] |
| **arena** | bump-pointer allocator; frees everything at once [P2 §8] |
| **back-pressure** | slowing/stopping producers when background work falls behind [P5 §9] |
| **block (SSTable)** | ~4 KiB unit of an SSTable read at once, with a 5-byte trailer (type+CRC) [P6 §5–6] |
| **block (WAL)** | 32 KiB framing unit of the log; resync point after corruption [P6 §3] |
| **BlockHandle** | (offset varint64, size varint64) pointer inside an SSTable [P6 §4] |
| **Bloom filter** | bit array answering "definitely not" / "maybe" for set membership [P3 §7] |
| **cache line** | 64-byte unit moved between RAM and CPU cache (measured) [P1 §3] |
| **comparator** | total order over keys; baked into files forever [P3 §2] |
| **compaction** | background merge of sorted runs into the next level, dropping garbage [P5 §7] |
| **CRC32C** | error-detecting checksum used per WAL record and per block [P3 §8] |
| **CURRENT** | text file naming the live MANIFEST; replaced atomically by rename [P6 §9] |
| **endianness** | byte order of multi-byte integers in memory/files [P2 §4] |
| **erase block** | 128–256 KiB flash unit that must be erased before pages can be reprogrammed [P1 §5] |
| **false sharing** | threads contending on one cache line through different variables [P2 §14] |
| **fdatasync / fsync** | flush file data (and needed/all metadata) to stable storage [P1 §7] |
| **footer** | fixed 48-byte tail of an SSTable: two handles + padding + magic [P6 §8] |
| **FTL** | flash translation layer; log-structured remapping inside an SSD [P1 §5] |
| **group commit** | one log write/fsync covering many writers' batches [P5 §2] |
| **immutable memtable** | a full memtable frozen while it is being flushed [P5 §5] |
| **index block** | separator key → BlockHandle for each data block [P6 §6] |
| **internal key** | user key ‖ fixed64(seq << 8 | type) [P5 §4] |
| **L0 / level** | L0 = overlapping fresh flushes; L≥1 = one non-overlapping sorted run each [P5 §6] |
| **leveling / tiering** | one run per level (read-optimised) vs up to T runs per level (write-optimised) [P4 §10, P5 §8] |
| **magic number** | fixed constant identifying a file type/version [P6 §8] |
| **MANIFEST** | log of VersionEdits describing which files are live [P6 §9] |
| **memtable** | in-memory sorted structure (skip list) absorbing writes [P5 §4] |
| **memory_order** | how an atomic op orders surrounding memory ops (relaxed/acquire/release/seq_cst) [P2 §13] |
| **padding** | unused bytes inserted in structs for alignment [P2 §3] |
| **page** | 4096-byte unit of virtual memory and page cache (measured) [P1 §2] |
| **page cache** | kernel's in-RAM cache of file pages; dirty pages written back later [P1 §6] |
| **placement new** | construct an object in pre-allocated storage [P2 §9] |
| **prefix compression** | storing only the bytes of a key that differ from the previous key [P6 §5] |
| **RAII** | resource lifetime bound to object lifetime [P2 §10] |
| **read / write / space amplification** | I/O per lookup / bytes written per user byte / bytes stored per unique byte [P5 §8] |
| **release store** | atomic store that publishes all prior writes to acquirers [P2 §13] |
| **rename (atomic)** | replaces the target name with no window where it's missing [P1 §7] |
| **restart point** | entry stored with full key; binary-search anchor inside a block [P6 §5] |
| **rolling merge** | the 1996 incremental merge between adjacent components [P4 §4] |
| **sector** | 512-byte disk unit; the only atomic write unit [P1 §4] |
| **separator key** | short key between two blocks' ranges, used in the index [P6 §6] |
| **sequence number** | global per-update counter giving a total order of writes [P5 §4] |
| **skip list** | probabilistically balanced linked list with express lanes [P3 §4] |
| **snapshot** | read view at a sequence number; compaction keeps versions it needs [P5 §10] |
| **sorted run** | sequence of unique keys in order; every on-disk component is one [P3 §1] |
| **SSTable** | immutable sorted file of blocks + index + filter + footer [P4 §9, P6 §4] |
| **tombstone** | deletion marker that shadows older values until safely dropped [P5 §7] |
| **torn write** | partially completed multi-sector write after power loss [P1 §4] |
| **varint** | 7-bits-per-byte integer encoding with a continuation bit [P2 §6] |
| **Version / VersionEdit** | immutable set of live files / a delta to it [P5 §10, P6 §9] |
| **WAL** | write-ahead log; appended before the memtable is updated [P5 §2] |
| **write stall** | throttling/blocking writes when L0 or memtables are full [P5 §9] |
