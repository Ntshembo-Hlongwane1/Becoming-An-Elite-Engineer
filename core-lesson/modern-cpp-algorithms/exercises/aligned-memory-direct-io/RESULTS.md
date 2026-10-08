# Results — Exercise 6

## Machine
| Fact | Value | How measured |
|---|---|---|
| filesystem of the test dir | | `df -T <dir>` |
| DIO alignment (mem / offset / reported) | | `drill_bench` output (statx) |
| logical sector size | | `lsblk -o NAME,LOG-SEC` |
| build type | Release | |

## Write throughput (64 MiB)
| mode | block | pattern | write loop (s) | fsync (s) | total (s) | MiB/s | µs per write |
|---|---|---|---|---|---|---|---|
| buffered | 4 KiB | sequential | | | | | |
| buffered | 4 KiB | random | | | | | |
| O_DIRECT | 4 KiB | sequential | | | | | |
| O_DIRECT | 4 KiB | random | | | | | |
| … | 64 KiB | … | | | | | |
| … | 1 MiB | … | | | | | |

## Appender write amplification (bonus)
| flush every k records | logical bytes | device bytes | amplification | time (s) |
|---|---|---|---|---|

## Explanation
(Every number above explained with notes Part 5. Compare with the numbers in Part 5 §11.)
