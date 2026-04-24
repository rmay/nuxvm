# VM Example Programs

Written by Claude Sonnet 4.5

Real-world example programs demonstrating your stack-based VM from `main.go`.

## Quick Start

```bash
# Make sure main.go is in the same directory
go run main.go vm_examples.go
```

## What's Included

7 working example programs that demonstrate various algorithms and VM features:

### Example 1: GCD (Greatest Common Divisor)
**Input:** 48, 18  
**Output:** 6  
**Demonstrates:** Loops, MOD operation, Euclidean algorithm

### Example 2: Even/Odd Check
**Input:** 42  
**Output:** 0 (even)  
**Demonstrates:** Conditional branching, modulus

### Example 3: Absolute Value
**Input:** -25  
**Output:** 25  
**Demonstrates:** Conditional execution, negate (PUSH 0; SWAP; SUB)

### Example 4: Minimum of Two Numbers
**Input:** 34, 21  
**Output:** 21  
**Demonstrates:** Comparison, conditional selection

### Example 5: Maximum of Two Numbers
**Input:** 15, 28  
**Output:** 28  
**Demonstrates:** Comparison, conditional selection

### Example 6: Square a Number
**Input:** 12  
**Output:** 144  
**Demonstrates:** Stack duplication, multiplication

### Example 7: Count Down
**Input:** 10  
**Output:** 10 9 8 7 6 5 4 3 2 1  
**Demonstrates:** Loop iteration, decrement, multiple outputs

## How It Works

Each example builds a bytecode program using your VM's opcodes:

```go
func ex1_GCD() {
    prog := []byte{}
    
    // Push initial values
    prog = append(prog, push(48)...)
    prog = append(prog, push(18)...)
    
    // Loop: while b != 0
    loop := int32(len(prog))
    prog = append(prog, 0x02) // DUP
    prog = append(prog, jz(endAddr)...)
    
    // ... algorithm ...
    
    prog = append(prog, jmp(loop)...)
    prog = append(prog, 0x1B, 0x1C) // OUT, HALT
    
    // Run it!
    vm := NewVM(prog)
    vm.Run()
}
```

## Helper Functions

The file includes these helpers for building programs:

- **`enc(value int32)`** - Encode 32-bit integer to bytes
- **`push(value int32)`** - Create PUSH instruction
- **`jz(addr int32)`** - Create Jump-if-Zero instruction
- **`jmp(addr int32)`** - Create unconditional Jump instruction

## Building Your Own Examples

Template for creating new examples:

```go
func myExample() {
    fmt.Println("═══ My Example ═══")
    prog := []byte{}
    
    // 1. Initialize values
    prog = append(prog, push(42)...)
    
    // 2. Your algorithm
    // Use opcodes: 0x00=PUSH, 0x01=POP, 0x02=DUP, 0x03=SWAP
    //              0x04=ROLL, 0x05=ROT, 0x06=ADD, etc.
    
    // 3. Output and halt
    prog = append(prog, 0x1B) // OUT
    prog = append(prog, 0x1C) // HALT
    
    // 4. Run
    fmt.Print("Result: ")
    vm := NewVM(prog)
    vm.Run()
    fmt.Println()
}
```

## Opcode Quick Reference

| Hex  | Name      | Action |
|------|-----------|--------|
| 0x00 | PUSH      | Push 32-bit value |
| 0x01 | POP       | Pop and discard |
| 0x02 | DUP       | Duplicate top |
| 0x03 | SWAP      | Swap top two |
| 0x04 | ROLL      | Roll nth element to top |
| 0x05 | ROT       | Rotate top 3 |
| 0x06 | ADD       | Add top two |
| 0x07 | SUB       | Subtract |
| 0x08 | MUL       | Multiply |
| 0x09 | DIV       | Divide |
| 0x0A | MOD       | Modulus |
| 0x0B | INC       | Increment |
| 0x0C | DEC       | Decrement |
| 0x0D | AND       | Bitwise AND |
| 0x0E | OR        | Bitwise OR |
| 0x0F | XOR       | Bitwise XOR |
| 0x10 | NOT       | Bitwise NOT |
| 0x11 | SHL       | Left shift |
| 0x12 | EQ        | Equal? (1 or 0) |
| 0x13 | LT        | Less than? |
| 0x14 | CALLSTACK | Call address from stack |
| 0x15 | JMP       | Jump to address |
| 0x16 | JZ        | Jump if zero |
| 0x17 | CALL      | Call inline address |
| 0x18 | RET       | Return from call |
| 0x19 | LOAD      | Load from inline address |
| 0x1A | STORE     | Store to inline address |
| 0x1B | OUT       | Output value (format + value) |
| 0x1C | HALT      | Stop |
| 0x1D | YIELD     | Yield to host |
| 0x1E | LOADI     | Indirect load (address from stack) |
| 0x1F | STOREI    | Indirect store (address from stack) |
| 0x20 | SHR       | Logical right shift (unsigned, fills with 0s) |
| 0x21 | SAR       | Arithmetic right shift (signed, sign-extends) |
| 0x22 | JNZ       | Jump if non-zero (pops condition, inverse of JZ) |
| 0x23 | NEG       | Negate (multiply by -1) |
| 0x24 | GT        | Greater than |
| 0x25 | NEQ       | Not equal |
| 0x26 | LTE       | Less than or equal |
| 0x27 | GTE       | Greater than or equal |
| 0x28 | PICK      | Pop index n, copy nth stack element (0=top) to top |
| 0x29 | DIVMOD    | Divide and modulus (pushes quotient, then remainder) |
| 0x2A | ABS       | Absolute value |
| 0x2B | MIN       | Minimum of two values |
| 0x2C | MAX       | Maximum of two values |

## Stack Notation

Throughout the examples, stack state is shown as:
- `[a, b, c]` - where `c` is the top of stack
- Operations consume from right (top)
- Results push to right (top)

Example:
```
[10, 20]    // Stack before ADD
ADD
[30]        // Stack after ADD (10 + 20)
```

## Common Patterns

### Loop Pattern
```go
loop := int32(len(prog))
prog = append(prog, 0x02)          // DUP counter
endPH := len(prog)
prog = append(prog, jz(0)...)      // Exit when 0
// ... loop body ...
prog = append(prog, jmp(loop)...)
copy(prog[endPH+1:], enc(int32(len(prog)))) // Patch address
```

### If-Else Pattern
```go
// Condition (pushes 1 or 0)
elsePH := len(prog)
prog = append(prog, jz(0)...)      // If false, goto else
// ... then branch ...
endPH := len(prog)
prog = append(prog, jmp(0)...)
elseAddr := int32(len(prog))
copy(prog[elsePH+1:], enc(elseAddr))
// ... else branch ...
endAddr := int32(len(prog))
copy(prog[endPH+1:], enc(endAddr))
```

## Tips for Success

1. **Track Stack State** - Comment what's on the stack after each operation
2. **Calculate Addresses** - Jump addresses must be computed after the code is placed
3. **Test Incrementally** - Build programs one section at a time
4. **Use ROLL for Peeking** - Copy values without consuming them
5. **Watch for Underflows** - Ensure enough values are on stack for operations

## Try These Challenges

1. **Fibonacci** - Calculate nth Fibonacci number
2. **Prime Check** - Determine if a number is prime
3. **Palindrome** - Check if a number reads same forwards/backwards
4. **Power** - Calculate a^b
5. **Sum of Digits** - Add all digits in a number
6. **Reverse Number** - Reverse the digits of a number

## Notes

- All arithmetic is 32-bit signed integers
- Stack maximum size: 1024 elements
- Program counter is 32-bit (can address 4GB)
- OUT prints numbers without spaces/newlines
- Multiple OUT calls concatenate output

## Sample Output

```
╔══════════════════════════════════════════════════════╗
║      Stack VM Examples Using main.go                 ║
╚══════════════════════════════════════════════════════╝

═══ EXAMPLE 1: GCD (48, 18) ═══
Result: 6 (Expected: 6)

═══ EXAMPLE 2: Even/Odd Check (42) ═══
Result: 0 (0=even, 1=odd; Expected: 0)

...
```


