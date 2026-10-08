# Decisions (one line each: decision — why — source)

## out-of-line side table (not an inline header) — keep detector metadata safe from overflow (16.1 §2)

## fixed-capacity storage — the detector must not allocate through the allocator it hooks (16.1 §3)

## untrack return value = double-/invalid-free detection (free a key not present) (16.3 §3)

## tracked_free only std::free() when untrack returns true — never double/invalid-free real memory

## report returns total-live but writes up to cap — caller controls the buffer

## total_allocations never decreases; live_count/live_bytes do — what each counter means

## notes: how this maps to LeakSanitizer; why whole-program leak reporting needs reachability (16.3 §2)
