# NUX & LUX

A lightweight stack-based virtual machine (NUXVM) and its companion high-level language (LUX) — designed for simplicity, education, and extensibility.

> *nux* Latin f noun; third declension

> **a nut**

Out of all the small VMs out there, this one is one of them.

I wanted to do something besides CRUD projects, and writing a VM seemed like something I thought would be easy to do.

After reading blogs and looking at other VMs, I kicked some ideas around with a few LLMs. I started out with just a simple VM, then decided what I really needed was scope creep.

> *lux* Latin f noun; third declension

> **daylight**

Lux is the higher-level language that generates the Nux opcodes. I drew heavily on Forth for inspiration and took everything not nailed down.

While I did write code, I also argued with LLMs, especially Grok and Claude, to clean up, extend, expand, and fix my mistakes. An infinite number of monkeys would be hard pressed to beat what the vector math did. 

## Why Go?

I picked Go over something like C because of:

- Simplicity
- Performance
- Cross-Platform
- Memory Management -- And this was the most import reason for me.

---

## Table of Contents

- [Overview](#overview)
- [Architecture](#architecture)
- [Getting Started](#getting-started)
- [LUX Language Guide](#lux-language-guide)
- [NUXVM Bytecode](#nuxvm-bytecode)
- [Tools](#tools)
- [Examples](#examples)
- [Module System](#module-system)
- [Development](#development)

---

## Overview

**NUX** is a 32-opcode stack-based virtual machine with a simple instruction set, designed for learning and experimentation. It features:

- **32-bit integer stack** with overflow protection (8192 elements max, which is 32KB, with 4KB reserved)
- **Separate return stack** for clean subroutine calls
- **32 opcodes** covering stack ops, arithmetic, bitwise, comparisons, control flow, and I/O
- **Big-endian bytecode** format
- **Memory-mapped program and data space**

**LUX** is a Forth-inspired high-level language that compiles to NUX bytecode. It provides:

- **Reverse Polish Notation** (postfix) syntax
- **User-defined words** (functions)
- **Module system** with namespacing and imports
- **Hex and decimal literals**
- **String output** support
- **Interactive REPL** for rapid prototyping

---

## Architecture

### NUX Virtual Machine

NUX uses a dual-stack architecture:

- **Data Stack**: Primary stack for computation (32-bit signed integers)
- **Return Stack**: Dedicated stack for subroutine return addresses
- **Memory**: Unified space for program code and runtime data
- **Program Counter (PC)**: 32-bit address pointer

### Execution Model

1. Fetch instruction at PC
2. Decode opcode
3. Execute operation (may manipulate stacks, memory, or PC)
4. Advance PC (or jump for control flow)
5. Repeat until HALT or error

---

## Getting Started

### Prerequisites

- Go 1.25+ (for building from source)
- Basic understanding of stack-based computing

### Installation

```bash
# Clone the repository
git clone <repository-url>
cd nuxvm

# Build all tools
go build -o bin/nux cmd/nux/main.go
go build -o bin/luxc cmd/luxc/main.go
go build -o bin/luxrepl cmd/luxrepl/main.go

# Or use go install
go install ./cmd/nux
go install ./cmd/luxc
go install ./cmd/luxrepl
```

### Quick Start

```bash
# Start the interactive REPL
./bin/luxrepl

# Compile a LUX source file to bytecode
./bin/luxc program.lux

# Run the compiled bytecode
./bin/nux program.bin

# Run with debugging
./bin/nux --debug program.bin

# Run with execution trace
./bin/nux --trace program.bin
```

---

## LUX Language Guide

LUX uses postfix notation where operators follow their operands. Data flows through a stack.

### Basic Syntax

```forth
5 10 +        ( Push 5, push 10, add → stack: [15] )
7 dup *       ( Push 7, duplicate, multiply → stack: [49] )
42 .          ( Push 42, print as number → output: 42 )
72 emit       ( Push 72, print as character → output: H )
```

### Numbers

```forth
42            ( Decimal )
-17           ( Negative )
0xFF          ( Hexadecimal )
0x10          ( Also hex )
```

### Stack Operations

```forth
dup           ( Duplicate top: a → a a )
drop          ( Remove top: a → )
swap          ( Swap top two: a b → b a )
roll          ( Copy second: a b → a b a )
rot           ( Rotate three: a b c → b c a )
```

### Arithmetic

```forth
+ - * /       ( Add, subtract, multiply, divide )
mod           ( Modulus )
inc dec       ( Increment, decrement )
negate        ( Negate value )
```

### Bitwise Operations

```forth
and or xor    ( Bitwise AND, OR, XOR )
not           ( Bitwise NOT )
lshift        ( Left shift )
```

### Comparisons

```forth
=             ( Equal )
<             ( Less than )
>             ( Greater than )
!=            ( Not equal )
```

Results: 1 for true, 0 for false

### Output

```forth
.             ( Print top of stack as number )
emit          ( Print top of stack as ASCII character )
"Hello"       ( Print string literal )
```

### Comments

```forth
( This is a comment )
5 10 +  ( Inline comment )

// This is also a comment
// Line comments work too
```

### Word Definitions

Define reusable functions with `@name ... ;`

```forth
@square dup * ;

@cube dup square * ;

( NOT DONE YET )
@factorial
    dup 1 >
    ( n -- n! )
    dup 1 - factorial *
;

5 square .     ( Output: 25 )
3 cube .       ( Output: 27 )
```

**Note**: Word definitions are compiled first, then the main program code runs.

### Reserved symbols and words

| Category       | Word     | Meaning|
|----------------|----------|--|
| Stack Operations | DUP     ||
| Stack Operations | DROP    ||
| Stack Operations | SWAP    ||
| Stack Operations | ROLL    ||
| Stack Operations | ROT     ||
| Arithmetic     | +       ||
| Arithmetic     | -       ||
| Arithmetic     | *       ||
| Arithmetic     | /       ||
| Arithmetic     | MOD     ||
| Arithmetic     | INC     ||
| Arithmetic     | DEC     ||
| Arithmetic     | NEGATE  ||
| Bitwise        | AND     ||
| Bitwise        | OR      ||
| Bitwise        | XOR     ||
| Bitwise        | NOT     ||
| Bitwise        | LSHIFT  ||
| Comparison     | =       ||
| Comparison     | <       ||
| Comparison     | >       ||
| Control Flow   | EXIT    ||
| Combinators    | ?:      | IF-ELSE |
| Combinators    | ?       | IF |
| Combinators    | !:      | UNLESS |
| Combinators    | \|:      | WHILE |
| Combinators    | #:      | TIMES |
| Combinators    | CALL    ||
| Combinators    | DIP     ||
| Combinators    | KEEP    ||
| Directives     | MODULE  ||
| Directives     | IMPORT  ||
---

## Module System

LUX supports organizing code into modules for better structure and namespacing.

### Defining Modules

```forth
MODULE MATH
@square dup * ;
@cube dup square * ;

MODULE GEOMETRY  
@area-square MATH::SQUARE ;
@volume-cube MATH::CUBE ;
```

### Using Modules

```forth
( Qualified access )
5 MATH::SQUARE .          ( Output: 25 )

( Import with shorthand )
MODULE MAIN
IMPORT MATH AS M
10 M::SQUARE .            ( Output: 100 )

( Within a module, local words don't need qualification )
MODULE MATH
@double 2 * ;
@quadruple double double ;
```

### Module Resolution

The compiler resolves words in this order:

1. Exact match (fully qualified: `MODULE::WORD`)
2. Current module prefix (if unqualified and in a module)
3. Import shorthand resolution (if using `AS` alias)
4. Built-in words

### Module Best Practices

- Use UPPER_CASE for module names
- Organize related functionality into modules
- Use imports to make code more readable
- Avoid circular dependencies

---

## NUXVM Bytecode

### Opcode Reference

| Hex  | Mnemonic | Stack Effect | Description |
|------|----------|--------------|-------------|
| 0x00 | PUSH     | `[] → [value]` | Push 32-bit immediate value |
| 0x01 | POP      | `[a] → []`         | Discard top of stack |
| 0x02 | DUP      | `[a] → [a a]`   | Duplicate top |
| 0x03 | SWAP     | `[a b] → [b a]`  | Swap top two |
| 0x04 | ROLL     | `[a b] → [a b a]` | Copy second to top |
| 0x05 | ROT      | `[a b c] → [b c a]` | Rotate top three |
| 0x06 | ADD      | `[a b] → [a + b]` | Add |
| 0x07 | SUB      | `[a b] → [a - b]` | Subtract |
| 0x08 | MUL      | `[a b] → [a * b]` | Multiply |
| 0x09 | DIV      | `[a b] → [a / b]` | Integer divide |
| 0x0A | MOD      | `[a b] → [a % b]` | Modulus |
| 0x0B | INC      | `[a] → [a + 1]`   | Increment |
| 0x0C | DEC      | `[a] → [a - 1]`   | Decrement |
| 0x0D | NEG      | `[a] → [-a]`    | Negate |
| 0x0E | AND      | `[a b] → [a & b]` | Bitwise AND |
| 0x0F | OR       | `[a b] → [a \| b]` | Bitwise OR |
| 0x10 | XOR      | `[a b] → [a ^ b]` | Bitwise XOR |
| 0x11 | NOT      | `[a] → [~a]` | Bitwise NOT |
| 0x12 | SHL      | `[a b] → [a<<b]` | Left shift (b mod 32) |
| 0x13 | EQ       | `[a, b] → [a == b ? 1 : 0]`| Equal (1 or 0) |
| 0x14 | LT       | `[a, b] → [a < b ? 1 : 0]` | Less than |
| 0x15 | GT       | `[a, b] → [a > b ? 1 : 0]` | Greater than |
| 0x16 | CALLSTACK | `[addr] → [result]` | Pop address, push return address to return stack, jump to address (for calling quotations) |
| 0x17 | JMP      | `[] → []`  | Unconditional jump to address |
| 0x18 | JZ       | `[cond] → []` | Jump if zero |
| 0x19 | JNZ      | `[cond] → []` | Jump if non-zero |
| 0x1A | CALL     | `[] → [ret_addr]` | Call subroutine (pushes return address to return stack) |
| 0x1B | RET      | `[addr] → []` | Return from subroutine (pops from return stack) |
| 0x1C | LOAD     | `[] → [mem[addr]]` | Load from memory address |
| 0x1D | STORE    | `[value] → []` | Store to memory address |
| 0x1E | OUT      | `[format n]  → []` | Output value (format: 0=number, 1=char) |
| 0x1F | HALT     | --    | Stop execution |

### Bytecode Format

All multi-byte values are **big-endian**:

```
PUSH 42:     00 00 00 00 2A
             ^^ opcode
                ^^^^^^^^^^ 32-bit immediate

JMP 0x100:   17 00 00 01 00
             ^^ opcode
                ^^^^^^^^^^ 32-bit address
```

### Writing Bytecode Manually

```go
package main

import "vapor.solarvoid.com/russell/nuxvm/pkg/vm"

func main() {
    program := []byte{}
    
    // PUSH 5
    program = append(program, vm.PushInstruction(5)...)
    
    // PUSH 10
    program = append(program, vm.PushInstruction(10)...)
    
    // ADD
    program = append(program, vm.OpAdd)
    
    // OUT (format: 0 = number)
    program = append(program, vm.OutNumber()...)
    
    // HALT
    program = append(program, vm.OpHalt)
    
    machine := vm.NewVM(program)
    machine.Run()
}
```

---

## Tools

### 1. luxrepl - Interactive REPL

A REPL? In this economy?

Yes, I've spared no effort. This is a luxury tiny VM.

An interactive environment for experimenting with LUX:

```bash
./bin/luxrepl
```

You can also use rlwrap.

```bash
rlwrap ./bin/luxrepl
```

**Features:**
- Persistent stack across commands
- Word definitions persist
- History tracking
- Built-in commands

**REPL Commands:**

```
help, ?          Show help
exit, quit, q    Exit REPL
clear, reset     Clear word definitions
clearstack, cs   Clear the stack
stack, .s        Show current stack
drop             Drop top stack value
words            List defined words
history          Show definition history
```

**Example Session:**

```
lux> 5
  Stack: [5]

lux> 10
  Stack: [5 10]

lux> +
  Stack: [15]

lux> @double dup + ;
Defined word 'DOUBLE'

lux> 21 double
  Stack: [42]

lux> .s
  Stack: [42]
```

### 2. luxc - LUX Compiler

Compiles LUX source files to NUXVM bytecode:

```bash
./bin/luxc program.lux
# Creates program.bin
```

### 3. nux - NUXVM Runner

Executes NUXVM bytecode:

```bash
# Normal execution
./bin/nux program.bin

# Debug mode (step-by-step)
./bin/nux --debug program.bin

# Trace mode (show each instruction)
./bin/nux --trace program.bin
```

**Debug Mode:**
- Press Enter to step through instructions
- Type `c` to continue without stepping
- Type `q` to quit
- View PC and stack state at each step

**Trace Mode:**
- Shows PC and stack state before each instruction
- Useful for understanding program flow

---

## Examples

### Example 1: Hello World

```forth
"Hello, World!\n"
```
### Example 2: Simple Calculation

```forth
( Calculate (5 + 3) * 2 )
5 3 + 2 * .
( Output: 16 )
```

### Example 3: Using Word Definitions

```forth
@square dup * ;
@double 2 * ;

5 square .     ( Output: 25 )
10 double .    ( Output: 20 )

( Compose words )
@quad double double ;
7 quad .       ( Output: 28 )
```

### Example 4: Bitwise Operations

```forth
( Binary calculations )
0xFF 0x0F and .   ( Output: 15, binary: 1111 )
12 10 or .        ( Output: 14 )
5 2 lshift .      ( Output: 20, shift left by 2 )

( Using in word definitions )
@is-even 2 mod 0 = ;
10 is-even .      ( Output: 1 for true )
7 is-even .       ( Output: 0 for false )
```

### Example 5: Module Usage

```forth
MODULE MATH
@square dup * ;
@cube dup dup * * ;
@power4 square square ;

MODULE SHAPES
IMPORT MATH AS M
@area-circle 
    ( radius -- area )
    M::SQUARE 
    314 * 100 /    ( π ≈ 3.14 )
;

MODULE MAIN
IMPORT MATH
IMPORT SHAPES

5 MATH::SQUARE .        ( 25 )
3 MATH::CUBE .          ( 27 )
10 SHAPES::AREA-CIRCLE . ( 314 )
```

### Example 6: Practical REPL Session

Here's a realistic workflow in the REPL:

```forth
lux> @double 2 * ;
Defined word 'DOUBLE'

lux> @triple 3 * ;
Defined word 'TRIPLE'

lux> 10 double
  Stack: [20]

lux> 5 triple
  Stack: [20 15]

lux> +
  Stack: [35]

lux> .
35  Stack: []

lux> @hypotenuse dup * swap dup * + ;
Defined word 'HYPOTENUSE'

lux> 3 4 hypotenuse
  Stack: [25]

lux> ( that's 3² + 4² = 9 + 16 = 25 )

lux> .s
  Stack: [25]
```

---

## Development

### Project Structure

```
nuxvm/
├── cmd/
│   ├── nux/        - VM runner
│   ├── luxc/       - LUX compiler
│   └── luxrepl/    - Interactive REPL
├── pkg/
│   ├── vm/         - Virtual machine implementation
│   │   ├── vm.go       - Core VM
│   │   ├── opcodes.go  - Opcode definitions
│   │   └── vm_test.go  - VM tests
│   └── lux/        - LUX language implementation
│       ├── lexer.go    - Tokenizer
│       ├── compiler.go - Bytecode compiler
│       └── *_test.go   - Tests
└── README.md
```

### Running Tests

```bash
# Test the VM
go test ./pkg/vm -v

# Test the compiler
go test ./pkg/lux -v

# Test everything
go test ./... -v

# Run with coverage
go test ./... -cover
```

### Makefile

The Makefile contains many shortcuts to doing the above commands.

### Contributing

Contributions welcome! Areas for improvement:

- **Language features**: loops, conditionals
- **Standard library**: more built-in words
- **Optimization**: bytecode optimization passes
- **Debugging**: better error messages, source maps
- **Documentation**: more examples and tutorials

---

## Technical Details

### Stack Size Limits

- Maximum stack depth: **1024 elements**
- Maximum return stack depth: **1024 elements**
- Stack overflow causes runtime error

### Integer Arithmetic

- All integers are **32-bit signed** (-2,147,483,648 to 2,147,483,647)
- Overflow/underflow wraps around (two's complement)
- Division by zero causes runtime error

### Memory Model

- Program and data share the same memory space
- Memory is byte-addressed
- LOAD/STORE use 32-bit addresses
- Out-of-bounds access causes runtime error

### Performance

- Interpreted bytecode (no JIT)
- Stack operations are very fast
- Subroutine calls use return stack (efficient)
- Suitable for educational purposes and small programs

---

## Acknowledgments

- Inspired by **Forth** and other stack-based languages
- Test suite written with the help of Claude Sonnet 4.5
- Code written by me, but enhanced and expanded through using Grok 3.5 and Claude Sonnet 4.5
- Designed for learning and experimentation
- Documentation rewritten by Claude Sonnet 4.5

---

## FAQ

**Q: Why stack-based?**  
A: Stack machines are simple, have minimal syntax, and teach fundamental CS concepts.

**Q: Can I embed NUXVM in other programs?**  
A: Yes! The VM is a pure Go package. Import it and feed it bytecode.

**Q: Is LUX Turing complete?**  
A: Yes, with word definitions, conditionals (via jumps), and recursion.

---

# Versioning

I'm using the Kelvin versioning system as defined here: https://jtobin.io/kelvin-versioning 

Currently at 300K.

---

**Happy hacking!**
