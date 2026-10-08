# Results — Exercise 6 (fill in from ./build-rel/bench)

| Experiment | Your number | Notes' measurement |
|---|---|---|
| cache line | | 64 B |
| sum row-major vs col-major (ratio) | | 4.8x |
| transpose naive | | — |
| transpose blocked, best tile (which T?) | | — |
| false sharing (same line) | | 530 ms |
| padded (separate lines) | | 107 ms (~5x) |

## Explanation
(Explain each number with Lesson 6. Why does blocking help the transpose? Which tile size and why?
Why is col-major ~5x slower? Why does padding fix false sharing?)
