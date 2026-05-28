# NUX Opcode Reference

Complete reference for all 55 opcodes in the NUX virtual machine (0x00–0x36), grouped by function.

## Stack Notation

- `[a, b, c]` — stack with `c` at top
- `[a] → [b]` — transformation from state `a` to state `b`

## Encoding

Most opcodes are **1 byte**. Seven opcodes carry a 4-byte big-endian immediate operand and are **5 bytes** total: `PUSH`, `JMP`, `JZ`, `JNZ`, `CALL`, `LOAD`, `STORE`.

---

## Stack Manipulation (0x00–0x07)

#### 0x00 — PUSH
**Format**: `PUSH value` (5 bytes)  
**Action**: `[] → [value]`  
Push a 32-bit signed integer onto the stack.

#### 0x01 — POP
**Format**: `POP` (1 byte)  
**Action**: `[a] → []`  
Remove and discard the top value.

#### 0x02 — DUP
**Format**: `DUP` (1 byte)  
**Action**: `[a] → [a, a]`  
Duplicate the top value.

#### 0x03 — SWAP
**Format**: `SWAP` (1 byte)  
**Action**: `[a, b] → [b, a]`  
Swap the top two values.

#### 0x04 — OVER
**Format**: `OVER` (1 byte)  
**Action**: `[a, b] → [a, b, a]`  
Copy the second-from-top value to the top.

#### 0x05 — ROT
**Format**: `ROT` (1 byte)  
**Action**: `[a, b, c] → [b, c, a]`  
Rotate the top three values: move the third item to the top.

#### 0x06 — PICK
**Format**: `PICK` (1 byte)  
**Action**: `[... stack[n] ... n] → [... stack[n] ... stack[n]]`  
Pop index `n` from the top, then copy the nth stack element (0 = top) to the top. Useful for reaching values at arbitrary depth without temporaries.

#### 0x07 — ROLL
**Format**: `ROLL` (1 byte)  
**Action**: `[... x_n ... x_0 n] → [... x_{n-1} ... x_0 x_n]`  
Pop index `n`, then move the nth element to the top (destructive, unlike PICK).

---

## Arithmetic (0x08–0x13)

#### 0x08 — ADD
**Format**: `ADD` (1 byte)  
**Action**: `[a, b] → [a+b]`

#### 0x09 — SUB
**Format**: `SUB` (1 byte)  
**Action**: `[a, b] → [a-b]`  
Subtracts top from second: `second - top`.

#### 0x0A — MUL
**Format**: `MUL` (1 byte)  
**Action**: `[a, b] → [a*b]`

#### 0x0B — DIV
**Format**: `DIV` (1 byte)  
**Action**: `[a, b] → [a/b]`  
Integer division. **Error** on divide-by-zero.

#### 0x0C — MOD
**Format**: `MOD` (1 byte)  
**Action**: `[a, b] → [a%b]`  
**Error** on modulo-by-zero.

#### 0x0D — INC
**Format**: `INC` (1 byte)  
**Action**: `[a] → [a+1]`

#### 0x0E — DEC
**Format**: `DEC` (1 byte)  
**Action**: `[a] → [a-1]`

#### 0x0F — NEG
**Format**: `NEG` (1 byte)  
**Action**: `[a] → [-a]`  
Two's-complement negation. LUX word: `NEGATE`.

#### 0x10 — ABS
**Format**: `ABS` (1 byte)  
**Action**: `[a] → [|a|]`  
Absolute value.

#### 0x11 — DIVMOD
**Format**: `DIVMOD` (1 byte)  
**Action**: `[a, b] → [a/b, a%b]`  
Pushes quotient then remainder in a single operation. **Error** on divide-by-zero.

#### 0x12 — MIN
**Format**: `MIN` (1 byte)  
**Action**: `[a, b] → [min(a, b)]`  
Signed minimum.

#### 0x13 — MAX
**Format**: `MAX` (1 byte)  
**Action**: `[a, b] → [max(a, b)]`  
Signed maximum.

---

## Bitwise & Shifts (0x14–0x1A)

#### 0x14 — AND
**Format**: `AND` (1 byte)  
**Action**: `[a, b] → [a & b]`

#### 0x15 — OR
**Format**: `OR` (1 byte)  
**Action**: `[a, b] → [a | b]`

#### 0x16 — XOR
**Format**: `XOR` (1 byte)  
**Action**: `[a, b] → [a ^ b]`

#### 0x17 — NOT
**Format**: `NOT` (1 byte)  
**Action**: `[a] → [~a]`  
Bitwise complement.

#### 0x18 — SHL
**Format**: `SHL` (1 byte)  
**Action**: `[a, b] → [a << (b % 32)]`  
Left shift. LUX word: `LSHIFT`.

#### 0x19 — SHR
**Format**: `SHR` (1 byte)  
**Action**: `[a, b] → [a >>> (b % 32)]`  
Logical (unsigned) right shift — fills with zeros. LUX word: `RSHIFT`.

#### 0x1A — SAR
**Format**: `SAR` (1 byte)  
**Action**: `[a, b] → [a >> (b % 32)]`  
Arithmetic (signed) right shift — fills with the sign bit. LUX word: `ARSHIFT`.

---

## Comparison (0x1B–0x20)

All comparisons are signed and push `1` (true) or `0` (false).

#### 0x1B — EQ
**Action**: `[a, b] → [a == b ? 1 : 0]`  LUX word: `=`

#### 0x1C — NEQ
**Action**: `[a, b] → [a != b ? 1 : 0]`  LUX word: `<>`

#### 0x1D — LT
**Action**: `[a, b] → [a < b ? 1 : 0]`  LUX word: `<`

#### 0x1E — LTE
**Action**: `[a, b] → [a <= b ? 1 : 0]`  LUX word: `<=`

#### 0x1F — GT
**Action**: `[a, b] → [a > b ? 1 : 0]`  LUX word: `>`

#### 0x20 — GTE
**Action**: `[a, b] → [a >= b ? 1 : 0]`  LUX word: `>=`

---

## Control Flow (0x21–0x27)

#### 0x21 — JMP
**Format**: `JMP address` (5 bytes)  
**Action**: `pc = address`  
Unconditional jump to an inline absolute address.

#### 0x22 — JZ
**Format**: `JZ address` (5 bytes)  
**Action**: `[cond] → []`; jump if `cond == 0`  
Conditional jump — taken when the top of stack is zero.

#### 0x23 — JNZ
**Format**: `JNZ address` (5 bytes)  
**Action**: `[cond] → []`; jump if `cond != 0`  
Conditional jump — taken when the top of stack is non-zero. Inverse of JZ.

#### 0x24 — CALL
**Format**: `CALL address` (5 bytes)  
**Action**: push return address to return stack, then `pc = address`  
Call a subroutine at an inline absolute address.

#### 0x25 — RET
**Format**: `RET` (1 byte)  
**Action**: `pc = return_stack.pop()`  
Return from a subroutine.

#### 0x26 — CALLSTACK
**Format**: `CALLSTACK` (1 byte)  
**Action**: `[addr] → [...]`; push return address to return stack, then `pc = addr`  
Call a subroutine whose address is on the main stack. Used by the compiler to invoke quotations (anonymous code blocks).

#### 0x27 — JMPSTACK
**Format**: `JMPSTACK` (1 byte)  
**Action**: `[addr] → []`; `pc = addr`  
Jump to an address from the main stack without pushing a return address. Useful for tail calls.

---

## Memory (0x28–0x2B)

The VM has a flat 32-bit address space. Addresses at 0x10000 and above are device-mapped and routed through the bus.

#### 0x28 — LOAD
**Format**: `LOAD address` (5 bytes)  
**Action**: `[] → [memory[address]]`  
Load a 32-bit value from an inline (compile-time) address.

#### 0x29 — STORE
**Format**: `STORE address` (5 bytes)  
**Action**: `[value] → []`  
Store a 32-bit value to an inline (compile-time) address.

#### 0x2A — LOADI
**Format**: `LOADI` (1 byte)  
**Action**: `[addr] → [memory[addr]]`  
Indirect load — address is popped from the stack at runtime. Used for dynamic memory access and device I/O. LUX word: `LOADI` / `@`.

#### 0x2B — STOREI
**Format**: `STOREI` (1 byte)  
**Action**: `[value, addr] → []`  
Indirect store — pops address then value. Used for dynamic memory access and device I/O. LUX word: `STOREI` / `!`.

---

## Loop Stack (0x2C–0x2F)

The VM maintains a separate **loop stack** (also called the R-stack) for loop indices and other short-lived values. This keeps loop variables off the main stack and avoids juggling them across iterations.

#### 0x2C — PUSHR
**Format**: `PUSHR` (1 byte)  
**Action**: `[a] → []`; push `a` to loop stack  
Move the top of the main stack onto the loop stack.

#### 0x2D — POPR
**Format**: `POPR` (1 byte)  
**Action**: `[] → [a]`; pop from loop stack to main stack  
Move the top of the loop stack back to the main stack.

#### 0x2E — PEEKR
**Format**: `PEEKR` (1 byte)  
**Action**: `[] → [a]`; `a = loop_stack.top` (non-destructive)  
Copy the top of the loop stack to the main stack without removing it.

#### 0x2F — PEEKR2
**Format**: `PEEKR2` (1 byte)  
**Action**: `[] → [a, b]`; copies top two loop stack items  
Copy the top two loop stack items to the main stack (non-destructive). Useful when a loop carries two indices simultaneously.

---

## Frame & Local Variables (0x30–0x33)

These opcodes implement a **frame pointer**-based local variable system. A frame is established at word entry via `FRAME` and torn down at exit via `UNFRAME`. Locals live in a reserved region above the frame pointer.

#### 0x30 — FRAME
**Format**: `FRAME` (1 byte)  
**Action**: `[n, v_n ... v_1] → []`  
Save the old frame pointer, set `FP = SP`, then copy `n` values from the stack into the local variable slots. LUX word: `FRAME!`.

#### 0x31 — UNFRAME
**Format**: `UNFRAME` (1 byte)  
**Action**: `[] → []`; `SP = FP`, restore old `FP`  
Tear down the current frame: discard all locals by resetting `SP` to `FP`, then restore the caller's frame pointer. LUX word: `UNFRAME!`.

#### 0x32 — LOCALGET
**Format**: `LOCALGET` (1 byte)  
**Action**: `[offset] → [val]`  
Pop `offset`, push the local variable at `FP + offset`. LUX word: `LOCAL@`.

#### 0x33 — LOCALSET
**Format**: `LOCALSET` (1 byte)  
**Action**: `[offset, val] → []`  
Pop `offset` then `val`, store `val` at local slot `FP + offset`. LUX word: `LOCAL!`.

---

## I/O & System (0x34–0x36)

#### 0x34 — OUT
**Format**: `OUT` (1 byte)  
**Action**: `[format, value] → []`  
Pop format flag and value; write to the console output handler.
- `format = 0`: print `value` as a decimal integer
- `format = 1`: print `value` as an ASCII character

Note: graphical programs typically write to `/dev/draw` via STOREI rather than using OUT.

#### 0x35 — HALT
**Format**: `HALT` (1 byte)  
Stop VM execution.

#### 0x36 — YIELD
**Format**: `YIELD` (1 byte)  
Yield to the host. Calls the VM's `YieldHandler` if one is set, allowing the host to render a frame, handle input, sleep, etc. Execution resumes after the handler returns. Used for cooperative multitasking in CLOISTER.

---

## Complete Opcode Table

| Hex  | Name      | Bytes | Stack Effect |
|------|-----------|-------|--------------|
| 0x00 | PUSH      | 5     | `[] → [value]` |
| 0x01 | POP       | 1     | `[a] → []` |
| 0x02 | DUP       | 1     | `[a] → [a, a]` |
| 0x03 | SWAP      | 1     | `[a, b] → [b, a]` |
| 0x04 | OVER      | 1     | `[a, b] → [a, b, a]` |
| 0x05 | ROT       | 1     | `[a, b, c] → [b, c, a]` |
| 0x06 | PICK      | 1     | `[... n] → [... stack[n]]` |
| 0x07 | ROLL      | 1     | `[... x_n...x_0 n] → [... x_{n-1}...x_0 x_n]` |
| 0x08 | ADD       | 1     | `[a, b] → [a+b]` |
| 0x09 | SUB       | 1     | `[a, b] → [a-b]` |
| 0x0A | MUL       | 1     | `[a, b] → [a*b]` |
| 0x0B | DIV       | 1     | `[a, b] → [a/b]` |
| 0x0C | MOD       | 1     | `[a, b] → [a%b]` |
| 0x0D | INC       | 1     | `[a] → [a+1]` |
| 0x0E | DEC       | 1     | `[a] → [a-1]` |
| 0x0F | NEG       | 1     | `[a] → [-a]` |
| 0x10 | ABS       | 1     | `[a] → [\|a\|]` |
| 0x11 | DIVMOD    | 1     | `[a, b] → [a/b, a%b]` |
| 0x12 | MIN       | 1     | `[a, b] → [min(a,b)]` |
| 0x13 | MAX       | 1     | `[a, b] → [max(a,b)]` |
| 0x14 | AND       | 1     | `[a, b] → [a&b]` |
| 0x15 | OR        | 1     | `[a, b] → [a\|b]` |
| 0x16 | XOR       | 1     | `[a, b] → [a^b]` |
| 0x17 | NOT       | 1     | `[a] → [~a]` |
| 0x18 | SHL       | 1     | `[a, b] → [a<<(b%32)]` |
| 0x19 | SHR       | 1     | `[a, b] → [a>>>(b%32)]` |
| 0x1A | SAR       | 1     | `[a, b] → [a>>(b%32)]` |
| 0x1B | EQ        | 1     | `[a, b] → [a==b ? 1 : 0]` |
| 0x1C | NEQ       | 1     | `[a, b] → [a!=b ? 1 : 0]` |
| 0x1D | LT        | 1     | `[a, b] → [a<b ? 1 : 0]` |
| 0x1E | LTE       | 1     | `[a, b] → [a<=b ? 1 : 0]` |
| 0x1F | GT        | 1     | `[a, b] → [a>b ? 1 : 0]` |
| 0x20 | GTE       | 1     | `[a, b] → [a>=b ? 1 : 0]` |
| 0x21 | JMP       | 5     | unconditional jump |
| 0x22 | JZ        | 5     | `[cond] → []`, jump if zero |
| 0x23 | JNZ       | 5     | `[cond] → []`, jump if non-zero |
| 0x24 | CALL      | 5     | call inline address |
| 0x25 | RET       | 1     | return from call |
| 0x26 | CALLSTACK | 1     | `[addr] → [...]`, call from stack |
| 0x27 | JMPSTACK  | 1     | `[addr] → []`, jump from stack |
| 0x28 | LOAD      | 5     | `[] → [mem[addr]]` |
| 0x29 | STORE     | 5     | `[value] → []` |
| 0x2A | LOADI     | 1     | `[addr] → [mem[addr]]` |
| 0x2B | STOREI    | 1     | `[value, addr] → []` |
| 0x2C | PUSHR     | 1     | `[a] → []`, push to loop stack |
| 0x2D | POPR      | 1     | `[] → [a]`, pop from loop stack |
| 0x2E | PEEKR     | 1     | `[] → [a]`, copy loop stack top |
| 0x2F | PEEKR2    | 1     | `[] → [a, b]`, copy top two of loop stack |
| 0x30 | FRAME     | 1     | `[n, v_n...v1] → []`, set up frame |
| 0x31 | UNFRAME   | 1     | `[] → []`, tear down frame |
| 0x32 | LOCALGET  | 1     | `[offset] → [val]` |
| 0x33 | LOCALSET  | 1     | `[offset, val] → []` |
| 0x34 | OUT       | 1     | `[format, value] → []` |
| 0x35 | HALT      | 1     | stop |
| 0x36 | YIELD     | 1     | yield to host |

## Example Programs

### Hello World (print 42)
```
00 00 00 00 2A    PUSH 42
00 00 00 00 00    PUSH 0        (format: number)
34                OUT
35                HALT
```

### Add Two Numbers
```
00 00 00 00 05    PUSH 5
00 00 00 00 03    PUSH 3
08                ADD
00 00 00 00 00    PUSH 0        (format: number)
34                OUT
35                HALT
```

### Simple Loop (count down from 5)
```
; addr 0x00
00 00 00 00 05    PUSH 5        ; counter
; addr 0x05  ← loop top
02                DUP
22 00 00 00 0F    JZ end        ; jump to 0x0F if zero
0E                DEC
21 00 00 00 05    JMP 0x05      ; jump back to loop top
; addr 0x0F  ← end
01                POP
35                HALT
```
