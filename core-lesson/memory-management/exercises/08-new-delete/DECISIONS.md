# Decisions (one line each: decision — why — source)

## size header (why you must store the size yourself, not rely on sized delete alone)

## header size = 16 (why 16 keeps the returned pointer 16-aligned — tie to default new alignment)

## counters are constinit (why a lazily-built counter would break before main)

## no `new`/containers inside the operators (why that recurses / how you avoided it)

## matched family (which forms you replaced and why replacing a subset is unsafe)

## peak_bytes update (how you compute the high-water mark; why it never decreases)

## no double-free detection here (why reading the header after free would be a UAF; deferred to L16/17)

## RESULTS.md: bench/newdelete_probe output vs the notes (alignment, order, cookie)
