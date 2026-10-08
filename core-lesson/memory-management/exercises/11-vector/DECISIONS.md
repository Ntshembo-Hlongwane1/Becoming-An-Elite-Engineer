# Decisions (one line each: decision — why — source)

## copy ctor — deep copy + cleanup-on-throw (why a shallow/default copy would double-free, 11.2)

## move ctor noexcept — why noexcept matters (containers move vs copy on realloc, 11.3/11.4)

## copy assignment via copy-and-swap — why it gives the strong guarantee and is self-safe (11.4 §2)

## move assignment — self-move guard and release-before-steal order (11.3)

## reserve — std::move_if_noexcept not std::move (the strong guarantee on a throwing element, 11.4 §3)

## reserve rollback — what you destroy/free on a mid-relocation throw to leave *this unchanged

## emplace_back — bump size_ only after the element is constructed (per-element strong guarantee)

## RESULTS.md: bench move/copy counts for noexcept-move vs throwing-move elements
