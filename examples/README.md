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
    prog = append(prog, vm.OpDup)
    prog = append(prog, jz(endAddr)...)
    
    // ... algorithm ...
    
    prog = append(prog, jmp(loop)...)
    prog = append(prog, vm.OutNumber()...)
    prog = append(prog, vm.OpHalt)
    
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
    
    // 2. Your algorithm — always use named constants, never raw hex values
    prog = append(prog, vm.OpDup)
    prog = append(prog, vm.OpAdd)
    
    // 3. Output and halt
    prog = append(prog, vm.OutNumber()...)
    prog = append(prog, vm.OpHalt)
    
    // 4. Run
    fmt.Print("Result: ")
    vm.NewVM(prog).Run()
    fmt.Println()
}
```

## Opcode Quick Reference

The canonical reference is [`docs/opcodes.md`](../docs/opcodes.md). Quick summary:

| Hex  | Name      | Action |
|------|-----------|--------|
| 0x00 | PUSH      | Push 32-bit value (5 bytes) |
| 0x01 | POP       | Pop and discard |
| 0x02 | DUP       | Duplicate top |
| 0x03 | SWAP      | Swap top two |
| 0x04 | OVER      | Copy second-from-top to top |
| 0x05 | ROT       | Rotate top 3 |
| 0x06 | PICK      | Copy nth element (0=top) to top |
| 0x07 | ROLL      | Move nth element to top (destructive) |
| 0x08 | ADD       | Add top two |
| 0x09 | SUB       | Subtract (second - top) |
| 0x0A | MUL       | Multiply |
| 0x0B | DIV       | Divide |
| 0x0C | MOD       | Modulus |
| 0x0D | INC       | Increment |
| 0x0E | DEC       | Decrement |
| 0x0F | NEG       | Negate |
| 0x10 | ABS       | Absolute value |
| 0x11 | DIVMOD    | Divide and modulus (pushes quotient, then remainder) |
| 0x12 | MIN       | Minimum of two values |
| 0x13 | MAX       | Maximum of two values |
| 0x14 | AND       | Bitwise AND |
| 0x15 | OR        | Bitwise OR |
| 0x16 | XOR       | Bitwise XOR |
| 0x17 | NOT       | Bitwise NOT |
| 0x18 | SHL       | Left shift |
| 0x19 | SHR       | Logical right shift (unsigned) |
| 0x1A | SAR       | Arithmetic right shift (signed) |
| 0x1B | EQ        | Equal? (1 or 0) |
| 0x1C | NEQ       | Not equal? |
| 0x1D | LT        | Less than? |
| 0x1E | LTE       | Less than or equal? |
| 0x1F | GT        | Greater than? |
| 0x20 | GTE       | Greater than or equal? |
| 0x21 | JMP       | Jump to address (5 bytes) |
| 0x22 | JZ        | Jump if zero (5 bytes) |
| 0x23 | JNZ       | Jump if non-zero (5 bytes) |
| 0x24 | CALL      | Call inline address (5 bytes) |
| 0x25 | RET       | Return from call |
| 0x26 | CALLSTACK | Call address from stack |
| 0x27 | JMPSTACK  | Jump to address from stack (tail call) |
| 0x28 | LOAD      | Load from inline address (5 bytes) |
| 0x29 | STORE     | Store to inline address (5 bytes) |
| 0x2A | LOADI     | Indirect load (address from stack) |
| 0x2B | STOREI    | Indirect store (address from stack) |
| 0x2C | PUSHR     | Push to loop stack |
| 0x2D | POPR      | Pop from loop stack |
| 0x2E | PEEKR     | Copy top of loop stack |
| 0x2F | PEEKR2    | Copy top two of loop stack |
| 0x30 | FRAME     | Set up local variable frame |
| 0x31 | UNFRAME   | Tear down frame |
| 0x32 | LOCALGET  | Load local variable |
| 0x33 | LOCALSET  | Store local variable |
| 0x34 | OUT       | Output value (format + value) |
| 0x35 | HALT      | Stop |
| 0x36 | YIELD     | Yield to host |

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
loop := vm.UserMemoryOffset + uint32(len(prog))
prog = append(prog, vm.OpDup)      // DUP counter
endPH := len(prog)
prog = append(prog, jz(0)...)      // Exit when 0
// ... loop body ...
prog = append(prog, jmp(loop)...)
endAddr := vm.UserMemoryOffset + int32(len(prog))
copy(prog[endPH+1:], enc(endAddr)) // Patch address
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
4. **Use Over for Peeking** - Copy values without consuming them
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


