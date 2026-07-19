# Parsing: From Flat Text to Structured Meaning

## Who This Is For

You hit a wall in V3. Your boolean query engine handles `redis AND kafka` fine, but `(redis OR kafka) AND replication` makes your left-to-right scan collapse. You correctly sensed that you need a **stack**. What you actually need is a thing the stack is *part of*: a **parser**.

This document teaches parsing from first principles. The examples use a **calculator** — evaluating arithmetic like `2 + 3 * 4 - (5 - 1)`. This is intentional, and it is the same trick used in the subsystem lesson: you learn the pattern in a clean, neutral domain, then apply it yourself to AegisSearch.

Why a calculator? Because arithmetic and boolean queries are the **same problem wearing different clothes**:

| Arithmetic | Boolean Query | Operation |
|------------|---------------|-----------|
| `3 * 4`    | `redis AND kafka` | combine two operands |
| `3 + 4`    | `redis OR kafka`  | combine two operands |
| `-5`       | `NOT redis`       | unary prefix operator |
| `*` binds tighter than `+` | `AND` binds tighter than `OR` | precedence |
| `(...)`    | `(...)`           | override precedence |

If you can build a calculator that respects `* > +`, you can build a query engine that respects `NOT > AND > OR`. They are structurally identical. Master one, you own the other.

By the end you should be able to write a recursive-descent parser from scratch without a reference, explain why precedence lives in the *shape* of the grammar, and implement your boolean query engine cleanly.

---

## The Mental Model: Three Stages

The single biggest mistake — the one your current V3 code is making — is trying to do everything in one pass. Real parsers separate three stages:

```
  "2 + 3 * 4"                          raw string
        │
        ▼   ── STAGE 1: TOKENIZE ──
  [NUM(2)] [PLUS] [NUM(3)] [STAR] [NUM(4)]      flat list of tokens
        │
        ▼   ── STAGE 2: PARSE ──
            (+)
           /   \                       a TREE that captures structure
        (2)    (*)
              /   \
           (3)    (4)
        │
        ▼   ── STAGE 3: EVALUATE ──
           14                          walk the tree, compute the answer
```

- **Tokenize** turns characters into *tokens* — meaningful atoms. `"23"` becomes one number token, not two digit characters. Spaces vanish here.
- **Parse** turns the flat token list into a **tree** that encodes the structure — what groups with what, what happens first. This is where precedence and parentheses are resolved.
- **Evaluate** walks the tree and produces the answer.

Each stage is simple *because* it only does one job. When you mash them together (peeking ahead, computing partial results, juggling a stack inline) every case multiplies against every other case. Separation is the whole lesson.

> The tree in stage 2 is called an **Abstract Syntax Tree (AST)**. "Abstract" because it throws away noise like parentheses and whitespace — the structure they implied is now baked into the tree's *shape*. Note in the tree above there are no parens and no spaces; `*` sits below `+`, which *is* the statement "multiply first."

---

## Stage 1 — Tokenizing

Tokenizing (also called *lexing* or *scanning*) is the easy stage. You walk the string left to right and emit tokens.

A token is a small struct: a **kind** and, when relevant, a **value**.

```go
type TokKind int

const (
    TokNum TokKind = iota
    TokPlus
    TokMinus
    TokStar
    TokSlash
    TokLParen
    TokRParen
    TokEOF        // "end of input" — a sentinel, explained below
)

type Token struct {
    Kind TokKind
    Val  float64   // only meaningful when Kind == TokNum
}
```

The scanner itself:

```go
func tokenize(input string) []Token {
    var tokens []Token
    i := 0
    for i < len(input) {
        c := input[i]
        switch {
        case c == ' ':
            i++                     // skip whitespace
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
            // a number can be many digits — consume them all
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
    tokens = append(tokens, Token{Kind: TokEOF})   // mark the end
    return tokens
}
```

**The one subtle part: multi-character tokens.** When you hit a digit, you don't emit one token — you keep consuming digits until you hit a non-digit. `"234"` is *one* `TokNum` with value `234`, not three tokens. This inner loop is the difference between a tokenizer and a naive character splitter.

**Why the `TokEOF` sentinel?** The parser constantly asks "what's the next token?" Without an explicit end marker, every such check needs a bounds test (`if pos < len(tokens)`). By appending one `TokEOF` at the end, the parser can always safely peek — it'll just see `EOF` and know to stop. This is the same trick as a null terminator on a C string: a sentinel removes a whole category of edge cases.

> **Map to AegisSearch:** your tokens are `WORD("redis")`, `AND`, `OR`, `NOT`, `LPAREN`, `RPAREN`, `EOF`. A "word" is your multi-character token — consume letters until you hit a space or paren. Structurally identical to consuming digits.

---

## Stage 2 — The Heart: Grammars, Precedence, and Associativity

Before any parsing code, you need to be able to *describe* the language. We do that with a **grammar**.

### What a grammar is

A grammar is a set of rules. Each rule names a structure and lists what it's made of. The standard notation (BNF / EBNF):

```
expr    := NUMBER '+' NUMBER
```

Read `:=` as "is defined as." This rule says: an `expr` is a number, then a plus, then a number. That grammar only describes things like `2 + 3`. Too weak. We need recursion and repetition.

Notation you'll use:
- `A := B C` — an A is a B followed by a C (sequence)
- `A := B | C` — an A is a B *or* a C (choice)
- `( B )*` — zero or more repetitions of B
- `( B )?` — optional B (zero or one)
- UPPERCASE = a token (terminal, can't be broken down)
- lowercase = a rule (nonterminal, defined elsewhere)

### Precedence lives in the SHAPE of the grammar

Here is the entire trick. This is the most important idea in the document. Read it twice.

A naive grammar for arithmetic:

```
expr := expr OP expr | NUMBER | '(' expr ')'
OP   := '+' | '-' | '*' | '/'
```

This is **ambiguous**. For `2 + 3 * 4` it permits *two* different trees:

```
       (+)                         (*)
      /   \                       /   \
   (2)     (*)                  (+)    (4)
          /   \                /   \
        (3)   (4)            (2)    (3)

   = 2 + (3*4) = 14         = (2+3) * 4 = 20
```

The grammar doesn't say which is right. To remove the ambiguity, you **layer** the grammar — one rule per precedence level, lowest-binding operator on the outside:

```
expr    := term   ( ('+' | '-') term   )*      ← lowest precedence (loosest)
term    := factor ( ('*' | '/') factor )*      ← higher precedence (tighter)
factor  := '-' factor | primary                ← unary minus (tightest)
primary := NUMBER | '(' expr ')'               ← atoms, and the recursion back to expr
```

Why does this layering encode precedence? Because **the operators you want to bind tightest are reached deepest in the recursion, so they get grouped first.**

Walk it for `2 + 3 * 4`:
- `expr` must first find a `term`. It descends.
- `term` must first find a `factor` → `primary` → `2`. Back in `term`, the next token is `+`, which `term` does **not** handle (term only handles `*` `/`). So `term` returns just `2`.
- Back in `expr`: it sees `+`, consumes it, and asks for *another* `term`.
- That second `term` reads `3`, then sees `*` — which it **does** handle — consumes it, reads `4`, and combines: `3 * 4 = 12`. The `term` returns `12` as a single unit.
- `expr` now has `2 + 12 = 14`. ✓

The `*` got pulled into a tighter bundle because `term` sits *underneath* `expr`. **The grammar's nesting depth IS the precedence.** You never write a precedence comparison anywhere — the structure does it for you.

### Associativity: which way do ties lean?

When operators have *equal* precedence, associativity decides grouping. `10 - 3 - 2`:
- Left-associative (correct for `-`): `(10 - 3) - 2 = 5`
- Right-associative (wrong here): `10 - (3 - 2) = 9`

In the layered grammar, the `( ('+'|'-') term )*` **loop** gives you **left-associativity** for free — you accumulate left-to-right into a running value. If you instead wrote the rule with *recursion on the right* (`term := factor ('+' term)?`), you'd get right-associativity. Exponentiation (`2 ^ 3 ^ 2 = 2^(3^2) = 512`) and unary prefix operators are right-associative; most binary operators are left. For your boolean engine, `AND` and `OR` are associative either way (the result is the same), so a left loop is simplest.

---

## Approach 1 — Recursive Descent (learn this first)

Recursive descent is the most direct way to turn a layered grammar into code. The rule is mechanical and beautiful:

> **Every grammar rule becomes one function. A rule that references another rule becomes a function call. Repetition `()*` becomes a loop. Choice `|` becomes an if/switch.**

The grammar above translates *line for line* into code. Here is the complete evaluator.

```go
type Parser struct {
    tokens []Token
    pos    int
}

// peek returns the current token without consuming it.
func (p *Parser) peek() Token { return p.tokens[p.pos] }

// next consumes and returns the current token.
func (p *Parser) next() Token {
    t := p.tokens[p.pos]
    p.pos++
    return t
}
```

```go
// expr := term ( ('+' | '-') term )*
func (p *Parser) parseExpr() float64 {
    value := p.parseTerm()                 // left operand
    for {
        switch p.peek().Kind {
        case TokPlus:
            p.next()                       // consume '+'
            value += p.parseTerm()         // accumulate left-to-right
        case TokMinus:
            p.next()
            value -= p.parseTerm()
        default:
            return value                   // no more + or -, we're done at this level
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
        p.next()                           // consume unary '-'
        return -p.parseFactor()            // recurse: handles "--5", "-(2+3)"
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
        value := p.parseExpr()             // recurse back to the TOP of the grammar
        p.next()                           // consume the ')'
        return value
    default:
        panic(fmt.Sprintf("unexpected token: %v", t.Kind))
    }
}
```

Three things to notice, because each one teaches something:

1. **`parseExpr` calls `parseTerm`, which calls `parseFactor`, which calls `parsePrimary`.** The call stack mirrors the grammar's layers exactly. The deeper the call, the tighter the binding. *The recursion stack is the stack you were reaching for* — you don't manage it by hand; the language's call stack does it.

2. **Parentheses are nearly free.** In `parsePrimary`, a `(` simply calls `parseExpr` again — jumping back to the top of the grammar. Whatever is inside the parens is parsed as a fully independent expression, then handed back as a single value. This is why `(2 + 3) * 4` works: the `(2 + 3)` collapses to `5` *before* the surrounding `term` applies the `*`. Recursion gives you arbitrary nesting depth with zero extra code.

3. **Each function only knows its own precedence level.** `parseTerm` literally cannot handle `+`; it returns the moment it sees one. That "ignorance" is what makes the layering work.

### Trace: `2 + 3 * 4`

```
parseExpr
 ├─ parseTerm → parseFactor → parsePrimary → 2     (term returns 2, next is '+', not its job)
 ├─ sees '+', consumes
 └─ parseTerm
     ├─ parseFactor → parsePrimary → 3
     ├─ sees '*', consumes
     ├─ parseFactor → parsePrimary → 4
     └─ returns 3 * 4 = 12
 returns 2 + 12 = 14 ✓
```

### Building an AST instead of a value

The evaluator above computes the answer *during* parsing. Often you want to separate stages fully: parse into a tree, evaluate later (so you can also print it, optimize it, or — in your case — run it against different document sets). Same structure, but each function returns a `Node` instead of a `float64`:

```go
type Node interface{ Eval() float64 }

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
    case TokStar:  return l * r
    // ...
    }
    panic("bad op")
}
```

`parseExpr` would then do `value = BinNode{Op: TokPlus, Left: value, Right: p.parseTerm()}` instead of `value += ...`. You get a real tree you can walk however you like. **For AegisSearch, build the AST** — because `Eval` there returns a *set of documents*, and you'll want the tree around.

---

## Approach 2 — Shunting-Yard (the stack you were reaching for)

Recursive descent hides the stack inside the call stack. The **shunting-yard algorithm** (Edsger Dijkstra) makes the stack *explicit*. This is the algorithm your V3 comments were groping toward — so it's worth understanding even if you ultimately choose recursive descent.

The idea: convert the infix expression (`2 + 3 * 4`) into **postfix / Reverse Polish Notation** (`2 3 4 * +`), which has **no parentheses and no precedence ambiguity** and can be evaluated by a trivial stack walk.

You use two structures:
- an **output** list (the postfix result building up)
- an **operator stack** (operators waiting to be placed)

with a **precedence table**:

```go
var prec = map[TokKind]int{
    TokPlus: 1, TokMinus: 1,
    TokStar: 2, TokSlash: 2,
}
```

The algorithm:

```
for each token:
    NUMBER         → append to output
    operator o1    → while operator o2 on top of stack has prec(o2) >= prec(o1):
                         pop o2 to output
                     push o1
    '('            → push to stack
    ')'            → pop operators to output until '(' is popped (discard the parens)
at end: pop all remaining operators to output
```

### Trace: `2 + 3 * 4`

```
token   action                          output        op-stack
2       output                          2
+       stack empty, push               2             +
3       output                          2 3           +
*       prec(*)=2 > prec(+)=1, push     2 3           + *
4       output                          2 3 4         + *
EOF     pop all                         2 3 4 * +
```

Result: `2 3 4 * +`. Now evaluate that with a one-stack walk:

```
read 2 → push        stack: [2]
read 3 → push        stack: [2, 3]
read 4 → push        stack: [2, 3, 4]
read * → pop 4,3, push 12   stack: [2, 12]
read + → pop 12,2, push 14  stack: [14]
answer = 14 ✓
```

**Why the `>=` comparison is the precedence engine:** when you meet `+` after already stacking nothing, you push it. When you later meet `*`, `*` binds tighter than the `+` below it, so you *don't* pop the `+` yet — `*` jumps ahead. But if you met `+` again, the existing `+` has equal precedence and (for left-associativity) gets popped first. That one `>=` is doing the same job the grammar layering did in recursive descent — just out in the open.

> The RPN evaluator (the second half) is your **easy exercise** below. It is the cleanest possible demonstration that *a stack evaluates structured expressions*, and it's the kernel of everything here.

---

## Which Approach Should You Use?

| | Recursive Descent | Shunting-Yard |
|---|---|---|
| Precedence lives in | grammar shape (implicit) | a precedence table (explicit) |
| The stack is | the call stack (hidden) | a data structure you manage |
| Unary ops (`NOT`, `-`) | natural (`factor := '-' factor`) | fiddly (needs special-casing) |
| Readability | reads like the grammar | reads like a state machine |
| Best when | you control the grammar, want an AST | simple flat precedence, want one pass |

For **AegisSearch I recommend recursive descent.** Your `NOT` is a unary prefix operator, and recursive descent handles unary operators trivially while shunting-yard makes them awkward. It also gives you a clean AST whose `Eval()` returns document sets. But implement shunting-yard at least once (the exercises make you) so you understand the explicit-stack mechanics and can read either when you meet them in the wild.

---

## Mapping Back to AegisSearch

Now collect the payoff. Your boolean grammar, with **NOT > AND > OR** precedence, is the arithmetic grammar with the names swapped:

```
expr   := term   ( OR  term   )*      ← OR  is loosest  (like +)
term   := factor ( AND factor )*      ← AND is tighter  (like *)
factor := NOT factor | atom           ← NOT is tightest, unary (like unary -)
atom   := WORD | '(' expr ')'         ← a word, or a parenthesized subquery
```

The only real difference from the calculator: **`Eval()` returns a set of document IDs, not a number.** And the operators are set operations:

| Node | Eval returns |
|------|--------------|
| `WORD("redis")` | the postings list for `redis` — i.e. `searchIdx["redis"]` as a set |
| `a AND b` | `Eval(a) ∩ Eval(b)` — set **intersection** |
| `a OR b`  | `Eval(a) ∪ Eval(b)` — set **union** |
| `NOT a`   | `U \ Eval(a)` — set **difference** from the universe `U` of all docs |

That universe `U` for `NOT` is the cost of treating `NOT` as unary: you must be able to produce "all document IDs." That's cheap — your store already knows every doc. (Note this resolves the binary-vs-unary `NOT` question from our earlier discussion: the grammar above is the principled **unary** form, so you'd write `redis AND NOT cache`. If you prefer the shorthand `redis NOT cache` meaning "redis minus cache," that's a **binary** difference operator sitting at `AND`'s level instead — pick one deliberately and make the grammar say so.)

Everything else — the three-stage split, the layered grammar, the one-function-per-level recursion, parentheses-via-recursion — transfers unchanged. You already have the postings lists from V2. You're not building something new; you're renaming the calculator.

---

## Exercises

Do these in order. Each builds the muscle the next one needs. Don't skip to the hard one — the whole point is that by the time you arrive at it, it feels like a renaming.

### Exercise 1: The Search Lexer (Tokenization)

> **Stage 1 of the pipeline.** This is the warm-up. You are converting a raw query string into a flat list of typed tokens — nothing more. No structure, no precedence, no trees yet. Get this rock-solid first, because every later exercise consumes its output.

**Goal:** Write a function `Tokenize(query string) []Token` that breaks a search query into meaningful symbols, not just whitespace-separated words.

**Why this matters:** `strings.Fields()` would split `title:"Redis Cache"` into three broken pieces (`title:"Redis`, `Cache"`). A real lexer recognizes that a quoted phrase is *one* atom and that `title:` is a field qualifier. You are teaching the program to read structure that whitespace alone hides.

#### Your Task

Define these token types:

```go
type TokenType int

const (
    T_TERM   TokenType = iota // a bare search word, e.g. redis
    T_PHRASE                  // quoted text, e.g. "Redis Cache"
    T_AND                     // the AND keyword
    T_OR                      // the OR keyword
    T_NOT                     // the NOT keyword
    T_LPAREN                  // (
    T_RPAREN                  // )
    T_FIELD                   // a field qualifier, e.g. title:
)

type Token struct {
    Type  TokenType
    Value string
}
```

Then implement `func Tokenize(query string) []Token`.

#### Tokenizing Rules

| Rule | Input fragment | Produces |
|------|----------------|----------|
| Keywords are **case-insensitive** | `AND`, `and`, `And` | `T_AND` (store value as `"AND"`) |
| Parentheses are operators | `(` / `)` | `T_LPAREN` / `T_RPAREN` |
| Quoted text is **one** token, quotes stripped | `"Redis Cache"` | `T_PHRASE` value `Redis Cache` |
| A word ending in `:` is a field | `title:` | `T_FIELD` value `title` |
| Anything else is a plain term | `redis` | `T_TERM` value `redis` |

> **Edge cases to handle deliberately:** a field qualifier may be followed by either a term (`title:redis`) or a phrase (`title:"Redis Cache"`). Collapse extra whitespace. A lone `not` with nothing after it is a malformed query — decide whether to error or pass it through, but be consistent.

#### Worked Examples (6)

> **Output order = input order (left to right).** The lexer never reorders anything. Tokens come out in exactly the same sequence the symbols appear in the query — it only *groups* characters into atoms (a multi-word phrase, a `field:`) and *labels* each atom with a type. Read each output slice straight across, left to right, and it lines up one-for-one with the input. (Reordering is a *later* stage's job: Exercise 2's shunting-yard is where token order actually changes.)

**Example 1 — keywords and a phrase**

```text
Input:   title:"Redis Cache" AND redis OR kafka
Output:  [{FIELD, "title"}, {PHRASE, "Redis Cache"}, {AND, "AND"},
          {TERM, "redis"}, {OR, "OR"}, {TERM, "kafka"}]
```

**Example 2 — case-insensitive keywords**

```text
Input:   redis and Kafka or GOLANG
Output:  [{TERM, "redis"}, {AND, "AND"}, {TERM, "Kafka"},
          {OR, "OR"}, {TERM, "GOLANG"}]
```

**Example 3 — parentheses and NOT**

```text
Input:   (redis OR kafka) AND NOT cache
Output:  [{LPAREN, "("}, {TERM, "redis"}, {OR, "OR"}, {TERM, "kafka"},
          {RPAREN, ")"}, {AND, "AND"}, {NOT, "NOT"}, {TERM, "cache"}]
```

**Example 4 — field applied to a bare term**

```text
Input:   body:database AND title:redis
Output:  [{FIELD, "body"}, {TERM, "database"}, {AND, "AND"},
          {FIELD, "title"}, {TERM, "redis"}]
```

**Example 5 — single bare term (the simplest valid query)**

```text
Input:   golang
Output:  [{TERM, "golang"}]
```

**Example 6 — parenthesized groups on both sides of an operator**

Two `(...)` groups joined by `AND`. Notice the lexer just walks straight through: it emits every paren and every inner token in order and does **not** care that the parens "balance" or match — that's the parser's job in Exercise 3. To the lexer, `(` and `)` are simply two more tokens in the stream.

```text
Input:   (redis OR kafka) AND (golang OR cache)
Output:  [{LPAREN, "("}, {TERM, "redis"}, {OR, "OR"}, {TERM, "kafka"}, {RPAREN, ")"},
          {AND, "AND"},
          {LPAREN, "("}, {TERM, "golang"}, {OR, "OR"}, {TERM, "cache"}, {RPAREN, ")"}]
```

Read it left to right and it maps one-for-one onto the input: open paren, three tokens, close paren, the `AND`, then the second group the same way. Eleven symbols in, eleven tokens out, same order.

**Integration:** Replace your V2 `strings.Fields()` call with this `Tokenize()` function. Print the tokens (`fmt.Printf("%+v\n", tokens)`) and confirm all six examples match before moving on.

---

### Exercise 2: Shunting-Yard to RPN (Reverse Polish Notation)

> **Stage 2, explicit-stack flavor.** Here you make precedence and parentheses disappear by reordering the tokens. The output — Reverse Polish Notation — has no parentheses and no ambiguity, so a trivial stack walk (Exercise 4, Path B) can evaluate it. This is the algorithm your V3 code was instinctively reaching for.

**Goal:** Use Dijkstra's Shunting-Yard algorithm to convert your infix token list into a postfix (RPN) token list.

**Precedence (tightest binds first):** `NOT` (highest) > `AND` > `OR` (lowest). `NOT` is a **unary prefix** operator; `AND` and `OR` are binary and left-associative.

**Reading RPN:** in postfix, the operator comes *after* its operands. `redis kafka AND` means "take `redis`, take `kafka`, then AND them." There is exactly one valid evaluation order, which is the entire point.

#### Your Task

Write `func ToRPN(tokens []Token) []Token`. You maintain two structures: an **output list** (the RPN result) and an **operator stack** (operators waiting their turn).

#### Algorithm

```text
for each token:
    TERM | PHRASE | FIELD   → append to output
    AND | OR  (call it o1)   → while top of stack is an operator with
                                prec(top) >= prec(o1):  pop top to output
                              then push o1
    NOT                      → push to stack (unary, right-associative)
    LPAREN                   → push to stack
    RPAREN                   → pop operators to output until LPAREN is
                               removed (discard both parens)
at end:                      → pop every remaining operator to output
```

The `>=` comparison is the precedence engine: an incoming `OR` flushes any waiting `AND` (because `AND` binds tighter and must group first), but an incoming `AND` does **not** flush a waiting `OR`.

#### Worked Examples (5)

Each example gives the **input** infix tokens, a readable RPN line (operands by value, operators by keyword), and the **actual `[]Token` slice** your `ToRPN` must return. Note: parentheses never appear in the output — they only steer the stack. A `FIELD` token stays immediately in front of the `TERM`/`PHRASE` it qualifies.

**Example 1 — precedence with a field/phrase operand**

```text
Input (infix):  title:"Redis Cache" AND redis OR kafka
Readable RPN:   title:"Redis Cache"  redis  AND  kafka  OR
```

```text
Output []Token:
[{FIELD, "title"}, {PHRASE, "Redis Cache"}, {TERM, "redis"}, {AND, "AND"}, {TERM, "kafka"}, {OR, "OR"}]
```

Step-by-step (so you can see the stack do its job):

```text
token                   action                                   output                              op-stack
title:"Redis Cache"     operand → output                         FIELD,PHRASE                         (empty)
AND                     stack empty → push                       FIELD,PHRASE                         AND
redis                   operand → output                         FIELD,PHRASE,redis                   AND
OR                      prec(AND)=2 >= prec(OR)=1 → pop AND,      FIELD,PHRASE,redis,AND               OR
                        then push OR
kafka                   operand → output                         FIELD,PHRASE,redis,AND,kafka         OR
(end)                   pop remaining                            FIELD,PHRASE,redis,AND,kafka,OR      (empty)
```

**Example 2 — parentheses override precedence**

```text
Input (infix):  (redis OR kafka) AND cache
Readable RPN:   redis  kafka  OR  cache  AND
```

```text
Output []Token:
[{TERM, "redis"}, {TERM, "kafka"}, {OR, "OR"}, {TERM, "cache"}, {AND, "AND"}]
```

**Example 3 — unary NOT binds tightest**

```text
Input (infix):  redis AND NOT cache
Readable RPN:   redis  cache  NOT  AND
```

```text
Output []Token:
[{TERM, "redis"}, {TERM, "cache"}, {NOT, "NOT"}, {AND, "AND"}]
```

**Example 4 — chained same-precedence (left-associative)**

```text
Input (infix):  redis OR kafka OR golang
Readable RPN:   redis  kafka  OR  golang  OR
```

```text
Output []Token:
[{TERM, "redis"}, {TERM, "kafka"}, {OR, "OR"}, {TERM, "golang"}, {OR, "OR"}]
```

**Example 5 — nested parentheses and mixed operators**

```text
Input (infix):  (redis OR kafka) AND (golang OR NOT cache)
Readable RPN:   redis  kafka  OR  golang  cache  NOT  OR  AND
```

```text
Output []Token:
[{TERM, "redis"}, {TERM, "kafka"}, {OR, "OR"}, {TERM, "golang"}, {TERM, "cache"}, {NOT, "NOT"}, {OR, "OR"}, {AND, "AND"}]
```

**Integration:** This RPN list feeds the fast stack evaluator in Exercise 4 (Path B). Verify Example 2 first — the absence of any `LPAREN`/`RPAREN` in the output is the clearest proof that parentheses were correctly dissolved.

---

### Exercise 3: Recursive Descent Parser (AST Builder)

> **Stage 2, implicit-stack flavor — and the most important exercise here.** Instead of flattening to RPN, you build an explicit **Abstract Syntax Tree**. A tree is easier to debug, easier to print, and easier to extend later (V6 ranking will hang scores on these nodes). Precedence lives in the *shape* of the grammar, so you never write a single precedence comparison.

**Goal:** Write `func Parse(tokens []Token) Node` that turns the token list into an AST whose structure already encodes `NOT > AND > OR`.

#### Your Task

Define the node types:

```go
type Node interface{ node() }

type TermNode struct {
    Value string
    Field string // optional, e.g. "title"; empty means no field qualifier
}

type PhraseNode struct {
    Value string
    Field string // optional
}

type AndNode struct{ Left, Right Node }
type OrNode  struct{ Left, Right Node }
type NotNode struct{ Child Node }

func (TermNode) node()   {}
func (PhraseNode) node() {}
func (AndNode) node()    {}
func (OrNode) node()     {}
func (NotNode) node()    {}
```

Implement `Parse` against this layered grammar — **one function per rule**:

```text
expression := orExpr
orExpr     := andExpr ( "OR"  andExpr )*
andExpr    := notExpr ( "AND" notExpr )*
notExpr    := "NOT" notExpr | primary
primary    := "(" expression ")" | FIELD? ( TERM | PHRASE )
```

> **How the shape encodes precedence:** `orExpr` sits on the outside, so it groups last (loosest). `andExpr` is nested inside it, so `AND` groups before `OR`. `notExpr` is deeper still, so `NOT` binds tightest. The `( ... )*` loops give left-associativity for free. A `(` in `primary` recurses back to `expression`, which is how parentheses get arbitrary nesting with zero extra code.

#### Worked Examples (5)

Each example shows the **input** token stream, the **output** as a visual tree (so you can see the shape at a glance), and the same output written as the literal Go structs your `Parse` should return. `Field` is shown only when set.

**Example 1 — AND binds tighter than OR (no parens needed)**

The `AND` groups first even though `OR` is written last, so `OR` ends up on top.

```text
Input:  redis AND kafka OR golang

Output (tree):          OrNode
                        /     \
                   AndNode    TermNode("golang")
                   /     \
        TermNode("redis") TermNode("kafka")
```

```go
// Output (structs):
OrNode{
    Left: AndNode{
        Left:  TermNode{Value: "redis"},
        Right: TermNode{Value: "kafka"},
    },
    Right: TermNode{Value: "golang"},
}
```

**Example 2 — parentheses override the default grouping**

The parens force `OR` underneath `AND` — the opposite of Example 1.

```text
Input:  redis AND (kafka OR golang)

Output (tree):          AndNode
                        /     \
         TermNode("redis")    OrNode
                              /     \
                 TermNode("kafka") TermNode("golang")
```

```go
// Output (structs):
AndNode{
    Left: TermNode{Value: "redis"},
    Right: OrNode{
        Left:  TermNode{Value: "kafka"},
        Right: TermNode{Value: "golang"},
    },
}
```

**Example 3 — unary NOT is tightest**

```text
Input:  redis AND NOT cache

Output (tree):          AndNode
                        /     \
         TermNode("redis")    NotNode
                                 |
                          TermNode("cache")
```

```go
// Output (structs):
AndNode{
    Left:  TermNode{Value: "redis"},
    Right: NotNode{Child: TermNode{Value: "cache"}},
}
```

**Example 4 — field qualifier and phrase as leaves**

```text
Input:  title:"Redis Cache" OR body:database

Output (tree):          OrNode
                        /     \
   PhraseNode            TermNode
   {Value:"Redis Cache", {Value:"database",
    Field:"title"}        Field:"body"}
```

```go
// Output (structs):
OrNode{
    Left:  PhraseNode{Value: "Redis Cache", Field: "title"},
    Right: TermNode{Value: "database", Field: "body"},
}
```

**Example 5 — left-associative chain (note the left-leaning nesting)**

Two `OR`s at the same level nest to the **left** because of the `( "OR" andExpr )*` loop.

```text
Input:  redis OR kafka OR golang

Output (tree):          OrNode
                        /     \
                   OrNode     TermNode("golang")
                   /     \
        TermNode("redis") TermNode("kafka")
```

```go
// Output (structs):
OrNode{
    Left: OrNode{
        Left:  TermNode{Value: "redis"},
        Right: TermNode{Value: "kafka"},
    },
    Right: TermNode{Value: "golang"},
}
```

**Integration:** This `Parse` function is your Version 5.5 core. Write a small recursive `Print(node Node, depth int)` to indent and dump the tree, then confirm all five ASTs above. **Do not skip this exercise** — it is the one that makes the whole lesson click.

---

### Exercise 4: The Evaluator (Hooking into YOUR V4 Inverted Index)

> **Stage 3 — the payoff.** Everything so far produced *structure*. Now you walk that structure against your real inverted index and return actual documents. The operators stop being arithmetic and become **set operations**: `AND` is intersection, `OR` is union, `NOT` is complement against the universe of all documents.

**Goal:** Write an evaluator that consumes your AST (Exercise 3) *or* your RPN (Exercise 2), uses your real V4 `map[string][]string` inverted index, and returns the matching document filenames.

#### Your Task — choose ONE path

**Path A — evaluate the AST (recommended):** `func EvalAST(node Node, index map[string][]string) []string`

| Node | Returns |
|------|---------|
| `TermNode`   | `index[strings.ToLower(value)]` — missing term ⇒ empty set, not a panic |
| `PhraseNode` | the docs matching the phrase (for now, treat like a term lookup) |
| `AndNode`    | `Intersection(Eval(Left), Eval(Right))` |
| `OrNode`     | `Union(Eval(Left), Eval(Right))` |
| `NotNode`    | `Difference(allDocs, Eval(Child))` |

**Path B — evaluate the RPN:** `func EvalRPN(tokens []Token, index map[string][]string) []string`

Walk the RPN list with a stack of result sets. On an operand, push its postings set. On `AND`, pop two sets, push their intersection. On `OR`, pop two, push their union. On `NOT`, pop one, push `allDocs` minus it. At the end the stack holds exactly one set — the answer.

> **Helper hint:** results are *sets*, so order is not significant — `[redis.txt, cache.txt]` and `[cache.txt, redis.txt]` are equal answers. Back your `Intersection`/`Union`/`Difference` helpers with a `map[string]bool` to dedupe, then collect the keys.

#### Test Data (your exact V4 index)

```go
index := map[string][]string{
    "redis":    {"redis.txt", "cache.txt"},
    "kafka":    {"kafka.txt"},
    "golang":   {"golang.txt"},
    "database": {"redis.txt", "postgres.txt"},
    "cache":    {"cache.txt", "redis.txt"},
}
allDocs := []string{"redis.txt", "kafka.txt", "golang.txt", "cache.txt", "postgres.txt"}
```

#### Worked Examples (5)

Results are sets; order may differ in your output.

**Example 1 — AND with no overlap**

```text
Query:  redis AND kafka
Work:   {redis.txt, cache.txt} ∩ {kafka.txt} = ∅
Output: []
```

**Example 2 — OR unions the postings**

```text
Query:  redis OR kafka
Work:   {redis.txt, cache.txt} ∪ {kafka.txt}
Output: [redis.txt, cache.txt, kafka.txt]
```

**Example 3 — parentheses drive the grouping**

```text
Query:  redis AND (kafka OR cache)
Work:   (kafka OR cache) = {kafka.txt} ∪ {cache.txt, redis.txt}
                         = {kafka.txt, cache.txt, redis.txt}
        redis ∩ that     = {redis.txt, cache.txt}
Output: [redis.txt, cache.txt]
```

**Example 4 — unary NOT against the universe**

```text
Query:  NOT cache
Work:   allDocs \ {cache.txt, redis.txt}
Output: [kafka.txt, golang.txt, postgres.txt]
```

**Example 5 — AND combined with NOT**

```text
Query:  database AND NOT redis
Work:   database          = {redis.txt, postgres.txt}
        NOT redis         = allDocs \ {redis.txt, cache.txt}
                          = {kafka.txt, golang.txt, postgres.txt}
        intersection      = {postgres.txt}
Output: [postgres.txt]
```

#### Integration

Replace your current `Search()` function so the full three-stage pipeline runs end to end:

```go
func Search(query string) []string {
    tokens := Tokenize(query)          // Ex1 — string → tokens
    ast := Parse(tokens)               // Ex3 — tokens → AST
    return EvalAST(ast, invertedIndex) // Ex4 — AST → documents
}
```

Run `go build`, then sanity-check against the examples above — especially Example 1 (`redis AND kafka` → `[]`) and Example 2 (`redis OR kafka` → `[redis.txt, cache.txt, kafka.txt]`).






## Summary Checklist

Before you write your V3 engine, make sure you can answer these from memory:

- [ ] What are the three stages of parsing, and why separate them?
- [ ] What is the difference between a token and a character?
- [ ] Why does a tokenizer need an inner loop for numbers / words?
- [ ] What is an ambiguous grammar, and why is `expr := expr OP expr` ambiguous?
- [ ] How does *layering* a grammar encode operator precedence?
- [ ] Where does left-associativity come from in recursive descent?
- [ ] Why does each parse function only handle its own precedence level?
- [ ] How do parentheses work with only a recursive call and no extra logic?
- [ ] In shunting-yard, what does the `>=` precedence comparison accomplish?
- [ ] Why is recursive descent better than shunting-yard for unary `NOT`?
- [ ] What does `Eval()` return for the AegisSearch AST, and what are AND/OR/NOT in set terms?
- [ ] What does `NOT` require from your document store, and why?
- [ ] What is operator arity, and how does the same `-` symbol act as both unary and binary?
- [ ] Why can a binary-`NOT` engine *not* express a bare complement like "all docs without cache"?

If you can answer all twelve without looking back, you understand parsing deeply enough to build the query engine — and to recognize this same shape everywhere it appears (config languages, JSON, SQL, regex, every compiler ever written).

Go build it.
