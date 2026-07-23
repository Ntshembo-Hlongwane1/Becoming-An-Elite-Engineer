# Bit masking

## Binary Representatioin

The unsigned integer value stored in `x` is:

$$
X = \sum_{k=0}^{w-1} X_k \cdot 2^k
$$

For example, the 8-bit word 0b10010110 represents the unsigned value 150 = 2 + 4 + 16 + 128

---
The **signed Integer (Two's Complement)** value store in x is 

$$
X = -X_{w-1} \cdot 2^{w-1} + \sum_{k=0}^{w-2} X_k \cdot 2^k
$$
---
**sign bit**
$$

-X_{w-1}
$$

For example, the 8-bit word 0b10010110 represents the unsigned value -106 = 2 + 4 + 16 - 128

---

## Complementary Relatioinship
Since we have x + ~x = -1 it follows that -x = ~x + 1

> ```
> E.g x  = 0b011011000
>     ~x = 0b100100111
>     -x = 0b100101000
> ```

### Binary to Hexadecimal Lookup Table

| Hexadecimal | Binary | Decimal |
|:-----------:|:------:|:-------:|
|     0       |  0000  |    0    |
|     1       |  0001  |    1    |
|     2       |  0010  |    2    |
|     3       |  0011  |    3    |
|     4       |  0100  |    4    |
|     5       |  0101  |    5    |
|     6       |  0110  |    6    |
|     7       |  0111  |    7    |
|     8       |  1000  |    8    |
|     9       |  1001  |    9    |
|     A       |  1010  |   10    |
|     B       |  1011  |   11    |
|     C       |  1100  |   12    |
|     D       |  1101  |   13    |
|     E       |  1110  |   14    |
|     F       |  1111  |   15    |

---

### How to Convert Larger Numbers

Group the binary digits into sets of **4** (starting from the right), then replace each group with its hex equivalent.

**Example:**  
`0b110101101011` → group as `1101 0110 1011` → `D` `6` `B` → **`0xD6B`**

Conversely, expand each hex digit into 4 bits:  
`0x3F7` → `3`=`0011`, `F`=`1111`, `7`=`0111` → **`0b001111110111`**

## C Bitwise Operators Reference Table

| Operator | Name              | Example (`a = 0b1100` (12), `b = 0b1010` (10)) | Result | Description |
| :------: | :---------------- | :--------------------------------------------: | :----: | :---------- |
| **`&`**  | Bitwise AND       |           `a & b` → `0b1000` (8)               | 1000   | Sets each bit to **1** if *both* corresponding bits are **1**. Used for **bit masking** (clearing specific bits). |
| **`\|`** | Bitwise OR        |           `a \| b` → `0b1110` (14)             | 1110   | Sets each bit to **1** if *at least one* corresponding bit is **1**. Used for **setting** specific bits. |
| **`^`**  | Bitwise XOR       |           `a ^ b` → `0b0110` (6)               | 0110   | Sets each bit to **1** if *exactly one* corresponding bit is **1** (i.e., they differ). Used for **toggling** bits. |
| **`~`**  | Bitwise NOT       |           `~a` → `0b...0011` (-13)             | 0011   | Inverts **all** bits (0→1, 1→0). Result depends on integer width (e.g., for 8-bit: `~0b1100 = 0b11110011`). |
| **`<<`** | Left Shift        |           `a << 2` → `0b110000` (48)           | 110000 | Shifts bits to the **left**, filling rightmost bits with **0**. Equivalent to **multiplying by 2ⁿ** (for unsigned integers). |
| **`>>`** | Right Shift       |           `a >> 2` → `0b0011` (3)              | 0011   | Shifts bits to the **right**. For **unsigned**, fills left with **0**. For **signed**, behaviour is implementation-defined (usually **arithmetic** shift, filling with the sign bit). Equivalent to **dividing by 2ⁿ** (floor for positive integers). |

---

### Compound Assignment Operators

C also provides shorthand forms that combine the operation with assignment:

| Operator | Equivalent to |
| :------: | :------------ |
| `x &= y` | `x = x & y`   |
| `x \|= y`| `x = x \| y`  |
| `x ^= y` | `x = x ^ y`   |
| `x <<= n`| `x = x << n`  |
| `x >>= n`| `x = x >> n`  |

---

## Common problems

### 1. Set the kth Bit

**Problem**: Set kth bit in a word x to 1.

**Idea:** Shift and OR

```
Y = X | (1 << k)
```


**Example**: `X` is a random 8‑bit word, `k = 7` (bit 7 is the MSB).

| Operation                         | Binary Representation (target bit **flagged**) |
| :-------------------------------- | :--------------------------------------------- |
| `X` (initial random word)         | `0b**0**1010101`  (bit 7 = 0, to be set)       |
| `1 << 7` (mask)                   | `0b**1**0000000`  (the 1 is at position 7)     |
| `Y = X \| (1 << 7)` (after setting bit 7) | `0b**1**1010101`  (bit 7 is now 1)             |


### 2. Clear the kth Bit

**Problem:** Clear the kth bit in a word x.

**Idea:** Shift, Complement, and AND

```
y = x & ~ (1 << k);
```


**Example**: `X` is a random 8‑bit word, `k = 7` (bit 7 is the MSB).

| Operation                       | Binary Representation (target bit **flagged**) |
| :------------------------------ | :--------------------------------------------- |
| `X`        | `0b**1**0101011`  (bit 7 = 1, to be cleared)   |
| `1 << 7`          | `0b**1**0000000`  (1 at position 7)            |
| `~(1 << 7)`  | `0b**0**1111111`  (bit 7 = 0, all others 1)    |
| `Y = X & ~(1 << 7)`      | `0b**0**0101011`  (bit 7 is now 0)             |

### 3. Toggle the kth Bit

**Problem:** Flip the kth bit in a word x

**Idea:** Shift and XOR

```
y = x ^ (1 << k);
```

**Example**: `X` is a random 8‑bit word, `k = 7` (bit 7 is the MSB).

| Operation                       | Binary Representation (target bit **flagged**) |
| :------------------------------ | :--------------------------------------------- |
| `X` (initial random word)       | `0b**0**1010101`  (bit 7 = 0, to be toggled)   |
| `1 << 7` (shifted mask)         | `0b**1**0000000`  (1 at position 7)            |
| `Y = X ^ (1 << 7)` (final)      | `0b**1**1010101`  (bit 7 is now flipped to 1)  |

### 4. Extract a Bit Field

**Problem:** Extract a bit field from a word x 

**Idea:** Mask and shift

```
(x & mask) >> shift;
```


**Example**: `X` is a random 8‑bit word. Extract bits **4-6** (3‑bit field) from `X`.  
`mask = 0b01110000` (bits 4, 5, 6 set to 1)  
`shift = 4` (to move the field to the LSB position)

| Operation                         | Binary Representation (target field **flagged**) |
| :-------------------------------- | :----------------------------------------------- |
| `X` (initial random word)         | `0b**101**01011`  (target field = bits 4-6)      |
| `mask` (bit field mask)           | `0b**111**00000`  (1s at positions 4, 5, 6)      |
| `X & mask` (after masking)        | `0b**101**00000`  (isolated field, still shifted)|
| `Y = (X & mask) >> 4` (final)     | `0b00000**101**`  (extracted field at LSB)       |

### 5. Set a Bit Field

**Problem:** Set a bit field in a word x to a value y 

**Idea:** Invert mask to clear, and OR the shifted value

x = (x & ~mask) | (y << shift);


**Example**: `x = 0b10101011` (random 8‑bit).  
Set bits **4‑6** (a 3‑bit field) to `y = 0b101`.  
`mask = 0b01110000` (bits 4,5,6 = 1), `shift = 4`.

| Operation                         | Binary Representation (target field **flagged**) |
| :-------------------------------- | :----------------------------------------------- |
| `x` (initial word)                | `0b1**010**1011`  (field bits 4‑6 = `010`)       |
| `mask` (field selector)           | `0b0**111**0000`  (1s at positions 4,5,6)        |
| `~mask` (inverted mask)           | `0b1**000**1111`  (0s at positions 4,5,6)        |
| `x & ~mask` (cleared field)       | `0b1**000**1011`  (field bits now `000`)         |
| `(x & ~mask) \| (y << 4)` (final)| `0b1**101**1011`  (field bits set to `101`)      |


### 6. Ordinary Swap
**Problem:** Swap x and y without a temporary

**Idea**: Use XOR properties: `(a ^ b) ^ b = a` and `(a ^ b) ^ a = b`.


**Example**: `x = 0b10101010`, `y = 0b01010101`.

| Operation                         | Binary Representation (changed variable(s) **bolded**) |
| :-------------------------------- | :------------------------------------------------------ |
| Initial state                     | `**x** = 0b10101010`, `**y** = 0b01010101`              |
| `x = x ^ y`                       | `**x** = 0b11111111`, `y = 0b01010101`                  |
| `y = x ^ y`                       | `x = 0b11111111`, `**y** = 0b10101010`                  |
| `x = x ^ y`                       | `**x** = 0b01010101`, `y = 0b10101010`                  |


### Why the XOR Swap Works

The XOR swap works because of three fundamental properties of the XOR (`^`) operation:

| Property | Rule | Example |
| :--- | :--- | :--- |
| **Identity** | `a ^ 0 = a` | `1 ^ 0 = 1`, `0 ^ 0 = 0` |
| **Self-Inverse** | `a ^ a = 0` | `1 ^ 1 = 0`, `0 ^ 0 = 0` |
| **Commutative & Associative** | Order/grouping doesn't matter | `(a ^ b) ^ c = a ^ (b ^ c)` |

#### XOR Truth Table

| Input A | Input B | Output (A ^ B) |
| :-----: | :-----: | :------------: |
|    0    |    0    |       0        |
|    0    |    1    |       1        |
|    1    |    0    |       1        |
|    1    |    1    |       0        |

**Observation**: XOR outputs `1` only when the inputs **differ**. It outputs `0` when they are the **same**.

---

#### Step‑by‑Step Algebraic Proof

Let the original values be `a` and `b`.

1. **`x = x ^ y`**  
   `x` now holds `t = a ^ b`.

2. **`y = x ^ y`**  
   Substitute `x = t`:  
   `y = t ^ b = (a ^ b) ^ b`  
   By associativity: `= a ^ (b ^ b)`  
   By self‑inverse: `= a ^ 0`  
   By identity: `= a`  
   ✅ `y` now holds the **original** `a`.

3. **`x = x ^ y`**  
   Now `x = t` and `y = a`:  
   `x = t ^ a = (a ^ b) ^ a`  
   By commutativity: `= (b ^ a) ^ a = b ^ (a ^ a)`  
   By self‑inverse: `= b ^ 0`  
   By identity: `= b`  
   ✅ `x` now holds the **original** `b`.

The values have been successfully swapped without using any temporary storage!

### 7. Minimum of Two Intgers with Branch

**Problem:** Find the min r of the two integers x and y

```cpp
if (x < y)
    r = x
else
    r = y

    OR
r = (x < y) ? x : y;
```

**Perfomance:** A mispredicted branch empties the processor pipeline

**Caveat:** The compiler is usually smart enough to optimise away the unpredictable branch, but maybe not.

### 8. Minimum of Two Intgers without Branch

**Problem:** Find the min r of two integers x and y without a branch

```cpp
r = y ^ ((x ^ y) & -(x < y));
```

**Why it works:**
- The C language represents the Booleans **TRUE** and **FALSE** with the integers 1 and 0, respectively.

- If x < y, then -(x < y) => -1, which is all 1's in two's complement representation. Therefore, we have ```y ^ (x ^ y) => x```

- If x >= y, then -(x < y) => 0. Therefore, we have y ^ 0 => y

### 9. Modular Addtion

**Problem:** Compute (x + y) mod n, assuming that 0 <= x < n and 0 <= y < n;

**Option 1:**
```
r = (x + y) % n;
```
> Division is expensive, unless by a power of 2

**Option 2:**
```
z = x + y;
r = (z < n) ? z : z - n
```
> Unpredictable branch is expensive

**Option 3: (Best)**
```
z = x + y;
r = z - (n & -(z >= n));
```

### 10. Least-Significant 1

**Problem:** Compute the mask of the least-significant 1 in word x 

```
r = y & (-x);
```


**Example**: `x = 0b10101000` (8‑bit word).  
The least‑significant 1 is at position 3 (the 4th bit from the right).

| Operation       | Binary Representation (LSB **flagged**) | Decimal |
| :-------------- | :-------------------------------------- | :------ |
| `x`             | `0b10101**1**00`  (LSB at position 3)   | 168     |
| `-x`            | `0b01010**1**00`  (two's complement)    | -168    |
| `r = x & (-x)`  | `0b00000**1**00`  (isolated LSB)        | 4       |

**Why it works?**

The binary representation of -x is (~x) + 1

**Question:** How do you find the index of the bit?
> In other words I want to find the log base 2 of the power 2


### 11. Log Base 2 of the power of 2

**Problem:** Compute lg x where x is a power of 2

```cpp
const unit64_t deBruijn = 0x022fff63cc95386d;
const int convert[64] = {
     0,  1,  2,  7,  3, 13,  8, 27,
     4, 33, 14, 36,  9, 49, 28, 19,
     5, 25, 34, 53, 15, 55, 37, 58,
    10, 43, 50, 46, 29, 60, 20, 62,
     6, 12, 26, 32, 35, 48, 18, 24,
    52, 54, 57, 42, 45, 59, 61, 11,
    31, 47, 17, 23, 51, 56, 41, 44,
    60, 16, 22, 50, 40, 21, 39, 38
};
r = convert[(x * deBruijn) >> 58]
```

**Why it works**
- A deBruijn sequence s of length 2^k is cyclic 0-1 sequence such that each of the 2^k 0-1 string of length k occurs exactly once as a substring of s

## 12. Comparing a Digit to Values Present in a Bitmask

**Problem:**  
Given an integer `usedMask` where each bit `i` (0–9) is set if digit `i` appears somewhere in a number, and a current `digit`, we want to check if the `digit` is **less than**, **greater than**, or **equal to** any potential value that is *actually present* in the mask.

**Idea:**  
Iterate over all possible digits (0–9). For each `i`, test whether that digit is present in the mask using a **bitwise AND mask** (same principle as extracting a bit field). If present, compare it with the current `digit`.

**Bitwise presence check:**  
`(usedMask & (1 << i))` → non‑zero means digit `i` is in the mask.

This is exactly the same masking operation used in field extraction, but applied to a **single‑bit mask**:

| Operation | Field Extraction | Presence Check |
|-----------|-------------------|----------------|
| Mask type | Multi‑bit (e.g., `0b11100000`) | Single‑bit (`1 << i`) |
| Purpose | Retrieve the numeric value of a bit field | Test whether that specific bit is set |
| Shift required | Yes (`>> shift`) to align value | No – boolean result is enough |
