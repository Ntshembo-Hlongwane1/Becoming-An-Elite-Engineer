# Decisions (one line each: decision — why — source)

## Arena::allocate — aligning the absolute address vs the offset (why absolute is correct for any align)

## Arena::reset — what it does and does NOT do (storage vs object lifetime — Lesson 10.2 §3)

## Pool block size — the two lower bounds you enforce (>= sizeof(T), >= sizeof(FreeNode), aligned)

## Pool allocate/deallocate — LIFO head push/pop and the address-recycling it produces (10.3 §4)

## ArenaResource::do_deallocate is a no-op — why that is correct, not a bug (10.2 §2 / 10.4)

## ArenaResource::do_is_equal — identity for a stateful resource; why not value-equality (10.4 §2)

## RESULTS.md: your bench numbers (arena vs malloc; pool recycling; pmr vector in the arena)
