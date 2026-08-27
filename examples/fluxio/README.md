# Fluxio Examples

C-like imperative programs (`.fx`) for the NUX VM.

Each `.fx` file's compile/run command is documented in its own header comment; the general pattern is `./bin/fluxioc -target <headless|graphical> -o out.bin file.fx` then `./bin/nux out.bin` or `./bin/cloister out.bin`.

## Quick Start

```bash
# From the repo root, after `make`:
./bin/fluxioc -target headless -o examples/fluxio/hello_console.bin examples/fluxio/hello_console.fx
./bin/nux examples/fluxio/hello_console.bin
```

Windowed programs need `-target graphical` and `cloister`:

```bash
./bin/fluxioc -target graphical -o examples/fluxio/hello_cloister.bin examples/fluxio/hello_cloister.fx
./bin/cloister examples/fluxio/hello_cloister.bin
```

## What's Included

- `hello_console.fx` — console "hello world" via `emit()`, the Fluxio counterpart to `lux/hello.lux`
- `fib.fx` — bounded recursion (`recursive(N)`)
- `array_string_demo.fx` — fixed-size arrays, string-literal initializers, bounds-checked indexing
- `struct_demo.fx` — `struct` declarations, field access, pass-by-reference mutation
- `include_demo.fx` + `include_lib/mathlib.fx` — splitting a program across files with `include "...";`
- `float_demo.fx` — fixed-point `Float` from `lib/float.fx`
- `cloister_hello.fx` — windowed draw-device demo (filled rects + text), runnable headless or under Cloister
- `hello_cloister.fx` — the Fluxio counterpart to a windowed Lux hello-world, meant to be watched under `./bin/cloister`

See [`docs/fluxio-language-plan.md`](../../docs/fluxio-language-plan.md) for the design rationale behind each of these.

The Lux counterparts live in [`../lux/`](../lux/).
