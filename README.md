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

## Why C?

NUXVM started life in Go, but the whole system is now pure C:

- Minimal dependencies (a C compiler, pkg-config, and SDL2)
- Full control over memory layout and the framebuffer
- One small `Makefile` builds every tool and test

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
- [Cloister](#cloister)
- [The Why](#the-why)

Guest apps (Quill, Illumos, Tabula, Nib, Easel): **[docs/user-manual.md](docs/user-manual.md)**

---

## Overview

**NUX** is a stack-based virtual machine with a simple instruction set, designed for learning and experimentation. It features:

- **32-bit integer stack** with overflow protection (8192 elements max)
- **Separate return stack** for clean subroutine calls (1024 elements max)
- **55 opcodes** covering stack ops, arithmetic, bitwise, comparisons, and control flow
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

- A C compiler (gcc or clang)
- pkg-config and SDL2 development headers (`brew install sdl2` / `apt install libsdl2-dev`)
- Basic understanding of stack-based computing

### Installation

```bash
# Clone the repository
git clone <repository-url>
cd nuxvm

# Build all tools (bin/nux, bin/luxc, bin/luxrepl, bin/cloister)
make

# Run the test suite
make test
```

### Quick Start

```bash
# Start the interactive REPL
./bin/luxrepl

# Compile a LUX source file to bytecode
./bin/luxc program.lux

# Graphical fantasy machine (picker, or a ROM)
./bin/cloister
./bin/cloister apps/Quill.bin
# Guest app manual: docs/user-manual.md (Quill, Illumos, Tabula, Nib, Easel)

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

| Category       | Word          | Meaning / Alias          |
|----------------|---------------|--------------------------|
| Stack          | DUP           |                          |
| Stack          | DROP          |                          |
| Stack          | SWAP          |                          |
| Stack          | OVER          |                          |
| Stack          | ROT           |                          |
| Stack          | PICK          |                          |
| Stack          | ROLL          |                          |
| Memory         | LOAD          |                          |
| Memory         | STORE         |                          |
| Memory         | LOADI         | indirect load            |
| Memory         | STOREI        | indirect store           |
| Arithmetic     | + / ADD       |                          |
| Arithmetic     | - / SUB       |                          |
| Arithmetic     | * / MUL       |                          |
| Arithmetic     | / / DIV       |                          |
| Arithmetic     | MOD           |                          |
| Arithmetic     | INC           |                          |
| Arithmetic     | DEC           |                          |
| Arithmetic     | NEGATE        |                          |
| Arithmetic     | ABS           |                          |
| Arithmetic     | MIN           |                          |
| Arithmetic     | MAX           |                          |
| Arithmetic     | DIVMOD        | quotient + remainder     |
| Bitwise        | AND           |                          |
| Bitwise        | OR            |                          |
| Bitwise        | XOR           |                          |
| Bitwise        | NOT           |                          |
| Bitwise        | SHL / LSHIFT  | left shift               |
| Bitwise        | SHR / RSHIFT  | logical right shift      |
| Bitwise        | SAR           | arithmetic right shift   |
| Comparison     | = / EQ        |                          |
| Comparison     | < / LT        |                          |
| Comparison     | > / GT        |                          |
| Comparison     | <> / NEQ      | not equal                |
| Comparison     | <= / LTE      |                          |
| Comparison     | >= / GTE      |                          |
| Control Flow   | EXIT          | return from word         |
| Control Flow   | HALT          | stop VM                  |
| Control Flow   | YIELD         | cooperative yield        |
| Control Flow   | JNZ           | jump if non-zero         |
| Combinators    | ?:            | IF-ELSE                  |
| Combinators    | ?             | IF                       |
| Combinators    | !:            | UNLESS                   |
| Combinators    | \|:           | WHILE                    |
| Combinators    | #:            | TIMES                    |
| Combinators    | CALL          |                          |
| Combinators    | DIP           |                          |
| Combinators    | KEEP          |                          |
| Frame/Local    | GIRD name     | bind TOS to a name       |
| Frame/Local    | UNGIRD        | pop named frame          |
| Frame/Local    | { names }     | bind several names       |
| Frame/Local    | FRAME!        | push local frame         |
| Frame/Local    | UNFRAME!      | pop local frame          |
| Frame/Local    | LOCAL@        | read local variable      |
| Frame/Local    | LOCAL!        | write local variable     |
| I/O            | .             | print top of stack       |
| I/O            | EMIT          | print as character       |
| Directives     | MODULE        |                          |
| Directives     | IMPORT        |                          |
| Directives     | INCLUDE       |                          |
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

**Stack Manipulation**

| Hex  | Mnemonic  | Stack Effect | Description |
|------|-----------|--------------|-------------|
| 0x00 | PUSH      | `[] → [value]` | Push 32-bit immediate (5-byte encoding) |
| 0x01 | POP       | `[a] → []` | Discard top of stack |
| 0x02 | DUP       | `[a] → [a, a]` | Duplicate top |
| 0x03 | SWAP      | `[a, b] → [b, a]` | Swap top two |
| 0x04 | OVER      | `[a, b] → [a, b, a]` | Copy second to top |
| 0x05 | ROT       | `[a, b, c] → [b, c, a]` | Rotate top three |
| 0x06 | PICK      | `[... n] → [... stack[n]]` | Copy nth element (0=top) to top |
| 0x07 | ROLL      | `[... n] → [...]` | Move nth element to top (destructive) |

**Arithmetic**

| Hex  | Mnemonic | Stack Effect | Description |
|------|----------|--------------|-------------|
| 0x08 | ADD      | `[a, b] → [a+b]` | Add |
| 0x09 | SUB      | `[a, b] → [a-b]` | Subtract |
| 0x0A | MUL      | `[a, b] → [a*b]` | Multiply |
| 0x0B | DIV      | `[a, b] → [a/b]` | Integer divide |
| 0x0C | MOD      | `[a, b] → [a%b]` | Modulus |
| 0x0D | INC      | `[a] → [a+1]` | Increment |
| 0x0E | DEC      | `[a] → [a-1]` | Decrement |
| 0x0F | NEG      | `[a] → [-a]` | Negate |
| 0x10 | ABS      | `[a] → [\|a\|]` | Absolute value |
| 0x11 | DIVMOD   | `[a, b] → [a/b, a%b]` | Divide and modulus (quotient, then remainder) |
| 0x12 | MIN      | `[a, b] → [min(a,b)]` | Minimum |
| 0x13 | MAX      | `[a, b] → [max(a,b)]` | Maximum |

**Bitwise & Shifts**

| Hex  | Mnemonic | Stack Effect | Description |
|------|----------|--------------|-------------|
| 0x14 | AND      | `[a, b] → [a&b]` | Bitwise AND |
| 0x15 | OR       | `[a, b] → [a\|b]` | Bitwise OR |
| 0x16 | XOR      | `[a, b] → [a^b]` | Bitwise XOR |
| 0x17 | NOT      | `[a] → [~a]` | Bitwise NOT |
| 0x18 | SHL      | `[a, b] → [a<<(b%32)]` | Left shift |
| 0x19 | SHR      | `[a, b] → [a>>>(b%32)]` | Logical right shift (fills with 0) |
| 0x1A | SAR      | `[a, b] → [a>>(b%32)]` | Arithmetic right shift (sign-extends) |

**Comparison**

| Hex  | Mnemonic | Stack Effect | Description |
|------|----------|--------------|-------------|
| 0x1B | EQ       | `[a, b] → [a==b ? 1 : 0]` | Equal |
| 0x1C | NEQ      | `[a, b] → [a!=b ? 1 : 0]` | Not equal |
| 0x1D | LT       | `[a, b] → [a<b ? 1 : 0]` | Less than |
| 0x1E | LTE      | `[a, b] → [a<=b ? 1 : 0]` | Less than or equal |
| 0x1F | GT       | `[a, b] → [a>b ? 1 : 0]` | Greater than |
| 0x20 | GTE      | `[a, b] → [a>=b ? 1 : 0]` | Greater than or equal |

**Control Flow**

| Hex  | Mnemonic  | Stack Effect | Description |
|------|-----------|--------------|-------------|
| 0x21 | JMP       | `[] → []` | Unconditional jump to inline address (5-byte encoding) |
| 0x22 | JZ        | `[cond] → []` | Jump if zero; pops condition (5-byte encoding) |
| 0x23 | JNZ       | `[cond] → []` | Jump if non-zero; pops condition (5-byte encoding) |
| 0x24 | CALL      | `[] → []` | Call inline address; pushes return addr to return stack (5-byte encoding) |
| 0x25 | RET       | `[] → []` | Return from call; pops return stack |
| 0x26 | CALLSTACK | `[addr] → [...]` | Call address from stack (for quotations) |
| 0x27 | JMPSTACK  | `[addr] → []` | Jump to address from stack (tail calls) |

**Memory**

| Hex  | Mnemonic | Stack Effect | Description |
|------|----------|--------------|-------------|
| 0x28 | LOAD     | `[] → [mem[addr]]` | Load from inline address (5-byte encoding) |
| 0x29 | STORE    | `[value] → []` | Store to inline address (5-byte encoding) |
| 0x2A | LOADI    | `[addr] → [mem[addr]]` | Indirect load — pop address, push value |
| 0x2B | STOREI   | `[value, addr] → []` | Indirect store — addr on top, value below |

**Loop Stack**

| Hex  | Mnemonic | Stack Effect | Description |
|------|----------|--------------|-------------|
| 0x2C | PUSHR    | `[a] → []` | Push from main stack to loop stack |
| 0x2D | POPR     | `[] → [a]` | Pop from loop stack to main stack |
| 0x2E | PEEKR    | `[] → [a]` | Copy top of loop stack (non-destructive) |
| 0x2F | PEEKR2   | `[] → [a, b]` | Copy top two of loop stack to main stack |

**Frame & Local Variables**

| Hex  | Mnemonic  | Stack Effect | Description |
|------|-----------|--------------|-------------|
| 0x30 | FRAME     | `[n, v_n...v1] → []` | Save FP, copy n locals into frame (v1 becomes local[0]) |
| 0x31 | UNFRAME   | `[n] → []` | Pop n, restore frame pointer |
| 0x32 | LOCALGET  | `[offset] → [val]` | Load local variable at FP+offset |
| 0x33 | LOCALSET  | `[val, offset] → []` | Store local variable at FP+offset (offset on top) |

**I/O & System**

| Hex  | Mnemonic | Stack Effect | Description |
|------|----------|--------------|-------------|
| 0x34 | OUT      | `[format, value] → []` | Console output (format: 0=number, 1=char) |
| 0x35 | HALT     | — | Stop execution |
| 0x36 | YIELD    | — | Yield to host (calls YieldHandler) |

You can see where I went back and added more op codes because while 32 opcodes, my original plan, was a good idea, it wasn't enough. It's never enough. Scope creep. But now I have it down. *Really*.

Moving on.

### Bytecode Format

All multi-byte values are **big-endian**:

```
PUSH 42:     00 00 00 00 2A
             ^^ opcode
                ^^^^^^^^^^ 32-bit immediate

JMP 0x100:   21 00 00 01 00
             ^^ opcode
                ^^^^^^^^^^ 32-bit address
```

### Writing Bytecode Manually

```c
#include "vm.h"

int main(void) {
    uint8_t program[] = {
        OP_PUSH, 0x00, 0x00, 0x00, 0x05,  // PUSH 5
        OP_PUSH, 0x00, 0x00, 0x00, 0x0A,  // PUSH 10
        OP_ADD,                           // ADD
        OP_PUSH, 0x00, 0x00, 0x00, 0x00,  // format 0 = number
        OP_OUT,                           // OUT
        OP_HALT,                          // HALT
    };

    VM* vm = vm_create(program, sizeof(program), HEADLESS_BASE_ADDRESS,
                       4 * 1024 * 1024, false);
    vm_run(vm);
    vm_free(vm);
    return 0;
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

Runnable programs live in two trees: [`examples/lux/`](examples/lux/) (Forth-style) and [`examples/fluxio/`](examples/fluxio/) (C-like). Both compile to the same NUX bytecode.

### Lux examples

#### Hello World

```forth
"Hello, World!\n"
```

See `examples/lux/hello.lux`.

#### Simple Calculation

```forth
( Calculate (5 + 3) * 2 )
5 3 + 2 * .
( Output: 16 )
```

#### Using Word Definitions

```forth
@square dup * ;
@double 2 * ;

5 square .     ( Output: 25 )
10 double .    ( Output: 20 )

( Compose words )
@quad double double ;
7 quad .       ( Output: 28 )
```

#### Named locals

`GIRD` names the top of the stack. `UNGIRD` takes the name off. `n` reads; `n!` writes.

```forth
5 GIRD n
    n n * .
UNGIRD
( Output: 25 )
```

Several names at once. The `}` in `{ a b }` only ends the name list:

```forth
3 4 { a b }
    a b + .
UNGIRD
( Output: 7 )
```

Inside a word, `{ a b -- sum }` binds the parameters and `;` ungirds for you:

```forth
@add { a b -- sum } a b + ;
3 4 add .     ( Output: 7 )
```

See `examples/lux/gird.lux`.

#### Bitwise Operations

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

#### Module Usage

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

See `examples/lux/modules/` and `examples/lux/modules/MODULE_SYSTEM.md`.

#### Practical REPL Session

Here's a realistic workflow in the Lux REPL (`./bin/luxrepl`):

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

### Fluxio examples

#### Hello World

```c
/** prints "Hello, World!" followed by a newline */
int say_hello() {
    emit(72);  /* H */
    emit(101); /* e */
    emit(108); /* l */
    emit(108); /* l */
    emit(111); /* o */
    emit(44);  /* , */
    emit(32);  /* space */
    emit(87);  /* W */
    emit(111); /* o */
    emit(114); /* r */
    emit(108); /* l */
    emit(100); /* d */
    emit(33);  /* ! */
    emit(10);  /* newline */
    return 0;
}

/** entry point */
int main() {
    say_hello();
    return 0;
}
```

See `examples/fluxio/hello_console.fx`. Compile, then run:

```bash
./bin/fluxioc -target headless -o examples/fluxio/hello_console.bin examples/fluxio/hello_console.fx
./bin/nux examples/fluxio/hello_console.bin
```

#### Simple Calculation

```c
/** entry point */
int main() {
    print((5 + 3) * 2);  /* Output: 16 */
    return 0;
}
```

#### Using Functions

```c
/** n squared */
int square(int n) { return n * n; }

/** n times two */
int double_n(int n) { return n * 2; }

/** compose double twice */
int quad(int n) { return double_n(double_n(n)); }

/** entry point */
int main() {
    print(square(5));     /* 25 */
    emit(10);
    print(double_n(10));  /* 20 */
    emit(10);
    print(quad(7));       /* 28 */
    emit(10);
    return 0;
}
```

#### Bitwise Operations

```c
/** 1 if n is even, else 0 */
int is_even(int n) { return n % 2 == 0; }

/** entry point */
int main() {
    print(0xFF & 0x0F);  /* 15 */
    emit(10);
    print(12 | 10);      /* 14 */
    emit(10);
    print(5 << 2);       /* 20 */
    emit(10);
    print(is_even(10));  /* 1 */
    emit(10);
    print(is_even(7));   /* 0 */
    emit(10);
    return 0;
}
```

#### Includes (no modules)

Fluxio has no `MODULE`/`IMPORT` namespacing — it is a single flat global namespace, so related helpers live in another file and are pulled in with `include`:

```c
include "include_lib/mathlib.fx";

/** entry point */
int main() {
    int m = fx_max(17, 42);
    int f = fx_factorial(5);
    return m + f; /* 42 + 120 = 162 */
}
```

See `examples/fluxio/include_demo.fx` and `examples/fluxio/include_lib/mathlib.fx`.

There is no Fluxio REPL — compile with `fluxioc`, then run the `.bin` under `nux` or `cloister`. Other Fluxio demos (bounded recursion, arrays, structs, fixed-point floats, Cloister drawing) are listed in [`examples/fluxio/README.md`](examples/fluxio/README.md).

---

## Development

### Project Structure

```
nuxvm/
├── apps/           - Sample Lux applications
├── docs/           - Extended documentation
├── examples/
│   ├── lux/        - Lux (Forth-style) example programs
│   └── fluxio/     - Fluxio (C-like) example programs
├── include/        - C headers
│   ├── vm.h        - Core VM
│   ├── opcodes.h   - Opcode definitions
│   ├── system.h    - Hardware abstraction layer
│   ├── vfs.h       - Virtual filesystem
│   ├── compiler.h  - Lux compiler
│   └── lexer.h     - Tokenizer
├── lib/            - Lux standard library
│   ├── core.lux
│   ├── draw.lux
│   ├── file.lux
│   ├── log.lux
│   ├── memory.lux
│   ├── time.lux
│   └── vfs.lux
├── src/
│   ├── vm.c        - Core VM interpreter
│   ├── system.c    - SCI trap (VFS syscalls) + framebuffer
│   ├── vfs.c       - Virtual filesystem
│   ├── machine.c   - Machine (CPU + System)
│   ├── lexer.c     - Tokenizer
│   ├── compiler.c  - Bytecode compiler
│   ├── dialog.c    - File dialog modal
│   ├── nux.c       - VM console runner
│   ├── luxc.c      - Lux compiler CLI
│   ├── repl.c      - Interactive REPL
│   ├── cloister.c  - Graphical tiny OS (SDL2)
│   └── test_*.c    - Test suites
├── resources/      - Static assets (fonts, etc.)
├── Makefile
└── README.md
```

### Running Tests

```bash
# Build everything and run all test suites (VM, VFS, compiler)
make test
```

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

# Cloister

Cloister is the graphical host for NUX — a Varvara-shaped fantasy machine, not a multi-app OS. One program owns the screen, `/dev/draw`, `/dev/mouse`, and `/dev/kbd`. Run `./bin/cloister` to pick a ROM: the picker is itself a Lux app (`apps/Picker.lux`) with two group-box columns, Lux sources on the left (compiled on the fly) and compiled Fluxio bins from `apps/fluxio/` on the right. **Cloister > About Cloister** is a modal info box; **Cloister > Quit** leaves the host. Pass a `.bin` / `.lux` path to boot that ROM directly. Esc is Continue / Restart / Quit inside an app (and closes About on the picker).

The five guest apps — **Quill** (text), **Illumos** (bitmap fonts), **Tabula** (spreadsheet), **Nib** (object drawing), and **Easel** (pixel painting) — are documented in **[docs/user-manual.md](docs/user-manual.md)**. Menus and buttons are Chicago. Quill, Tabula, and Nib have **Font > Chicago / Geneva / Monaco** for document text.

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
A: Yes! The VM is plain C with no dependencies outside the standard library. Link `src/vm.c` and feed it bytecode.

**Q: Is LUX Turing complete?**  
A: Yes, with word definitions, conditionals (via jumps), and recursion.

---

# Versioning

I'm using the Kelvin versioning system as defined here: https://jtobin.io/kelvin-versioning 

Currently at 280K.

---

**Happy hacking!**
