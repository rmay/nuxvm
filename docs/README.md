# NUX

A simple stack-based virtual machine written in C, with two front-end languages: **Lux** (Forth-style, concatenative) and **Fluxio** (C-like, imperative).

## Features

- 32-bit signed integer operations
- 8192-element stack with 4096-elements reserved memory space
- Big-endian bytecode
- Jump instructions and subroutines
- Memory load/store operations
- Comprehensive test suite

## Installation

```bash
# From the repo root; needs a C compiler, pkg-config, and SDL2
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

## Architecture

- **Stack**: 8192 x 32-bit integers
- **Memory**: Byte-addressable (program + data)
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
                   4 * 1024 * 1024, false);
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

- [user-manual.md](user-manual.md) — Quill, Illumos, Tabula, Nib, and Easel (Cloister guest apps).
- [using-lux.md](using-lux.md) / [lux_tutorial.md](lux_tutorial.md) — the Lux language and toolchain.
- [using-fluxio.md](using-fluxio.md) / [fluxio_tutorial.md](fluxio_tutorial.md) — the Fluxio language and toolchain.
- [examples/README.md](../examples/README.md) — index for both languages.
- [examples/lux/README.md](../examples/lux/README.md) — Lux example programs.
- [examples/fluxio/README.md](../examples/fluxio/README.md) — Fluxio example programs.

## License

MIT
