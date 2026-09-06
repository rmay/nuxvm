# NUX

A simple stack-based virtual machine written in C, with two front-end languages: **Lux** (Forth-style, concatenative) and **Fluxio** (C-like, imperative).

## Features

- 55 opcodes, frozen, with a normative operational semantics
- 32-bit signed integer operations, wrapping and total — only division by zero traps
- Big-endian bytecode; big-endian, word-aligned memory
- 8192-slot data stack, plus separate return, loop and locals stacks
- Jump instructions and subroutines
- Memory load/store operations
- A test suite, a differential model, and machine-checked safety proofs

## Installation

```bash
# From the repo root; needs a C compiler. Cloister also needs pkg-config and SDL2.
make
```

## Usage

### Run a program
```bash
./bin/nux program.bin      # precompiled bytecode
./bin/nux program.lux      # compiles on the fly
```

### Debug mode (step-by-step execution)
```bash
./bin/nux -debug program.bin
```

### Trace mode (show all steps)
```bash
./bin/nux -trace program.bin
```

### Run a Lux example
```bash
./bin/luxc -target headless examples/lux/modules/module_basic.lux
./bin/nux examples/lux/modules/module_basic.bin
```

### Compile and run a Fluxio example
```bash
./bin/fluxioc -target headless -o examples/fluxio/fib.bin examples/fluxio/fib.fx
./bin/nux examples/fluxio/fib.bin
```

### Run tests
```bash
make test
```

`make test` runs the VFS, VM opcode, Lux compiler, Fluxio compiler, ABI and
ROM suites; re-runs the opcode tests under ASan + UBSan; differential-tests
the interpreter against an independent model; and recompiles every app image
to check it reproduces bit-for-bit.

### Check the interpreter against its specification

[`semantics.md`](semantics.md) is the normative definition of the machine.
Three commands keep `src/vm.c` honest to it, in increasing order of strength:

```bash
make check-docs   # opcodes.md agrees with opcodes.h and vm.h
make ubsan-vm     # opcode tests under AddressSanitizer + UndefinedBehaviorSanitizer
make difftest     # random programs through the C VM and an independent model
make verify       # prove safety properties over one step, for all inputs
```

`make check-docs` compares [opcodes.md](opcodes.md) against the headers:
every opcode has a detail section and a table row, names and numbers agree,
instruction lengths are right, and every trap the doc names is a real
`NuxTrap`. This exists because that document drifted from the implementation
once already.

`make difftest` runs random programs through both `src/vm.c` and
[`tools/nuxref.py`](../tools/nuxref.py) — a second interpreter written from
the specification rather than translated from the C — and compares the full
machine state after **every** step. Take it further with
`python3 tools/difftest.py --count 100000`, or reproduce a specific run with
`--seed N`.

`make verify` needs CBMC (`brew install cbmc`). It builds a wholly
nondeterministic machine, runs one `vm_tick()`, and proves memory safety,
freedom from undefined behaviour, image immutability, execution confinement
and preservation of the representation invariant — for every opcode and every
input, not just tested ones. Because the invariant is assumed before the step
and asserted after it, the proof is inductive: one step proves every step of
every run of every program.

To trace a program one instruction at a time and dump the full state after
each, use the harness the differential test drives:

```bash
printf '000000002a000000000034 35' | tr -d ' ' | ./bin/nuxstep --steps 8
```

## Architecture

- **Stack**: 8192 x 32-bit integers, plus a 1024-slot return stack, a
  1024-slot loop stack and a 4096-slot locals array — all separate
- **Memory**: Byte-addressable (program + data). Headless `nux` sizes guest RAM to the ROM (~68 KB for hello, ~1.4 MB process RSS on macOS). Cloister uses the 16 MB reserved map.
- **PC**: 32-bit program counter
- **Encoding**: Big-endian

## Quick Example

```c
#include "vm.h"

// Create a simple program: 5 + 3
uint8_t program[] = {
    OP_PUSH, 0, 0, 0, 5,
    OP_PUSH, 0, 0, 0, 3,
    OP_ADD,
    OP_PUSH, 0, 0, 0, 0,   // output format: number
    OP_OUT,
    OP_HALT,
};

// Run it
VM* vm = vm_create(program, sizeof(program), HEADLESS_BASE_ADDRESS,
                   nux_guest_memory_size(HEADLESS_BASE_ADDRESS, sizeof(program)), false);
vm_run(vm);   // Outputs: 8
vm_free(vm);
```

## Project Structure

```
nuxvm/
├── src/              # C sources (VM, compiler, tools, tests)
├── include/          # C headers
├── examples/
│   ├── lux/          # Lux (Forth-style) examples
│   └── fluxio/       # Fluxio (C-like) examples
└── docs/             # Documentation
```

## Documentation

### The machine

- [opcodes.md](opcodes.md) — reference for all 55 opcodes, the encoding, and the trap table.
- [semantics.md](semantics.md) — the normative operational semantics: state tuple, one rule per opcode, exact fault conditions. The definition the other two follow.
- [memory-map.md](memory-map.md) — the address-space bands, and the collisions that motivated naming them.
- [formal-semantics-plan.md](formal-semantics-plan.md) — how the specification, the reference model and the proofs were built, and what a validator would add.

### Languages and toolchain

- [using-lux.md](using-lux.md) / [lux_tutorial.md](lux_tutorial.md) — the Lux language and toolchain.
- [using-fluxio.md](using-fluxio.md) / [fluxio_tutorial.md](fluxio_tutorial.md) — the Fluxio language and toolchain.
- [reserve-directive.md](reserve-directive.md) — the Lux `RESERVE` allocator that replaced hand-picked addresses.
- [examples/README.md](../examples/README.md) — index for both languages.
- [examples/lux/README.md](../examples/lux/README.md) — Lux example programs.
- [examples/fluxio/README.md](../examples/fluxio/README.md) — Fluxio example programs.

### Guest apps and games

- [user-manual.md](user-manual.md) — Quill, Illumos, Tabula, Nib, and Easel (Cloister guest apps).
- [ui.md](ui.md) — the Lux UI toolkit and widget protocol.
- [game-loop.md](game-loop.md) — why `APP::on-tick!` exists: the key-up fix and the simulation/render split.
- [games/breakout_clone.md](games/breakout_clone.md) — Breakout: fixed-point ball motion, sub-step collision, and the fixed-timestep game loop.
- [games/road_escape.md](games/road_escape.md) — Road Escape: a scrolling road as a ring buffer, traffic that rides the kerb, fuel and ammo pickups, and grayscale presentation.
- [tile-games-toolkit.md](tile-games-toolkit.md) — tile blitting and `lib/tilemap.lux`.

### Formats

- [CFF.md](CFF.md) — Cloister Font Format.
- [icn-format.md](icn-format.md) — 16x16 icon glyphs.
- [palette.md](palette.md) — the fixed 16-colour system palette.

## License

MIT
