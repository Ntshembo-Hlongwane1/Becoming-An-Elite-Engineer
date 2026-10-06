# Part 5 — The LSM Engine, End to End

> Now we assemble Parts 1–4 into a working machine, following LevelDB/RocksDB closely, because
> their source is the best "reputable textbook" on this topic that exists. Code excerpts are from
> LevelDB `main` (fetched 2026-10-04).

Contents

1. The three constructs and the files on disk
2. The write path, step by step (and group commit)
3. What "committed" means: `sync=false` vs `sync=true`
4. Inside the memtable: internal keys, sequence numbers, value types
5. Memtable full → immutable memtable → flush to L0
6. Levels, overlap rules, and the read path
7. Compaction: picking, merging, dropping (with the exact LevelDB rule)
8. Leveling vs tiering, and the amplification math
9. Write stalls: when compaction falls behind
10. Concurrency & lifetime: reference counting, versions, snapshots, iterators
11. Recovery (preview — full byte-level version in Part 6)
12. Summary + drills

---

## 1. The three constructs

RocksDB: "The three basic constructs of RocksDB are **memtable**, **sstfile** and **logfile**. The
memtable is an in-memory data structure — new writes are inserted into the memtable and are
optionally written to the logfile (aka. Write Ahead Log(WAL)). The logfile is a sequentially-written
file on storage. When the memtable fills up, it is flushed to a sstfile on storage and the
corresponding logfile can be safely deleted. The data in an sstfile is sorted to facilitate easy
lookup of keys." `[ROCKS-OVERVIEW]`

LevelDB's database directory `[LDB-IMPL]`:

| File | Role |
|---|---|
| `000123.log` | WAL: "stores a sequence of recent updates. Each update is appended to the current log file." |
| `000124.ldb` | SSTable: "a sequence of entries sorted by key. Each entry is either a value for the key, or a deletion marker" |
| `MANIFEST-000005` | "lists the set of sorted tables that make up each level, the corresponding key ranges, and other important metadata … formatted as a log" |
| `CURRENT` | "a simple text file that contains the name of the latest MANIFEST file" |
| `LOCK` | prevents two processes from opening the same DB ("A database may only be opened by one process at a time" `[LDB-DOC]`) |
| `LOG`, `LOG.old` | human-readable info logs |

**(derived)** All numbered files share one monotonically increasing counter (the "next file
number", stored in the MANIFEST as `kNextFileNumber` `[LDB-SRC:db/version_edit.cc]`). Newer file ⇒
bigger number. That single fact is used for ordering L0 files (§6) and for garbage collection.

## 2. The write path

User-facing API `[LDB-DOC]`: `Put(key, value)`, `Delete(key)`, and `Write(batch)`. A `WriteBatch`
"holds a sequence of edits to be made to the database, and these edits within the batch are
applied in order" — atomically.

Its encoding `[LDB-SRC:db/write_batch.cc]`:

```
WriteBatch::rep_ :=
   sequence: fixed64
   count: fixed32
   data: record[count]
record :=
   kTypeValue varstring varstring         |
   kTypeDeletion varstring
varstring :=
   len: varint32
   data: uint8[len]
```

`DBImpl::Write` `[LDB-SRC:db/db_impl.cc]`, annotated:

```cpp
Status DBImpl::Write(const WriteOptions& options, WriteBatch* updates) {
  Writer w(&mutex_);
  w.batch = updates;  w.sync = options.sync;  w.done = false;

  MutexLock l(&mutex_);
  writers_.push_back(&w);                          // (1) join the queue of writers
  while (!w.done && &w != writers_.front()) {
    w.cv.Wait();                                   //     wait until I'm the leader (or someone did my work)
  }
  if (w.done) return w.status;                     //     another leader committed my batch for me

  Status status = MakeRoomForWrite(updates == nullptr);   // (2) maybe stall / switch memtable (§9)
  uint64_t last_sequence = versions_->LastSequence();
  Writer* last_writer = &w;
  if (status.ok() && updates != nullptr) {
    WriteBatch* write_batch = BuildBatchGroup(&last_writer);    // (3) GROUP COMMIT: merge queued batches
    WriteBatchInternal::SetSequence(write_batch, last_sequence + 1);   // (4) assign sequence numbers
    last_sequence += WriteBatchInternal::Count(write_batch);
    {
      mutex_.Unlock();
      status = log_->AddRecord(WriteBatchInternal::Contents(write_batch));   // (5) append to WAL
      bool sync_error = false;
      if (status.ok() && options.sync) {
        status = logfile_->Sync();                                           // (6) fsync if asked
        if (!status.ok()) sync_error = true;
      }
      if (status.ok()) {
        status = WriteBatchInternal::InsertInto(write_batch, mem_);          // (7) apply to memtable
      }
      mutex_.Lock();
      if (sync_error) {
        // The state of the log file is indeterminate: the log record we
        // just added may or may not show up when the DB is re-opened.
        // So we force the DB into a mode where all future writes fail.
        RecordBackgroundError(status);                                       // (8) fail-stop
      }
    }
    versions_->SetLastSequence(last_sequence);                               // (9) make visible
  }
  ... wake every writer whose batch was included, signal the next leader ...
}
```

What to take away:

- **Order: WAL first, memtable second.** If we crash after (5)/(6), replaying the WAL rebuilds the
  memtable. If we inserted into the memtable first and crashed, an acknowledged write could vanish
  **(derived)**.
- **Group commit (3):** "Group commit is used to improve the throughput of lots of small mutations"
  `[BIGTABLE06 §5.3]`; RocksDB: "batch-commit mechanism to batch transactions into the log so that it
  can potentially commit multiple transactions using a single fsync call" `[ROCKS-OVERVIEW]`. One
  leader writes everyone's batches; one fsync covers all of them.
- **Fail-stop on sync error (8)** is exactly the fsyncgate lesson (Part 1 §9): after a failed sync,
  the file's state is unknown, so the engine refuses further writes.
- **Visibility (9):** readers use `LastSequence()` as their implicit snapshot (§10); bumping it
  *after* the memtable insert means readers never see a half-applied batch **(derived)**.

## 3. What "committed" means

From LevelDB's options and docs:

- `sync = false` (default): "a DB write with sync==false has similar crash semantics as the
  'write()' system call. A DB write with sync==true has similar crash semantics to a 'write()' system
  call followed by 'fsync()'." `[LDB-SRC:include/leveldb/options.h]`
- "Asynchronous writes are often more than a thousand times as fast as synchronous writes. The
  downside of asynchronous writes is that a crash of the machine may cause the last few updates to be
  lost. Note that a crash of just the writing process (i.e., not a reboot) will not cause any loss"
  `[LDB-DOC]` — because the data already reached the kernel's page cache (Part 1 §6).
- A middle ground: put many updates in one `WriteBatch` with `sync=true`: "The extra cost of the
  synchronous write will be amortized across all of the writes in the batch." `[LDB-DOC]`

> **Systems-engineer reflex #6:** "durable" is a *contract you choose per write*, with a price. State
> it explicitly in your API (your object store will need this decision too).

## 4. Inside the memtable

### Entry encoding `[LDB-SRC:db/memtable.cc]`

```cpp
void MemTable::Add(SequenceNumber s, ValueType type, const Slice& key, const Slice& value) {
  // Format of an entry is concatenation of:
  //  key_size     : varint32 of internal_key.size()
  //  key bytes    : char[internal_key.size()]
  //  tag          : uint64((sequence << 8) | type)
  //  value_size   : varint32 of value.size()
  //  value bytes  : char[value.size()]
  size_t key_size = key.size();
  size_t val_size = value.size();
  size_t internal_key_size = key_size + 8;
  const size_t encoded_len = VarintLength(internal_key_size) + internal_key_size +
                             VarintLength(val_size) + val_size;
  char* buf = arena_.Allocate(encoded_len);          // one arena allocation per entry (Part 2 §8)
  char* p = EncodeVarint32(buf, internal_key_size);
  std::memcpy(p, key.data(), key_size);
  p += key_size;
  EncodeFixed64(p, (s << 8) | type);
  p += 8;
  p = EncodeVarint32(p, val_size);
  std::memcpy(p, value.data(), val_size);
  assert(p + val_size == buf + encoded_len);
  table_.Insert(buf);                                // skip list of const char* (Part 3 §4)
}
```

So the skip list stores **pointers to self-describing byte strings in the arena** — not
`std::string`s, not structs. One allocation, contiguous bytes, no padding, no destructors **(derived)**.

### Internal keys and the 8-byte tag `[LDB-SRC:db/dbformat.h]`

```cpp
// Value types encoded as the last component of internal keys.
// DO NOT CHANGE THESE ENUM VALUES: they are embedded in the on-disk
// data structures.
enum ValueType { kTypeDeletion = 0x0, kTypeValue = 0x1 };

typedef uint64_t SequenceNumber;
// We leave eight bits empty at the bottom so a type and sequence#
// can be packed together into 64-bits.
static const SequenceNumber kMaxSequenceNumber = ((0x1ull << 56) - 1);
```

**internal key = user key bytes ‖ fixed64( sequence << 8 | type )**

- **Sequence number**: a global counter, +1 per update (assigned in §2 step 4). It totally orders
  all writes in time.
- **Type**: value or **tombstone** (deletion).
- 56 bits of sequence ⇒ 2⁵⁶ ≈ 7.2 × 10¹⁶ updates before wrap **(derived)**.
- "DO NOT CHANGE THESE ENUM VALUES" — once a number is in a file, it's an ABI forever.

### The internal-key order `[LDB-SRC:db/dbformat.cc]`

```cpp
int InternalKeyComparator::Compare(const Slice& akey, const Slice& bkey) const {
  // Order by:
  //    increasing user key (according to user-supplied comparator)
  //    decreasing sequence number
  //    decreasing type (though sequence# should be enough to disambiguate)
  int r = user_comparator_->Compare(ExtractUserKey(akey), ExtractUserKey(bkey));
  if (r == 0) {
    const uint64_t anum = DecodeFixed64(akey.data() + akey.size() - 8);
    const uint64_t bnum = DecodeFixed64(bkey.data() + bkey.size() - 8);
    if (anum > bnum) r = -1;
    else if (anum < bnum) r = +1;
  }
  return r;
}
```

Example — three writes to key `"k"`: Put(k,a)@5, Put(k,b)@9, Delete(k)@12. Sorted:

```
("k", seq 12, DEL)   ← newest first
("k", seq  9, VAL b)
("k", seq  5, VAL a)
```

**(derived)** Consequences:

- *Point lookup*: seek to `(k, snapshot_seq)`; the first entry with that user key is the newest
  version visible to the snapshot. `MemTable::Get` does exactly this: "We do not check the sequence
  number since the Seek() call above should have skipped all entries with overly large sequence
  numbers", then switches on the type: value → return it; deletion → `NotFound`
  `[LDB-SRC:db/memtable.cc]`.
- *Merging*: "newest wins" (Part 3 §6) falls out of the sort order — the first version you meet is
  the newest.
- *Snapshots for free*: a reader with snapshot seq 9 seeks to `(k, 9)` and sees `b`, even though a
  delete @12 exists.

## 5. Memtable full → immutable → L0

LevelDB `[LDB-IMPL]`, "Level 0": when the log grows past ~4 MB:

> "Create a brand new memtable and log file and direct future updates here.
> In the background:
> 1. Write the contents of the previous memtable to an sstable.
> 2. Discard the memtable.
> 3. Delete the old log file and the old memtable.
> 4. Add the new sstable to the young (level-0) level."

RocksDB generalises it into a pipeline: "When a memtable is full, it becomes an immutable memtable
and a background thread starts flushing its contents to storage. Meanwhile, new writes continue to
accumulate to a newly allocated memtable." `[ROCKS-OVERVIEW]`. During flush, "Duplicate updates for
the same key are removed from the output stream. Similarly, if an earlier put is hidden by a later
delete, then the put is not written to the output file at all." `[ROCKS-OVERVIEW]`

**(derived)** The ordering that makes this crash-safe (detailed in Part 6 §8):
`write SSTable → fsync it → record "SSTable added, old log obsolete" in the MANIFEST → fsync
MANIFEST → only then delete the old log`. Deleting the log first would lose data if we crashed
before the SSTable was durable.

## 6. Levels, overlap, and the read path

### Overlap rules `[LDB-IMPL]`

- "Files in the young level may contain overlapping keys" — each L0 file is one flushed memtable,
  covering whatever keys it saw.
- "However files in other levels have distinct non-overlapping key ranges." Each level L ≥ 1 is a
  single sorted run split into ~2 MB files `[LDB-IMPL]` (`max_file_size = 2 * 1024 * 1024`
  `[LDB-SRC:include/leveldb/options.h]`).
- Level size limits: "(10^L) MB (i.e., 10MB for level-1, 100MB for level-2, ...)". LevelDB has
  `kNumLevels = 7` `[LDB-SRC:db/dbformat.h]`.

### The read path

`DBImpl::Get` `[LDB-SRC:db/db_impl.cc]`:

```cpp
snapshot = (options.snapshot != nullptr) ? <that snapshot's seq> : versions_->LastSequence();
MemTable* mem = mem_;  MemTable* imm = imm_;  Version* current = versions_->current();
mem->Ref();  if (imm != nullptr) imm->Ref();  current->Ref();     // pin everything we'll read (§10)
mutex_.Unlock();
// First look in the memtable, then in the immutable memtable (if any).
LookupKey lkey(key, snapshot);
if (mem->Get(lkey, value, &s)) { /* Done */ }
else if (imm != nullptr && imm->Get(lkey, value, &s)) { /* Done */ }
else { s = current->Get(options, lkey, value, &stats); }        // then the SSTables
```

and `Version::ForEachOverlapping` `[LDB-SRC:db/version_set.cc]`:

```cpp
// Search level-0 in order from newest to oldest.
... collect every L0 file whose [smallest, largest] user-key range contains user_key ...
std::sort(tmp.begin(), tmp.end(), NewestFirst);       // NewestFirst: a->number > b->number
... probe each; stop at the first that answers ...

// Search other levels.
for (int level = 1; level < config::kNumLevels; level++) {
  // Binary search to find earliest index whose largest key >= internal_key.
  uint32_t index = FindFile(vset_->icmp_, files_[level], internal_key);
  ... at most ONE file per level can contain the key ...
}
```

Inside one SSTable (Part 6 has the bytes): check the **Bloom filter** → binary-search the **index
block** (in memory) → `pread` **one data block** → verify its CRC → search inside the block. That's
Bigtable's "single disk seek" `[BIGTABLE06 §4]`.

**Worst case cost (derived):** memtable + immutable + every overlapping L0 file (up to the stop
trigger, §9) + one file per level. Bloom filters make the *misses* nearly free; the *hit* costs about
one block read. This is the "read amplification" of §8.

## 7. Compaction

### When and what `[LDB-IMPL]`

- L0 → L1 when "the number of young files exceeds a certain threshold (currently four)"
  (`kL0_CompactionTrigger = 4` `[LDB-SRC:db/dbformat.h]`): "all of the young files are merged together
  with all of the overlapping level-1 files".
- L → L+1 when level L exceeds its size limit: "one file in level-L, and all of the overlapping files
  in level-(L+1) are merged to form a set of new files for level-(L+1)."
- **Round-robin**: "for each level L, we remember the ending key of the last compaction at level L.
  The next compaction for level L will pick the first file that starts after this key" — so
  compactions "rotate through the key space" (stored as `kCompactPointer` in the MANIFEST).
- Output files are cut at 2 MB, and also "when the key range of the current output file has grown
  enough to overlap more than ten level-(L+2) files", bounding the *next* compaction's size.
- RocksDB picks the level with the highest **score** = size/target (or L0 file count / trigger)
  `[ROCKS-LEVELED]`.

### Cost of one compaction `[LDB-IMPL]` "Timing"

"we will pick one 2MB file from level L. In the worst case, this will overlap ~ 12 files from level
L+1 (10 because level-(L+1) is ten times the size of level-L, and another two at the boundaries …).
The compaction will therefore read 26MB and write 26MB. Assuming a disk IO rate of 100MB/s …, the
worst compaction cost will be approximately 0.5 second."

### The merge, and what gets dropped — LevelDB's exact rule `[LDB-SRC:db/db_impl.cc]`

The compaction is a k-way merge (Part 3 §6) over the input files in internal-key order. For each
entry:

```cpp
if (!has_current_user_key ||
    user_comparator()->Compare(ikey.user_key, Slice(current_user_key)) != 0) {
  // First occurrence of this user key
  current_user_key.assign(ikey.user_key.data(), ikey.user_key.size());
  has_current_user_key = true;
  last_sequence_for_key = kMaxSequenceNumber;
}

if (last_sequence_for_key <= compact->smallest_snapshot) {
  // Hidden by an newer entry for same user key
  drop = true;  // (A)
} else if (ikey.type == kTypeDeletion &&
           ikey.sequence <= compact->smallest_snapshot &&
           compact->compaction->IsBaseLevelForKey(ikey.user_key)) {
  // For this user key:
  // (1) there is no data in higher levels
  // (2) data in lower levels will have larger sequence numbers
  // (3) data in layers that are being compacted here and have
  //     smaller sequence numbers will be dropped in the next
  //     few iterations of this loop (by rule (A) above).
  // Therefore this deletion marker is obsolete and can be dropped.
  drop = true;
}
last_sequence_for_key = ikey.sequence;
```

(LevelDB's comments say "higher levels" for the *larger-numbered*, older levels.)

Read rule (A) slowly **(derived)**: entries for one user key arrive newest-first. `last_sequence_for_key`
is the sequence of the *previous (newer)* version we saw. If that newer version is already visible to
**every** live snapshot (`<= smallest_snapshot`), nobody can ever see this older one → drop. With no
snapshots open, `smallest_snapshot` is the latest sequence, so every non-newest version is dropped.

Rule (B), tombstones: a deletion marker may be dropped only if (i) no snapshot could still need it and
(ii) **no older level below the compaction output holds this key** (`IsBaseLevelForKey`). This matches
`[LDB-IMPL]`: "They also drop deletion markers if there are no higher numbered levels that contain a
file whose range overlaps the current key."

> **Why (ii) is a correctness rule, not an optimisation:** drop the tombstone too early and an older
> value deeper down becomes visible again — the deleted data **comes back to life** ("resurrection").
> Part 7 treats this as a security bug class.

## 8. Leveling vs tiering, and the amplification math

### Three amplifications (definitions)

- **Write amplification** "measures the amortized I/O cost of inserting an entry into an LSM-tree"
  `[LUO20 §2.3]` — physically written bytes ÷ user bytes.
- **Read amplification**: I/Os (or files probed) per lookup — determined by "the number of components"
  `[LUO20 §2.3]`.
- **Space amplification**: "the overall number of entries divided by the number of unique entries"
  `[LUO20 §2.3]`.

The **RUM conjecture**: "each access method has to make certain trade-offs among the read cost (R),
update cost (U), and memory or storage cost (M)" `[LUO20 §2.3]`. You can move cost around, not
eliminate it.

### The cost table `[LUO20 Table 1]`

T = size ratio, L = number of levels, B = entries per page, M = total Bloom bits, N = total entries,
s = entries in a range query.

| Policy | Write | Point lookup (miss / hit) | Short range | Long range | Space amp |
|---|---|---|---|---|---|
| Leveling | O(T·L/B) | O(L·e^(−M/N)) / O(1) | O(L) | O(s/B) | O((T+1)/T) |
| Tiering | O(L/B) | O(T·L·e^(−M/N)) / O(1) | O(T·L) | O(T·s/B) | O(T) |

Where the T comes from `[LUO20 §2.3]`: "For leveling, a component at each level will be merged T − 1
times until it fills up and is pushed to the next level. For tiering, multiple components at each
level are merged only once".

Worked **(derived)**: leveling, T = 10, L = 5 levels → each byte is rewritten up to ~10 times per
level × 5 levels ≈ **write amplification up to ~50** (plus 1 for the WAL, 1 for the flush). That is
why "the leveling merge policy, which has been adopted by modern key-value stores such as LevelDB and
RocksDB, still incurs relatively high write amplification", which "reduces the lifespan of SSDs"
`[LUO20 §3.1]`.

RocksDB's summary of the trade-off `[ROCKS-OVERVIEW]`: Level style "optimizes disk footprint vs.
logical database size (space amplification)"; Universal (tiered) "optimizes total bytes written to
disk vs. logical database size (write amplification) … typically results in lower write-amplification
but higher space- and read-amplification". And a hidden cost of tiering: "During compaction, both of
input files and the output file need to be kept, so the DB will be temporarily double the disk space
usage" `[ROCKS-UNIVERSAL]`.

## 9. Write stalls: when compaction falls behind

Writes are cheap; compaction is the debt they create. If debt grows faster than it is repaid, L0
fills with overlapping files and *reads* slow down (each L0 file is probed). LevelDB's design doc
works the numbers: with compaction throttled to 10 MB/s and users writing 10 MB/s, "we might build up
lots of level-0 files (~50 to hold the 5*10MB). This may significantly increase the cost of reads"
`[LDB-IMPL]`. Among its proposed solutions: "decrease write rate artificially when the number of
level-0 files goes up".

That is exactly what the code does `[LDB-SRC:db/dbformat.h]`, `[LDB-SRC:db/db_impl.cc]`:

```cpp
static const int kL0_CompactionTrigger = 4;       // start compacting L0
static const int kL0_SlowdownWritesTrigger = 8;   // "Soft limit … We slow down writes at this point."
static const int kL0_StopWritesTrigger = 12;      // "Maximum number of level-0 files. We stop writes"
```

```cpp
} else if (allow_delay && versions_->NumLevelFiles(0) >= config::kL0_SlowdownWritesTrigger) {
  // We are getting close to hitting a hard limit on the number of
  // L0 files.  Rather than delaying a single write by several
  // seconds when we hit the hard limit, start delaying each
  // individual write by 1ms to reduce latency variance.
  mutex_.Unlock();
  env_->SleepForMicroseconds(1000);
  ...
} else if (versions_->NumLevelFiles(0) >= config::kL0_StopWritesTrigger) {
  // There are too many level-0 files.
  Log(options_.info_log, "Too many L0 files; waiting...\n");
  background_work_finished_signal_.Wait();
}
```

This is **back-pressure**: the system pushes back on its producers instead of collapsing. RocksDB also
reserves threads for flushes so "a sudden burst of writes" doesn't stall behind long compactions
`[ROCKS-OVERVIEW]`.

> **Systems-engineer reflex #7:** every asynchronous background process needs (a) a measure of its
> debt, (b) a soft limit that slows producers smoothly, (c) a hard limit that stops them. Otherwise
> you have an unbounded queue — a memory/latency bomb and a DoS vector (Part 7).

## 10. Concurrency & lifetime: versions, refs, snapshots, iterators

The survey `[LUO20 §2.2.3]`: "Concurrent flush and merge operations … modify the metadata of an
LSM-tree, e.g., the list of active components. Thus, accesses to the component metadata must be
properly synchronized. To prevent a component in use from being deleted, each component can maintain a
reference counter. Before accessing the components of an LSM-tree, a query can first obtain a snapshot
of active components and increment their in-use counters."

That's the `mem->Ref(); imm->Ref(); current->Ref();` you saw in `DBImpl::Get`. In LevelDB a
**Version** is an immutable list of files per level; a compaction produces a *new* Version; old
Versions live as long as someone holds a reference **(derived from the code in §6)**. File deletion
waits for that: RocksDB "When compaction finishes, the input SST files are replaced by the output ones
in the LSM-tree. However, they may not qualify for being deleted immediately. Ongoing operations
depending the older version of the LSM-tree …" `[ROCKS-DELETE]`.

Two related user-facing features `[ROCKS-OVERVIEW]`, `[LDB-DOC]`:

- **Iterator**: "A consistent-point-in-time view of the database is created when the Iterator is
  created … An Iterator keeps a reference count on all underlying files that correspond to that
  point-in-time-view of the database - these files are not deleted until the Iterator is released."
- **Snapshot**: "does not prevent file deletions; instead the compaction process understands the
  existence of Snapshots and promises never to delete a key that is visible in any existing Snapshot."
  (That's `smallest_snapshot` in §7.) "Snapshots are not persisted across database restarts".

Connect to Part 2 §11: memtable `Slice`s point into the arena; block `Slice`s point into block
buffers. Ref counting is what keeps those views valid **(derived)**.

## 11. Recovery (preview)

`[LDB-IMPL]`:

> * Read CURRENT to find name of the latest committed MANIFEST
> * Read the named MANIFEST file
> * Clean up stale files
> * We could open all sstables here, but it is probably better to be lazy...
> * Convert log chunk to a new level-0 sstable
> * Start directing new writes to a new log file with recovered sequence#

And "Garbage collection of files": `RemoveObsoleteFiles()` "deletes all log files that are not the
current log file. It deletes all table files that are not referenced from some level and are not the
output of an active compaction." `[LDB-IMPL]`

The survey explains the design choice: "existing systems typically employ a no-steal buffer
management policy … During recovery … the transaction log is replayed to redo all successful
transactions, but no undo is needed" and, for partitioned LSM-trees, "a typical approach, used in
LevelDB and RocksDB, is to maintain a separate metadata log to store all changes to the structural
metadata, such as adding or deleting SSTables" `[LUO20 §2.2.3]`.

Part 6 makes all of this byte-exact.

## 12. Summary — one write and one read, end to end

```
Put("k","v")
  └─ queue as writer; leader builds batch group                     [group commit]
  └─ seq = LastSequence+1 … ; encode WriteBatch                     [fixed64 seq, fixed32 count, records]
  └─ append to WAL (32 KB blocks, CRC per record); fsync if sync=true
  └─ insert ("k"‖seq‖VAL, "v") into skip list (arena memory)
  └─ SetLastSequence → visible
  … memtable ≥ 4 MB → becomes imm; new memtable + new WAL
  … background: imm → L0 SSTable (sorted, blocks + index + filter + footer) → fsync
                 MANIFEST += "new file N at L0, log M obsolete" → fsync → delete old WAL
  … L0 ≥ 4 files → compact into L1 (merge, drop shadowed versions & safe tombstones)

Get("k")
  └─ pin mem, imm, current Version (Ref)
  └─ memtable → imm → L0 files newest-first → one file per level (binary search on file ranges)
       per file: Bloom says "maybe"? → index block → pread one data block → CRC → search block
  └─ first match wins: VAL → return; DEL → NotFound
  └─ Unref
```

## Drills

1. Trace `Put(a,1)@1, Put(b,2)@2, Delete(a)@3, Put(a,4)@4` through a memtable. Write the four internal
   keys in sorted order with their tags in hex (`seq << 8 | type`).
2. Same data spread across L0 (seq 3–4) and L2 (seq 1–2). A compaction L0→L1 runs with no snapshots.
   Using rules (A)/(B), which entries survive? Now redo it with a snapshot at seq 2 open.
3. Compute write amplification for leveling with T = 4, 10, 20 and L chosen so the last level is
   1 TiB with a 64 MiB L1. Plot (by hand) WA vs read cost (number of runs).
4. Explain why `SetLastSequence` happens *after* the memtable insert. What would a concurrent reader
   observe if it happened before?
5. Write the back-pressure rule for your object store's background work (e.g. garbage collection of
   deleted objects): what is the debt metric, soft limit, hard limit?

## My summary

_(write 5 lines here)_
