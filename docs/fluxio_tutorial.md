# The Fluxio Language

Fluxio is a C-like imperative language for the NUX VM — the second front end alongside Lux (Forth-style, concatenative; see [`lux_tutorial.md`](lux_tutorial.md)). Both compile to the same 55-opcode bytecode (`docs/opcodes.md`) and run on the same `nux`/`cloister` hosts. Fluxio exists for programs that read better as `if`/`while`/`int x = ...` than as stack-shuffling words.

For the compiler command line, see [`using-fluxio.md`](using-fluxio.md). For the design history and rationale behind each feature (why arrays can't decay from locals, why there's no struct-by-value, the JSF AV safety mapping), see [`fluxio-language-plan.md`](fluxio-language-plan.md) — this tutorial is the "how to write it" companion to that "why it's built this way" document.

## Hello, world

Fluxio v1 has no string literals as general values, so console output is one character at a time via `emit()`:

```c
/** prints "Hello, World!" followed by a newline, one ASCII code at a time */
int say_hello() {
    emit(72);  /* H */
    emit(101); /* e */
    emit(108); /* l */
    emit(108); /* l */
    emit(111); /* o */
    emit(33);  /* ! */
    emit(10);  /* newline */
    return 0;
}

/** entry point */
int main() {
    say_hello();
    return 0;
}
```

```bash
./bin/fluxioc -target headless -o hello.bin hello.fx
./bin/nux hello.bin
```

Every Fluxio program needs an `int main()` — that's the compiled entry point. See `examples/fluxio/hello_console.fx`.

## Naming and documentation rules

These are compile errors, not lint warnings — inherited from the JSF AV safety-discipline goal of making source auditable by construction (see `fluxio-language-plan.md`'s "Safety & language discipline" table):

- Functions, variables, parameters, and struct fields must be `lower_snake_case` (`^[a-z][a-z0-9_]*$`).
- Struct type names must be `UpperCamelCase` (`^[A-Z][A-Za-z0-9]*$`) — e.g. `Point`, not `point` or `POINT`. This also means a struct name and a value identifier can never collide, since the two conventions are disjoint on their first character.
- Every top-level function and struct declaration must be immediately preceded by a `/** ... */` doc comment. A `//` line comment or a non-adjacent `/** */` doesn't satisfy this.

```c
/** returns the greater of a and b */
int fx_max(int a, int b) {
    if (a > b) { return a; }
    return b;
}
```

Ordinary `//` and `/* */` comments are otherwise unrestricted anywhere else in a file.

## Types, variables, operators

The only type is 32-bit signed `int` (plus fixed-size arrays and structs built on top of it — no floats, no pointers as a surface type). Declare with or without an initializer:

```c
int count;
int total = 0;
int mask = 0xFF00;   /* decimal or 0x-prefixed hex literals */
```

Globals declared outside any function live in a fixed-address globals segment; locals declared inside a function body live in the VM's frame-pointer locals region (see "Arrays" below for why that distinction matters).

Standard C-family operator set, in the usual precedence (loosest to tightest): assignment, `||`, `&&`, `|`, `^`, `&`, `==`/`!=`, `<`/`<=`/`>`/`>=`, `<<`/`>>`, `+`/`-`, `*`/`/`/`%`, unary `-`/`!`/`~`. `&&` and `||` short-circuit. There's no `++`/`--` and no compound assignment (`+=` etc.) — write `i = i + 1`.

## Control flow

```c
if (x > 0) {
    print(1);
} else {
    print(-1);
}

while (i < 10) {
    i = i + 1;
}

for (int i = 0; i < n; i = i + 1) {
    total = total + i;
}
```

`if`/`else`, `while`, and C-style three-clause `for` (with a `for`-scoped `int` declaration in the init clause) are the full set — no `switch`, no `do/while`, no `goto` (and none planned; see the JSF AV table in `fluxio-language-plan.md`).

## Functions

```c
/** entry point */
int main() {
    return 0;
}
```

Every function returns `int` (there's no `void` — return `0` if the value is unused) and every parameter is declared `int name` (or `int name[]` / `StructType name`, see below). Forward calls are fine — a function can call one declared later in the same file (or an included file).

### Recursion

Plain functions **cannot** recurse — a function calling itself, directly or through a cycle, is a compile error. To recurse, declare an explicit compile-time-constant depth bound:

```c
/** fibonacci, bounded */
recursive(32) int fib(int n) {
    if (n < 2) { return n; }
    return fib(n - 1) + fib(n - 2);
}
```

Exceeding the declared depth halts the program cleanly with a distinct sentinel return value (`-1`) rather than overrunning the VM's fixed-size return stack. This is a deliberate JSF AV-style constraint: "no unbounded recursion," enforced by the compiler rather than trusted to the programmer. See `examples/fluxio/fib.fx`.

## Arrays

Fixed-size `int` arrays, global or local, with bounds-checked `[]` indexing:

```c
int numbers[10];
int greeting[] = "Hello, Fluxio!";   /* string literal as an array initializer */
```

A string literal is only legal as an array initializer — `int msg[] = "Hi";` auto-sizes to `strlen+1` (room for the trailing NUL); `int msg[10] = "Hi";` uses an explicit size (must fit; the rest is zero-padded). A string literal used as a general expression value elsewhere is not supported — Fluxio has no first-class runtime string type yet.

**Global vs. local arrays are not interchangeable**, because of how the VM stores them:

- **Global arrays** live in the globals segment at a real, fixed address — they decay to that base address when used as a plain value (e.g. passed to a function), exactly like C array-to-pointer decay.
- **Local arrays** occupy consecutive slots in the VM's frame-pointer locals region, which has no stable address the way main memory does. A local array can be indexed within its own function, but **cannot decay to a value** — passing one to another function is a compile error, not silent corruption.
- A parameter declared `int name[]` is, under the hood, an ordinary scalar holding a decayed base address, so indexing it is **unchecked** (no compile-time-known length) — same caveat C has for array parameters. Every other array access (locals, globals) is bounds-checked at runtime; an out-of-range index halts with sentinel `-2`.

```c
int greeting[] = "Hello, Fluxio!";
int numbers[10];

/** prints every character of a global char array up to (not including) len */
int print_str(int s[], int len) {
    for (int i = 0; i < len; i = i + 1) {
        emit(s[i]);
    }
    return 0;
}

/** entry point */
int main() {
    print_str(greeting, 14);
    for (int i = 0; i < 10; i = i + 1) {
        numbers[i] = i * i;
    }
    return 0;
}
```

See `examples/fluxio/array_string_demo.fx`.

## Structs

`struct` groups `int`-only fields under a compile-time-constant field offset — no nested structs, no array fields, no arrays-of-structs, and no bounds check is needed (unlike `[i]`, every field access is offset-checked by the compiler, not at runtime):

```c
/** a point in 2D space */
struct Point {
    int x;
    int y;
}

Point cursor;

/** moves a point by (dx, dy), mutating it through the reference */
int translate(Point p, int dx, int dy) {
    p.x = p.x + dx;
    p.y = p.y + dy;
    return 0;
}
```

Struct instances follow the same reference rules as arrays, deliberately:

- A **global** struct decays to its base address when passed to a function — since the parameter's declared type (`Point p`) carries full field-offset info at compile time, `translate` above genuinely mutates the caller's `cursor` through the reference.
- A **local** struct instance has no stable address (same VM constraint as local arrays) and cannot be passed to a function at all — compile error if attempted.
- There is **no whole-instance assignment** (`a = b;` between two structs is rejected) and **no struct-typed return** — a function that needs to hand back struct-shaped data takes the struct as a parameter and mutates it, as `translate` does above.

See `examples/fluxio/struct_demo.fx`.

## Splitting a program across files

```c
include "include_lib/mathlib.fx";

/** entry point */
int main() {
    int m = fx_max(17, 42);
    return m;
}
```

`include "path.fx";` is textual inclusion, resolved relative to the *including* file's own directory (not the entry file's), spliced in before parsing. There is no module/namespace keyword — Fluxio stays a single flat global namespace, same as the rest of the language, so cross-file name collisions are caught by the ordinary "already defined" check. The convention is descriptive, prefixed names (`fx_max`, `fx_factorial`) rather than a namespacing mechanism, matching C/embedded-C practice.

Circular includes are a compile error, not infinite recursion, and a file included through two different paths (a diamond) contributes its tokens exactly once. Pass only the entry file to `fluxioc` — includes are resolved automatically. See `examples/fluxio/include_demo.fx` + `examples/fluxio/include_lib/mathlib.fx`.

## Cloister builtins

A curated set of builtins reaches Cloister's windowing/input surface (see [`ui.md`](ui.md)) — the Fluxio equivalent of Lux's `SCI`/`DRAW`/`APP` modules (`lib/draw.lux`, `lib/ui.lux`), wrapping the same underlying SCI syscalls and `/dev/draw` command buffer. They work identically whether the host is `bin/nux` (headless) or `bin/cloister` (windowed) — `machine_create()` wires up the same System/VFS/draw device either way.

| Builtin | Signature | Notes |
| --- | --- | --- |
| `emit(c)` | `int -> void` | write one ASCII byte to the console |
| `print(n)` | `int -> void` | write a decimal integer to the console |
| `vfs_open(path)` | `string -> int fd` | `path` must be a string literal |
| `vfs_close(fd)` | `int -> void` | |
| `yield()` | `-> void` | hand control back to the host for this frame |
| `set_window_title(title)` | `string -> void` | `title` must be a string literal |
| `canvas_size(fd)` | `int -> int` | packed `(w << 16) | h` |
| `begin_frame(fd)` / `end_frame(fd)` | `int -> void` | bracket a frame's draw commands |
| `fill_rect(fd, x, y, w, h, color)` | `int×5 -> void` | |
| `draw_str(fd, x, y, color, scale, text)` | `int×5, string -> void` | `text` must be a string literal |
| `poll_mouse(fd)` | `int -> int` | 1 if an event was read, else 0; fields via the accessors below |
| `mouse_type()` / `mouse_button()` / `mouse_x()` / `mouse_y()` | `-> int` | read fields of the last-polled mouse event |
| `poll_kbd(fd)` | `int -> int` | 1 if an event was read, else 0 |
| `kbd_type()` / `kbd_key()` | `-> int` | read fields of the last-polled keyboard event |

Two hard rules, not simplifications:

- **Path/title/text arguments must be a string literal at the call site** — `vfs_open("/dev/draw")`, never a computed string. Fluxio has no runtime string-as-value type yet, so these bytes are packed entirely at compile time.
- **`yield()` is a Cloister-protocol call, not the VM's `OP_YIELD` opcode.** A plain `bin/nux` run is one-shot — it executes straight through to `HALT` and stops at the *first* `yield()`, it does not pump multiple frames. A live host like `bin/cloister` calls `machine_tick()` once per real frame, which is what actually resumes execution after each `yield()`. A Fluxio program with a `while` frame loop will draw exactly one frame under `bin/nux` and animate correctly under `bin/cloister`.

```c
/** entry point */
int main() {
    int fd = vfs_open("/dev/draw");
    if (fd < 0) { return -1; }

    set_window_title("Fluxio says hello");

    int frame = 0;
    while (frame < 30) {
        begin_frame(fd);
        fill_rect(fd, 0, 0, 320, 200, 0x001020);
        fill_rect(fd, frame * 8, 80, 40, 40, 0xFF4020);
        draw_str(fd, 20, 20, 0xFFFFFF, 12, "Hello, Cloister!");
        end_frame(fd);
        yield();
        frame = frame + 1;
    }

    vfs_close(fd);
    return 0;
}
```

Compile for the target that will actually run it (`-target graphical` for `bin/cloister`, `-target headless` for `bin/nux`) — see [`using-fluxio.md`](using-fluxio.md). See `examples/fluxio/cloister_hello.fx` and `examples/fluxio/hello_cloister.fx`.

## What's not here (yet)

Deferred by design, not oversight — see `fluxio-language-plan.md` for the reasoning behind each: floats, general pointers/pointer arithmetic, `++`/`--` and compound assignment, ternary `?:`, `switch`, `do/while`, `goto`, unions, templates/generics, nested structs or arrays of structs, by-value struct copy/return, and runtime (non-literal) strings. Some of these are permanent (`goto`, unbounded recursion, dynamic allocation) under the JSF AV-inspired discipline Fluxio is built around; others are open for a future language slice if a real program needs them.

## Further reading

- [`using-fluxio.md`](using-fluxio.md) — compiler CLI, `-target`, running under `nux`/`cloister`.
- [`fluxio-language-plan.md`](fluxio-language-plan.md) — design rationale for every feature, versioned by slice (v1 → v2d).
- [`lux_tutorial.md`](lux_tutorial.md) — the sibling Forth-style language.
- [`opcodes.md`](opcodes.md) — the shared bytecode target.
- [`ui.md`](ui.md) — the Cloister devices/UI surface Fluxio's builtins wrap.
