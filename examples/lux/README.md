# Lux Examples

Forth-style concatenative programs (`.lux`) for the NUX VM.

## Quick Start

```bash
# From the repo root, after `make`:
./bin/luxc -target headless examples/lux/hello.lux
./bin/nux examples/lux/hello.bin
```

Or compile-on-run:

```bash
./bin/nux examples/lux/hello.lux
```

Module demos need an explicit compile (they write a sibling `.bin`):

```bash
./bin/luxc -target headless examples/lux/modules/module_basic.lux
./bin/nux examples/lux/modules/module_basic.bin
```

## What's Included

- `hello.lux` — hello-world via `EMIT` (`./bin/nux examples/lux/hello.lux`)
- `modules/module_basic.lux` — defining and calling words inside a `MODULE`
- `modules/module_imports.lux` — `IMPORT ... AS ...` aliasing between modules
- `modules/module_isolation.lux` — namespace isolation between modules
- `modules/calculator.lux` — a small calculator built from modules

See `modules/MODULE_SYSTEM.md` for a walkthrough of the module system.

The Fluxio counterparts live in [`../fluxio/`](../fluxio/).
