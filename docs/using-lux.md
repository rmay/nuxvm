# Using Lux

This guide covers the two workflows most users need: running the lux compiler from the command line, and writing/running lux inside CLOISTER. For the language itself, see [`lux_tutorial.md`](lux_tutorial.md).

## Overview

The toolchain has three user-facing commands:

| Command     | Role                                                                 |
| ----------- | -------------------------------------------------------------------- |
| `luxc`      | Compiles `.lux` source into `.bin` bytecode.                         |
| `nux`       | Console runner. Executes `.bin` (or `.lux`) and prints to stdout.    |
| `cloister`  | Graphical OS. Runs `.bin`/`.lux`, or launches a built-in REPL.       |

Typical flow:

```
  hello.lux ──(luxc)──▶ hello.bin ──(nux | cloister)──▶ output
```

Both `nux` and `cloister` accept `.lux` directly — they compile in-process before running. Use that for quick iteration; use `luxc` when you want a reusable `.bin` you can ship or check in.

## Building the tools

```bash
make buildall
```

Produces `bin/luxc`, `bin/nux`, `bin/cloister`, and `bin/luxrepl`. To build just the compiler:

```bash
make luxbuild
```

See `Makefile` for the full target list (`make help`).

## `luxc` — the command-line compiler

### Synopsis

```
luxc [-trace] [-o out.bin] <file.lux>
```

Source: `src/luxc.c`.

### Flags

| Flag        | Default                             | Purpose                                       |
| ----------- | ----------------------------------- | --------------------------------------------- |
| `-o <path>` | `<input>.bin` (strips `.lux` first) | Write bytecode to this path.                  |
| `-trace`    | off                                 | Verbose compile-time tracing to stderr.       |

The default output path logic is exactly: strip a trailing `.lux` extension (case-insensitive) from the input and append `.bin`. If the input has no `.lux` suffix, `.bin` is appended as-is.

Every app build must also declare a top-level `VERSION <n>` directive (Kelvin versioning — see `AGENTS.md`), anywhere in the main file or an `INCLUDE`d one; `luxc` refuses to compile an app that never declares one. A library build (`-base 0xADDR`, meant to be linked into another program rather than run standalone) is exempt from this check.

### Examples

Compile with defaults:

```bash
./bin/luxc examples/lux/hello.lux
# Compiled: examples/lux/hello.bin
```

Explicit output path:

```bash
./bin/luxc -o /tmp/out.bin examples/lux/hello.lux
```

Capture a trace for a compile failure:

```bash
./bin/luxc -trace examples/lux/hello.lux 2> trace.log
```

### Compile-and-run

```bash
./bin/luxc examples/lux/hello.lux
./bin/nux examples/lux/hello.bin
```

Or skip the intermediate file — `nux` and `cloister` detect a `.lux` suffix and compiles in-process:

```bash
./bin/nux examples/lux/hello.lux
```

The shortcut recompiles on every run. Prefer `luxc` when you're distributing a program or want a stable bytecode artifact.

### `nux` runtime flags

```
nux [-debug | -trace] <program.bin|program.lux>
```

- `-debug` — step by step. Press Enter to advance, `c` to continue to end, `q` to quit.
- `-trace` — print PC and stack before each instruction.

### Bytecode format

`.bin` files are big-endian 32-bit bytecode. Each program begins with a `JMP` over its word-definition table and ends with a `HALT`. The full opcode reference is in [`opcodes.md`](opcodes.md); memory layout is in [`NUX_ARCHITECTURE.md`](NUX_ARCHITECTURE.md).

## Lux inside CLOISTER

CLOISTER is the graphical environment: a windowed framebuffer, keyboard/mouse input, sound, file I/O, and a built-in REPL. See [`CLOISTER.md`](CLOISTER.md) for the device and register map.

### Launching

```bash
./bin/cloister                       # REPL mode; loads lib/boot.lux if present
./bin/cloister program.lux           # compile and run this program
./bin/cloister program.fx            # same, for a Fluxio source (see using-fluxio.md)
./bin/cloister program.bin           # run precompiled bytecode
```

With no argument, CLOISTER tries to load `lib/boot.lux` relative to the current working directory. If that file isn't there, it falls back to a single `HALT` so the REPL still comes up. That's why `cloister` is usually invoked from the repo root — the path is resolved against cwd, not the binary's location (`src/cloister.c`).

### CLI flags

| Flag      | Default | Purpose                                                                  |
| --------- | ------- | ------------------------------------------------------------------------ |
| `-mem N`  | 32      | RAM size in MB. Capped at 128.                                           |
| `-w N`    | —       | Screen width override.                                                   |
| `-h N`    | —       | Screen height override.                                                  |
| `-scale N`| —       | Window pixel-scale override (otherwise derived from `TEXT::font-size!`).  |

Example:

```bash
./bin/cloister -mem 64 -w 320 -h 240 mygame.lux
```

### File sandbox

The File device is pinned to the directory CLOISTER was launched from (`src/vfs.c`). Reads, writes, stats, and deletes that escape that root — via `..`, an absolute path, or a symlink — return `-1`. See [`file-device.md`](file-device.md) for the full protocol.

### The REPL

Start CLOISTER without an argument to enter the graphical REPL. Input goes at the `lux> ` prompt at the top of the window.

Built-in commands:

| Command                    | Action                                       |
| -------------------------- | -------------------------------------------- |
| `help`, `?`                | Show help.                                   |
| `exit`, `quit`, `q`        | Exit CLOISTER.                               |
| `clear`                    | Clear the REPL log.                          |
| `stack`, `.s`              | Show the current stack.                      |
| `drop`                     | Pop the top value.                           |
| `clearstack`, `cs`         | Empty the stack.                             |
| Up / Down arrows           | Scroll line history.                         |
| F1                         | Toggle the debug overlay (PC, stack).        |

Anything else is compiled with `compile_source` and executed by injecting the bytecode at the start of user memory and triggering vector 0 (`src/cloister.c`).

### Defining words in the REPL

Lines starting with `@` and ending with `;` are word definitions. They don't execute immediately — they accumulate in a session-local buffer and are prepended to every subsequent compile. A definition missing its `;` is rejected with `Error: Word definition must end with ';'`, so if the REPL appears silent, check that you closed the definition.

Example session:

```
lux> 2 3 + .
5
lux> @double dup + ;
Defined word
lux> 21 double .
42
lux> 5 GIRD n n n * UNGIRD .
25
lux> .s
  Stack: []
```

`GIRD` names the top of the stack; `UNGIRD` takes the name off. Inside a word, `;` ungirds for you. See [`lux_tutorial.md`](lux_tutorial.md#named-locals) and `examples/lux/gird.lux`.

### Boot files

`lib/boot.lux` is the default boot program. It:

- Configures the screen (`SCREEN::width!`, `SCREEN::height!`, `SCREEN::clear`).
- Configures text rendering (`TEXT::font-size!`, `TEXT::color!`).
- Installs `on-key` as the controller vector.
- Prints `"CLOISTER Booted."` via `emit`.
- Drops into `keep-alive`, a `YIELD`-tail-recursive loop that returns control to the host every frame.

To use your own boot program, write it in the same shape and pass it on the command line:

```bash
./bin/cloister mybootfile.lux
```

The structure you almost always want:

```forth
INCLUDE "lib/system.lux"
IMPORT SCREEN
IMPORT TEXT
IMPORT CTRL

@main
    256 SCREEN::width!
    192 SCREEN::height!
    65535 SCREEN::clear
    ( ... your setup ... )
    keep-alive
;

@keep-alive
    YIELD
    keep-alive
;

main
```

The first tick after launch runs until `YIELD` or `HALT`. Cloister is a
fixed 960×720 window. Guests draw with `/dev/draw` and read input from
`/dev/mouse` and `/dev/kbd` — see `ARCHITECTURE.md`. There are no
MMIO device ports.

### Using the system library

Graphical apps include `lib/app.lux` (which pulls in VFS, draw, event, UI).
Clock words live in `lib/time.lux` and read `/dev/time`. Programs that
touch the machine typically start with:

```forth
INCLUDE "lib/app.lux"
IMPORT APP
IMPORT DRAW
IMPORT EVENT
```

Naming conventions:

- `@name!` — setter. Takes the value from the stack.
- `@name@` — getter. Pushes the value onto the stack.
- `@name-get` / `@name-set` — used when the getter/setter shape needs extra args.

App state goes in `RESERVE name <bytes> ;` declarations, which let the
compiler pick a non-overlapping address rather than making you hand-pick one
out of [`memory-map.md`](memory-map.md):

```forth
RESERVE CUR_VAL 4 ;
RESERVE GRID  240 ;
```

Large buffers and addresses the host agrees on are still hand-picked — see
[`reserve-directive.md`](reserve-directive.md). To find out where a
reservation landed, ask the compiler:

```bash
./bin/luxc -target graphical -symbols /tmp/app.symtab.json apps/Calculator.lux
```

The dump's `reservations` array carries each name's data address and size.

For example, to fill a rectangle through `/dev/draw`:

```forth
dfd LOADI 10 20 8 8 0x000000 DRAW::fill-rect
```

Productivity apps that want a System 6 grayscale screen call `APP::grayscale!` after `APP::init` (which opens `/dev/draw`). That writes cmd 11 (`k8`); later fills still take RGB colors, and the host stores Rec. 601 luma. `APP::color!` switches back. Color apps such as Whittle leave the default RGB channel.

## Troubleshooting

- **Compile error, not sure why** — re-run with `-trace` and look at the last successful pass in the stderr output.
- **"VERSION <n> is not a legal Kelvin version"** — the declared version breaks one of the Kelvin rules. Most often it is *colder than the platform*: versions count down, so a number below the current everything-else value asks to be supported by something more final than exists. A guest may be hotter than the platform (an older ROM still compiles and runs); it may never be colder. The current numbers are in `include/kelvin.h` and `AGENTS.md`.
- **"missing required 'VERSION <n>' directive"** — the file (and everything it `INCLUDE`s) never declared `VERSION <n>` at the top level. Unless a specific app has a reason to declare otherwise, use the current default, `VERSION 399000` (399K) — check `AGENTS.md`'s versioning section for the number in force and for what 300K is reserved for. Library builds (`luxc -base ...`) are exempt.
- **`read <path>: no such file`** from CLOISTER at launch — the path is resolved relative to your working directory. Running from outside the repo root means `lib/boot.lux` isn't found; CLOISTER falls back to a HALT and enters REPL mode. Either `cd` into the repo or pass an explicit path.
- **File-device calls return `-1`** — the path escaped the sandbox root (`..`, absolute path, or a symlink). Relative paths under the launch directory are the safe bet.
- **REPL seems to ignore input** — most often an unterminated `@word` definition. Close it with `;` and re-enter. The REPL logs `Defined word` on success and `Compile error: ...` otherwise.
- **Window geometry looks wrong** — Cloister is a fixed 960×720 host window. Guest canvas size is read from `/dev/draw`.

## Further reading

- [`lux_tutorial.md`](lux_tutorial.md) — the language itself: words, quotations, modules, combinators.
- [`reserve-directive.md`](reserve-directive.md) — compiler-managed app state (`RESERVE`).
- [`opcodes.md`](opcodes.md) — bytecode opcode reference.
- [`NUX_ARCHITECTURE.md`](NUX_ARCHITECTURE.md) — VM internals and memory map.
- [`../ARCHITECTURE.md`](../ARCHITECTURE.md) — VM, Plan 9 VFS, Cloister.
- Host files are `/sys/file/…` under the launch directory.
- [`lexer.md`](lexer.md) — token grammar.
