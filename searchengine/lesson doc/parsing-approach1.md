# Approach 1 — Recursive Descent (from absolute zero)

> **What this document is.** A complete, self-contained explanation of the recursive-descent parsing technique. It assumes you know how to program and nothing else — no prior parsing knowledge, no grammar theory, no compiler background. By the end you will understand *why* the technique works, be able to write one from a blank file without a reference, reason precisely about its execution speed and memory use, and know when to reach for it and when not to.
>
> We teach it on a **calculator** — evaluating arithmetic like `2 + 3 * 4 - (5 - 1)`. Arithmetic is the cleanest possible domain for the idea. Once you own it, the same skeleton parses boolean search queries, config files, JSON, SQL — anything with nested, prioritized structure.

---

## Part 0 — The problem, stated plainly

Take the string `"2 + 3 * 4"`. As a human you instantly know the answer is `14`, not `20`, because you "do multiplication first." Now make a computer do it.

The naive instinct is to scan left to right and compute as you go:

```
start with 2
see + 3   → 2 + 3 = 5
see * 4   → 5 * 4 = 20     ✗ WRONG
```

Left-to-right scanning gives `20` because it has no concept that `*` should have grabbed the `3` *before* the `+` ever touched it. Add parentheses — `(2 + 3) * 4` versus `2 + (3 * 4)` — and the left-to-right scan has no way to even represent "this group happens first." It collapses.

The lesson buried in that failure: **a flat string has structure that a flat scan cannot see.** `2 + 3 * 4` *looks* flat but actually means a little tree:

```
        (+)
       /   \
     2     (*)
          /   \
         3     4
```

The `*` is *deeper* than the `+`. "Deeper" means "happens first." Your entire job in parsing is to recover that hidden tree from the flat text. Once you have the tree, evaluating it is trivial — you walk it bottom-up.

Recursive descent is one way (the most direct way) to recover that tree.

---

## Part 1 — The three stages

Every serious parser splits the work into three stages. The single biggest beginner mistake is trying to do all three at once (that is exactly why the left-to-right scan above failed — it mashed "read characters," "figure out structure," and "compute" into one pass). Keep them separate:

```
  "2 + 3 * 4"                          ① raw string (characters)
        │
        ▼   ── STAGE 1: TOKENIZE ──
  [NUM 2] [PLUS] [NUM 3] [STAR] [NUM 4]   ② flat list of tokens
        │
        ▼   ── STAGE 2: PARSE ──
            (+)
           /   \                          ③ a TREE capturing structure
         2     (*)
              /   \
             3     4
        │
        ▼   ── STAGE 3: EVALUATE ──
            14                            ④ walk the tree → the answer
```

- **Tokenize** turns characters into *tokens* — meaningful atoms. `"23"` becomes one number, not two digits. Whitespace disappears here.
- **Parse** turns the flat token list into a **tree** that encodes structure: what groups with what, what happens first. Precedence and parentheses are resolved here. **This is the hard stage, and recursive descent is a technique for this stage.**
- **Evaluate** walks the tree and produces the answer.

Each stage is simple *because* it does only one job. Recursive descent lives in stage 2, but you cannot do stage 2 without the tokens from stage 1, so we build stage 1 first.

> The tree from stage 2 is called an **Abstract Syntax Tree (AST)**. "Abstract" because it discards noise like parentheses and spaces — the structure they *implied* is now baked into the tree's shape. Notice the tree above has no parens: `*` sits below `+`, and that position *is* the statement "multiply first."

---

## Part 2 — Stage 1: Tokenizing (the easy stage)

Tokenizing (also called *lexing* or *scanning*) walks the input string left to right and emits a list of tokens. A token is a tiny record: a **kind** (what sort of thing it is) and, when relevant, a **value**.

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
    TokEOF                  // end of input — a sentinel, explained below
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
            i++ // skip whitespace, emit nothing
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
            // A number can be many digits — consume them ALL.
            start := i
            for i < len(input) && input[i] >= '0' && input[i] <= '9' {
                i++
            }
            n, _ := strconv.ParseFloat(input[start:i], 64)
            tokens = append(tokens, Token{Kind: TokNum, Val: n})
        default:
            panic(fmt.Sprintf("unexpected character: %q", c))
        }
    }
    tokens = append(tokens, Token{Kind: TokEOF}) // mark the end
    return tokens
}
```

Two details are the whole point of this stage:

**1. Multi-character tokens.** When you hit a digit you do *not* emit one token and move on. You keep consuming digits until you hit a non-digit. `"234"` is *one* `TokNum` with value `234`, not three tokens. That inner `for` loop is the entire difference between a real tokenizer and a naive character splitter. (When you later tokenize search queries, a "word" like `redis` works identically: consume letters until you hit a space or paren.)

**2. The `TokEOF` sentinel.** The parser in stage 2 constantly asks "what is the next token?" Without an explicit end marker, every such question needs a bounds check (`if pos < len(tokens)`) or it crashes off the end of the slice. By appending exactly one `TokEOF` at the end, the parser can *always* safely look at the current token — at worst it sees `EOF` and knows to stop. This is the same trick as the null terminator on a C string: one sentinel deletes an entire category of edge cases. Do not skip it.

That is tokenizing. There is no cleverness here and no recursion. It is a flat loop. The intelligence is all in stage 2.

---

## Part 3 — The idea you must own before any code: grammars

You cannot write a parser until you can *describe* the language you're parsing. We describe it with a **grammar**: a set of rules, each naming a structure and listing what it's made of.

### The notation (read this once, refer back as needed)

```
A := B C        an A is a B followed by a C          (sequence)
A := B | C      an A is a B OR a C                   (choice)
( B )*          zero or more repetitions of B        (repetition)
( B )?          an optional B (zero or one)
UPPERCASE       a TOKEN — a terminal, can't break down further (NUMBER, '+')
lowercase       a RULE — a nonterminal, defined by its own line
```

Read `:=` as "is defined as." A first attempt at arithmetic:

```
expr := NUMBER '+' NUMBER
```

This says "an expression is a number, a plus, a number." It describes `2 + 3` and nothing else — no subtraction, no three-term sums, no multiplication. Far too weak. We need recursion (rules that reference themselves or each other) and repetition.

### Why a single flat rule is broken: ambiguity

Here is the naive "everything" grammar:

```
expr := expr OP expr | NUMBER | '(' expr ')'
OP   := '+' | '-' | '*' | '/'
```

It *does* describe `2 + 3 * 4`. The fatal problem: it describes it in **two different ways**. The grammar permits both of these trees, and gives you no rule to choose:

```
       (+)                         (*)
      /   \                       /   \
    2     (*)                   (+)    4
         /   \                 /   \
        3     4               2     3

   = 2 + (3*4) = 14         = (2+3) * 4 = 20
```

A grammar that allows more than one tree for the same input is **ambiguous**. Ambiguous grammars are useless for parsing because "the structure" is not well-defined. We must rewrite the grammar so that for any valid input there is *exactly one* legal tree — and so that the one legal tree is the one we mean (multiply-first).

### The fix: layer the grammar by precedence

This is the single most important idea in parsing. Read it slowly.

We split the one ambiguous `expr` rule into **one rule per precedence level**, stacked so that the **loosest-binding operator is on the outside (top)** and the **tightest-binding operator is on the inside (bottom)**:

```
expr    := term   ( ('+' | '-') term   )*      ← lowest precedence  (loosest)
term    := factor ( ('*' | '/') factor )*      ← higher precedence  (tighter)
factor  := '-' factor | primary                ← unary minus        (tightest op)
primary := NUMBER | '(' expr ')'               ← atoms + recursion back to the top
```

Why does stacking the rules like this encode "multiply before add"? Because **the operators you want to bind tightest are reached *deepest* in the rule references, and the deepest things get grouped into a single unit first.**

Trace the logic for `2 + 3 * 4` by hand, following the rules:

- To build an `expr`, the rule says: first find a `term`.
- To build that `term`, the rule says: first find a `factor`, which finds a `primary`, which reads `2`. Back up in `term`: the next token is `+`. But look at the `term` rule — it only loops on `*` and `/`. It does **not** know what to do with `+`. So `term` stops and hands back just `2`.
- Back in `expr`: the next token is `+`, which `expr` *does* handle. It consumes the `+` and asks for *another* `term`.
- That second `term` reads `3`, then sees `*` — which it **does** handle. It consumes `*`, reads `4`, and groups them: `3 * 4`. The `term` hands back the whole bundle `(3 * 4)` as one unit.
- `expr` now combines `2 + (3 * 4)`. The `*` was forced into a tight bundle *because the rule for `*` (term) sits underneath the rule for `+` (expr)*. ✓

You never wrote a line that says "multiplication beats addition." The *shape* of the grammar says it. **Grammar nesting depth IS precedence.** Add another level (say exponentiation, tighter than `*`) by inserting another rule below `term`. The mechanism scales without any precedence-comparison code.

### Associativity: how ties break

Precedence settles `*` vs `+`. But what about two operators of the *same* precedence, like `10 - 3 - 2`? That can mean:

- `(10 - 3) - 2 = 5`  — **left-associative** (correct for `-`)
- `10 - (3 - 2) = 9`  — **right-associative** (wrong for `-`)

In the layered grammar, the choice is decided by *how* you wrote the repetition. The form we used —

```
expr := term ( ('+' | '-') term )*
```

— is a **loop**: "a term, then zero-or-more (operator, term) pairs." When you implement that loop you accumulate into a running left-hand value, which gives **left-associativity for free**: `10`, then `- 3` → `7`, then `- 2` → `5`. ✓

If instead you had written the rule with recursion on the *right*:

```
expr := term ('+' expr)?     ← right-recursive
```

you would get **right-associativity**: the right-hand side is a whole fresh `expr`, so it groups from the right. That is *wrong* for subtraction but *correct* for things like exponentiation (`2 ^ 3 ^ 2 = 2^(3^2)`) and for unary prefix operators. So: **loop ⇒ left-associative; right-recursion ⇒ right-associative.** Remember that sentence; it is the whole rule.

---

## Part 4 — Recursive descent: turning the grammar into code

Now the technique itself. Recursive descent is the most direct possible translation of a layered grammar into a program. It is governed by one mechanical rule, and it is genuinely mechanical — once the grammar is right, the code writes itself:

> **Every grammar rule becomes one function.**
> **A rule referencing another rule becomes a function call.**
> **Repetition `( ... )*` becomes a loop.**
> **Choice `|` becomes an `if`/`switch`.**

That's it. "Descent" because the functions call *down* through the precedence layers (expr → term → factor → primary). "Recursive" because the bottom layer (`primary`, on seeing a `(`) calls back up to the top (`expr`), so the functions form a cycle.

### The parser's state

The parser holds the token list and a cursor (`pos`) marking the current token. Two helpers do all the bookkeeping:

```go
type Parser struct {
    tokens []Token
    pos    int
}

// peek returns the current token WITHOUT advancing.
func (p *Parser) peek() Token { return p.tokens[p.pos] }

// next returns the current token AND advances past it.
func (p *Parser) next() Token {
    t := p.tokens[p.pos]
    p.pos++
    return t
}
```

`peek` lets a function ask "what's next?" without committing. `next` commits — it *consumes* the token. The distinction matters: a function peeks to decide what to do, then consumes only the tokens it is responsible for, leaving the rest for other functions.

### The four functions (one per grammar rule)

This version computes the answer directly while parsing. (We build a real tree right after — read this first.)

```go
// expr := term ( ('+' | '-') term )*
func (p *Parser) parseExpr() float64 {
    value := p.parseTerm()             // left operand: descend to the tighter level
    for {
        switch p.peek().Kind {
        case TokPlus:
            p.next()                   // consume '+'
            value += p.parseTerm()     // accumulate left-to-right ⇒ left-associative
        case TokMinus:
            p.next()
            value -= p.parseTerm()
        default:
            return value               // not my operator — hand back up, stop here
        }
    }
}

// term := factor ( ('*' | '/') factor )*
func (p *Parser) parseTerm() float64 {
    value := p.parseFactor()
    for {
        switch p.peek().Kind {
        case TokStar:
            p.next()
            value *= p.parseFactor()
        case TokSlash:
            p.next()
            value /= p.parseFactor()
        default:
            return value
        }
    }
}

// factor := '-' factor | primary
func (p *Parser) parseFactor() float64 {
    if p.peek().Kind == TokMinus {
        p.next()                       // consume the unary '-'
        return -p.parseFactor()        // RIGHT-recursive ⇒ handles "--5", "-(2+3)"
    }
    return p.parsePrimary()
}

// primary := NUMBER | '(' expr ')'
func (p *Parser) parsePrimary() float64 {
    t := p.next()
    switch t.Kind {
    case TokNum:
        return t.Val
    case TokLParen:
        value := p.parseExpr()         // recurse back to the TOP of the grammar
        p.next()                       // consume the matching ')'
        return value
    default:
        panic(fmt.Sprintf("unexpected token: %v", t.Kind))
    }
}
```

Driver:

```go
func evaluate(input string) float64 {
    p := &Parser{tokens: tokenize(input)}
    return p.parseExpr()
}
```

### Three things to internalize

**1. The call stack mirrors the grammar layers exactly.** `parseExpr` calls `parseTerm` calls `parseFactor` calls `parsePrimary`. The deeper the call, the tighter the binding. Remember how, back in V3 with `(redis OR kafka) AND replication`, you sensed you "needed a stack"? *This is that stack.* You are not building or managing one — the programming language's own call stack is doing it for you, automatically, with perfect nesting. That realization is the heart of recursive descent.

**2. Parentheses cost almost nothing.** In `parsePrimary`, a `(` just calls `parseExpr` again — jumping straight back to the top of the grammar. Whatever is inside the parens is parsed as a completely independent expression and handed back as a single value. That is *why* `(2 + 3) * 4` works: the `(2 + 3)` collapses to `5` before the surrounding `term` ever applies the `*`. Recursion buys you arbitrary nesting depth with zero extra code. `((((1))))` works for free.

**3. Each function only knows its own precedence level — and that ignorance is the feature.** `parseTerm` literally cannot process a `+`; the moment it sees one in its loop, it falls into `default` and returns. By being deliberately ignorant of everything but `*` and `/`, it hands control back to `parseExpr`, which *does* own `+`. The layering only works *because* each function refuses to do anyone else's job.

### Full execution trace: `2 + 3 * 4`

```
parseExpr()
 │  value = parseTerm()
 │   │  value = parseFactor() → parsePrimary() → next() = NUM 2 → returns 2
 │   │  peek() = '+'  → not '*' or '/', default → returns 2
 │  value = 2 ; peek() = '+'
 │  case TokPlus: next() consumes '+'
 │  value += parseTerm()
 │   │  value = parseFactor() → parsePrimary() → next() = NUM 3 → returns 3
 │   │  peek() = '*'  → case TokStar: next() consumes '*'
 │   │  value *= parseFactor() → parsePrimary() → next() = NUM 4 → returns 4
 │   │  value = 3 * 4 = 12 ; peek() = EOF → default → returns 12
 │  value = 2 + 12 = 14 ; peek() = EOF → default
 └─ returns 14  ✓
```

Read that until the indentation *is* the tree. The depth of each line is the depth in the AST. That correspondence — call depth = tree depth = precedence depth — is the entire technique in one picture.

### Full execution trace: `(2 + 3) * 4` (proving parentheses)

```
parseExpr()
 │  value = parseTerm()
 │   │  value = parseFactor() → parsePrimary()
 │   │   │  next() = '('  → case TokLParen
 │   │   │  value = parseExpr()              ← RECURSION back to the top
 │   │   │   │  parseTerm → ... → 2
 │   │   │   │  '+', parseTerm → ... → 3
 │   │   │   │  returns 2 + 3 = 5
 │   │   │  next() consumes ')'  → returns 5
 │   │  back in parseTerm: value = 5 ; peek() = '*'
 │   │  case TokStar: next(); value *= parseFactor() → 4 ; value = 20
 │   │  peek() = EOF → returns 20
 │  returns 20  ✓
```

The `(2 + 3)` was evaluated to `5` *entirely inside* the recursive `parseExpr` before the outer `parseTerm` ever got to multiply by `4`. The parentheses overrode precedence with nothing but a recursive call.

---

## Part 5 — Building a tree instead of a number

The version above computes the answer *during* parsing. That is fine for a calculator that only ever computes once. But usually you want to parse *once* and then do several things with the result: print it, optimize it, evaluate it repeatedly against different data. For that you build the **AST** — a real tree of objects — and evaluate it as a separate step.

The change is mechanical: each parse function returns a `Node` instead of a `float64`.

```go
type Node interface {
    Eval() float64
}

type NumNode struct{ Val float64 }
func (n NumNode) Eval() float64 { return n.Val }

type BinNode struct {
    Op    TokKind
    Left  Node
    Right Node
}
func (b BinNode) Eval() float64 {
    l, r := b.Left.Eval(), b.Right.Eval()
    switch b.Op {
    case TokPlus:  return l + r
    case TokMinus: return l - r
    case TokStar:  return l * r
    case TokSlash: return l / r
    }
    panic("bad op")
}

type UnaryNode struct {
    Op      TokKind
    Operand Node
}
func (u UnaryNode) Eval() float64 {
    v := u.Operand.Eval()
    if u.Op == TokMinus {
        return -v
    }
    panic("bad unary op")
}
```

The parse functions change only in what they *return* — the control flow is identical:

```go
// expr := term ( ('+' | '-') term )*
func (p *Parser) parseExpr() Node {
    left := p.parseTerm()
    for {
        switch p.peek().Kind {
        case TokPlus:
            p.next()
            left = BinNode{Op: TokPlus, Left: left, Right: p.parseTerm()}
        case TokMinus:
            p.next()
            left = BinNode{Op: TokMinus, Left: left, Right: p.parseTerm()}
        default:
            return left
        }
    }
}
```

Notice the left-associativity is now *visible in the tree shape*: each loop iteration wraps the previous `left` as the **left child** of a new `BinNode`, so `10 - 3 - 2` builds `((10 - 3) - 2)` — the older operand sinks deeper-left. That nesting *is* left-associativity, made structural.

Then evaluation is a separate, dead-simple tree walk:

```go
func evaluate(input string) float64 {
    p := &Parser{tokens: tokenize(input)}
    ast := p.parseExpr()
    return ast.Eval()
}
```

> **This is the form you want for a search engine.** There, `Eval()` returns a *set of documents* instead of a number, and you want the tree to stick around so you can run it, inspect it, or re-run it. Same skeleton; only the leaf operation changes. Hold that thought — it is the punchline of the whole exercise series.

---

## Part 6 — Execution performance (how fast, and *why*)

Now reason about cost precisely. Let **n** = the number of tokens.

### Time complexity: O(n), linear

A recursive-descent parser of this kind runs in **O(n)** time — it does a constant amount of work per token. Here is the actual argument, not just the claim:

- Every call to `next()` advances `pos` by one and never moves it backward. `pos` only goes up.
- A token, once consumed by `next()`, is never revisited. There is no backtracking — the parser never says "oops, undo, try a different rule." (This is true *because* our grammar is **LL(1)**; see below.)
- `peek()` looks at the current token without advancing; each grammar position peeks a bounded number of times before either consuming or returning.

So total work is bounded by a constant times the number of tokens consumed, which is `n`. **Linear time.** Doubling the input length roughly doubles the running time — no worse.

### Why no backtracking? The grammar is LL(1)

"LL(1)" is the property that makes this parser fast and simple: **by looking at just the current token (1 token of lookahead), each function can decide unambiguously what to do.**

- In `parseExpr`'s loop: see `+` or `-`? Keep looping. See anything else? Return. One peek decides.
- In `parseFactor`: see `-`? It's a unary minus. Otherwise it's a primary. One peek decides.
- In `parsePrimary`: see a number? Return it. See `(`? Parse a parenthesized expr. One peek decides.

Because one token of lookahead is always enough to pick the right branch, the parser never has to *guess* a branch, run down it, fail, rewind, and try another. Guess-fail-rewind is **backtracking**, and it is what turns some parsers into exponential-time monsters. Recursive descent over an LL(1) grammar has *none* of it. That is the performance story: **predictive (no backtracking) ⇒ each token touched O(1) times ⇒ O(n) total.**

(If you write a sloppy grammar where one token of lookahead is *not* enough to decide — e.g. two rules that both can start with a `NUMBER` and only differ later — you'd be forced into backtracking and could lose the linear guarantee. The cure is to refactor the grammar so the first token always disambiguates. For arithmetic and boolean queries this is automatic; you rarely have to think about it.)

### Evaluation cost

Evaluating the AST (`ast.Eval()`) visits each node exactly once and does O(1) work per node. The tree has O(n) nodes, so evaluation is also **O(n)**. Tokenize O(n) + parse O(n) + evaluate O(n) = **O(n) end to end.** You cannot do better than linear — you have to at least read every character once.

---

## Part 7 — Memory (where the bytes go, and the one real risk)

Two distinct memory costs. Keep them separate in your head.

### 1. The call stack — O(d), where d = nesting depth

This is the subtle, characteristic cost of recursive descent, and the one place it can actually fall over.

Every function call pushes a **stack frame** — a chunk of memory holding the function's local variables and the return address. When `parseExpr` calls `parseTerm` calls `parseFactor` calls `parsePrimary`, four frames are stacked. When `parsePrimary` hits a `(` and recursively calls `parseExpr` again, the stack grows *another* four frames deep. Deeply nested input means a deep call stack.

How deep? Proportional to the **nesting depth of the expression**, call it `d`:

- `1 + 2 + 3 + ... ` (flat, no parens): the loop in `parseExpr` handles all the `+`s *iteratively*, so the stack stays shallow — roughly constant depth regardless of how many terms. Good.
- `((((((1))))))` (deeply nested parens): each `(` forces another recursion through `expr → term → factor → primary → expr`, so the stack depth grows with the nesting. **O(d)** memory.

For human-written expressions `d` is tiny (you rarely nest parens 10 deep). So in practice the call stack is negligible. **But the risk is real and worth naming:** if a *malicious or pathological* input nests parentheses tens of thousands deep, the call stack can overflow and crash the program (a `stack overflow`). A production parser that accepts untrusted input either (a) caps the maximum nesting depth explicitly, or (b) rewrites the recursion as an explicit loop with a heap-allocated stack (which is essentially what Approach 2, shunting-yard, does — see the sibling document). For a calculator or a search box, the default recursion is completely fine. Just *know* this is the failure mode, because it is the one thing that distinguishes recursive descent's memory profile from the alternatives.

### 2. The token list and the AST — O(n) heap

- The token slice from stage 1 holds `n` tokens: **O(n)** heap memory.
- If you build an AST (Part 5), it has O(n) nodes, each a small heap object: **O(n)** heap memory. If you evaluate inline (Part 4) without building a tree, you skip this — the inline evaluator uses *only* the call stack and a few `float64` locals, no heap tree at all. That is the memory advantage of the inline form, and the cost is that you can only evaluate once.

### Memory summary

| What | Cost | Notes |
|------|------|-------|
| Token list | O(n) | unavoidable; produced in stage 1 |
| Call stack | O(d), d = nesting depth | the characteristic cost; overflow risk on adversarial nesting |
| AST (if built) | O(n) | skip it (inline eval) if you only compute once |
| Inline eval (no AST) | O(d) only | smallest footprint; single-use |

---

## Part 8 — Pitfalls (the bugs you *will* hit, pre-empted)

**Left recursion is fatal.** If you ever write a grammar rule that references itself as its *very first* element —

```
expr := expr '+' term      ← LEFT-recursive: DO NOT do this
```

— and translate it mechanically, `parseExpr` calls `parseExpr` as its first action, with `pos` unchanged. Infinite recursion, instant stack overflow, no progress. This is *the* classic recursive-descent trap. The cure is exactly what we already did: rewrite left recursion as a **loop** (`term ( '+' term )*`). The loop form both eliminates the infinite recursion *and* gives left-associativity. Whenever you see left recursion in a grammar, mechanically convert it to the loop form before writing code.

**Forgetting to consume the closing `)`.** In `parsePrimary`, after `parseExpr` returns from inside parentheses, you must `next()` to consume the `)`. Forget it and the `)` is left in the stream, derailing everything after. (A robust parser checks that the consumed token actually *is* `)` and reports a clear error if not — "expected ')'".)

**Peeking when you meant to consume (or vice-versa).** `peek` looks; `next` eats. Use `next` for tokens you are taking responsibility for, `peek` for deciding. Mixing them up either loops forever (you peeked, decided to act, but never advanced) or skips tokens (you consumed something you only meant to inspect). The "infinite loop" smell in `parseExpr` is almost always a missing `next()`.

**Panic is not error handling.** The code above `panic`s on bad input. For real use, return an `error` describing *where* (token position) and *why* ("expected number, found ')'"). Recursive descent is *excellent* at this — because each function knows exactly what it expected, it can produce precise, local error messages. That is one of its biggest practical advantages, and the exercises' "stretch" goal asks you to use it.

---

## Part 9 — When to use recursive descent (and when not), and why

### Use it when…

- **You control the grammar and can shape it into layers / LL(1).** Arithmetic, boolean queries, config languages, JSON, most programming languages — all fit naturally.
- **You want an AST.** Recursive descent produces a tree directly and cleanly; each function naturally returns a node.
- **You have unary prefix operators** (`-x`, `NOT x`, `!x`). They are *trivially* natural — just a rule `factor := '-' factor | primary`. (In the sibling shunting-yard approach, unary operators are notoriously fiddly. This is the single biggest reason to prefer recursive descent for the boolean engine, whose `NOT` is unary.)
- **You want excellent error messages.** Each function knows what it expected, so errors are precise and local.
- **You want code that reads like the grammar.** The four functions *are* the four rules. Maintenance is easy; a new precedence level is a new function slotted in at the right depth.

### Lean away from it when…

- **The input can be adversarially deeply nested and you can't cap depth.** The call-stack overflow risk (Part 7) is real for untrusted input. Either cap nesting or use an explicit-stack technique.
- **You need to parse a language someone else specified that isn't LL(1)** and you can't refactor it (rare for the domains you'll meet, but real in the wild — some languages need more lookahead or a fundamentally different algorithm like LR).

### Why it's the workhorse of real compilers

This is not a toy technique. **GCC and Clang (C/C++), the Go compiler, and many others use hand-written recursive-descent parsers** for exactly the reasons above: predictable linear performance, total control, and superb error messages — which matter enormously when humans read your compiler errors all day. When people *do* reach for a parser generator instead (yacc/bison produce LR parsers), it's usually for languages that aren't comfortably LL(1). For anything *you* are likely to design — a query language, a config format, a calculator — hand-written recursive descent is the default right answer.

---

## Part 10 — Self-implementation blueprint (do this from a blank file)

You should now be able to build it without looking back. The order matters — each step is testable on its own before you move on.

1. **Define the tokens.** Enumerate every token kind your language needs (`TokNum`, `TokPlus`, …, `TokEOF`). Define the `Token` struct (`Kind` + optional `Val`).

2. **Write `tokenize`.** A flat left-to-right loop. One `case` per single-character token. For multi-character tokens (numbers, words) an *inner loop* to consume the whole run. Skip whitespace. Append `TokEOF` at the very end. **Test it in isolation**: feed `"2 + 34 * 5"` and print the token list. If you don't get `[NUM 2, PLUS, NUM 34, STAR, NUM 5, EOF]`, fix this before going further.

3. **Write the grammar on paper.** One rule per precedence level, loosest on top, tightest on the bottom, atoms + `'(' expr ')'` at the bottom. Use loops `( ... )*` for left-associative binary operators; use right-recursion (`'-' factor`) for unary prefix operators. *Do not write code until the grammar is right* — the code is a mechanical translation of it, so a wrong grammar guarantees wrong code.

4. **Set up the parser state.** `Parser{tokens, pos}` plus `peek()` and `next()`. Get these two helpers exactly right; everything depends on them.

5. **Translate each grammar rule into one function, top-down.** `parseExpr`, `parseTerm`, `parseFactor`, `parsePrimary`. Repetition → `for` loop with a `switch` on `peek()`. Choice → `if`/`switch`. Rule reference → function call. A `(` in `parsePrimary` → recursive `parseExpr()` then consume `)`.

6. **Decide: inline value or AST?** For a one-shot calculator, return `float64` and compute inline (smallest, simplest). For anything you'll evaluate more than once or inspect, return `Node` and add an `Eval()` walk. For the search engine: **build the AST.**

7. **Test associativity deliberately.** `10 - 2 - 3` must be `5`, not `11`. `100 / 5 / 2` must be `10`, not `40`. If you get the wrong answer, you used right-recursion where you needed a loop. This one test catches the most common structural bug.

8. **Test nesting and unary.** `(2 + 3) * 4 = 20`, `-5 + 3 = -2`, `-(2 + 3) = -5`, `--5 = 5`. These exercise the recursion-back-to-top and the right-recursive unary rule.

9. **Add error handling last.** Replace `panic` with errors that report position and expectation. Test malformed input: `2 +`, `(2 + 3`, `* 4`.

---

## Part 11 — Self-check (answer from memory; if you can, you've got it)

- Why does a left-to-right scan get `2 + 3 * 4` wrong, and what does parsing recover that the scan can't see?
- What are the three stages, and what is the one job of each?
- Why must a tokenizer have an *inner loop* for numbers/words? What breaks without it?
- What does the `TokEOF` sentinel buy you, concretely?
- What makes `expr := expr OP expr` *ambiguous*, and how does *layering* the grammar remove the ambiguity *and* encode precedence?
- Exactly where does left-associativity come from in the code — and what would you change to get right-associativity?
- Why can `parseTerm` not handle a `+`, and why is that *required* rather than a limitation?
- How do parentheses work using nothing but a recursive call?
- Why is the parser O(n) in time? State the no-backtracking / LL(1) argument.
- What is the call stack's depth proportional to, and what is the one input that can crash a recursive-descent parser?
- What is left recursion, why is it fatal, and what is the fix?
- Why is recursive descent the natural choice when your language has a *unary* operator like `NOT`?

If those answers come without flipping back, you understand recursive descent deeply enough to write the boolean query engine — and to recognize the same shape in every config parser, JSON reader, and compiler you'll ever open. Now go do the exercises in `parsing.md`; Exercises 2, 3, and 4 are this document applied directly.
