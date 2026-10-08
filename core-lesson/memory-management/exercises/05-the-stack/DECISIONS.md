# Decisions (one line each: decision — why — source)

## stack_direction (how you forced a real nested call, not a folded constant)

## stack_limit_bytes (RLIMIT_STACK; the unlimited case)

## adjacent_frame_delta (why volatile / noinline matters here)

## capture_backtrace (backtrace() vs libunwind; why addresses need no symbols)

## -rdynamic / -fno-omit-frame-pointer (what each buys the backtrace)
