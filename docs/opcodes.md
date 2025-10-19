# NUX Opcode Reference

Complete reference for all 32 opcodes in the NUX virtual machine.

## Stack Notation

- `[a, b, c]` - Stack with `c` at top
- `[a] → [b]` - Transformation from state `a` to state `b`

## Opcodes

### Stack Manipulation

#### 0x00 - PUSH
**Format**: `PUSH value` (5 bytes: opcode + 4-byte value)  
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

#### 0x04 - OVER
**Format**: `OVER` (1 byte)  
**Action**: `[a, b] → [a, b, a]`  
**Description**: Copy the second-from-top value to the top of the stack.

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

#### 0x0D - NEG
**Format**: `NEG` (1 byte)  
**Action**: `[a] → [-a]`  
**Description**: Negate the top value.

### Bitwise Operations

#### 0x0E - AND
**Format**: `AND` (1 byte)  
**Action**: `[a, b] → [a & b]`  
**Description**: Bitwise AND of top two values.

#### 0x0F - OR
**Format**: `OR` (1 byte)  
**Action**: `[a, b] → [a | b]`  
**Description**: Bitwise OR of top two values.

#### 0x10 - XOR
**Format**: `XOR` (1 byte)  
**Action**: `[a, b] → [a ^ b]`  
**Description**: Bitwise XOR of top two values.

#### 0x11 - NOT
**Format**: `NOT` (1 byte)  
**Action**: `[a] → [~a]`  
**Description**: Bitwise NOT of top value.

#### 0x12 - SHL
**Format**: `SHL` (1 byte)  
**Action**: `[a, b] → [a << (b % 32)]`  
**Description**: Shift second value left by top value (mod 32) bits.

### Comparison Operations

#### 0x13 - EQ
**Format**: `EQ` (1 byte)  
**Action**: `[a, b] → [a == b ? 1 : 0]`  
**Description**: Push 1 if values are equal, 0 otherwise.

#### 0x14 - LT
**Format**: `LT` (1 byte)  
**Action**: `[a, b] → [a < b ? 1 : 0]`  
**Description**: Push 1 if second < top (signed), 0 otherwise.

#### 0x15 - GT
**Format**: `GT` (1 byte)  
**Action**: `[a, b] → [a > b ? 1 : 0]`  
**Description**: Push 1 if second > top (signed), 0 otherwise.

### Control Flow

#### 0x16 - CALLSTACK
**Format**: `CALLSTACK` (1 byte)  
**Action**: `[quotation-addr] → [...]`  
**Description**: Call code at address from stack. Pushes return address to return stack and jumps to the quotation address.

#### 0x17 - JMP
**Format**: `JMP address` (5 bytes: opcode + 4-byte address)  
**Action**: Jump to address  
**Description**: Unconditional jump to specified address.

#### 0x18 - JZ
**Format**: `JZ address` (5 bytes: opcode + 4-byte address)  
**Action**: `[cond] → []` (jump if cond == 0)  
**Description**: Pop value, jump to address if it's zero.

#### 0x19 - JNZ
**Format**: `JNZ address` (5 bytes: opcode + 4-byte address)  
**Action**: `[cond] → []` (jump if cond != 0)  
**Description**: Pop value, jump to address if it's non-zero.

#### 0x1A - CALL
**Format**: `CALL address` (5 bytes: opcode + 4-byte address)  
**Action**: `[] → [return_address]` then jump  
**Description**: Push return address (PC+4) and jump to subroutine.

#### 0x1B - RET
**Format**: `RET` (1 byte)  
**Action**: `[address] → []` then jump  
**Description**: Pop address from stack and jump to it.

### Memory Operations

#### 0x1C - LOAD
**Format**: `LOAD address` (5 bytes: opcode + 4-byte address)  
**Action**: `[] → [memory[address]]`  
**Description**: Load 32-bit value from memory address and push it.

#### 0x1D - STORE
**Format**: `STORE address` (5 bytes: opcode + 4-byte address)  
**Action**: `[value] → []`  
**Description**: Pop value and store it at memory address.

### I/O Operations

#### 0x1E - OUT
**Format**: `OUT` (1 byte)  
**Action**: `[value, format] → []`  
**Description**: Pop format flag (0=number, 1=character) and value, then output to console.
- If format=0: Output value as decimal number (e.g., 42 → "42")
- If format=1: Output value as ASCII character (e.g., 72 → "H")

**Stack Before**: `[value, format]`  
**Stack After**: `[]`


### System Operations

#### 0x1F - HALT
**Format**: `HALT` (1 byte)  
**Action**: Stop execution  
**Description**: Terminate VM execution.

## Encoding

All multi-byte values use **big-endian** byte order:
- Immediate values: 4 bytes
- Memory addresses: 4 bytes (32-bit address space)

## Example Programs

### Hello World (print 42)
```
00 00 00 00 2A    PUSH 42
1E                OUT
1F                HALT
```

### Add Two Numbers
```
00 00 00 00 05    PUSH 5
00 00 00 00 03    PUSH 3
06                ADD
1E                OUT
1F                HALT
```

### Simple Loop
```
00 00 00 00 05    PUSH 5        ; counter
02                DUP           ; duplicate counter
18 00 00 00 0E    JZ end        ; if 0, exit
0C                DEC           ; decrement
17 00 00 00 05    JMP loop      ; jump back
01                POP           ; clean up
1F                HALT
```
