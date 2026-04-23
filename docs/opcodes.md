# NUX Opcode Reference

Complete reference for all 32 opcodes in the NUX virtual machine.

## Stack Notation

- `[a, b, c]` - Stack with `c` at top
- `[a] → [b]` - Transformation from state `a` to state `b`

## Opcodes

### Stack Manipulation

#### 0x00 - PUSH
**Format**: `PUSH value` (5 bytes: opcode + 4-byte big-endian value)  
**Action**: `[] → [value]`  
**Description**: Push a 32-bit signed integer onto the stack.

#### 0x01 - POP
**Format**: `POP` (1 byte)  
**Action**: `[a] → []`  
**Description**: Remove and discard the top value from the stack.

#### 0x02 - DUP
**Format**: `DUP` (1 byte)  
**Action**: `[a] → [a, a]`  
**Description**: Duplicate the top value on the stack.

#### 0x03 - SWAP
**Format**: `SWAP` (1 byte)  
**Action**: `[a, b] → [b, a]`  
**Description**: Swap the top two values on the stack.

#### 0x04 - ROLL
**Format**: `ROLL` (1 byte)  
**Action**: `[a, b] → [a, b, a]`  
**Description**: Copy the second-from-top value to the top of the stack (roll nth element to top).

#### 0x05 - ROT
**Format**: `ROT` (1 byte)  
**Action**: `[a, b, c] → [b, c, a]`  
**Description**: Rotate the top three values, moving third to top.

### Arithmetic Operations

#### 0x06 - ADD
**Format**: `ADD` (1 byte)  
**Action**: `[a, b] → [a + b]`  
**Description**: Pop two values, add them, push result.

#### 0x07 - SUB
**Format**: `SUB` (1 byte)  
**Action**: `[a, b] → [a - b]`  
**Description**: Pop two values, subtract (second - top), push result.

#### 0x08 - MUL
**Format**: `MUL` (1 byte)  
**Action**: `[a, b] → [a * b]`  
**Description**: Pop two values, multiply them, push result.

#### 0x09 - DIV
**Format**: `DIV` (1 byte)  
**Action**: `[a, b] → [a / b]`  
**Description**: Pop two values, divide (second / top), push quotient.  
**Error**: Division by zero raises an error.

#### 0x0A - MOD
**Format**: `MOD` (1 byte)  
**Action**: `[a, b] → [a % b]`  
**Description**: Pop two values, compute modulus (second % top), push result.  
**Error**: Modulus by zero raises an error.

#### 0x0B - INC
**Format**: `INC` (1 byte)  
**Action**: `[a] → [a + 1]`  
**Description**: Increment the top value by 1.

#### 0x0C - DEC
**Format**: `DEC` (1 byte)  
**Action**: `[a] → [a - 1]`  
**Description**: Decrement the top value by 1.

### Bitwise Operations

#### 0x0D - AND
**Format**: `AND` (1 byte)  
**Action**: `[a, b] → [a & b]`  
**Description**: Bitwise AND of top two values.

#### 0x0E - OR
**Format**: `OR` (1 byte)  
**Action**: `[a, b] → [a | b]`  
**Description**: Bitwise OR of top two values.

#### 0x0F - XOR
**Format**: `XOR` (1 byte)  
**Action**: `[a, b] → [a ^ b]`  
**Description**: Bitwise XOR of top two values.

#### 0x10 - NOT
**Format**: `NOT` (1 byte)  
**Action**: `[a] → [~a]`  
**Description**: Bitwise NOT of top value.

#### 0x11 - SHL
**Format**: `SHL` (1 byte)  
**Action**: `[a, b] → [a << (b % 32)]`  
**Description**: Shift second value left by top value (mod 32) bits.

### Comparison Operations

#### 0x12 - EQ
**Format**: `EQ` (1 byte)  
**Action**: `[a, b] → [a == b ? 1 : 0]`  
**Description**: Push 1 if values are equal, 0 otherwise.

#### 0x13 - LT
**Format**: `LT` (1 byte)  
**Action**: `[a, b] → [a < b ? 1 : 0]`  
**Description**: Push 1 if second < top (signed), 0 otherwise.

> **Note**: There is no GT opcode. To test `a > b`, use `SWAP; LT`. The LUX compiler
> handles this automatically when you write `>`.

### Control Flow

#### 0x14 - CALLSTACK
**Format**: `CALLSTACK` (1 byte)  
**Action**: `[quotation-addr] → [...]`  
**Description**: Pop address from stack, push return address to return stack, and jump to that address. Used for calling quotations (anonymous code blocks).

#### 0x15 - JMP
**Format**: `JMP address` (5 bytes: opcode + 4-byte address)  
**Action**: Jump to address  
**Description**: Unconditional jump to specified address.

#### 0x16 - JZ
**Format**: `JZ address` (5 bytes: opcode + 4-byte address)  
**Action**: `[cond] → []` (jump if cond == 0)  
**Description**: Pop value, jump to address if it's zero.

> **Note**: There is no JNZ opcode. To jump if non-zero, use `PUSH 0; EQ; JZ`. The LUX
> compiler emits this sequence automatically.

#### 0x17 - CALL
**Format**: `CALL address` (5 bytes: opcode + 4-byte address)  
**Action**: Push return address to return stack, then jump  
**Description**: Call subroutine at inline address.

#### 0x18 - RET
**Format**: `RET` (1 byte)  
**Action**: Pop address from return stack and jump  
**Description**: Return from subroutine.

### Memory Operations

#### 0x19 - LOAD
**Format**: `LOAD address` (5 bytes: opcode + 4-byte address)  
**Action**: `[] → [memory[address]]`  
**Description**: Load 32-bit value from inline memory address and push it.

#### 0x1A - STORE
**Format**: `STORE address` (5 bytes: opcode + 4-byte address)  
**Action**: `[value] → []`  
**Description**: Pop value and store it at inline memory address.

#### 0x1E - LOADI
**Format**: `LOADI` (1 byte)  
**Action**: `[addr] → [memory[addr]]`  
**Description**: Pop address from stack, push value at that address. Indirect load — address is computed at runtime. Used for device I/O and dynamic memory access.

#### 0x1F - STOREI
**Format**: `STOREI` (1 byte)  
**Action**: `[value, addr] → []`  
**Description**: Pop address, pop value, store value at address. Indirect store — address is computed at runtime. Used for device I/O and dynamic memory access.

### I/O Operations

#### 0x1B - OUT
**Format**: `OUT` (1 byte)  
**Action**: `[format, value] → []`  
**Description**: Pop format flag (0=number, 1=character) and value, then output to console.
- If format=0: Output value as decimal number (e.g., 42 → "42")
- If format=1: Output value as ASCII character (e.g., 72 → "H")

**Stack Before**: `[format, value]` (format on top)  
**Stack After**: `[]`

### System Operations

#### 0x1C - HALT
**Format**: `HALT` (1 byte)  
**Action**: Stop execution  
**Description**: Terminate VM execution.

#### 0x1D - YIELD
**Format**: `YIELD` (1 byte)  
**Action**: Pause and call host  
**Description**: Yield execution to the host. Calls the VM's `YieldHandler` if one is set, allowing the host to render frames, sleep, handle input, etc. Execution resumes after the handler returns. Used for device I/O and cooperative multitasking.

## Removed Opcodes

The following opcodes **no longer exist** and will cause an error if encountered:

| Former Name | Former Hex | Replacement sequence |
|-------------|------------|----------------------|
| NEG         | (was 0x0D) | `PUSH 0; SWAP; SUB`  |
| GT          | (was 0x15) | `SWAP; LT`           |
| JNZ         | (was 0x19) | `PUSH 0; EQ; JZ`     |

The LUX compiler provides `NEGATE` and `>` words that expand to the replacement sequences automatically.

## Complete Opcode Table

| Hex  | Name      | Bytes | Stack Effect |
|------|-----------|-------|--------------|
| 0x00 | PUSH      | 5     | `[] → [value]` |
| 0x01 | POP       | 1     | `[a] → []` |
| 0x02 | DUP       | 1     | `[a] → [a, a]` |
| 0x03 | SWAP      | 1     | `[a, b] → [b, a]` |
| 0x04 | ROLL      | 1     | `[a, b] → [a, b, a]` |
| 0x05 | ROT       | 1     | `[a, b, c] → [b, c, a]` |
| 0x06 | ADD       | 1     | `[a, b] → [a+b]` |
| 0x07 | SUB       | 1     | `[a, b] → [a-b]` |
| 0x08 | MUL       | 1     | `[a, b] → [a*b]` |
| 0x09 | DIV       | 1     | `[a, b] → [a/b]` |
| 0x0A | MOD       | 1     | `[a, b] → [a%b]` |
| 0x0B | INC       | 1     | `[a] → [a+1]` |
| 0x0C | DEC       | 1     | `[a] → [a-1]` |
| 0x0D | AND       | 1     | `[a, b] → [a&b]` |
| 0x0E | OR        | 1     | `[a, b] → [a\|b]` |
| 0x0F | XOR       | 1     | `[a, b] → [a^b]` |
| 0x10 | NOT       | 1     | `[a] → [~a]` |
| 0x11 | SHL       | 1     | `[a, b] → [a<<(b%32)]` |
| 0x12 | EQ        | 1     | `[a, b] → [a==b ? 1 : 0]` |
| 0x13 | LT        | 1     | `[a, b] → [a<b ? 1 : 0]` |
| 0x14 | CALLSTACK | 1     | `[addr] → [...]` |
| 0x15 | JMP       | 5     | unconditional jump |
| 0x16 | JZ        | 5     | `[cond] → []`, jump if zero |
| 0x17 | CALL      | 5     | call inline address |
| 0x18 | RET       | 1     | return from call |
| 0x19 | LOAD      | 5     | `[] → [mem[addr]]` |
| 0x1A | STORE     | 5     | `[value] → []` |
| 0x1B | OUT       | 1     | `[format, value] → []` |
| 0x1C | HALT      | 1     | stop |
| 0x1D | YIELD     | 1     | yield to host |
| 0x1E | LOADI     | 1     | `[addr] → [mem[addr]]` |
| 0x1F | STOREI    | 1     | `[addr, value] → []` |

## Encoding

All multi-byte values use **big-endian** byte order:
- Immediate values: 4 bytes
- Memory addresses: 4 bytes (32-bit address space)

## Example Programs

### Hello World (print 42)
```
00 00 00 00 2A    PUSH 42
00 00 00 00 00    PUSH 0     (format: number)
1B                OUT
1C                HALT
```

### Add Two Numbers
```
00 00 00 00 05    PUSH 5
00 00 00 00 03    PUSH 3
06                ADD
00 00 00 00 00    PUSH 0     (format: number)
1B                OUT
1C                HALT
```

### Simple Loop
```
00 00 00 00 05    PUSH 5        ; counter
02                DUP           ; duplicate counter
16 00 00 00 0E    JZ end        ; if 0, exit
0C                DEC           ; decrement
15 00 00 00 05    JMP loop      ; jump back
01                POP           ; clean up
1C                HALT
```
