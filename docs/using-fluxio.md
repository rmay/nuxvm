# Using Fluxio

This guide covers the fluxio compiler workflow: compiling `.fx` source to `.bin` bytecode and running it. For the language itself, see [`fluxio_tutorial.md`](fluxio_tutorial.md).

## Overview

Fluxio is a second, C-like language for the NUX VM, alongside Lux (the Forth-style concatenative language — see [`using-lux.md`](using-lux.md)). Both compile to the same 55-opcode bytecode and run on the same `nux`/`cloister` hosts; they differ only in front end.

| Command    | Role                                                          |
| ---------- | -------------------------------------------------------------- |
| `fluxioc`  | Compiles `.fx` source into `.bin` bytecode.                   |
| `nux`      | Console runner. Executes `.bin` and prints to stdout.         |
| `cloister` | Graphical OS. Executes `.bin`, with windowing/input/draw.     |

```
  hello.fx ──(fluxioc)──▶ hello.bin ──(nux | cloister)──▶ output
```

Unlike `luxc`'s output, `nux` and `cloister` do **not** compile `.fx` in-process — they only recognize a `.lux` suffix for that shortcut (`src/nux.c`, `src/cloister.c`). Fluxio programs always go through `fluxioc` to a `.bin` first.

## Building the tools

```bash
make
```

Produces `bin/fluxioc` along with `bin/nux`, `bin/luxc`, `bin/cloister`, `bin/luxrepl`, and the test binaries (see `TARGETS` in `Makefile`). To build just the Fluxio compiler:

```bash
make bin/fluxioc
```

Run the Fluxio compiler's own test suite (parser/codegen/VM-level assertions, including a pixel-exact draw test):

```bash
make test           # runs test_vfs, test_vm, test_compiler, test_fluxio_compiler
```

## `fluxioc` — the command-line compiler

### Synopsis

```
fluxioc [-o out.bin] [-target graphical|headless] [-dumpAt 0xADDR] [-dumpRange N] <file.fx>
```

Source: `src/fluxioc.c`.

### Flags

| Flag              | Default      | Purpose                                                                 |
| ----------------- | ------------ | ------------------------------------------------------------------------ |
| `-o <path>`       | `<input>.bin`| Write bytecode to this path (strips a trailing `.fx`, then appends `.bin`). |
| `-target <t>`     | `graphical`  | Base load address: `headless` (for `nux`) or `graphical` (for `cloister`). |
| `-dumpAt 0xADDR`  | —            | Instead of writing a file, disassemble a window of bytecode around this address and print it (hex + best-effort disassembly), then exit. |
| `-dumpRange N`    | 64           | Half-width (in bytes either side of `-dumpAt`) of the disassembly window. |

The two base addresses matter because `nux` and `cloister` load programs at different fixed base addresses (`HEADLESS_BASE_ADDRESS` / `GRAPHICAL_BASE_ADDRESS`, `include/opcodes.h`); a `.bin` compiled for the wrong target will jump to the wrong absolute addresses. Compile for `headless` to run under `bin/nux`, `graphical` to run under `bin/cloister`.

### Examples

Compile for the console runner:

```bash
./bin/fluxioc -target headless -o examples/fib.bin examples/fib.fx
./bin/nux examples/fib.bin
```

Compile for the graphical host:

```bash
./bin/fluxioc -target graphical -o examples/hello_cloister.bin examples/hello_cloister.fx
./bin/cloister examples/hello_cloister.bin
```

Programs that only touch `emit`/`print` (no `vfs_open`/draw calls) behave identically under either target — but the target must still match whichever host actually runs the `.bin`, since it's baked into every absolute address in the bytecode.

Inspect the compiled bytecode around a specific address (useful when a VM fault reports a PC):

```bash
./bin/fluxioc -dumpAt 0x10120 -dumpRange 32 examples/fib.fx
```

### Multi-file programs

`include "path.fx";` splices another file's tokens in place, resolved relative to the *including* file's directory. `fluxioc` handles this transparently — pass only the entry file:

```bash
./bin/fluxioc -target headless -o examples/include_demo.bin examples/include_demo.fx
./bin/nux examples/include_demo.bin
```

See `examples/include_demo.fx` + `examples/include_lib/mathlib.fx`.

## Fluxio and Cloister

Programs that call `vfs_open("/dev/draw")` and friends reach the same windowing/input surface Lux apps use (see [`ui.md`](ui.md)), just through a curated set of builtins instead of the `SCI`/`DRAW` modules. See [`fluxio_tutorial.md`](fluxio_tutorial.md#cloister-builtins) for the full list.

A `.bin` built this way runs fine headlessly too — `machine_create()` wires up the same System/VFS/draw device for both `nux` and `cloister`, so draw calls execute (and were verified pixel-exact against the framebuffer) even with no window to look at. The difference is pacing: `bin/nux` is a one-shot runner that stops at the *first* `yield()` rather than pumping a frame loop, so a Fluxio program with a `while` frame loop draws exactly one frame under `nux` and then exits. Run it under `bin/cloister` to see it animate.

## Troubleshooting

- **"Lexing failed" / "Parsing failed" / "Compilation failed"** — `fluxioc` prints one of these three fixed messages and exits 1; the specific error already went to stderr above it (unknown identifier, arity mismatch, naming-convention violation, missing doc comment, etc.). There's no `-trace` flag for Fluxio the way `luxc` has one — errors are reported at the point of failure, not accumulated.
- **"struct/function declaration must be preceded by a /** ... */ doc comment"** — every top-level `func_decl` and `struct_decl` needs an immediately preceding `/** ... */` block comment. See [`fluxio_tutorial.md`](fluxio_tutorial.md#naming-and-documentation-rules).
- **"(must be lower_snake_case)" / "(must be UpperCamelCase...)"** — functions, variables, params, and struct fields must be `lower_snake_case`; struct type names must be `UpperCamelCase`. Not a lint warning — a compile error.
- **Recursion rejected at compile time** — a function that calls itself (directly or through a cycle) must be declared `recursive(N) int f(...)` with a literal bound `N`. Plain functions can't recurse at all. See [`fluxio_tutorial.md`](fluxio_tutorial.md#recursion).
- **Bytecode runs but halts with an unexpected negative return** — `-1` is the recursion-depth-exceeded sentinel, `-2` is an out-of-bounds array/index access. Both halt cleanly rather than corrupting memory.
- **A string argument "must be a string literal"** — `vfs_open`, `set_window_title`, and `draw_str`'s text argument only accept a literal `"..."` at the call site, never a computed/runtime value. Fluxio has no general string-as-value type yet.

## Further reading

- [`fluxio_tutorial.md`](fluxio_tutorial.md) — the language itself: types, control flow, arrays, structs, includes, Cloister builtins.
- [`fluxio-language-plan.md`](fluxio-language-plan.md) — design history and rationale for each language slice (v1 → v2d), including the JSF AV safety-discipline mapping.
- [`using-lux.md`](using-lux.md) — the sibling Forth-style language and its CLOISTER workflow.
- [`opcodes.md`](opcodes.md) — bytecode opcode reference (shared by both languages).
- [`NUX_ARCHITECTURE.md`](NUX_ARCHITECTURE.md) — VM internals and memory map.
