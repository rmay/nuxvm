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
- [The Why](#the-why)

---

## Overview

**NUX** is a 45-opcode stack-based virtual machine with a simple instruction set, designed for learning and experimentation. It features:

- **32-bit integer stack** with overflow protection (8192 elements max)
- **Separate return stack** for clean subroutine calls (1024 elements max)
- **45 opcodes** covering stack ops, arithmetic, bitwise, comparisons, and control flow
- **Big-endian bytecode** format
- **Memory-mapped program and data space**

**LUX** is a Forth-inspired high-level language that compiles to NUX bytecode. It provides:

- **Reverse Polish Notation** (postfix) syntax
- **User-defined words** (functions)
- **Module system** with namespacing and imports
- **File inclusion** via `INCLUDE`
- **Hex and decimal literals**
- **String output** support
- **Interactive REPL** for rapid prototyping

---

## Architecture

### NUX Virtual Machine

NUX uses a dual-stack architecture:

- **Data Stack**: Primary stack for computation (32-bit signed integers, max 8192)
- **Return Stack**: Dedicated stack for subroutine return addresses (max 1024)
- **Memory**: Unified space for program code and runtime data (default 32MB in cloister)
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
go build -o bin/cloister cmd/cloister/main.go
go build -o bin/luxc cmd/luxc/main.go
go build -o bin/luxrepl cmd/luxrepl/main.go

# Or use go install
go install ./cmd/nux
go install ./cmd/cloister
go install ./cmd/luxc
go install ./cmd/luxrepl
```

### Quick Start

```bash
# Start the interactive REPL
./bin/luxrepl

# Compile a LUX source file to bytecode
./bin/luxc program.lux

# Run the compiled bytecode in the graphical emulator
./bin/cloister program.bin

# Run the compiled bytecode in the console runner
./bin/nux program.bin
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

### File Inclusion

```forth
INCLUDE "lib/system.lux"
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
over          ( Copy second: a b → a b a )
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

( Define fact-iter )
@fact-iter 1 swap dup [ dup rot * swap 1 - ] swap #: drop ;

( Define factor recursive)
@fact-rec dup 1 > [ dup 1 - fact-rec * ] ? ;

5 square .        ( Output: 25 )
3 cube .          ( Output: 27 )
5 fact-iter .     ( Outputs 120 )
5 fact-rec .      ( Outputs 120 )
```

**Note**: Word definitions are compiled first, then the main program code runs. Idiomatic names can include symbols like `vector!` or `key@`.

### Reserved symbols and words

| Category       | Word     | Meaning|
|----------------|----------|--|
| Stack Operations | DUP     ||
| Stack Operations | DROP    ||
| Stack Operations | SWAP    ||
| Stack Operations | OVER    ||
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
| Directives     | INCLUDE ||
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

| Hex  | Mnemonic  | Stack Effect | Description |
|------|-----------|--------------|-------------|
| 0x00 | PUSH      | `[] → [value]` | Push 32-bit immediate value (5 bytes) |
| 0x01 | POP       | `[a] → []`         | Discard top of stack |
| 0x02 | DUP       | `[a] → [a a]`   | Duplicate top |
| 0x03 | SWAP      | `[a b] → [b a]`  | Swap top two |
| 0x04 | OVER      | `[a b] → [a b a]` | Over nth element to top |
| 0x05 | ROT       | `[a b c] → [b c a]` | Rotate top three |
| 0x06 | ADD       | `[a b] → [a + b]` | Add |
| 0x07 | SUB       | `[a b] → [a - b]` | Subtract |
| 0x08 | MUL       | `[a b] → [a * b]` | Multiply |
| 0x09 | DIV       | `[a b] → [a / b]` | Integer divide |
| 0x0A | MOD       | `[a b] → [a % b]` | Modulus |
| 0x0B | INC       | `[a] → [a + 1]`   | Increment |
| 0x0C | DEC       | `[a] → [a - 1]`   | Decrement |
| 0x0D | AND       | `[a b] → [a & b]` | Bitwise AND |
| 0x0E | OR        | `[a b] → [a \| b]` | Bitwise OR |
| 0x0F | XOR       | `[a b] → [a ^ b]` | Bitwise XOR |
| 0x10 | NOT       | `[a] → [~a]` | Bitwise NOT |
| 0x11 | SHL       | `[a b] → [a<<b]` | Left shift (b mod 32) |
| 0x12 | EQ        | `[a b] → [a==b ? 1 : 0]` | Equal (1 or 0) |
| 0x13 | LT        | `[a b] → [a<b ? 1 : 0]` | Less than |
| 0x14 | CALLSTACK | `[addr] → [...]` | Pop address, push return addr to return stack, jump (for quotations) |
| 0x15 | JMP       | `[] → []`  | Unconditional jump to address (5 bytes) |
| 0x16 | JZ        | `[cond] → []` | Jump if zero (pops condition) |
| 0x17 | CALL      | `[] → []` | Call subroutine at inline address (pushes return addr to return stack) |
| 0x18 | RET       | `[] → []` | Return from subroutine (pops return stack) |
| 0x19 | LOAD      | `[] → [mem[addr]]` | Load from inline address (5 bytes) |
| 0x1A | STORE     | `[value] → []` | Store to inline address (5 bytes) |
| 0x1B | OUT       | `[format value] → []` | Output value (format: 0=number, 1=char) |
| 0x1C | HALT      | --    | Stop execution |
| 0x1D | YIELD     | --    | Yield to host (calls YieldHandler) |
| 0x1E | LOADI     | `[addr] → [mem[addr]]` | Indirect load — pop address, push value |
| 0x1F | STOREI    | `[addr value] → []` | Indirect store — pop address and value, store |
| 0x20 | SHR       | `[a b] → [a>>>(b%32)]` | Logical right shift (unsigned, fills with 0s) |
| 0x21 | SAR       | `[a b] → [a>>(b%32)]` | Arithmetic right shift (signed, sign-extends) |
| 0x22 | JNZ       | `[cond] → []` | Jump if non-zero (pops condition, inverse of JZ) |
| 0x23 | NEG       | `[a] → [-a]` | Negate (multiply by -1) |
| 0x24 | GT        | `[a b] → [a>b ? 1 : 0]` | Greater than |
| 0x25 | NEQ       | `[a b] → [a!=b ? 1 : 0]` | Not equal |
| 0x26 | LTE       | `[a b] → [a<=b ? 1 : 0]` | Less than or equal |
| 0x27 | GTE       | `[a b] → [a>=b ? 1 : 0]` | Greater than or equal |
| 0x28 | PICK      | `[...n] → [...]` | Pop index n, copy nth stack element (0=top) to top |
| 0x29 | DIVMOD    | `[a b] → [a/b a%b]` | Divide and modulus (pushes quotient, then remainder) |
| 0x2A | ABS       | `[a] → [\|a\|]` | Absolute value |
| 0x2B | MIN       | `[a b] → [min(a,b)]` | Minimum of two values |
| 0x2C | MAX       | `[a b] → [max(a,b)]` | Maximum of two values |

You can see where I went back and added more op codes because while 32 opcodes, my original plan, was a good idea, it wasn't enough. It's never enough. Scope creep. But now I have it down. *Really*.

Moving on.

### Bytecode Format

All multi-byte values are **big-endian**:

```
PUSH 42:     00 00 00 00 2A
             ^^ opcode
                ^^^^^^^^^^ 32-bit immediate

JMP 0x100:   15 00 00 01 00
             ^^ opcode
                ^^^^^^^^^^ 32-bit address
```

### Writing Bytecode Manually

```go
package main

import "github.com/rmay/nuxvm/pkg/vm"

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

### 3. nux - NUXVM Console Runner

Executes NUXVM bytecode:

```bash
# Normal execution
./bin/nux program.bin
```
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
│   ├── nux/        - VM console runner
│   ├── cloister/   - Graphical tiny os
│   ├── luxc/       - LUX compiler
│   └── luxrepl/    - Interactive REPL
├── lib/
│   ├── system.lux  - System library (MMIO helpers)
│   └── boot.lux    - Default boot program
├── pkg/
│   ├── vm/         - Virtual machine implementation
│   │   ├── vm.go       - Core VM
│   │   ├── opcodes.go  - Opcode definitions
│   │   └── vm_test.go  - VM tests
│   ├── system/     - Hardware abstraction
│   │   ├── machine.go  - Machine (CPU + System)
│   │   └── system.go   - MMIO and devices
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

- Maximum stack depth: **8192 elements**
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

# The Why

Okay, this is all great and all, but why?


## Computer stewardship

**Credo:**
- God owns all; we are entrusted with His creation.
- Build not for the current cycle but with an eye toward the future.
- Utilize resources wisely.
- Accept limits.
- Minimize friction.
- Reuse when prudent.
- Purchase within your means.
- Do things with humility and wonder.


I started NUXVM as a side project: I wanted to explore small stack-based virtual machines and improve my skills in Go.

Since I had no real destination, scope creep became my lodestone. Because I kept thinking it would be neat to add different features, I started leveraging LLMs to try out different approaches and implementations. The project grew. 

I was slowly building a tiny computer. I leaned into it and kept going. It became a learning experience for me as I tried out different concepts, and seeing how some decisions played out, forcing me to rethink some assumptions. I’m drawing heavily on the Mac/SE System OS era for inspiration.

As I progressed while building this, something was tugging at me: the why of it all. It wasn’t just an experiment run wild, I was making specific choices to constrain and expand the system—dual stacks to handle instructions and returns to keep the surface small, while audio and video expand to help people feel the system.

People should be able to understand their tools, as deeply as they choose to dive into the mechanisms. Something that can be understood can be owned in different ways than mere possession. The project is something you can download and own completely. It’s small enough to run on modest hardware while being big enough for your creativity. 

Despite all of the modern world’s hustle and bustle—or maybe because of it all—I’m a medievalist at heart and soul. I like to build things, categorize, and think giving glory to God is right and just. The modern world is endlessly fascinating and distracting. This is, too, but it exists under your care.

This project grew from that spirit, and I drew upon inspiration from many different sources, modern and ancient, from cathedrals to Smalltalk/Lisp/Forth to agriculture—and many, many points between.

As such, this project is very much experimental as it should be. This isn’t a building, but a personal garden, and gardens are always undergoing growth, replanting, fallow periods, death, and rebirth.

I’m not standing against the world, against tides and trends, opposing this philosophy or the other. I am, however, sharing something small that I have enjoyed making and I hope you do, too.

---

## Acknowledgments

- Inspired by **Forth** and other stack-based languages
- Test suite written with the help of Claude Sonnet 4.5 and Gemini 3.
- Code written by me, but enhanced and expanded through using Grok 3.5, Claude Sonnet 4.5, and Gemini 3.
- Designed for learning and experimentation
- Documentation rewritten by Claude Sonnet 4.5 and Gemini 3.
- The boring bits are from my faithful robotic servants.

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

Currently at 280K.

---

**Happy hacking!**
