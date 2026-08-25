# Example Programs

Example programs and module demos for the NUX VM, in both front-end languages: Lux (`.lux`, Forth-style) and Fluxio (`.fx`, C-like).

## Quick Start — Lux

```bash
# From the repo root, after `make`:
./bin/luxc -target headless examples/modules/module_basic.lux
./bin/nux examples/modules/module_basic.bin
```

## Quick Start — Fluxio

```bash
./bin/fluxioc -target headless -o examples/hello_console.bin examples/hello_console.fx
./bin/nux examples/hello_console.bin
```

See [`docs/using-fluxio.md`](../docs/using-fluxio.md) and [`docs/fluxio_tutorial.md`](../docs/fluxio_tutorial.md) for the full CLI and language reference.

## What's Included — Lux

- `hello.bin` / `hello.lux` — hello-world bytecode (`./bin/nux examples/hello.bin`)
- `modules/module_basic.lux` — defining and calling words inside a `MODULE`
- `modules/module_imports.lux` — `IMPORT ... AS ...` aliasing between modules
- `modules/module_isolation.lux` — namespace isolation between modules
- `modules/calculator.lux` — a small calculator built from modules

See `modules/MODULE_SYSTEM.md` for a walkthrough of the module system.

## What's Included — Fluxio

Each `.fx` file's compile/run command is documented in its own header comment; the general pattern is `./bin/fluxioc -target <headless|graphical> -o out.bin file.fx` then `./bin/nux out.bin` or `./bin/cloister out.bin`.

- `hello_console.fx` — console "hello world" via `emit()`, the Fluxio counterpart to `hello.lux`
- `fib.fx` — bounded recursion (`recursive(N)`)
- `array_string_demo.fx` — fixed-size arrays, string-literal initializers, bounds-checked indexing
- `struct_demo.fx` — `struct` declarations, field access, pass-by-reference mutation
- `include_demo.fx` + `include_lib/mathlib.fx` — splitting a program across files with `include "...";`
- `cloister_hello.fx` — windowed draw-device demo (filled rects + text), runnable headless or under Cloister
- `hello_cloister.fx` — the Fluxio counterpart to a windowed Lux hello-world, meant to be watched under `./bin/cloister`

See [`docs/fluxio-language-plan.md`](../docs/fluxio-language-plan.md) for the design rationale behind each of these.

## Writing Bytecode Directly

The VM executes raw big-endian bytecode; you can build programs without Lux by
emitting opcodes yourself (see `include/opcodes.h` for the full set):

```c
#include "vm.h"

uint8_t prog[] = {
    OP_PUSH, 0, 0, 0, 48,
    OP_PUSH, 0, 0, 0, 18,
    OP_MOD,
    OP_PUSH, 0, 0, 0, 0,   // output format: number
    OP_OUT,
    OP_HALT,
};

VM* vm = vm_create(prog, sizeof(prog), HEADLESS_BASE_ADDRESS,
                   4 * 1024 * 1024, false);
vm_run(vm);
vm_free(vm);
```
