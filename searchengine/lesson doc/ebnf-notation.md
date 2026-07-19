# Reading Grammar Notation (EBNF) — from absolute zero

> **What this document is.** A complete, self-contained guide to reading the grammar notation used in `parsing-approach1.md` and `parsing-approach2.md`. It assumes you can program and nothing else — no compiler background, no language theory, never seen a "grammar" in your life. By the end you will be able to look at any line like `expr := term ( ('+' | '-') term )*` and read it out loud in plain English without hesitation, *and* explain why it describes the strings it describes. This is a **reading** skill, not a writing skill — the goal is that the approach docs become fully transparent.
>
> The notation those docs use is a stripped-down, practical dialect of something called **EBNF** (Extended Backus–Naur Form). We'll learn the exact dialect they use, then briefly note where "real" EBNF differs so other documents on the internet don't confuse you.

---

## Part 0 — What problem is this notation even solving?

Before any symbols: *why does this notation exist at all?*

When you build a parser, you are writing a program that decides **which strings of text are valid** and **what structure they have**. "Valid" for a calculator means things like `2 + 3 * 4` or `(5 - 1)` are allowed, while `2 + + )` is garbage. Before you can write that program, you have to *describe precisely* what counts as valid. English is too vague for this:

> "An expression is some numbers with plus and minus and times signs between them, and you can use brackets."

That sentence is full of holes. Can an expression be empty? Can it start with a `+`? Can brackets be empty `()`? How deep can brackets nest? You cannot write correct code from a description this loose.

A **grammar** is that same description written in a tiny, exact, symbolic language that has **no holes**. Every valid string is covered; every invalid string is excluded; and crucially, the grammar's *shape* tells you how to write the parser (that's the whole point of the approach docs — "each rule becomes one function").

EBNF is just the **notation we write grammars in.** That's all it is. It is a notation for spelling out "here is exactly what a valid input looks like." Think of it as regular expressions' more powerful cousin — and if you've never used regex either, don't worry, we assume nothing.

---

## Part 1 — The two halves of a rule

Every grammar is a list of **rules**. Every rule has the same two-part shape:

```
name  :=  definition
```

- The **left side** is a *name* you are defining.
- The **`:=`** is read as **"is defined as"** (some books write it `=`, `::=`, or `→`; this doc uses `:=`).
- The **right side** is the *definition* — a recipe describing what that name is made of.

So this rule:

```
expr := NUMBER '+' NUMBER
```

is read, out loud, as:

> "An **expr** is defined as a NUMBER, followed by a `+`, followed by a NUMBER."

It describes exactly the strings `2 + 3`, `7 + 7`, `100 + 1`, and so on — *one number, a plus, one number.* Nothing else. Not `2 + 3 + 4` (that's three numbers), not `2 - 3` (no minus mentioned), not `2 +` (missing the second number). The rule is a precise gate: a string either fits the recipe or it doesn't.

That `:=` and the idea of "a name defined by a recipe" is 80% of reading grammars. Everything else is just the vocabulary you're allowed to use on the right side.

---

## Part 2 — The single most important distinction: TERMINAL vs NONTERMINAL

This is the one concept that, once it clicks, makes every grammar readable. Read this part twice.

Everything on the right side of a rule is one of exactly **two kinds of thing**:

### Terminals — the actual literal text (the atoms)

A **terminal** is a literal piece of the real input. It is "terminal" because it is a dead end — you cannot break it down into anything smaller within the grammar. It *is* the raw material. In the approach docs, terminals appear in two visual styles:

- **`UPPERCASE` names** stand for a *category* of literal token that has internal variety: `NUMBER` means "any literal number — `2`, `45`, `1000`…". We write it uppercase because there are many possible numbers but they're all the same *kind* of atom to the parser. (`NUMBER` is exactly the `TokNum` token the tokenizer produces.)
- **`'quoted'` characters** stand for one *exact* literal symbol: `'+'` means a literal plus sign, `'('` means a literal open-parenthesis. The quotes say "this exact character, literally."

Terminals are the things the **tokenizer** hands you (Stage 1 in the approach docs). When a grammar mentions `NUMBER` or `'+'`, it's pointing at a token that already exists in the token stream. There is nothing more to "figure out" about a terminal — you either see that token next, or you don't.

### Nonterminals — names of *other rules* (the structures)

A **nonterminal** is a `lowercase` name that refers to *another rule in the grammar* — possibly the very rule you're inside. It is "non-terminal" because it is *not* a dead end: to know what it means, you have to go look up its rule and expand it. It stands for a **structure**, not a literal.

In `primary := NUMBER | '(' expr ')'`, the word `expr` is a nonterminal — it means "go expand the `expr` rule right here." The words `NUMBER`, `'('`, `')'` are terminals — literal tokens.

### The mental test

When reading any symbol on the right side, ask one question:

> **"Is this a literal token the tokenizer gives me (TERMINAL), or is it the name of another rule I must go expand (NONTERMINAL)?"**

- `UPPERCASE` or `'quoted'` → **terminal**, a literal atom, stop here.
- `lowercase` → **nonterminal**, the name of a rule, go expand it.

That's the whole vocabulary distinction. Terminals are leaves; nonterminals are branches that lead to more rules. A grammar is "done expanding" when everything has bottomed out into terminals — literal tokens you can match directly against the input.

> **Why the case convention matters.** It lets you read a rule at a glance without cross-referencing. The instant you see `term` (lowercase) you know "there's a rule called `term` somewhere I can look up," and the instant you see `'*'` or `NUMBER` you know "this is just literal input, nothing to look up." The visual style *is* the terminal/nonterminal flag.

---

## Part 3 — The four operators on the right side

The right side of a rule is built from terminals and nonterminals glued together with exactly **four** combining operators. Master these four and you can read every line in both approach docs. We'll take them one at a time, each with a tiny example.

### 3.1 — Sequence: just write things next to each other

Putting items side by side (separated by spaces) means **"this, then this, then this — in order."**

```
factor '*' factor
```

reads as: "a `factor`, then a literal `'*'`, then a `factor`." Order is strict — the `'*'` must come *between* the two factors, not before, not after. Sequence is implicit: there's no symbol for it, you just write the items in the order they must appear. This is the comma-less "and then" of grammars.

### 3.2 — Choice: `|` means OR

The vertical bar `|` separates *alternatives*. Exactly one of them is chosen.

```
'+' | '-'
```

reads as: "either a literal `'+'` **or** a literal `'-'`." When the parser reaches this point, it looks at the next token and picks whichever alternative matches. The alternatives can be bigger than single tokens:

```
primary := NUMBER | '(' expr ')'
```

reads as: "a `primary` is **either** a `NUMBER`, **or** the sequence `'(' expr ')'`." Two ways to be a primary; the parser picks based on what it sees next (a number, or an open-paren).

> **Reading tip for precedence of `|` itself:** `|` has the *loosest* binding in the notation — it splits the whole right side into alternatives. So `A B | C D` means `(A B) | (C D)` — "either the sequence A-then-B, or the sequence C-then-D" — **not** `A (B|C) D`. When you want a choice *inside* a sequence, you must wrap it in parentheses (next section). This is exactly why `( '+' | '-' )` is parenthesized in the real rules.

### 3.3 — Grouping: `( ... )` bundles items into one unit

Round parentheses group several items so an operator can apply to the whole bundle. **These are notation parentheses — they are *not* literal input.** This is a crucial and genuinely confusing point, so pin it down now:

- `( ... )` — **plain round parens** = grouping in the *notation*. Invisible structure, like parentheses in math. Not matched against input.
- `'('` and `')'` — **quoted parens** = literal parenthesis *characters* in the actual input. Matched against input.

So in this line:

```
( '+' | '-' )
```

the **outer** `( )` are grouping (notation), and the **inner** `'+'` / `'-'` are literal symbols (input). It reads: "a group consisting of: either a literal plus or a literal minus." The grouping exists so the next operator (`*`, below) can repeat the *whole* choice, and so the `|` doesn't bleed into the surrounding sequence.

Whenever you see parentheses in a grammar, immediately classify them: **quoted = real character, unquoted = just grouping.** Getting these two confused is the #1 beginner stumble with this notation.

### 3.4 — Repetition and option: `( ... )*` and `( ... )?`

A grouped bundle can be followed by a postfix symbol that says "how many times":

- **`( ... )*`** — the star means **"zero or more"**: the bundle can appear any number of times, *including not at all.*
- **`( ... )?`** — the question mark means **"optional"**: the bundle appears *zero or one* time.

So:

```
( ('+' | '-') term )*
```

reads as: "**zero or more** repetitions of the bundle `(a plus-or-minus, then a term)`." That is: nothing at all, or `+ term`, or `+ term - term`, or `- term + term + term`, … any-length chain.

And:

```
( '-' factor )?
```

reads as: "**optionally**, a literal minus followed by a factor" — i.e. it may be there once, or be absent entirely.

These two are the workhorses for "a list of things" (`*`) and "a thing that may or may not be present" (`?`). 

> **The four operators, all together, in one table:**
>
> | Notation | Name | Reads as | Becomes (in code) |
> |---|---|---|---|
> | `A B` | sequence | "A, then B" | one statement after another |
> | `A \| B` | choice | "A or B" | `if` / `switch` on the next token |
> | `( A )*` | repetition | "zero or more A" | a `for` loop |
> | `( A )?` | option | "zero or one A" | a single `if` (no loop) |
>
> This table is *also* the "every rule becomes code" recipe from Approach 1, Part 4 — which is not a coincidence. The notation was designed so each operator maps to one obvious code construct. Reading the grammar and writing the parser are the same skill viewed from two sides.

---

## Part 4 — Reading a real rule, symbol by symbol

Now we take the actual workhorse rule from `parsing-approach1.md` and read it with zero gaps. Here it is:

```
expr := term ( ('+' | '-') term )*
```

Go left to right, classifying every single symbol:

| Symbol | Classification | Meaning |
|---|---|---|
| `expr` | name being defined (left of `:=`) | we are defining what an "expr" is |
| `:=` | "is defined as" | — |
| `term` | nonterminal (lowercase) | go expand the `term` rule |
| `(` | grouping paren (unquoted) | start a bundle |
| `(` | grouping paren (unquoted) | start an inner bundle |
| `'+'` | terminal (quoted) | a literal plus sign |
| `\|` | choice | or |
| `'-'` | terminal (quoted) | a literal minus sign |
| `)` | close inner bundle | the bundle `('+' \| '-')` = "a plus or a minus" |
| `term` | nonterminal | go expand `term` again |
| `)` | close outer bundle | the bundle is `(a plus-or-minus, then a term)` |
| `*` | repetition | zero or more of that outer bundle |

Stitched into one English sentence:

> "An **expr** is a `term`, followed by zero or more repetitions of (a plus-or-minus sign followed by another `term`)."

Which describes: a single term (`2`), or a term plus more (`2 + 3`), or a long chain (`2 + 3 - 4 + 5`). Exactly the shape of an additive expression. **Read it again and confirm you can produce that English sentence yourself from the symbols.** When you can do that for this line, you can do it for every line in both docs — they're all built from the same four operators and the same terminal/nonterminal distinction.

---

## Part 5 — The whole calculator grammar, read as a paragraph

Here is the complete layered grammar from Approach 1. We'll read all four rules as one flowing description so you see how nonterminals chain the rules together.

```
expr    := term   ( ('+' | '-') term   )*
term    := factor ( ('*' | '/') factor )*
factor  := '-' factor | primary
primary := NUMBER | '(' expr ')'
```

Read top to bottom:

1. **`expr`** — "an expression is a `term`, then zero or more (plus-or-minus, `term`)." *To know what a `term` is, drop to the next rule.*
2. **`term`** — "a term is a `factor`, then zero or more (times-or-divide, `factor`)." *Same shape as `expr`, but for `*`/`/`. To know what a `factor` is, drop down again.*
3. **`factor`** — "a factor is **either** a literal `'-'` followed by another `factor` (that's unary minus, like `-5`), **or** a `primary`." *Note the `factor` references itself — that's legal and means `--5` works. To know what a `primary` is, drop down once more.*
4. **`primary`** — "a primary is **either** a `NUMBER`, **or** a literal `'('`, then a whole `expr`, then a literal `')'`." *And there it is — `primary` references `expr`, the top rule, which is how parentheses let you nest a full expression inside.*

Notice two things you can now *see* directly in the notation:

- **The rules form a chain by nonterminal references:** `expr` mentions `term`, `term` mentions `factor`, `factor` mentions `primary`, `primary` mentions `expr` again. That chain — top rule down to bottom rule and back up — is the entire "descent" of recursive descent. The notation *is* the call graph.
- **A rule mentioning itself (`factor := '-' factor | ...`, `primary := ... '(' expr ')'`) is recursion**, and it is completely normal. It's how the grammar describes unbounded structures (any depth of nesting, any number of unary minuses) with a finite number of rules. When you read a self-reference, don't panic — just read it as "and here, another one of these can appear."

> You do **not** need to understand *why* the rules are stacked in this particular order (that's the precedence story, and it's Approach 1's job to teach it). For *reading* the notation, all you need is: each line is a name defined by a recipe, the recipe is terminals + nonterminals glued by the four operators, and lowercase names send you to other lines. That's a complete reading.

---

## Part 6 — The two traps that catch every beginner

Almost every misreading of this notation is one of these two. Name them now and you'll never fall in.

### Trap 1 — Confusing grouping parens `( )` with literal parens `'(' ')'`

In the single rule `primary := NUMBER | '(' expr ')'` there are no grouping parens, only literal ones. In `expr := term ( ('+' | '-') term )*` there are no literal parens, only grouping ones. And a rule *could* contain both. The only way to tell them apart is the **quotes**:

- **Quoted** `'('` `')'` → a real parenthesis the user typed, a token to match.
- **Unquoted** `(` `)` → notation scaffolding, grouping for the operators `*`/`?`/`|`. Never appears in the input.

Before reading any parenthesis, check for quotes. This one habit removes the single biggest source of confusion.

### Trap 2 — Thinking `*` and `?` are literal characters

In this notation, a trailing `*` means "repeat" and a trailing `?` means "optional." They are **operators**, not input. This is awkward precisely because in a *calculator* `*` also happens to be the multiply symbol! But notice: when multiply is meant as literal input, it's written **quoted** — `'*'`. When repetition is meant, it's a **bare** `*` sitting right after a `)`. So:

- `'*'` → the literal multiplication character (a terminal).
- `( ... )*` → the bare `*` after a group → "zero or more" (an operator).

Same rule as Trap 1, really: **quotes mean literal, no quotes mean notation.** Quoting is the universal "this is real input" marker in this dialect. Internalize *quoted = literal, unquoted = notation* and both traps vanish at once.

---

## Part 7 — A few EBNF symbols you'll meet *elsewhere* (so other docs don't trip you)

The approach docs use the minimal dialect above (`:=`, `|`, `( )`, `*`, `?`, UPPERCASE terminals, `'quoted'` literals, lowercase rules). That's all you need for them. But if you go read other parsing material on the web, you'll see close cousins. Here's a quick decoder so they don't throw you — you do **not** need to memorize this, just recognize it:

| You might see | It means | Our docs' equivalent |
|---|---|---|
| `=` or `::=` or `→` | "is defined as" | `:=` |
| `{ A }` | zero or more A (curly braces) | `( A )*` |
| `[ A ]` | optional A (square brackets) | `( A )?` |
| `A+` | **one** or more A | `A ( A )*` |
| `"plus"` (double quotes) | a literal string | `'+'` (single quotes) |
| `(* comment *)` | a comment in the grammar | — |
| `;` at line end | end-of-rule marker | (we just use a newline) |

The big ones to watch: in *standard* EBNF, **`{ }` is repetition** and **`[ ]` is option** — some authors prefer those braces/brackets over the `( )*` / `( )?` style. And **`+` means "one or more"** (at least once), versus **`*`** which allows zero. Our docs happen to only need `*` and `?`, so they don't use `+` or the brace forms — but now if you see `{ digit }` somewhere else, you'll correctly read it as "zero or more digits," and `digit+` as "one or more digits."

Everything else — terminals vs nonterminals, sequence, choice, the idea of a rule — is identical across every EBNF dialect. The concepts transfer 100%; only the punctuation skins change.

---

## Part 8 — Self-check (answer from memory before returning to the approach docs)

If you can answer all of these without scrolling up, the approach docs' grammar lines will read like plain English. Try each one:

1. In a rule `name := definition`, what does `:=` mean, and what is the left side versus the right side?
2. What is the difference between a **terminal** and a **nonterminal**? Give the one-question test you apply to any symbol to tell which it is.
3. Why is `NUMBER` written uppercase but `expr` lowercase? What does each style tell you to do when you read it?
4. What do each of the four operators mean: `A B`, `A | B`, `( A )*`, `( A )?` — and what code construct does each become?
5. In `( ('+' | '-') term )*`, identify every parenthesis as either *grouping* or *literal*, and say how you can tell.
6. What's the difference between `'*'` and a bare `*` after a group? State the universal rule that resolves it.
7. Read this aloud in plain English: `term := factor ( ('*' | '/') factor )*`.
8. Read this aloud in plain English: `primary := NUMBER | '(' expr ')'` — and explain why a rule is allowed to mention `expr`, which is "above" it.
9. Why does `A B | C D` mean `(A B) | (C D)` and not `A (B | C) D`? What does that tell you about how loosely `|` binds?
10. If you saw `digit+` and `{ letter }` in some *other* document, what would each mean in our docs' notation?

When those answers come without flipping back, go (re)read `parsing-approach1.md` and `parsing-approach2.md`. Every grammar block in them is now just terminals and nonterminals glued by four operators — and you can read all of it.
