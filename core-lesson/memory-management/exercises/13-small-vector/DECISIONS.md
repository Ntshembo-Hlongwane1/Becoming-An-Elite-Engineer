# Decisions (one line each: decision — why — source)

## layout: inline buffer + data_ pointer; is_inline() == data_ points at inline_ (13.4 §1)

## reserve: free old buffer only if !is_inline() (never free the in-object inline buffer) (13.4 §2)

## emplace_back growth: spill from inline (cap N) to heap via reserve(cap*2) (13.4 §2)

## move ctor split: inline source -> relocate elements; heap source -> steal pointer (13.4 §3)

## move ops are NOT noexcept: the inline relocate move-constructs Ts, which may throw (13.4 §3)

## copy assign = copy-and-move (strong guarantee + self-safe); move assign release-then-split

## moved-from state: empty inline (data_=inline_ptr(), size 0, cap N)

## notes/observations: growth factor, SSO (string), invalidation on spill/move (notes 13.x)
