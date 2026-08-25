# Fluxio: a C-like imperative language for the NUX VM

## Context

The user has an existing Forth-style concatenative language, Lux, that targets the NUX VM (a fantasy-console-style stack machine, 55 fixed opcodes 0x00-0x36, defined in `include/opcodes.h`/`docs/opcodes.md`). They want a second language for the same VM — this time ALGOL/BASIC/C-family imperative, not stack-juggling RPN — so programs can be written in a more conventional, readable style while still compiling to the same bytecode the VM already runs.

Decided with the user: C-like syntax (braces/semicolons/C declarations), name **Fluxio**, and a **minimal core v1** — get a real compile→run loop working end-to-end first, then layer on strings/arrays/structs/modules/stdlib bindings later. Lux's compiler (`src/compiler.c`) is a single-pass, Forth-specific emitter with no reusable backend abstraction, so Fluxio gets its own lexer/parser/codegen from scratch; only the opcode contract and a few VM conventions (frame-based locals, leading-JMP entry pattern) are shared.

Fluxio is also meant to bake in the discipline of the JSF AV (Joint Strike Fighter Air Vehicle) C++ coding standard — a safety-critical/real-time subset philosophy — as enforced language design, not an opt-in linter. See "Safety & language discipline" below for how each JSF AV concern maps onto v1 and what it constrains for later versions.

Key VM facts that shape the design:
- Single native type: 32-bit signed int. Most opcodes 1 byte; `PUSH/JMP/JZ/JNZ/CALL/LOAD/STORE` are 5 bytes (4-byte big-endian immediate).
- `FRAME`/`UNFRAME`/`LOCALGET`/`LOCALSET` implement a frame-pointer locals region **separate from the data stack** (confirmed in `src/vm.c:526-575`): `FRAME` pops `n` values off the data stack into a dedicated `locals[]` array; `UNFRAME` restores the frame pointer but does **not** touch the data stack. This means a return value pushed before `UNFRAME` survives it — the calling convention below depends on this.
- No bytecode file header — a compiled program is a raw blob loaded at a base address; VM execution starts at `pc = base_address`. Writes into the loaded program image fault (no self-modifying code), so mutable globals must live outside the image.
- Low RAM `[0, 0x10000)` is plain zero-initialized memory outside every program image (device/MMIO space starts at `0x10000`) — safe home for a globals segment.

## v1 scope

In: global/local `int` variables, functions with int params/return, `if`/`else`, `while`, `for`, standard arithmetic/comparison/bitwise/logical operators with short-circuit `&&`/`||`, decimal and `0x` hex literals, `//` and `/* */` comments, forward function calls, **explicitly-bounded** recursion (see below), a `main()` entry point, compiling to a raw `.bin` blob runnable by `bin/nux`.

**Post-v1 addition:** two reserved builtins, `emit(int)` and `print(int)`, wrapping the VM's `OUT` opcode (ASCII-char and decimal-integer console output respectively). Added because v1 as originally scoped had *no* I/O at all — the only observable result was `main()`'s return value at `HALT` — which made it impossible to write even a console "hello world." Not user-shadowable (compile error to declare a function or global named `emit`/`print`); not part of the user call graph (no arity/cycle checks against them beyond their own fixed 1-arg signature). See `src/fluxio_codegen.c` (`builtin_arity`) and `examples/hello_console.fx`. This does **not** reach Cloister's windowing/SCI surface — that remains v2 scope (string literals + SCI/VFS builtin bindings), see `apps/Hello.lux` for what a real windowed Lux equivalent requires.

Out (deferred to v2): floats, strings, arrays, pointers, structs/classes, modules/includes, stdlib/SCI syscall bindings, `++`/`--`, compound assignment, ternary, templates/generics, `goto`, unions.

## v2a: fixed-size arrays, string-literal initializers (implemented)

The first v2 slice, scoped down from the full deferred list to the most load-bearing gap: fixed-size `int` arrays (global and local) with bounds-checked `[]` indexing, and string literals usable only as array initializers. No general pointer type was added — `[]` remains the *only* access syntax, consistent with the "restricted pointer arithmetic" JSF constraint above.

**A real VM architecture constraint drove the design**, not just a simplification: `FRAME`/`UNFRAME`/`LOCALGET`/`LOCALSET` address a separate `locals[]` array in the VM, not main memory — local variables have no stable memory address the way globals do (`src/vm.c:526-575`). Consequences:
- **Global arrays** live in the globals segment (`FX_GLOBALS_BASE`, each consuming `4*array_len` bytes) — real, stable addresses, so they decay to a base-address `int` when used as a plain value (e.g. passed to a function), exactly like C array-to-pointer decay.
- **Local arrays** occupy `array_len` consecutive frame slots (via the existing `K` local-count / `LOCALGET`/`LOCALSET` offset machinery) — indexable within their own function, but **cannot decay to a value** at all. Using a local array as a bare expression (e.g. passing it to a function) is a compile error explaining why, not silent UB.
- **Function parameters** declared `int name[]` are, under the hood, an ordinary scalar local holding a decayed base address — so indexing on a parameter is *unchecked* (no compile-time-known length), same caveat C has for parameter arrays. Every other array access (locals, globals) is bounds-checked at runtime: an out-of-range index halts cleanly with sentinel `-2` (distinct from the recursion guard's `-1`) rather than corrupting memory — same discipline as the v1 recursion guard.

**String literals** (`"..."`) are lexed with `\n \t \r \0 \\ \"` escapes, but are *only* legal as the initializer of an array declaration: `int msg[] = "Hi";` (auto-sized to `strlen+1` for the NUL) or `int msg[10] = "Hi";` (explicit size, must fit, rest zero-padded). Since the string's content is a compile-time constant, initialization is unrolled directly into `PUSH char; STORE/LOCALSET` sequences at the declaration site (global-init stub, or in-place for locals) — no separate read-only data segment or address-fixup machinery was needed for this slice. A string literal used as a general expression value (not bound to a sized declaration) is **not supported** — that would need the trailing-data-segment + fixup mechanism (mirroring Lux's `T"..."` strings), deferred until something actually needs to pass a string around as a first-class value (e.g. the SCI/VFS binding slice).

Grammar additions: `type_suffix = "[" const_expr "]" | "[" "]"`, `ident "[" expr "]"` as a postfix on a *bare identifier only* (no `f()[i]` or `(expr)[i]` — indexing always needs a named binding to resolve against, by design). See `include/fluxio_ast.h` (`FX_INDEX`/`FX_INDEX_ASSIGN`, `FxGlobal`/local-decl `array_len`/`has_string_init`), `src/fluxio_codegen.c` (`FxBinding`/`resolve_binding`, `emit_bounds_check`). Examples: `examples/array_string_demo.fx`.

Still out of v2a (unchanged from the v1 "deferred" list): general pointers/pointer arithmetic, structs/classes, modules/includes, SCI/VFS syscall bindings, floats, `++`/`--`, compound assignment, ternary, templates/generics, unions, `goto`.

## v2b: Cloister bindings — SCI/VFS/draw builtins (implemented)

Reaches Cloister's windowing surface: a curated set of builtins wrapping the SCI syscall protocol (`src/system.c:11-14`) and the `/dev/draw` command-buffer format (`src/vfs.c` `draw_write`), so Fluxio can open the draw device, draw filled rectangles and text, and poll mouse/keyboard input — verified pixel-exact against the real framebuffer (`test_fill_rect_pixel_exact` in `src/test_fluxio_compiler.c`). See `examples/cloister_hello.fx` and `examples/hello_cloister.fx` (the canonical windowed hello-world — compile with `-target graphical` and run via `bin/cloister`).

**Correction found while building the canonical hello-world**: Fluxio's `yield()` builtin goes through the `SCI_CMD_YIELD` syscall (sets `System::yielded`), *not* the VM's own `OP_YIELD` bytecode opcode — so `vm_run()`/`vm_yielded()` never stop at it (a bare `vm_run()` runs an entire multi-frame program straight through to `HALT` in one shot, silently skipping the "pump between frames" step). `machine_tick()` (`src/machine.c`) is the primitive that actually checks `System::yielded`, once per call — that's what a real host's per-frame loop (`src/cloister.c`) calls repeatedly, and what test helpers must pump through to faithfully exercise frame-by-frame yielding (fixed in `run_machine_pumped`, `src/test_fluxio_compiler.c`). Relatedly: a `poll_mouse`/`poll_kbd` call on an *empty* input queue also implicitly sets `System::yielded` (`src/vfs.c`'s blocking-read-as-yield behavior) — a program can "yield" a frame just by polling with nothing queued, even without an explicit `yield()` call, so per-frame budgets need to account for that.

**Protocol ground truth** (researched directly from `src/system.c`/`src/vfs.c`, not inferred from Lux): a call is issued by `STORE`-ing cmd/arg1/(arg3) in any order, then `STORE`-ing arg2 **last** — that write is what fires `handle_sci()` — then `LOADI SCI_PORT` for the result. `SCI_CREATE_WIN`/`SCI_DRAW_RECT` etc. are dead stubs; real windowing is VFS-based: the host's window already exists before any program runs, and `vfs_open("/dev/draw")` just attaches to it. Drawing is one `SCI_VFS_WRITE` per packed command onto that fd (byte layout per command documented inline in `src/fluxio_codegen.c`'s `codegen_builtin_call`). Confirmed there is **no structural need for callbacks/function pointers** — Lux's `APP::on-frame!` quotation registration is a pure convenience layer over what is, underneath, a plain imperative poll loop (open devices once → `while(1) { begin_frame(); poll input; draw; end_frame(); yield(); }`). This is what let v2b ship without adding first-class function values to Fluxio at all.

**Builtins added** (`src/fluxio_codegen.c`, `FX_BUILTINS` table): `vfs_open`, `vfs_close`, `yield`, `set_window_title`, `canvas_size`, `begin_frame`, `end_frame`, `fill_rect`, `draw_str`, `poll_mouse` + `mouse_type`/`mouse_button`/`mouse_x`/`mouse_y`, `poll_kbd` + `kbd_type`/`kbd_key`.

**Two real design constraints, not simplifications:**
- **Path/title/text arguments must be string literals**, not general expressions. `FX_STR_LIT` was added as a general (parseable-anywhere) expression node, but codegen rejects it everywhere except these specific builtin-argument positions — enforced via a per-builtin `FxArgKind` table (`FX_ARG_INT` / `FX_ARG_STRING`), checked in `visit_call`. This is because Fluxio still has no general string-as-runtime-value type (v2a strings are either array-initializer sugar or nothing); a literal's bytes are packed entirely at *compile time* into a compiler-owned scratch buffer, so no runtime string-to-bytes conversion machinery was needed. Passing a dynamically-computed string (e.g. a runtime-formatted number) to `draw_str` is **not supported** — deferred until Fluxio gets a real string/byte-array value type.
- **Byte-level packing without exposing pointer arithmetic.** The SCI/draw wire format needs individual-byte fields (`i16`/`u32` LE, arbitrary sub-word offsets) that the VM's word-only `LOAD`/`STORE`/`LOADI`/`STOREI` can't address directly. Rather than expose `store_byte(addr, val)`/`load_byte(addr)` as *user-callable* Fluxio builtins — which would reintroduce exactly the raw-pointer-poking the JSF discipline restricts — they're **compiler-internal-only** codegen helpers (`emit_store_byte`/`emit_load_byte` in `src/fluxio_codegen.c`, never reachable from Fluxio source), implementing the standard aligned-word read-modify-write trick (memory words are big-endian per `src/vm.c` `write_mem32`/`read_mem32`: byte `k` within an aligned word sits at bit `(3-k)*8`). Every draw/SCI builtin call is built from these two primitives plus `emit_pack_field`/`emit_pack_const_byte`/`emit_pack_string_literal`; the user only ever calls named, fixed-arity builtins like `fill_rect(fd,x,y,w,h,color)`.

Scratch memory layout: 4 words for the store/load-byte intermediates + a persistent 8-byte mouse-event buffer + a persistent 8-byte kbd-event buffer + a 256-byte transient buffer (draw-command packing, string-literal packing, canvas-size reads) — allocated unconditionally after user globals and recursion slots (`FX_SCI_BUF_SIZE` etc. in `src/fluxio_codegen.c`), ~288 bytes total, negligible against the ~61KB low-RAM budget.

Still out of v2b: structs/classes, modules/includes, floats, `++`/`--`, compound assignment, ternary, templates/generics, unions, `goto`, general pointers, dynamic/runtime strings (and therefore dynamic paths/window titles/draw text), the fuller Lux `lib/draw.lux` command set (`draw_rect`, `fill_pat`, `set_font_size`, CFF custom fonts, oval/line/roundrect compositing).

## v2c: `include "path.fx";` — splitting a program across files (implemented)

Deliberately scoped down from "modules + includes": textual inclusion only, **no module/namespace keyword**. Fluxio stays a single flat global namespace, same as the rest of the language — cross-file name collisions are caught by the existing "already defined" duplicate checks (same as C's own single-TU compilation model), and the expected convention is descriptive/prefixed names (`fx_max`, `fx_factorial` in `examples/include_lib/mathlib.fx`), consistent with C/embedded-C practice and the JSF-inspired auditability goal generally. Real `MODULE`/`IMPORT` namespacing (like Lux has) was considered and explicitly rejected for this slice as unnecessary added complexity — worth reconsidering only if flat-namespace collisions turn out to be a real problem in practice.

**Mechanism**: a new file, `src/fluxio_include.c` / `include/fluxio_include.h`, exposes `fx_load_with_includes(main_path)` — a preprocessing pass that runs *before* `fx_parse`, entirely at the token-list level (the grammar/AST/codegen are untouched). It tokenizes the entry file, scans for `include "path.fx";` triplets (a new keyword, `FXTOK_KW_INCLUDE`), and for each one recursively resolves the path (relative to the *including* file's own directory via `realpath`; absolute paths used as-is), loads and tokenizes the target, and splices its tokens in place of the directive — tokens are *moved*, not copied, so no double-free/string-ownership bugs. `fx_parse` itself now hard-rejects a stray `FXTOK_KW_INCLUDE` token as a defensive check (it should never see one — a sign the caller skipped the preprocessing step).

**Two things that could have been silent bugs, handled explicitly:**
- **Circular includes are a compile error**, not infinite recursion — a "currently resolving" ancestor stack is checked before each recursive `include` resolution.
- **Once-only inclusion** (diamond dedup): a "globally seen" path set means the same file included transitively through two different paths (`main → a → base`, `main → b → base`) contributes its tokens exactly once, sharing state correctly — verified in `test_include_diamond_dedup` (two call sites bumping the same global counter through two different include paths, asserting the counter is genuinely shared, not duplicated-and-desynced). This is a deliberate improvement over C's bare `#include` (which needs manual header guards) and over what a naive reading of Lux's own `INCLUDE` directive would give you.

`fluxioc.c` was switched from manual `fopen`/`fread` + `fx_tokenize` to `fx_load_with_includes(filename)` as its first compilation step; everything downstream (`fx_parse`/`fx_codegen`) is unchanged. See `examples/include_demo.fx` + `examples/include_lib/mathlib.fx`.

## v2d: structs (implemented)

`struct Name { int field; ... }` — int-only fields (no nested structs, no array fields, no arrays-of-structs), no runtime bounds check needed (unlike `[i]`, every field offset is compile-time-valid by construction). This slice reuses almost the entire array mechanism rather than inventing new storage: a struct instance is exactly an array whose slots are addressed by field name (resolved to a compile-time-constant offset) instead of a runtime index expression — same storage allocation, same decay-to-address rules, same "no whole-instance assignment" rule, same "local instances can't decay" VM constraint.

**New naming convention**: struct type names must be `UpperCamelCase` (`^[A-Z][A-Za-z0-9]*$`), distinct from `lower_snake_case` for every other identifier (functions, variables, params, fields). Chosen for auditability (visually distinguish types from values), and as a side effect the two naming conventions are disjoint on the first character, so a struct name and a function/variable name can never collide — an emergent separate-namespace property without having to implement one.

**Reference semantics, same as arrays, deliberately**: no by-value struct copying anywhere. A global struct instance decays to its base address when passed to a function; a local struct instance cannot decay at all (same VM constraint as local arrays — `locals[]` has no stable address, `src/vm.c:526-575`) and is a compile error if attempted. Whole-instance assignment (`a = b;`) is rejected for the same "reference type, no implicit copy" reasoning, with an error pointing at field-by-field assignment instead.

**One genuinely new binding kind was needed**, beyond what arrays required: a struct-typed **parameter** is different from both an array parameter and a struct's own local storage. Array parameters are "unchecked" (no compile-time-known length) because the array's size isn't part of a plain `int arr[]` parameter's declared type. Struct parameters are different: the declared parameter type (`Point p`) *does* carry full field-offset information at compile time — only the struct's *address* is a runtime value (decayed from whatever the caller passed), not its layout. So there's a fourth `FxBindingKind`, `FX_BINDING_PARAM_STRUCT` (alongside `LOCAL_STRUCT`/`GLOBAL_STRUCT` for a struct's own storage): the parameter is an ordinary local scalar holding a runtime address, but field access on it is `LOCALGET` the pointer, then indirect `LOADI`/`STOREI` at a *compile-time-known* field byte offset — this is what makes `translate(Point p, ...) { p.x = ...; }` correctly mutate the caller's global struct through the passed reference (verified in `test_struct_param_decay_and_write_through`).

**No function can return a struct.** Functions are always `int name(...)`; there is no struct-typed return type, and no plan to add by-value struct returns in this scope (would need either a whole-struct-copy mechanism or an out-parameter convention, both undesigned). A function that needs to hand back struct-shaped data takes the struct as a parameter and mutates it (as `translate`/`set_xy` do in `examples/struct_demo.fx`/tests).

**Known gap, matching an existing one**: like array parameters, struct parameter arguments aren't type-checked against the declared parameter type beyond arity — passing a plain `int` where a struct parameter is expected compiles and would misuse whatever value was passed as a base address at the callee's field-access sites, with no compile-time diagnostic. This is the same class of gap arrays already have (no argument-type verification, `visit_call` only checks `nargs == nparams`); worth closing if it proves a real footgun in practice, but consistent with what came before rather than a new hole introduced by structs.

Still deferred: nested structs, array-of-struct fields, arrays of structs, struct-typed globals/locals with an initializer syntax (currently always zero-initialized), by-value struct copy/return, argument-type checking. See `examples/struct_demo.fx`.

## v2e: floating point (library, not a core language feature)

Fluxio's `Float` is a plain user-level `struct` of two `int`s (`lib/float.fx`), added with **zero changes to the lexer/parser/codegen**. This works because everything a fixed-point float type needs already existed from v2d: struct field read/write, struct-typed parameters that mutate the caller's instance through a decayed reference (the same mechanism `translate(Point p, ...)` uses in `examples/struct_demo.fx`), and no operator overloading to design around, since Fluxio never had any. Arithmetic is exposed as ordinary functions — `float_add(out, a, b)`, `float_mul(out, a, b)`, etc. — that write their result into an `out` struct parameter, because structs can never be returned by value (the existing v2d restriction) and there is no `+`/`*` overloading to hook into.

**Representation**: `struct Float { int whole; int frac; }`, value = `whole + frac/float_scale` (`float_scale = 10000`), with `|frac| < float_scale` and `sign(frac)` matching `sign(whole)` whenever `whole != 0` (`frac` may be negative only when `whole == 0`). This convention falls directly out of the VM's native truncating `DIV`/`MOD` (`src/vm.c:359-370`, C semantics) — normalizing a combined value is just `whole = total / float_scale; frac = total % float_scale;`, no floor/ceil adjustment needed. `float_scale = 10000` was chosen so `frac * frac` (up to `9999*9999 ≈ 10^8`) never overflows the VM's 32-bit signed `int`, and so `print_float` can emit the fraction as plain zero-padded decimal digits with no binary-to-decimal conversion.

**Known limitation, not a new one**: `float_mul`/`float_div` collapse each operand to a single scale-`float_scale` "total" integer (`whole*float_scale + frac`) and multiply/divide those, with no overflow detection — same as every other arithmetic op in Fluxio/the VM (no opcode traps on overflow anywhere). Correct for magnitudes roughly within ±4.0 (so total×total stays under 2^31); larger values silently wrap, exactly like any other `int` overflow already does in this language.

**Because `Float` is an ordinary struct**, all of v2d's struct rules apply unchanged: a local `Float` has no stable address and cannot be passed to a function (compile error, same message as any other local struct) — every argument to a `float_*` function, including `out`, must be a global `Float` or an incoming struct parameter. No whole-instance assignment; reference semantics only.

See `lib/float.fx` for the full function set (`int_to_float`, `float_to_int`, `float_add`/`sub`/`mul`/`div`/`neg`/`abs`, `float_eq`/`lt`/`gt`, `print_float`) and `examples/float_demo.fx` for usage.

## Safety & language discipline (JSF AV C++ inspired)

The user wants Fluxio to structurally enforce the JSF Air Vehicle C++ coding-standard philosophy — real-time/safety-critical discipline — as compiler-enforced constraints, not style suggestions. Most JSF AV rules target C++ OOP features (RTTI, multiple inheritance, templates) that v1 doesn't have yet, since v1 is deliberately non-OOP; those map onto **future constraints**, recorded here so the architecture doesn't paint us into a corner. A few rules bind immediately in v1:

| JSF AV concern | v1 disposition |
|---|---|
| No dynamic allocation after init | Satisfied by construction: the VM has no heap opcode; globals are fixed compile-time addresses, locals live in the frame-pointer region. No allocator will ever be added — enforced by simply never adding one. |
| No exceptions | Satisfied by omission: v1 grammar has no throw/catch/exception construct, and never will; all failure is either a compile error or a VM-level halt with a fault message. |
| No unbounded recursion | **New v1 requirement.** Recursive calls (direct or mutual, detected via a compile-time call-graph analysis over `FxFunc` call sites) are a **compile error unless the function is explicitly annotated** `recursive(N) int f(...)`, where `N` is a required, literal, compile-time-constant max depth. Codegen for a `recursive` function's prologue increments a dedicated depth counter (a reserved global slot, e.g. `FX_RECURSION_BASE`) and its epilogue decrements it; the increment path compares against `N` and, if exceeded, halts with a distinct fault (`PUSH` a sentinel + `HALT`, or emit a message via `OUT` before halting) rather than silently overrunning the VM's fixed-size 1024-entry return stack. Plain (non-`recursive`) functions calling themselves, directly or through a cycle, are rejected at compile time — this is the "bounded and explicit" reading of the JSF rule, not a ban. |
| No `goto` | The grammar has no `goto` and never will. |
| Restricted pointer arithmetic | N/A in v1 (no pointers). Binding constraint for v2: when arrays/pointers are added, expose only bounds-checked indexing syntax (`a[i]`) in source; no raw address arithmetic surfaces at the language level even though codegen computes addresses under the hood. |
| Restricted unions | N/A in v1 (no unions). If ever added: discriminated/tagged unions only, no untagged raw reinterpretation. |
| No RTTI, restricted multiple inheritance, restricted templates | N/A in v1 (no OOP/generics at all). Binding constraint for whenever classes are considered: no `dynamic_cast`/`typeid`-equivalent ever; multiple inheritance, if added, restricted to pure-abstract interface bases only (single implementation base, JSF AV Rule-96 style); templates/generics, if added, restricted to a small monomorphizing subset with compile-time-bounded instantiation, not arbitrary metaprogramming. |
| Compile-time checks favored over runtime | Consistent with the existing design: undefined variable/function, redeclaration, arity mismatches, non-constant global initializers, and (new) unbounded-recursion cycles are all rejected at compile time in `fluxio_parser.c`/`fluxio_codegen.c`, not deferred to a VM fault. |
| Strict naming/formatting for auditability | **New v1 requirement, extended in v2d.** The lexer/parser enforce an identifier convention as a compile error, not a lint warning: function/variable/field identifiers must be `lower_snake_case`; struct type names must be `UpperCamelCase` (v2d — visually distinguishes types from values, and the disjoint first-character classes mean the two conventions can never collide). Every top-level `func_decl` *and* `struct_decl` must be preceded by a `/** ... */` doc comment (checked by the parser, which tracks the last comment span and its adjacency to the following declaration) — a missing or non-adjacent doc comment is a compile error. |

This section changes two things in the concrete plan below versus a "plain C" design: (1) recursion requires the `recursive(N)` annotation and a call-graph legality check plus a runtime depth guard, and (2) the parser enforces snake_case identifiers and mandatory doc comments on functions, both as hard compile errors.

## File layout

New files, mirroring the `luxc` naming pattern:

| File | Role |
|---|---|
| `include/fluxio_token.h` | Token type enum, `FxToken`, `FxTokenList` |
| `src/fluxio_lexer.c` | Scanner → `FxTokenList` |
| `include/fluxio_ast.h` | AST node structs (below) |
| `src/fluxio_ast.c` | AST node alloc/free |
| `include/fluxio_parser.h` / `src/fluxio_parser.c` | Recursive-descent + precedence-climbing parser → `FxProgram*` |
| `include/fluxio_codegen.h` / `src/fluxio_codegen.c` | AST → bytecode, symbol tables, backpatching, globals allocation |
| `src/fluxioc.c` | CLI driver (arg parsing, `-o`, `-target`, `-dumpAt`) — modeled directly on `src/luxc.c` |
| `src/test_fluxio_compiler.c` | Compile-and-run tests, modeled on `src/test_compiler.c` |

Reused unmodified: `include/opcodes.h`, `include/vm.h`, `src/vm.c`, `src/system.c`, `src/machine.c`, `src/display.c`, `src/vfs.c` (linked in for the VM the driver/tests run programs on).

## Grammar (EBNF)

```
program        = { top_decl } ;
top_decl       = func_decl | global_decl ;
global_decl    = "int" ident [ "=" const_expr ] ";" ;
func_decl      = doc_comment [ "recursive" "(" int_literal ")" ] "int" ident "(" [ param_list ] ")" block ;
doc_comment    = (* a `/** ... */` block comment immediately preceding the declaration, no blank/code line between *) ;
ident          = lower_snake_case ;   (* compiler-enforced: /^[a-z][a-z0-9_]*$/, compile error otherwise *)
param_list     = param { "," param } ;
param          = "int" ident ;
block          = "{" { statement } "}" ;
statement      = local_decl | if_stmt | while_stmt | for_stmt
               | return_stmt | block | expr_stmt | ";" ;
local_decl     = "int" ident [ "=" expr ] ";" ;
if_stmt        = "if" "(" expr ")" statement [ "else" statement ] ;
while_stmt     = "while" "(" expr ")" statement ;
for_stmt       = "for" "(" [ for_init ] ";" [ expr ] ";" [ expr ] ")" statement ;
return_stmt    = "return" [ expr ] ";" ;
expr_stmt      = expr ";" ;
expr           = assignment ;
assignment     = ident "=" assignment | logical_or ;   (* lvalue: bare ident only in v1 *)
logical_or     = logical_and { "||" logical_and } ;
logical_and    = bit_or { "&&" bit_or } ;
bit_or         = bit_xor { "|" bit_xor } ;
bit_xor        = bit_and { "^" bit_and } ;
bit_and        = equality { "&" equality } ;
equality       = relational { ("==" | "!=") relational } ;
relational     = shift { ("<" | "<=" | ">" | ">=") shift } ;
shift          = additive { ("<<" | ">>") additive } ;
additive       = multiplicative { ("+" | "-") multiplicative } ;
multiplicative = unary { ("*" | "/" | "%") unary } ;
unary          = ("-" | "!" | "~" | "+") unary | primary ;
primary        = number | ident | ident "(" [ arg_list ] ")" | "(" expr ")" ;
arg_list       = expr { "," expr } ;
number         = dec_int | hex_int ;   (* hex = 0x[0-9a-fA-F]+, matches Lux *)
```

Operator precedence lowest→highest: `=` , `||` , `&&` , `|` , `^` , `&` , `==`/`!=` , `<`/`<=`/`>`/`>=` , `<<`/`>>` , `+`/`-` , `*`/`/`/`%` , unary , call. `>>` compiles to `SAR` (signed/arithmetic).

## AST node design (`include/fluxio_ast.h`)

Single `FxNode` struct with a `FxNodeKind` tag and a union of payloads: `FX_INT_LIT`, `FX_VAR_REF`, `FX_ASSIGN`, `FX_BINARY`/`FX_UNARY` (tagged by an `FxOp` enum decoupled from token spelling), `FX_CALL`, `FX_LOCAL_DECL`, `FX_EXPR_STMT`, `FX_IF`, `FX_WHILE`, `FX_FOR`, `FX_RETURN`, `FX_BLOCK`, `FX_EMPTY`. Top level: `FxFunc { name, FxParam* params, int nparams, FxNode* body, bool is_recursive, int32_t max_depth }` (`max_depth` only meaningful when `is_recursive`) and `FxGlobal { name, has_init, int32_t init_value }`, collected into `FxProgram { FxGlobal* globals; FxFunc* funcs; }`.

## Calling convention (normative)

For a function with `P` params and `K` non-param locals (counted by a pre-pass over the body, all nested blocks), `L = P + K`.

**Call site**: push args left-to-right, `CALL <addr_f>`; return value is the single value left on the data stack.

**Callee prologue**: `PUSH 0` ×K (placeholder locals) ; `PUSH L` ; `FRAME`. Resulting offsets: body local `j` (declaration order) → `LOCALGET` offset `K-1-j`; param `i` (left-to-right) → offset `L-1-i`. Functions with `L==0` skip FRAME/UNFRAME entirely.

**Return**: `<eval expr>` (or `PUSH 0` for bare `return;`) ; `PUSH L` ; `UNFRAME` (omitted if `L==0`) ; `RET`. This works because `UNFRAME` doesn't touch the data stack, so the pushed result survives. Falling off the end of a function emits an implicit `return;`.

**`main`**: an ordinary function (`P=0`) but terminates in `HALT` instead of `RET`/`RET`-epilogue, and is preceded by a global-init stub (`PUSH <const>; STORE <addr>` per initialized global).

**Recursion guard** (only emitted for functions declared `recursive(N)`): prologue, before `FRAME`, does `LOAD FX_RECURSION_BASE+4*idx; INC; DUP; STORE FX_RECURSION_BASE+4*idx; PUSH N; GT; JZ Lok; <emit fault: PUSH -1; HALT>; Lok:` (one reserved counter slot per recursive function, allocated alongside globals). Epilogue, after producing the return value but before `RET`, decrements the same counter (`LOAD ...; DEC; STORE ...`). A plain (non-`recursive`) function that the call-graph pass finds in a cycle is a compile error — it never reaches codegen.

## Codegen per construct

- Int literal → `PUSH n`. Local var → `PUSH <offset>; LOCALGET`. Global var → `LOAD <addr>`.
- Assignment (leaves value, so composes as expression) → `<e>; DUP; PUSH <offset>; LOCALSET` (local) or `<e>; DUP; STORE <addr>` (global).
- Binary op → `<a>; <b>; <OPCODE>` (source order matches VM's `second op top` convention for SUB/DIV/MOD/shifts).
- Unary `-`/`~`/`!`/`+` → `NEG` / `NOT` / `(EQ 0)` / no-op.
- `&&`: `<a>;JZ Lfalse; <b>;JZ Lfalse; PUSH 1;JMP Lend; Lfalse: PUSH 0; Lend:`. `||` mirrors with `JNZ`/`Ltrue`.
- Call → args pushed left-to-right, `CALL <addr_f>` (backpatched if forward-declared).
- `if/else`, `while`, `for` → standard `JZ`/`JMP`-threaded control flow (see full agent output for exact byte sequences — straightforward structured-jump patterns).
- Expr-statement discards its value with `POP`.

## Address allocation & backpatching

**Globals**: `FX_GLOBALS_BASE = 0x1000`; global `i` → `addr = FX_GLOBALS_BASE + 4*i` (word-aligned). This range is outside every program image and below device space (`0x10000`), so `STORE` never faults. Compile error if allocation would cross `0x10000`.

**Functions**: emitted in declaration order (main last). Each function's address is recorded as `base_addr + code.len` immediately before its body is emitted — no size pre-pass needed. Calls to not-yet-known functions emit `CALL 0` plus a fixup record `{imm_offset, name}`; after all bodies are emitted, the fixup list is walked and each immediate patched via the resolved function-address table (undefined name → compile error). This is the AST-clean analogue of Lux's `UnresolvedRef`/`PatchRequest` mechanism in `include/compiler.h`, simplified since targets are always absolute call addresses.

**Call-graph legality pass** (runs after parsing, before codegen): build a directed graph of function→function call sites from the AST, find cycles (including self-loops) via DFS, and for every function in a cycle require `is_recursive == true`; any cycle containing a non-`recursive` function is a compile error naming the cycle. Each `recursive` function gets one reserved counter slot at `FX_RECURSION_BASE + 4*idx` (own sub-range of the globals allocator, immediately after user globals).

## Program image layout

```
offset 0: JMP <main_entry>              ; patched last
          <function bodies>, each ending RET
          ...
main_entry:
          <global-init stub>
          <main prologue (FRAME if L>0)>
          <main body>
          HALT
```
Same leading-JMP-to-main pattern as `src/compiler.c:962-995`. No data segment after code in v1 (no strings/arrays yet); `image_end = base_addr + code.len`.

## Makefile integration

Add `FLUXIO_COMPILER_SRCS`/`_OBJS` (lexer, ast, parser, codegen) and `FLUXIOC_SRCS`/`_OBJS` (= VM_SRCS + SYS_SRCS + vfs.c + FLUXIO_COMPILER_SRCS + fluxioc.c), following the exact pattern of `LUXC_SRCS`/`LUXC_OBJS` and their link rule. Add `bin/fluxioc` and `bin/test_fluxio_compiler` to `TARGETS`, with link rules copied from the `luxc`/`test_compiler` rules, and wire `test_fluxio_compiler` into the `test:` target.

## Test strategy (`src/test_fluxio_compiler.c`)

Modeled on `src/test_compiler.c` (`test_output_handler`, `must_compile`, `run_and_capture`, stack-content assertions — Fluxio has no print builtin in v1, so tests read `main`'s return value off the data stack at `HALT`). Coverage: lexer (literals/operators/comments), expression precedence, short-circuit evaluation (side-effect-observing test via a global write), globals (read/write/init/default-zero), locals/params (including nested-block shadowing), control flow (if/else/while/for incl. empty clauses), functions (forward call, `recursive(N)`-annotated plain and mutual recursion, leaf `L==0` path, fall-off-end), frame-offset correctness on a function with both params and locals, recursion-guard behavior (halts cleanly at depth `N+1` instead of corrupting the return stack), call-graph cycle rejection when `recursive` is omitted, and compile-error paths (undefined function/variable, syntax errors, redeclared global, missing `main`, non-constant global initializer, assignment to non-lvalue, non-snake_case identifier, function missing its doc comment). Add 1-2 end-to-end fixtures compiled by `bin/fluxioc` and run via `bin/nux` to validate the on-disk blob path.

## Milestones (runnable artifact at each step)

1. Lexer + token dump mode.
2. Expression parser + AST + pretty-printer (`fluxioc -ast`).
3. Codegen for a single top-level expression → `HALT`; run in VM, assert stack top. Validates code buffer, arithmetic/bitwise/compare opcodes, big-endian immediates.
4. Statements + globals, no functions yet (if/else/while/for wrapped in a synthetic main; global decl/access/init).
5. Real functions: local pre-pass, FRAME/UNFRAME/LOCALGET/LOCALSET, params/locals, return, call sites.
6. Forward-call backpatch + undefined-function errors; call-graph cycle detection; `recursive(N)` annotation, parsing, and the runtime depth-guard codegen; snake_case identifier + mandatory doc-comment enforcement.
7. `fluxioc` CLI + Makefile targets; compile `.fx` → blob, run under `bin/nux`.
8. Error-path hardening, full suite wired into `make test`.

## Verification

After each milestone, `make` builds cleanly and `make test` passes (once `test_fluxio_compiler` exists). End-to-end: `bin/fluxioc -o out.bin examples/foo.fx && bin/nux out.bin` should run and (via a temporary debug dump or the test harness) show the expected `main()` return value on the stack at `HALT`.

### Critical files
- `src/vm.c:526-575` — FRAME/UNFRAME/LOCALGET/LOCALSET semantics (the calling-convention contract)
- `src/compiler.c:958-1070` — entry-JMP/backpatch/emission-order reference pattern
- `include/opcodes.h` — opcode constants for codegen
- `src/luxc.c` — CLI driver template for `src/fluxioc.c`
- `Makefile` — build-target integration pattern
