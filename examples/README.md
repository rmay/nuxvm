# Example Programs

Example Lux programs and module demos for the NUX VM.

## Quick Start

```bash
# From the repo root, after `make`:
./bin/luxc -target headless modules/module_basic.lux
../bin/nux modules/module_basic.bin
```

Or from the repo root:

```bash
./bin/luxc -target headless examples/modules/module_basic.lux
./bin/nux examples/modules/module_basic.bin
```

## What's Included

- `hello.bin` — precompiled hello-world bytecode (`./bin/nux examples/hello.bin`)
- `modules/module_basic.lux` — defining and calling words inside a `MODULE`
- `modules/module_imports.lux` — `IMPORT ... AS ...` aliasing between modules
- `modules/module_isolation.lux` — namespace isolation between modules
- `modules/calculator.lux` — a small calculator built from modules

See `modules/MODULE_SYSTEM.md` for a walkthrough of the module system.

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
