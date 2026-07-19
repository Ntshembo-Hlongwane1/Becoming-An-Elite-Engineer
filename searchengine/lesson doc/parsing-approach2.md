# Approach 2 — Shunting-Yard (from absolute zero)

> **What this document is.** A complete, self-contained explanation of Edsger Dijkstra's **shunting-yard algorithm** — a way to parse infix expressions using an *explicit* stack you manage by hand. It assumes you can program and nothing else: no prior parsing knowledge, no grammar theory, no stack-algorithm background. By the end you will understand *why* it works, be able to write it from a blank file, reason precisely about its speed and memory, handle the genuinely tricky parts (associativity and unary operators), and know exactly when it's the right tool.
>
> We teach it on a **calculator** — `2 + 3 * 4 - (5 - 1)` — because arithmetic is the cleanest domain for the idea. The same machinery converts boolean search queries and any other prioritized, parenthesized notation.
>
> **Relationship to Approach 1 (recursive descent).** Recursive descent hides its stack inside the language's *call stack*. Shunting-yard makes that stack a visible data structure you push and pop yourself. When V3's left-to-right scan collapsed on `(redis OR kafka) AND replication` and you sensed "I need a stack" — *this is the algorithm you were groping toward.* Even if you ultimately choose recursive descent for the real engine, building this once teaches you what that hidden call stack is actually doing.

---

## Part 0 — The problem, stated plainly

Take `"2 + 3 * 4"`. The answer is `14`, not `20`, because multiplication "happens first." Now make a computer do it.

The naive instinct — scan left to right and compute as you go — fails:

```
start with 2
see + 3   → 2 + 3 = 5
see * 4   → 5 * 4 = 20     ✗ WRONG (should be 14)
```

It gets `20` because nothing told it that `*` should have grabbed the `3` *before* the `+` touched it. Add parentheses — `(2 + 3) * 4` vs `2 + (3 * 4)` — and the flat scan can't even *represent* "this group goes first." It collapses.

The reason this is hard is that **the notation we write — infix — puts each operator *between* its two operands, so the operator's effect depends on things both to its left and its right, and on the precedence of *other* operators nearby.** `+` in `2 + 3` adds 2 and 3; the same `+` in `2 + 3 * 4` must *wait* because a tighter operator is lurking to its right. Infix is convenient for humans and awkward for machines.

Shunting-yard's core insight is to **convert the awkward infix form into a different notation that has no precedence problem and no parentheses at all** — and which a trivial stack can then evaluate in one pass.

---

## Part 1 — The three stages

Every parser splits the work into three stages; mashing them together is exactly why the naive scan failed. Keep them separate:

```
  "2 + 3 * 4"                          ① raw string (characters)
        │
        ▼   ── STAGE 1: TOKENIZE ──
  [NUM 2] [PLUS] [NUM 3] [STAR] [NUM 4]   ② flat list of tokens
        │
        ▼   ── STAGE 2: PARSE ──           (shunting-yard lives here)
  [NUM 2] [NUM 3] [NUM 4] [STAR] [PLUS]    ③ reordered into POSTFIX
        │
        ▼   ── STAGE 3: EVALUATE ──
            14                            ④ one stack walk → the answer
```

- **Tokenize** turns characters into *tokens* — meaningful atoms. `"23"` is one number, not two digits. Whitespace vanishes.
- **Parse** turns the flat infix token list into a form that encodes the structure. Recursive descent (the other approach) builds a *tree* here. **Shunting-yard instead reorders the tokens into postfix notation** (defined in Part 3), which carries the same structure in a flat, unambiguous sequence.
- **Evaluate** consumes the postfix sequence with a single stack and produces the answer.

We build them in order. Stage 1 first.

---

## Part 2 — Stage 1: Tokenizing (the easy stage)

Tokenizing walks the input left to right and emits tokens. A token is a tiny record: a **kind** and, when relevant, a **value**.

```go
type TokKind int

const (
    TokNum   TokKind = iota // a number literal, e.g. 234
    TokPlus                  // +
    TokMinus                 // -
    TokStar                  // *
    TokSlash                 // /
    TokLParen               // (
    TokRParen               // )
    TokEOF                  // end of input — a sentinel
)

type Token struct {
    Kind TokKind
    Val  float64 // only meaningful when Kind == TokNum
}
```

The scanner:

```go
func tokenize(input string) []Token {
    var tokens []Token
    i := 0
    for i < len(input) {
        c := input[i]
        switch {
        case c == ' ':
            i++ // skip whitespace
        case c == '+':
            tokens = append(tokens, Token{Kind: TokPlus}); i++
        case c == '-':
            tokens = append(tokens, Token{Kind: TokMinus}); i++
        case c == '*':
            tokens = append(tokens, Token{Kind: TokStar}); i++
        case c == '/':
            tokens = append(tokens, Token{Kind: TokSlash}); i++
        case c == '(':
            tokens = append(tokens, Token{Kind: TokLParen}); i++
        case c == ')':
            tokens = append(tokens, Token{Kind: TokRParen}); i++
        case c >= '0' && c <= '9':
            start := i
            for i < len(input) && input[i] >= '0' && input[i] <= '9' {
                i++ // consume ALL digits — multi-character token
            }
            n, _ := strconv.ParseFloat(input[start:i], 64)
            tokens = append(tokens, Token{Kind: TokNum, Val: n})
        default:
            panic(fmt.Sprintf("unexpected character: %q", c))
        }
    }
    tokens = append(tokens, Token{Kind: TokEOF})
    return tokens
}
```

Two details matter:

**1. Multi-character tokens.** On hitting a digit, do *not* emit one token and move on — keep consuming digits until a non-digit. `"234"` is *one* `TokNum` worth `234`, not three tokens. That inner loop is the difference between a tokenizer and a naive splitter. (Words like `redis` work the same way: consume letters until a space or paren.)

**2. The `TokEOF` sentinel.** Stage 2 repeatedly asks "is there another token?" An explicit end marker means you never run off the end of the slice and never need a scatter of bounds checks. It's the C-string null-terminator trick: one sentinel deletes a whole class of edge cases.

That's tokenizing — a flat loop, no cleverness. The algorithm is all in stage 2.

---

## Part 3 — The key idea: infix vs postfix (Reverse Polish Notation)

Shunting-yard rests entirely on one notation switch. You must *feel* this before the algorithm makes sense.

There are three ways to write the same operation:

| Notation | `2 + 3` looks like | Operator sits… |
|----------|-------------------|----------------|
| **Infix** | `2 + 3` | *between* operands (what humans write) |
| **Prefix** (Polish) | `+ 2 3` | *before* operands |
| **Postfix** (Reverse Polish, RPN) | `2 3 +` | *after* operands |

**Postfix is magic for machines because it needs no parentheses and no precedence rules.** The order of operations is fully determined by the *position* of the tokens. Compare:

```
infix:    2 + 3 * 4          (ambiguous without the rule "× before +")
postfix:  2 3 4 * +          (NO ambiguity — read left to right, it just works)

infix:    (2 + 3) * 4        (needs parentheses to override precedence)
postfix:  2 3 + 4 *          (NO parentheses needed — position says it all)
```

Look at the two postfix forms. `2 3 4 * +` says "take 3 and 4, multiply, then add 2." `2 3 + 4 *` says "take 2 and 3, add, then multiply by 4." **The parentheses and the precedence rule have been *compiled away* into pure ordering.** Once an expression is in postfix, evaluating it is laughably simple (Part 8 shows the four-line evaluator).

So the plan is:

1. Convert infix → postfix. **This is the shunting-yard algorithm** (Parts 4–7). This is the part with the cleverness.
2. Evaluate postfix with one stack (Part 8). This part is trivial.

> Why "shunting-yard"? Dijkstra named it after a railway shunting yard: tokens roll in like train cars, and operators get *shunted* onto a side track (the operator stack) and released back onto the main line (the output) in the right order. The metaphor is exact — hold onto it.

---

## Part 4 — The machinery: two structures and a precedence table

The conversion uses exactly two data structures plus one lookup table.

**1. The output list** — the postfix result, built up left to right. Numbers go here immediately; operators arrive here later, in corrected order.

**2. The operator stack** — a holding area (the "side track") for operators that are *waiting* to be placed into the output. An operator waits because we don't yet know whether something tighter-binding is coming to its right.

**3. The precedence table** — a number per operator. Higher = binds tighter.

```go
var prec = map[TokKind]int{
    TokPlus:  1, TokMinus: 1, // loosest
    TokStar:  2, TokSlash: 2, // tighter
}
```

This explicit table is the defining difference from recursive descent. In recursive descent, precedence is *implicit* in the shape of the grammar (which rule sits under which). In shunting-yard, precedence is *explicit data* — a literal map of numbers — and the algorithm consults it directly. Adding an operator level means adding a table entry, not restructuring code.

---

## Part 5 — The algorithm (the heart)

Process tokens left to right. For each token, follow these rules:

```
for each token:

  NUMBER          → append it to OUTPUT immediately.
                    (Operands never wait; their position is already correct.)

  operator o1     → while there is an operator o2 on TOP of the stack
                    AND o2 is not '('
                    AND prec(o2) >= prec(o1):          ← the precedence engine
                        pop o2 off the stack into OUTPUT
                    then push o1 onto the stack.

  '('             → push it onto the stack (a barrier; see ')').

  ')'             → pop operators off the stack into OUTPUT
                    until you pop a '('. Discard both parentheses
                    (they were only there to guide ordering).

at end of input  → pop every remaining operator off the stack into OUTPUT.
```

Two of these rules deserve a careful explanation, because they are *the whole algorithm*:

### The operator rule and its `while` loop — the precedence engine

When a new operator `o1` arrives, before you can park it on the stack you must ask: *are there operators already waiting that should fire first?* An operator already on the stack should fire first (be popped to output) if it **binds at least as tightly** as the newcomer — that's the `prec(o2) >= prec(o1)` test.

- If the stacked operator is *tighter* (or equal, for left-associativity — see Part 6), it has "won the race for the operand between them" and must be emitted now, *before* `o1`.
- If the stacked operator is *looser*, it must keep waiting — `o1` is tighter and jumps ahead of it onto the stack.

That single comparison does the exact job that grammar-layering did in recursive descent — but out in the open, as arithmetic on integers, instead of hidden in call depth.

### The parenthesis rules — a barrier on the stack

A `(` is pushed as a literal **barrier**. While it sits on the stack, the operator rule's `while` loop stops at it (`o2 is not '('`), so operators *inside* the parens can never be popped past operators *outside* the parens — the group stays sealed. When the matching `)` arrives, you pop everything back to the `(`, flushing the whole parenthesized sub-expression into the output as a finished unit, then throw both parens away (postfix doesn't need them). That is how `(2 + 3) * 4` forces the `+` to fire before the `*`.

---

## Part 6 — Worked trace: `2 + 3 * 4`

Watch the stack breathe. `output` grows on the right; `op-stack` top is on the right.

```
token   rule applied                                   output      op-stack
─────   ────────────────────────────────────────────  ──────────  ────────
2       number → output                                2
+       stack empty, nothing to pop → push +           2           +
3       number → output                                2 3         +
*       top is +, prec(+)=1 < prec(*)=2 → don't pop;   2 3         + *
        push *
4       number → output                                2 3 4       + *
EOF     pop all remaining operators                    2 3 4 *     +
                                                       2 3 4 * +
```

Result: **`2 3 4 * +`**. Read it: 3 and 4 get multiplied, then 2 is added — `2 + (3*4) = 14`. The `*` ended up *before* the `+` in the output even though it came *after* it in the input, because `*` binds tighter and was allowed to jump ahead. The precedence got baked into the ordering. ✓

### Trace with parentheses: `(2 + 3) * 4`

```
token   rule applied                                   output      op-stack
─────   ────────────────────────────────────────────  ──────────  ────────
(       push barrier                                               (
2       number → output                                2           (
+       top is '(' → loop stops; push +                2           ( +
3       number → output                                2 3         ( +
)       pop until '(' : pop + → output; discard ( )    2 3 +
*       stack empty → push *                            2 3 +       *
4       number → output                                2 3 + 4     *
EOF     pop all                                         2 3 + 4 *
```

Result: **`2 3 + 4 *`** = `(2 + 3) * 4 = 20`. The `(` barrier kept the `+` sealed inside, so it was flushed to output by the `)` *before* the `*` ever appeared. Parentheses overrode precedence — exactly as intended. ✓

---

## Part 7 — Associativity: the `>=` vs `>` distinction (subtle, get it right)

Associativity decides how *same-precedence* operators group. `10 - 3 - 2`:

- `(10 - 3) - 2 = 5`  — **left-associative** (correct for `-`)
- `10 - (3 - 2) = 9`  — **right-associative** (wrong for `-`)

In shunting-yard, this is controlled by **one character in the comparison**: whether you pop on `>=` or only on `>`.

- **Left-associative operators** (`+ - * /`): pop when the stacked operator's precedence is `>=` the newcomer's. The "equal" case means: when a second `-` arrives and a `-` is already waiting, the waiting one **fires first**, grouping left. That's left-associativity.
- **Right-associative operators** (exponentiation `^`, unary prefix ops): pop only when strictly `>`. On equal precedence the new operator waits *on top*, so the rightmost one fires first, grouping right.

Trace `10 - 3 - 2` with the left rule (`>=`):

```
token   action                                  output      op-stack
10      output                                  10
-       push                                    10          -
3       output                                  10 3        -
-       prec(-)=1 >= prec(-)=1 → POP the - ;    10 3 -      -
        push the new -
2       output                                  10 3 - 2    -
EOF     pop                                     10 3 - 2 -
```

Result `10 3 - 2 -` = `(10 - 3) - 2 = 5`. ✓ The first `-` was forced out *before* the second one stacked, because `>=` popped on the tie. Had you used `>`, the first `-` would have lingered and you'd get `10 3 2 - -` = `10 - (3 - 2) = 9` — wrong. **So: `>=` for left-associative, `>` for right-associative.** A correct general implementation stores each operator's associativity and chooses the comparison per operator:

```go
// pop the stacked operator o2 before pushing newcomer o1 when:
//   left-assoc o1:  prec(o2) >= prec(o1)
//   right-assoc o1: prec(o2) >  prec(o1)
func shouldPop(o2, o1 TokKind) bool {
    if rightAssoc[o1] {
        return prec[o2] > prec[o1]
    }
    return prec[o2] >= prec[o1]
}
```

(For the boolean engine, `AND` and `OR` give the same result either way, so the simple `>=` left rule is fine. But you must *understand* this knob, because getting it wrong is a silent correctness bug — the answer is just quietly wrong, never a crash.)

---

## Part 8 — Stage 3: Evaluating postfix (the trivial, beautiful part)

Once you have postfix, evaluation is the cleanest demonstration in all of parsing that **a single stack evaluates fully-structured expressions in one linear pass.** The rule:

```
for each token in the postfix sequence:
    NUMBER    → push it onto the stack
    operator  → pop the top TWO values, apply the operator, push the result
at the end: the stack holds exactly ONE value — the answer.
```

(Order matters for non-commutative ops: the *first* value popped is the *right* operand, the second is the *left*. For `-` and `/` you must respect that.)

```go
func evalRPN(output []Token) float64 {
    var stack []float64
    for _, t := range output {
        switch t.Kind {
        case TokNum:
            stack = append(stack, t.Val)
        default: // an operator
            // pop right then left
            right := stack[len(stack)-1]
            left := stack[len(stack)-2]
            stack = stack[:len(stack)-2]
            var r float64
            switch t.Kind {
            case TokPlus:  r = left + right
            case TokMinus: r = left - right
            case TokStar:  r = left * right
            case TokSlash: r = left / right
            }
            stack = append(stack, r)
        }
    }
    return stack[0]
}
```

Trace `2 3 4 * +`:

```
read 2  → push        stack: [2]
read 3  → push        stack: [2, 3]
read 4  → push        stack: [2, 3, 4]
read *  → pop 4,3; push 3*4=12     stack: [2, 12]
read +  → pop 12,2; push 2+12=14   stack: [14]
done → answer = 14  ✓
```

That's it. This little loop is the conceptual kernel of the entire subject: structure-in, answer-out, one stack, one pass. (It is also Exercise 1 in `parsing.md` — the "easy" one — precisely because it is the heart of everything.)

---

## Part 9 — Unary operators: the genuinely fiddly part (don't skip)

Everything above handles **binary** operators (two operands) cleanly. **Unary** operators — a `-` meaning "negate" as in `-5`, or `NOT x` in a boolean query — are where shunting-yard gets awkward. This awkwardness is the single biggest reason people prefer recursive descent when their language has unary operators. You must understand *why*, because the boolean engine's `NOT` is unary.

**The core problem: the same symbol can be unary or binary, and the algorithm can't tell from the symbol alone.** In `5 - 3` the `-` is binary (subtract). In `-5` the `-` is unary (negate). In `2 * -3` the `-` is unary again. The token is identical; only the *context* differs.

**How you tell them apart: position.** A `-` is **unary** if it appears where an *operand* (a number or `(`) was expected — i.e. at the very start of the input, or right after another operator, or right after a `(`. Otherwise it's binary. So you track "what did I see last?":

```go
// A '-' is unary if the previous token was: nothing (start),
// another operator, or '('. Otherwise it's binary.
expectOperand := true // true at start
for _, t := range tokens {
    switch t.Kind {
    case TokNum:
        // ... output ...
        expectOperand = false
    case TokMinus:
        if expectOperand {
            // treat as UNARY negate — give it its OWN, very high precedence,
            // and mark it right-associative so "--5" works.
            // (often represented as a distinct token, e.g. TokNeg)
        } else {
            // treat as BINARY subtract
        }
        expectOperand = true // after any operator we again expect an operand
    case TokLParen:
        expectOperand = true
    case TokRParen:
        expectOperand = false
    // ...
    }
}
```

Then in the postfix evaluator, a unary operator pops **one** value (not two), applies, and pushes:

```go
case TokNeg:
    v := stack[len(stack)-1]
    stack[len(stack)-1] = -v   // pop one, push one
```

So unary support costs you: (a) extra state (`expectOperand`) to disambiguate, (b) a separate high-precedence, right-associative entry, often a separate token kind, and (c) special-casing in the evaluator to pop one operand instead of two. None of this is *hard*, but it is fiddly, easy to get subtly wrong, and clutters an otherwise clean algorithm. Contrast recursive descent, where a unary prefix operator is just one extra grammar rule (`factor := '-' factor | primary`) with no special state at all. **That contrast is the practical headline of the two approaches.**

---

## Part 10 — Beyond a value: producing an AST with shunting-yard

A common misconception is that shunting-yard can only spit out a number or a postfix string. Not so — you can make it build the same **AST** that recursive descent produces. The trick: run the postfix-evaluation stack, but instead of pushing *numbers* and computing, push *tree nodes* and combine.

```go
// Instead of an output LIST of postfix tokens, keep an operand stack of Nodes.
// When you would "emit an operator to output," instead pop node(s), build a
// BinNode/UnaryNode, and push it back.
//   number   → push NumNode
//   binary   → pop right, pop left, push BinNode{op, left, right}
//   unary    → pop operand, push UnaryNode{op, operand}
// At the end, the single remaining node is the AST root.
```

This matters for the search engine: you may want the tree (to evaluate against different document sets, to inspect, to optimize). Shunting-yard *can* give you a tree; it's just less direct than recursive descent, which produces one as its natural output. If a tree is the goal, that's another nudge toward recursive descent — but know that shunting-yard is not locked out of it.

---

## Part 11 — Execution performance (how fast, and *why*)

Let **n** = number of tokens.

### Time complexity: O(n), linear

Shunting-yard (the infix→postfix conversion) runs in **O(n)**. The argument:

- The outer loop processes each input token **exactly once** — `n` iterations.
- The inner `while` loop pops operators off the stack. This *looks* like it could make things quadratic, but it can't: **every operator is pushed onto the stack exactly once and popped exactly once over the entire run.** A pop in the inner loop is "spending" a push that already happened. Across the whole algorithm the total number of pops is bounded by the total number of pushes, which is ≤ n. (This is an *amortized* argument: any single token might trigger several pops, but the grand total of pops over all tokens is still ≤ n.)

So: n iterations of the outer loop + at most n total pops = **O(n)**. The postfix evaluation pass (Part 8) likewise touches each output token once → **O(n)**. Tokenize O(n) + convert O(n) + evaluate O(n) = **O(n) end to end.** Same asymptotic class as recursive descent — both are optimal, since you must read every character at least once.

### A note on "one pass"

Shunting-yard is sometimes sold as "single-pass." It processes the input in one left-to-right sweep — true. But the *whole pipeline* is effectively two sweeps (infix→postfix, then postfix→value) unless you fuse them with the AST/operand-stack trick in Part 10. The constant factors are tiny either way; this is not a real performance difference between the approaches, just a framing one.

---

## Part 12 — Memory (where the bytes go)

The defining trait of shunting-yard's memory profile: **the stack is on the heap, not the call stack.** This is the mirror image of recursive descent.

### The operator stack — O(d) heap, but no overflow risk

The operator stack grows as operators wait. Its depth tracks the nesting/pending-operator depth `d` of the expression — the same `d` that drove recursive descent's *call*-stack depth. For `((((1))))` the operator stack holds the pile of `(` barriers; for a long chain of mixed-precedence operators it holds the waiting operators.

The crucial difference from recursive descent: this stack is a **heap-allocated slice you grow yourself**, not the program's call stack. A heap slice can grow to gigabytes before failing gracefully (out-of-memory), whereas the call stack overflows and *crashes* at a much smaller, fixed limit. **So shunting-yard does not have recursive descent's stack-overflow-on-deep-nesting failure mode.** That is its headline memory advantage and the reason it (or an equivalent explicit-stack rewrite) is preferred when input may be adversarially deeply nested. You traded implicit recursion for an explicit, bigger, safer stack.

### The output — O(n) heap

If you produce a postfix list, it holds up to `n` tokens: **O(n)** heap. The postfix-evaluation stack holds at most O(d) values at once. If you fuse conversion and evaluation (no intermediate postfix list), you save the O(n) output buffer and keep only the O(d) value/operand stack.

### Memory summary

| What | Cost | Notes |
|------|------|-------|
| Token list | O(n) | unavoidable; from stage 1 |
| Operator stack | O(d), d = nesting/pending depth | **heap**, not call stack → no overflow crash |
| Postfix output (if materialized) | O(n) | skip it by fusing convert+eval |
| Eval value stack | O(d) | at most depth-many values at once |

---

## Part 13 — Pitfalls (the bugs you *will* hit)

**Wrong associativity comparison (`>=` vs `>`).** The most insidious bug, because it never crashes — it silently returns a wrong number on `a - b - c` or `a / b / c`. Default to `>=` (left-associative); use `>` only for explicitly right-associative operators. Always test `10 - 3 - 2 = 5` and `100 / 5 / 2 = 10`.

**Operand order in non-commutative ops.** In the evaluator, the *first* value popped is the *right* operand. `left - right`, not `right - left`. Getting this backwards passes commutative tests (`+`, `*`) and silently fails on `-` and `/`. Test `10 - 3 = 7` (not `-7`).

**Mismatched parentheses.** Two cases: (a) a `)` with no matching `(` — your "pop until `(`" loop runs the stack dry; detect it and error. (b) Leftover `(` on the stack at end of input — a `(` that was never closed; if your final "pop all" blindly emits the `(` as an operator, you get garbage. Check for it and report "unclosed parenthesis."

**Unary minus mishandled.** Covered in Part 9. If you forget the `expectOperand` state, `-5` either crashes (tries to pop two operands when only one exists) or is misread as binary subtraction with a missing left operand. This is the fiddliness tax.

**Forgetting the final flush.** At end of input you must pop *all* remaining operators to the output. Forget it and the last operators vanish — `2 + 3` yields just `2 3` and the `+` is lost.

---

## Part 14 — When to use shunting-yard (and when not), and why

### Use it when…

- **You have simple, flat precedence and want an explicit, iterative algorithm.** Calculators and arithmetic-expression evaluators are the canonical fit — it's compact and famously efficient.
- **Input may be adversarially deeply nested and you cannot tolerate a crash.** The heap stack won't overflow the way recursion would (Part 12). This is a genuine robustness edge for untrusted input.
- **You're building a tiny expression evaluator and don't want to define a grammar or a tree.** Tokens in, postfix out, stack walk, done.
- **You want to *understand* what a call stack does.** Building this once makes recursive descent's hidden machinery concrete forever.

### Lean away from it when…

- **Your language has unary prefix operators** (`-x`, `NOT x`, `!x`). The `expectOperand` disambiguation and one-operand special-casing (Part 9) are fiddly and error-prone. Recursive descent handles unary ops with one trivial grammar rule. **For the boolean engine, whose `NOT` is unary, this is the decisive reason to prefer recursive descent.**
- **You want an AST as the natural output.** Shunting-yard *can* build one (Part 10), but recursive descent does it more directly.
- **You want pinpoint error messages.** Tracking "where and why" through an explicit stack is clumsier than recursive descent, where each function knows exactly what it expected.
- **The grammar has more than flat operator precedence** (statements, declarations, complex structure). Shunting-yard is an *expression* algorithm; it doesn't scale to whole languages the way recursive descent does.

### Why it exists and where it's used

Dijkstra designed it in 1961 for exactly the expression-to-postfix problem, and it remains the textbook way to evaluate arithmetic in calculators, spreadsheet formula engines, small embedded scripting evaluators, and the expression sub-parts of larger systems. It is *the* answer for "I have a flat infix expression with operator precedence and I want it evaluated efficiently with minimal code." It is *not* the answer for "I'm building a real language with unary operators, an AST, and good errors" — that's recursive descent's territory. Knowing both, and *why* each fits where it does, is the actual skill.

---

## Part 15 — Self-implementation blueprint (do this from a blank file)

Build it in this order; each step is testable alone.

1. **Define tokens and write `tokenize`** — identical to recursive descent's: flat loop, inner loop for multi-character numbers/words, skip whitespace, append `TokEOF`. Test it prints `[NUM 2, PLUS, NUM 34, STAR, NUM 5, EOF]` for `"2 + 34 * 5"`.

2. **Write the postfix evaluator FIRST** (Part 8). It's the simplest piece and the conceptual core. Feed it hand-written postfix token lists (`2 3 4 * +` → 14; `5 1 2 + 4 * + 3 -` → 14) and confirm. Once this works, you've proven "a stack evaluates structure" to yourself. *(This is literally Exercise 1 in `parsing.md`.)*

3. **Write the precedence table** — a `map[TokKind]int`. `+ -` → 1, `* /` → 2. Decide and record associativity (default left).

4. **Write the infix→postfix conversion** (Part 5): outer loop over tokens; numbers → output; operators → the `while`-pop-then-push dance using the precedence (and associativity) comparison; `(` → push; `)` → pop-until-`(`; end → flush all. Test `2 + 3 * 4` → `2 3 4 * +`.

5. **Chain them**: tokenize → convert → evaluate. Test the full pipeline on `2 + 3 * 4 = 14` and `(2 + 3) * 4 = 20`.

6. **Test associativity deliberately**: `10 - 3 - 2 = 5`, `100 / 5 / 2 = 10`. If wrong, fix your `>=`/`>` comparison (Part 7). Also test operand order: `10 - 3 = 7`.

7. **Add parentheses robustness**: error on mismatched `(`/`)` rather than producing garbage.

8. **(Stretch) Add unary minus** (Part 9): the `expectOperand` state machine, a high-precedence right-associative negate token, and one-operand handling in the evaluator. Test `-5 + 3 = -2`, `2 * -3 = -6`, `--5 = 5`. Feel the fiddliness — that feeling is the lesson.

9. **(Optional) Build an AST instead of a value** (Part 10): swap the value stack for a node stack. Useful prep for the search engine, where `Eval()` returns document sets.

---

## Part 16 — Self-check (answer from memory)

- Why does a left-to-right scan get `2 + 3 * 4` wrong, and what specifically about *infix* notation makes it hard for machines?
- What are the three notations (infix/prefix/postfix), and *why* does postfix need no parentheses and no precedence rules?
- What are the two data structures shunting-yard uses, and what is the job of each?
- In the operator rule, what does the `while prec(o2) >= prec(o1)` loop accomplish, in plain words? What is it the explicit version of?
- How does a `(` on the stack act as a barrier, and what does `)` do?
- Why does `>=` give left-associativity and `>` give right-associativity? What goes wrong if you pick the wrong one — a crash, or a silent wrong answer?
- In the postfix evaluator, which popped value is the right operand, and why does that matter?
- Why is the algorithm O(n) even though it has a nested `while` loop? State the amortized push-once/pop-once argument.
- Where does shunting-yard's stack live (heap vs call stack), and why does that mean it *doesn't* have recursive descent's overflow-on-deep-nesting crash?
- Exactly why are unary operators (`NOT`, unary `-`) fiddly here, and what three things do they cost you?
- Name two situations where you'd choose shunting-yard and two where you'd choose recursive descent instead — with the reason for each.

If those come without flipping back, you understand shunting-yard at the level where you could implement it blind — and, just as importantly, you understand *why* the boolean query engine (with its unary `NOT` and its desire for an AST) leans toward recursive descent instead. Now go do the exercises in `parsing.md`: Exercise 1 *is* Part 8 of this document, and Exercise 4 asks you to compare both approaches with full understanding of each.
