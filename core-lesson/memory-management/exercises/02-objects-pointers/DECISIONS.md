# Decisions (one line each: decision — why — source)

## room check (why `n > remaining()` and not `position()+n > size()`)

## read_bytes returns a view (no copy) — lifetime rule that makes this safe

## read_object/write_object use memcpy (why not reinterpret_cast; the trivially-copyable requirement)

## atomic failure (why a failed read/write must not advance the position)

## Differences from a real serialization lib (after you read LevelDB util/coding.cc)
