# Example Programs

Example programs for the NUX VM, split by front-end language:

| Tree | Language | Sources |
| ---- | -------- | ------- |
| [`lux/`](lux/) | Lux — Forth-style, concatenative | `.lux` |
| [`fluxio/`](fluxio/) | Fluxio — C-like, imperative | `.fx` |

Both compile to the same 55-opcode bytecode and run on `nux` (console) and `cloister` (graphical).

## Quick Start — Lux

```bash
# From the repo root, after `make`:
./bin/luxc -target headless examples/lux/hello.lux
./bin/nux examples/lux/hello.bin
```

`nux` and `cloister` also compile `.lux` in-process:

```bash
./bin/nux examples/lux/hello.lux
```

See [`lux/README.md`](lux/README.md) for the full list, and [`docs/using-lux.md`](../docs/using-lux.md) / [`docs/lux_tutorial.md`](../docs/lux_tutorial.md) for the CLI and language.

## Quick Start — Fluxio

```bash
./bin/fluxioc -target headless -o examples/fluxio/hello_console.bin examples/fluxio/hello_console.fx
./bin/nux examples/fluxio/hello_console.bin
```

Fluxio always goes through `fluxioc` first — `nux` and `cloister` do not compile `.fx` in-process.

See [`fluxio/README.md`](fluxio/README.md) for the full list, and [`docs/using-fluxio.md`](../docs/using-fluxio.md) / [`docs/fluxio_tutorial.md`](../docs/fluxio_tutorial.md) for the CLI and language.

## Writing Bytecode Directly

The VM executes raw big-endian bytecode; you can build programs without Lux or Fluxio by
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
