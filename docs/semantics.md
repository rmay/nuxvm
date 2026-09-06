# NUX Operational Semantics

Normative small-step semantics for the NUX virtual machine: 55 opcodes,
`0x00`–`0x36`.

**Status.** This document is normative. Where it and `src/vm.c` disagree,
one of them is a bug; where it and `docs/opcodes.md` disagree, this document
wins. `docs/opcodes.md` remains the readable per-opcode reference — this is
the one that fixes the edge cases.

**Why it exists.** Every operation below is *total*: for every state and
every instruction, exactly one rule applies, and it either produces a
successor state or a named trap. Nothing is left to the host C compiler.
That totality is what makes properties like "the machine never writes
outside its memory" statable and provable rather than merely believed.

---

## 1. Domains and notation

| Symbol | Meaning |
|---|---|
| `Int32` | integers in `[-2^31, 2^31)`, two's complement |
| `Word32` | integers in `[0, 2^32)` |
| `Byte` | integers in `[0, 256)` |
| `wrap(n)` | the unique `v ∈ Int32` with `v ≡ n (mod 2^32)` |
| `u(a)` | `a mod 2^32`, the `Word32` reading of `a ∈ Int32` |
| `a ⊞ b` | `wrap(a + b)` |
| `a ⊟ b` | `wrap(a - b)` |
| `a ⊠ b` | `wrap(a · b)` |

Sequences are written left-to-right with the **top of a stack on the
right**: `D = d₀ … d_{n-1}` has top `d_{n-1}`. `|D|` is length, `ε` is the
empty sequence, `·` is append.

A stack effect `… a b → … c` means the top two elements are replaced by
one; elements below the shown prefix are untouched.

## 2. Machine state

A state `σ` is a tuple

```
σ = ⟨ pc, D, R, K, L, fp, M, run, halt, trap, last ⟩
```

| Component | Domain | Bound | C counterpart |
|---|---|---|---|
| `pc` | `Word32` | — | `pc` |
| `D` | sequence of `Int32` | `\|D\| ≤ 8192` | `stack`, `stack_ptr` |
| `R` | sequence of `Word32` | `\|R\| ≤ 1024` | `return_stack`, `return_stack_ptr` |
| `K` | sequence of `Int32` | `\|K\| ≤ 1024` | `loop_stack`, `loop_stack_ptr` |
| `L` | `[0, 4096) → Int32` | fixed size | `locals` |
| `fp` | `Int32`, `-1 ≤ fp < 4096` | — | `fp` |
| `M` | `[0, msize) → Byte` | — | `memory` |
| `run` | boolean | — | `running` |
| `halt` | boolean | — | `halted` |
| `trap` | `Trap` (§8) | — | `trap` |
| `last` | `Byte` | — | `last_opcode` |

`D`, `R` and `K` are three independent stacks. `L` is **not** part of `D`:
locals live in their own array (this is the point on which
`docs/opcodes.md` was historically wrong).

There are no registers and no condition flags.

### Configuration

Three values are fixed when the machine is created and never change. They
are not part of `σ`:

| Symbol | Meaning | C counterpart |
|---|---|---|
| `ib` | first address of the program image | `image_base` |
| `ie` | one past the last address of the image | `image_end` |
| `msize` | size of guest memory in bytes | `memory_size` |

Write `inImage(x) ≜ ib ≤ x < ie`.

### Representation invariant

Every reachable state satisfies

```
INV(σ) ≜  |D| ≤ 8192  ∧  |R| ≤ 1024  ∧  |K| ≤ 1024
        ∧  -1 ≤ fp < 4096
        ∧  (run ⇒ ¬halt ∧ trap = None)
        ∧  ib ≤ ie ≤ msize
```

`INV` holds initially (§11) and is preserved by every rule below. This is
proof obligation 5 of §12.

## 3. The oracle

NUX is pure except at three points. Rather than call them "side effects",
the semantics threads an **oracle** `ω`, so a step is a *function*:

```
step(σ, ω)  =  (σ', η)
```

where `η` is the finite sequence of external interactions the step
performed. The three interaction forms are:

| Interaction | Raised by | Oracle answers |
|---|---|---|
| `DevRead(addr)` | `LOAD` / `LOADI` in the device window | `Ok(v)` or `Fail` |
| `DevWrite(addr, v)` | `STORE` / `STOREI` in the device window | — (no answer) |
| `Out(value, format)` | `OUT` | — (no answer) |

**Every other opcode is pure**: 52 of the 55 raise no interaction at all,
and the four memory opcodes raise one only inside the 4 KB device window.

Given the same `σ` and the same oracle answers, `step` is deterministic
(proof obligation 6 of §12).

### The device window

```
devWindow(x)  ≜  0x010000 ≤ x < 0x011000
```

This is the SCI trap and the machine's *entire* memory-mapped surface. It
is 4 KB out of a 16 MB address space. The framebuffer is **not** in it:
guests draw by writing `/dev/draw` through the SCI trap, not by MMIO.

### Host aliasing (an honest caveat)

Guest memory is not private. The host reads the guest's bytes directly at
`0x100000` to present the framebuffer (`src/system.c`,
`sys->screen_pixels = &mem[0x100000]`). So a theorem of the form "only the
guest observes `M`" is **false** for the band
`[0x100000, 0x100000 + 1280·1024·4)`. Everything in this document still
holds; the caveat matters only when reasoning about confidentiality, which
NUX does not claim.

## 4. Memory access

All guest memory access is 32-bit, big-endian, and 4-byte aligned. There
are no byte or halfword accesses in the ISA; guest languages synthesise
them from aligned word access plus shifting and masking.

```
inRange(x)  ≜  x < msize  ∧  msize - x ≥ 4
```

`inRange` is computed in `Word32` without overflow, so an address near
`0xFFFFFFFF` cannot wrap past the top of memory.

### Load — `read(σ, x)`

```
1.  x ∧ 3 ≠ 0                      ⇒  Trap(UnalignedRead)
2.  devWindow(x) ∧ no bus attached ⇒  Trap(NoBus)
3.  devWindow(x)                   ⇒  raise DevRead(x);
                                      Ok(v) ⇒ v
                                      Fail  ⇒ Trap(DeviceReadFailed)
4.  ¬inRange(x)                    ⇒  Trap(ReadOutOfBounds)
5.  otherwise                      ⇒  (M[x]≪24) ∨ (M[x+1]≪16)
                                      ∨ (M[x+2]≪8) ∨ M[x+3]   as Int32
```

Rules are tried **in this order**, and the order is observable: an
unaligned device address traps as `UnalignedRead`, and a device address is
resolved *before* the range check, so the device window need not lie
inside `[0, msize)`.

Reading the program image is legal — `T"` string literals are compiled
into the image and read back at run time.

### Store — `write(σ, x, v)`

```
1.  x ∧ 3 ≠ 0                          ⇒  Trap(UnalignedWrite)
2.  ¬inRange(x)                        ⇒  Trap(WriteOutOfBounds)
3.  x < ie  ∧  x + 4 > ib              ⇒  Trap(WriteIntoImage)
4.  otherwise                          ⇒  M' = M with the four big-endian
                                          bytes of v stored at x;
                                          if devWindow(x) also raise
                                          DevWrite(x, v)
```

Rule 3 is the write half of W^X: any word overlapping `[ib, ie)` — even
partially — is refused. Note rule 4: a device store writes RAM **and**
notifies the bus. It is both a memory effect and an external one.

## 5. Instruction fetch

```
fetch(σ):
  ¬inImage(pc)   ⇒  Trap(ExecOutsideImage)
  pc ≥ msize     ⇒  Trap(PcOutOfBounds)
  otherwise      ⇒  op = M[pc],  pc' = pc + 1,  last' = op
```

Execution is confined to the image. This is the execute half of W^X, and
it is what stops a runaway machine: memory past the image is zero, and
`0x00` decodes as `PUSH`, so without this check a fetch off the end would
loop forever pushing zeros.

## 6. Immediate operands

Exactly seven opcodes carry a 4-byte big-endian immediate and are 5 bytes
long: `PUSH`, `JMP`, `JZ`, `JNZ`, `CALL`, `LOAD`, `STORE`. The other 48
are one byte.

```
imm(σ):
  ¬inImage(pc) ∨ ie - pc < 4  ⇒  Trap(TruncatedImmediate)
  otherwise                   ⇒  v = (M[pc]≪24) ∨ (M[pc+1]≪16)
                                    ∨ (M[pc+2]≪8) ∨ M[pc+3]
                                  pc' = pc + 4
```

The immediate must lie wholly **inside the image**, not merely inside
memory. An instruction whose operand is truncated by the end of the image
traps rather than reading whatever follows.

## 7. Guards

Two guards appear throughout. They are checked *before* any state change:

```
need(n)  ≜  |D| ≥ n        else Trap(StackUnderflow)
room(n)  ≜  |D| ≤ 8192 - n else Trap(StackOverflow)
```

### Fault atomicity

**A trapping instruction commits nothing.** If any guard or range check
fails, `σ'` differs from `σ` only in `run`, `trap`, and the parts of `pc`
and `last` already consumed by fetch and immediate decoding.

This is a deliberate property, not an accident, and it is what makes the
rules below readable as a relation: a step either happens completely or
does not happen. (Before this was specified, `SWAP` on a one-element stack
consumed that element on its way to trapping.)

## 8. Traps

A trap sets `run = false`, leaves `halt = false`, and records its cause in
`trap`. The first trap wins: a stopped machine never re-traps, so `trap`
always names the original cause. There is no trap handler, no fault
vector, and no way for a guest to catch a trap — the machine simply stops.

| Trap | Raised when |
|---|---|
| `ExecOutsideImage` | `pc ∉ [ib, ie)` at fetch |
| `JumpOutsideImage` | a jump, call or return target `∉ [ib, ie)` |
| `PcOutOfBounds` | `pc ≥ msize` at fetch |
| `TruncatedImmediate` | fewer than 4 immediate bytes remain in the image |
| `UnknownOpcode` | the byte is not in `0x00`–`0x36` |
| `StackUnderflow` | `need(n)` failed |
| `StackOverflow` | `room(n)` failed |
| `ReturnStackUnderflow` / `ReturnStackOverflow` | `R` empty / full |
| `LoopStackUnderflow` / `LoopStackOverflow` | `K` too shallow / full |
| `PickRange` / `RollRange` | index outside the reachable stack |
| `FrameRange` | `FRAME`/`UNFRAME` index outside `L` |
| `LocalRange` | `LOCALGET`/`LOCALSET` index outside `L` |
| `DivideByZero` | divisor is `0` in `DIV`, `MOD` or `DIVMOD` |
| `UnalignedRead` / `UnalignedWrite` | address not a multiple of 4 |
| `ReadOutOfBounds` / `WriteOutOfBounds` | `¬inRange(addr)` |
| `WriteIntoImage` | a store overlaps `[ib, ie)` |
| `DeviceReadFailed` | the oracle answered `Fail` |
| `NoBus` | a device read with no bus attached |

`None` is the absence of a trap. The enumeration is `NuxTrap` in
`include/vm.h`; `nux_trap_name` maps each to the text above.

`PcOutOfBounds` is **unreachable by construction** and is retained only as a
defensive check. A machine always has `ie ≤ msize`, and fetch has already
established `ib ≤ pc < ie`; therefore `pc < msize` always holds by the time
the second test runs. Nothing in the ISA can raise it.

## 9. The rules

Throughout, `a` is the value below the top and `b` is the top, so a binary
operator reads `a ⊕ b`. Every rule below implicitly runs after `fetch`.

### Stack manipulation `0x00`–`0x07`

| Op | Guards | Effect |
|---|---|---|
| `0x00 PUSH` | `room(1)`, then `imm` | `D → D·v` |
| `0x01 POP` | `need(1)` | `… a → …` |
| `0x02 DUP` | `need(1)`, `room(1)` | `… a → … a a` |
| `0x03 SWAP` | `need(2)` | `… a b → … b a` |
| `0x04 OVER` | `need(2)`, `room(1)` | `… a b → … a b a` |
| `0x05 ROT` | `need(3)` | `… a b c → … b c a` |
| `0x06 PICK` | `need(1)` | pops `n`; then with `d = \|D\|`, requires `0 ≤ n < d` else `Trap(PickRange)`; pushes `D[d-1-n]` |
| `0x07 ROLL` | `need(1)` | pops `n`; then with `d = \|D\|`, requires `0 ≤ n < d` else `Trap(RollRange)`; moves `D[d-1-n]` to the top, shifting the rest down. `ROLL 0` is a no-op that still consumes `n` |

For `PICK` and `ROLL`, `d` is the depth **after** removing `n`. Both are
net-zero on stack depth, so neither needs `room`.

### Arithmetic `0x08`–`0x13`

`DIV`, `MOD` and `DIVMOD` trap with `DivideByZero` when `b = 0`.
Otherwise division truncates toward zero and the remainder takes the sign
of the dividend.

| Op | Effect |
|---|---|
| `0x08 ADD` | `… a b → … a ⊞ b` |
| `0x09 SUB` | `… a b → … a ⊟ b` |
| `0x0A MUL` | `… a b → … a ⊠ b` |
| `0x0B DIV` | `… a b → … quo(a,b)` |
| `0x0C MOD` | `… a b → … rem(a,b)` |
| `0x0D INC` | `… a → … a ⊞ 1` |
| `0x0E DEC` | `… a → … a ⊟ 1` |
| `0x0F NEG` | `… a → … wrap(-a)` |
| `0x10 ABS` | `… a → … (a < 0 ? wrap(-a) : a)` |
| `0x11 DIVMOD` | `… a b → … quo(a,b) rem(a,b)` (remainder on top) |
| `0x12 MIN` | `… a b → … min(a,b)` |
| `0x13 MAX` | `… a b → … max(a,b)` |

where

```
quo(a, b)  =  INT32_MIN            if a = INT32_MIN ∧ b = -1
              trunc(a / b)         otherwise
rem(a, b)  =  0                    if a = INT32_MIN ∧ b = -1
              a - b · trunc(a / b) otherwise
```

**All arithmetic wraps; none of it traps on overflow.** In particular
`INT32_MAX ⊞ 1 = INT32_MIN`, `NEG(INT32_MIN) = INT32_MIN`, and
`ABS(INT32_MIN) = INT32_MIN`. `INT32_MIN / -1` is the one case where the
true quotient is not representable; it wraps, and its remainder is `0`.
`DIVMOD` is net-zero on depth and needs no `room`.

### Bitwise and shifts `0x14`–`0x1A`

Bitwise operators act on the `Word32` reading of their operands. Shift
counts are reduced `s = b mod 32` (equivalently `b ∧ 31`, which is total
for negative `b` — C's `%` is not).

| Op | Effect |
|---|---|
| `0x14 AND` | `… a b → … a ∧ b` |
| `0x15 OR` | `… a b → … a ∨ b` |
| `0x16 XOR` | `… a b → … a ⊕ b` |
| `0x17 NOT` | `… a → … ¬a` (bitwise complement) |
| `0x18 SHL` | `… a b → … wrap(u(a) · 2^s)` |
| `0x19 SHR` | `… a b → … ⌊u(a) / 2^s⌋` as `Int32` (logical) |
| `0x1A SAR` | `… a b → … ⌊a / 2^s⌋` (arithmetic; floor division) |

`SAR` is exactly floor division by `2^s`, which sign-extends: `-8 SAR 1 =
-4` and `-1 SAR 31 = -1`. Since counts are masked, `x SHL 32 = x` and
`x SHL 33 = x SHL 1`.

### Comparison `0x1B`–`0x20`

All are signed and yield `1` for true, `0` for false.

| Op | Effect | | Op | Effect |
|---|---|---|---|---|
| `0x1B EQ` | `… a b → … [a = b]` | | `0x1E LTE` | `… a b → … [a ≤ b]` |
| `0x1C NEQ` | `… a b → … [a ≠ b]` | | `0x1F GT` | `… a b → … [a > b]` |
| `0x1D LT` | `… a b → … [a < b]` | | `0x20 GTE` | `… a b → … [a ≥ b]` |

### Control flow `0x21`–`0x27`

`target(x)` requires `inImage(x)` and traps `JumpOutsideImage` otherwise.

| Op | Rule |
|---|---|
| `0x21 JMP` | `imm` → `t`; `target(t)`; `pc' = t` |
| `0x22 JZ` | `imm` → `t`; `target(t)`; `need(1)`; pop `c`; if `c = 0` then `pc' = t` |
| `0x23 JNZ` | `imm` → `t`; `target(t)`; `need(1)`; pop `c`; if `c ≠ 0` then `pc' = t` |
| `0x24 CALL` | `imm` → `t`; `\|R\| < 1024` else `Trap(ReturnStackOverflow)`; `target(t)`; `R' = R·pc`; `pc' = t` |
| `0x25 RET` | `\|R\| > 0` else `Trap(ReturnStackUnderflow)`; `t = top(R)`; `target(t)`; pop `R`; `pc' = t` |
| `0x26 CALLSTACK` | `need(1)`; `t = u(top(D))`; `\|R\| < 1024`; `target(t)`; pop `D`; `R' = R·pc`; `pc' = t` |
| `0x27 JMPSTACK` | `need(1)`; `t = u(top(D))`; `target(t)`; pop `D`; `pc' = t` |

**The order of checks in `JZ`/`JNZ` is observable and normative**: the
target is decoded and range-checked *before* the condition is popped, so a
branch to an invalid target traps even when it would not have been taken.

The return stack holds return addresses only. Guests cannot read it, push
to it, or forge a return address: `CALL` is the sole writer and `RET` the
sole reader. `R` is not `K`.

### Memory `0x28`–`0x2B`

| Op | Rule |
|---|---|
| `0x28 LOAD` | `room(1)`; `imm` → `x`; push `read(σ, x)` |
| `0x29 STORE` | `imm` → `x`; `need(1)`; `write(σ, x, top(D))`; on success pop |
| `0x2A LOADI` | `need(1)`; `x = u(top(D))`; replace top with `read(σ, x)` |
| `0x2B STOREI` | `need(2)`; `x = u(top(D))`, `v = D[|D|-2]`; `write(σ, x, v)`; on success pop both |

`STOREI` takes `[value, address]` with the **address on top**.

### Loop stack `0x2C`–`0x2F`

`K` is a scratch stack for loop counters, independent of `D` and `R`.

| Op | Rule |
|---|---|
| `0x2C PUSHR` | `need(1)`; `\|K\| < 1024` else `Trap(LoopStackOverflow)`; move top of `D` to `K` |
| `0x2D POPR` | `\|K\| > 0` else `Trap(LoopStackUnderflow)`; `room(1)`; move top of `K` to `D` |
| `0x2E PEEKR` | `\|K\| > 0`; `room(1)`; copy top of `K` to `D` |
| `0x2F PEEKR2` | `\|K\| > 1`; `room(1)`; copy the second element of `K` to `D` |

### Frames and locals `0x30`–`0x33`

Locals live in `L`, a separate 4096-entry array — **not** on `D`. A frame
stores the caller's `fp` at `base = fp + 1` and its `n` locals immediately
above, so local `0` sits at the new `fp` and indices count **downward**.

```
0x30 FRAME   need(1); n = top(D); base = fp + 1
             require 0 ≤ n  ∧  0 ≤ base  ∧  base + n < 4096
                                                   else Trap(FrameRange)
             require |D| - 1 ≥ n                   else Trap(StackUnderflow)
             pop n; L'[base] = fp
             the topmost of the n popped values becomes local 0:
               for i in 0..n-1:  L'[base + n - i] = pop(D)
             fp' = base + n

0x31 UNFRAME need(1); n = top(D)
             require 0 ≤ n  ∧  0 ≤ fp - n < 4096   else Trap(FrameRange)
             require -1 ≤ L[fp - n] < 4096         else Trap(FrameRange)
             pop; fp' = L[fp - n]

0x32 LOCALGET need(1); n = top(D)
             require 0 ≤ fp - n < 4096             else Trap(LocalRange)
             replace top with L[fp - n]

0x33 LOCALSET need(2); n = top(D), v = D[|D|-2]
             require 0 ≤ fp - n < 4096             else Trap(LocalRange)
             L'[fp - n] = v; pop both
```

`fp` is `-1` when no frame is active, so the first `FRAME` uses
`base = 0`. `LOCALSET` takes `[value, index]` with the **index on top**.

**Both of the range checks that mention `4096` bound their index at both
ends, and `UNFRAME` validates the value it restores.** This is load-bearing,
not belt-and-braces. The saved-`FP` slot is an ordinary local, so a guest
can overwrite it with `LOCALSET`; if `UNFRAME` then restored it unchecked,
`fp` would leave `[-1, 4096)` and the next `FRAME` would compute a negative
`base` and write `L[base]` outside the array. Bounding only the top of
`FRAME`'s index is not enough. See §14.

The bound `fp + 1 + n < 4096` and the index `fp - n` are computed over the
integers, not in `Int32`: an `n` near `INT32_MIN` must fail the range check
rather than overflow it.

Note that `LOCALGET n` with `n = ` the frame's local count reads the saved
`fp` slot rather than a local. That is a consequence of the layout, not a
guarantee; compilers do not emit it.

### I/O and termination `0x34`–`0x36`

| Op | Rule |
|---|---|
| `0x34 OUT` | `need(2)`; `v = D[\|D\|-2]`, `f = top(D)`; pop both; raise `Out(v, f)` |
| `0x35 HALT` | `run' = false`, `halt' = true`, `trap' = None` |
| `0x36 YIELD` | `run' = false`, `halt' = false`, `trap' = None` |

`OUT` takes `[value, format]` with the **format on top**. Format `0` is a
decimal integer, `1` is a character; the oracle decides what that means.

### Unknown opcodes

Any byte outside `0x00`–`0x36` traps `UnknownOpcode`. The opcode space is
frozen (`AGENTS.md`: *"Never add new opcodes"*), so `0x37`–`0xFF` are
permanently reserved and a conforming implementation must never assign
them.

## 10. Termination

A stopped machine (`run = false`) is in exactly one of three conditions,
and they are distinguishable from `σ` alone:

| Condition | `halt` | `trap` | `last` |
|---|---|---|---|
| Halted | `true` | `None` | `0x35` |
| Yielded | `false` | `None` | `0x36` |
| Trapped | `false` | ≠ `None` | the offending opcode |

A yielded machine may be resumed by the host — that is the scheduling seam
between the guest and the frame loop. A halted or trapped machine may not.

## 11. Initial state

For a program image `P` loaded at `ib`:

```
pc = ib      D = ε      R = ε      K = ε      L = λi. 0     fp = -1
M = λx. (P[x - ib] if ib ≤ x < ib + |P| else 0)
run = true   halt = false   trap = None   last = 0
ie = ib + |P|
```

Memory outside the image starts zeroed, so an uninitialised read yields
`0` rather than anything host-dependent. `INV` holds.

## 12. Proof obligations

The properties this semantics exists to support. Numbers 1–6 are the
targets of the CBMC harness (`make verify`).

1. **Memory safety.** No step accesses `M` outside `[0, msize)`, or `D`,
   `R`, `K`, `L` outside their declared bounds.
2. **Totality.** For every `σ` satisfying `INV` and every oracle answer,
   exactly one rule applies. There are no undefined operations.
3. **Image immutability.** No step changes `M` on `[ib, ie)`. The bytes of
   a program are constant for its lifetime.
4. **Execution confinement.** `run ⇒ ib ≤ pc ≤ ie`, and an instruction is
   only ever fetched from an address strictly inside `[ib, ie)`.

   The upper bound is `≤ ie`, not `< ie`: a 5-byte instruction at the very
   end of the image leaves `pc` exactly at `ie` with the machine still
   running. It traps on the *next* fetch, which is what the fetch rule (§5)
   is for. Stating this as `pc < ie` is a natural mistake and CBMC rejects
   it.
5. **Invariant preservation.** `INV(σ) ⇒ INV(σ')`. Being inductive, this
   lifts from one step to any run.
6. **Determinism.** `step` is a function: equal states and equal oracle
   answers give equal successors and equal interaction sequences.

What is deliberately *not* claimed: that a program cannot trap. NUX checks
everything dynamically and has no static validator, so "this image never
faults" is not established by anything here. A build-time validator that
would establish it is discussed in `docs/formal-semantics-plan.md`, and is
constrained by the opcode freeze.

## 13. Deviations from `docs/opcodes.md`

`docs/opcodes.md` has been corrected to match this document. For the
record, it previously stated four things that were never true of the
implementation:

- `FRAME` took `n` at the bottom of its operands (it is on top).
- `FRAME` set `FP = SP` with locals on the data stack (locals are a
  separate array).
- `UNFRAME` had stack effect `[] → []` (it pops an operand).
- `LOCALGET` read `FP + offset` (it reads `FP - offset`).

It also referred to a `YieldHandler` that does not exist. These are the
kind of drift a normative document is meant to prevent.

## 14. What writing this down found

Two defects, neither of which the 15 hand-written opcode tests nor the
17-app image suite had caught. Both are recorded here because they are the
argument for having a specification at all.

**A guest could corrupt the frame pointer and write outside `locals[]`.**
`UNFRAME` restored `fp` from `L[fp - n]` without validating it, and that
slot is an ordinary local that `LOCALSET` can write. So a guest could put
any `Int32` in it, `UNFRAME` to load it into `fp`, and then `FRAME` — whose
range check bounded only the *top* of its index — would compute a negative
`base` and write `L[base]` out of bounds. Eleven instructions were enough:

```
PUSH 7  PUSH 1  FRAME            fp = 1, saved FP at L[0]
PUSH -1000000  PUSH 1  LOCALSET  overwrite the saved-FP slot
PUSH 1  UNFRAME                  fp = -1000000
PUSH 0  FRAME                    writes L[-999999]
```

The defect predates this work (it is present at `vm.c:521` of the commit
before it). It was found by the CBMC harness in `verify/`, which reported
that the invariant `-1 ≤ fp < 4096` was not preserved by a step — and an
invariant that is not inductive is exactly how a bug like this hides. The
fix bounds `FRAME`'s index at both ends and makes `UNFRAME` reject a frame
pointer outside the legal range. `test_frame_pointer_cannot_escape` in
`src/test_vm.c` is the regression.

**Every arithmetic opcode had undefined behaviour at the 32-bit boundary.**
`ADD SUB MUL INC DEC NEG ABS` were plain signed C arithmetic, so
`INT32_MAX + 1` was undefined rather than wrapping; `INT32_MIN / -1` was
unguarded in `DIV`, `MOD` and `DIVMOD`; and shift counts were reduced with
C's `%`, which truncates toward zero and so produced a *negative* — and
therefore undefined — shift distance for a negative operand. §9 now fixes
each of these, and `make test` re-runs the opcode suite under UBSan so they
cannot come back.

Neither defect was a matter of the implementation disagreeing with the
specification. There was no specification: the behaviour at the boundary was
whatever the host compiler chose, and no document said otherwise.