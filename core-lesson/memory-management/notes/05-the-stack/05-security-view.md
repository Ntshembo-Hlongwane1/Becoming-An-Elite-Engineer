# 5.5 — The Security Researcher's View: the Stack Smash

The oldest reliable exploitation technique lives here, and its defenses shaped the modern stack.
Three hats on one line of code from `[MEM §5.1]`:

```cpp
char buffer[10];
strcpy(buffer, "This string is too long");   // writes 24 bytes into a 10-byte local
```

## The classic chain (§5.2 geometry)
`buffer` is a local; just above it in the frame sit the saved frame pointer and the **return
address** (§5.2 §3). `strcpy` writes upward with no bounds check, so a long enough source overruns
`buffer` → saved RBP → return address. Overwrite the return address and when the function executes
`ret`, the CPU jumps wherever you wrote. That's **control-flow hijack**.

## Offense — discovery
- **Source/RE:** any fixed-size stack buffer filled from input without a length bound — `strcpy`,
  `gets`, `sprintf`, `memcpy(buf, src, attacker_len)`, a loop with no cap. The Lesson-1 integer bug
  often supplies the bad length.
- **Dynamic:** fuzz; a crash with the instruction pointer at an attacker-ish value (e.g. `0x4141…` =
  "AAAA") is the signature. ASan reports `stack-buffer-overflow` at the first byte past the buffer.

## Offense — value, honest ceiling
- **Ceiling:** overwrite the return address → **code execution**. Historically "jump to shellcode in
  the buffer." NX (Lesson 3.4) killed that, so the modern form is **ROP** — chain the addresses of
  existing code snippets ("gadgets") you write onto the stack. `[HPC §4.2]` references building a ROP
  chain; this course studies the *mechanism* defensively, not a weaponized chain.
- **Floor / obstacles:** a **stack canary** (below) usually turns the overwrite into a clean abort
  (DoS, not RCE) unless you can leak/avoid the canary; **ASLR** means you must also leak an address to
  know where gadgets are. So a lone stack overflow today is often "crash + needs a second bug," and
  saying precisely what's needed to escalate is the researcher's judgment.

## Defense — the three that shaped the stack
- **Stack canary / stack protector** (`-fstack-protector-strong`, default on many distros): the
  compiler puts a random "canary" value between the locals and the return address, and checks it
  before `ret`. An overflow that reaches the return address first corrupts the canary. **(measured)**:
  `./sc <long input>` → `*** stack smashing detected ***: terminated`. Cost: a few instructions per
  function; benefit: most stack smashes become aborts.
- **NX / W^X** (Lesson 3.4): the stack is non-executable, so injected shellcode can't run. Forces ROP.
- **ASLR / PIE** (Lesson 3.4): randomizes where code and stack are, so the attacker doesn't know what
  address to write without an info leak.

Plus: bounds-safe APIs (`std::string`, `std::span`, `snprintf` with sizes), the Lesson-1 length
discipline, and ASan in test/CI. "Impossible by construction" = don't write unbounded into a fixed
local; carry lengths (Lesson 2's `ByteCursor`).

## Through-line to the capstone
Your detector focuses on **heap** OOB/UAF (shadow + guard pages), but the *reporting* side is pure
Lesson 5: it unwinds the stack (§5.3) to show where the bad allocation/access happened. And when you
move to RE/stripped binaries (capstone extension), reading stack frames and the calling convention by
hand is the daily skill.

## My summary
