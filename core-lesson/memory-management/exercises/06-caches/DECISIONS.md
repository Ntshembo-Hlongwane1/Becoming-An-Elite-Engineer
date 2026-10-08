# Decisions (one line each: decision — why — source)

## transpose_blocked (how you handle ragged edges when size % block != 0)

## tile size sweet spot (from bench: which T, and why — relate T^2 to L1)

## sum_all order (why both orders give the same sum but different time)

## cache_line_size source (sysconf vs /sys)

## RESULTS.md: your measured row/col ratio and false-sharing ratio vs the notes' 4.8x / 5x
