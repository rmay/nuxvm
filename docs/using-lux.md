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

Source: `cmd/luxc/main.go`.

### Flags

| Flag        | Default                             | Purpose                                       |
| ----------- | ----------------------------------- | --------------------------------------------- |
| `-o <path>` | `<input>.bin` (strips `.lux` first) | Write bytecode to this path.                  |
| `-trace`    | off                                 | Verbose compile-time tracing to stderr.       |

The default output path logic is exactly: strip a trailing `.lux` extension (case-insensitive) from the input and append `.bin`. If the input has no `.lux` suffix, `.bin` is appended as-is.

### Examples

Compile with defaults:

```bash
./bin/luxc examples/hello.lux
# Compiled: examples/hello.bin
```

Explicit output path:

```bash
./bin/luxc -o /tmp/out.bin examples/hello.lux
```

Capture a trace for a compile failure:

```bash
./bin/luxc -trace examples/hello.lux 2> trace.log
```

### Compile-and-run

```bash
./bin/luxc examples/hello.lux
./bin/nux examples/hello.bin
```

Or skip the intermediate file — `nux` and `cloister` call `lux.LoadProgram` (`pkg/lux/load.go`), which detects a `.lux` suffix and compiles in-process:

```bash
./bin/nux examples/hello.lux
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
./bin/cloister program.bin           # run precompiled bytecode
```

With no argument, CLOISTER tries to load `lib/boot.lux` relative to the current working directory. If that file isn't there, it falls back to a single `HALT` so the REPL still comes up. That's why `cloister` is usually invoked from the repo root — the path is resolved against cwd, not the binary's location (`cmd/cloister/main.go:403-415`).

### CLI flags

| Flag      | Default | Purpose                                                                  |
| --------- | ------- | ------------------------------------------------------------------------ |
| `-mem N`  | 32      | RAM size in MB. Capped at 128.                                           |
| `-w N`    | —       | Screen width override. Wins over whatever boot.lux wrote via MMIO.       |
| `-h N`    | —       | Screen height override.                                                  |
| `-scale N`| —       | Window pixel-scale override (otherwise derived from `TEXT::cell-size`).  |

Example:

```bash
./bin/cloister -mem 64 -w 320 -h 240 mygame.lux
```

### File sandbox

The File device is pinned to the directory CLOISTER was launched from (`cmd/cloister/main.go:427-438`). Reads, writes, stats, and deletes that escape that root — via `..`, an absolute path, or a symlink — return `-1`. See [`file-device.md`](file-device.md) for the full protocol.

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
| F1                         | Toggle the debug overlay (PC, stack, MMIO). |

Anything else is compiled with `lux.Compile` and executed by injecting the bytecode at the start of user memory and triggering vector 0 (`cmd/cloister/main.go:174-194`).

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
lux> .s
  Stack: []
```

### Boot files

`lib/boot.lux` is the default boot program. It:

- Configures the screen (`SCREEN::width!`, `SCREEN::height!`, `SCREEN::clear`).
- Configures text rendering (`TEXT::cell-size!`, `TEXT::color!`).
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

The first tick after launch runs until `YIELD` or `HALT`. CLOISTER then reads `SCR_W`, `SCR_H`, and the `TEXT` scale back from MMIO to size the window (`cmd/cloister/main.go:443-473`), so boot code that writes those registers controls the initial window geometry.

### Using the system library

`lib/system.lux` wraps the MMIO device bus as lux modules: `SYSTEM`, `SCREEN`, `AUDIO`, `CTRL`, `MOUSE`, `FILE`, `TIME`, `TEXT`. Programs that touch hardware typically start with:

```forth
INCLUDE "lib/system.lux"
IMPORT SCREEN
IMPORT TEXT
IMPORT CTRL
```

Naming conventions:

- `@name!` — setter. Takes the value from the stack.
- `@name@` — getter. Pushes the value onto the stack.
- `@name-get` / `@name-set` — used when the getter/setter shape needs extra args.

For example, to draw a single pixel:

```forth
0xFFFFFF 10 20 SCREEN::pixel!   ( color x y -- )
```

## Troubleshooting

- **Compile error, not sure why** — re-run with `-trace` and look at the last successful pass in the stderr output.
- **`read <path>: no such file`** from CLOISTER at launch — the path is resolved relative to your working directory. Running from outside the repo root means `lib/boot.lux` isn't found; CLOISTER falls back to a HALT and enters REPL mode. Either `cd` into the repo or pass an explicit path.
- **File-device calls return `-1`** — the path escaped the sandbox root (`..`, absolute path, or a symlink). Relative paths under the launch directory are the safe bet.
- **REPL seems to ignore input** — most often an unterminated `@word` definition. Close it with `;` and re-enter. The REPL logs `Defined word` on success and `Compile error: ...` otherwise.
- **Window geometry looks wrong** — boot code writes `SCR_W`/`SCR_H`/`TEXT::cell-size` during the first tick; CLOISTER reads those to size the window. Use `-w`, `-h`, `-scale` to override.

## Further reading

- [`lux_tutorial.md`](lux_tutorial.md) — the language itself: words, quotations, modules, combinators.
- [`opcodes.md`](opcodes.md) — bytecode opcode reference.
- [`NUX_ARCHITECTURE.md`](NUX_ARCHITECTURE.md) — VM internals and memory map.
- [`CLOISTER.md`](CLOISTER.md) — CLOISTER devices and MMIO register map.
- [`file-device.md`](file-device.md) — File device protocol.
- [`lexer.md`](lexer.md) — token grammar.
