#include "fluxio_token.h"
#include "fluxio_include.h"
#include "fluxio_parser.h"
#include "fluxio_codegen.h"
#include "machine.h"
#include "vm.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <stdbool.h>
#include <unistd.h>

/* -----------------------------------------------------------------------
 * Output capture for emit()/print()
 * ----------------------------------------------------------------------- */
static char output_buffer[4096];
static int output_len = 0;

static void test_output_handler(int32_t value, int32_t format) {
    if (output_len >= (int) sizeof(output_buffer) - 1) return;
    if (format == 0) {
        int n = snprintf(output_buffer + output_len, sizeof(output_buffer) - output_len, "%d", value);
        if (n > 0) output_len += n;
    } else if (format == 1) {
        output_buffer[output_len++] = (char) value;
    }
    output_buffer[output_len] = '\0'; /* keep it a valid C string after every write, not just decimal ones */
}

static void reset_output(void) {
    output_buffer[0] = '\0';
    output_len = 0;
}

/* -----------------------------------------------------------------------
 * Helpers
 * ----------------------------------------------------------------------- */

static uint8_t* must_compile(const char* source, size_t* out_len) {
    FxTokenList* tokens = fx_tokenize(source);
    if (!tokens) {
        fprintf(stderr, "FATAL: lex failed for:\n%s\n", source);
        assert(tokens != NULL);
    }
    FxProgram* program = fx_parse(tokens);
    fx_token_list_free(tokens);
    if (!program) {
        fprintf(stderr, "FATAL: parse failed for:\n%s\n", source);
        assert(program != NULL);
    }
    uint8_t* bc = fx_codegen(program, HEADLESS_BASE_ADDRESS, out_len);
    fx_program_free(program);
    if (!bc) {
        fprintf(stderr, "FATAL: codegen failed for:\n%s\n", source);
        assert(bc != NULL);
    }
    return bc;
}

/* Returns true iff compilation is rejected at lex, parse, or codegen stage. */
static bool must_fail_compile(const char* source) {
    FxTokenList* tokens = fx_tokenize(source);
    if (!tokens) return true;
    FxProgram* program = fx_parse(tokens);
    fx_token_list_free(tokens);
    if (!program) return true;
    size_t len;
    uint8_t* bc = fx_codegen(program, HEADLESS_BASE_ADDRESS, &len);
    fx_program_free(program);
    if (!bc) return true;
    free(bc);
    return false;
}

static VM* run_and_capture(const uint8_t* bc, size_t len) {
    VM* vm = vm_create(bc, (uint32_t) len, HEADLESS_BASE_ADDRESS, 4 * 1024 * 1024, false);
    assert(vm != NULL);
    vm_run(vm);
    return vm;
}

static VM* run_capturing_output(const uint8_t* bc, size_t len) {
    VM* vm = vm_create(bc, (uint32_t) len, HEADLESS_BASE_ADDRESS, 4 * 1024 * 1024, false);
    assert(vm != NULL);
    vm->output_handler = test_output_handler;
    vm_run(vm);
    return vm;
}

static void check_stack_top(VM* vm, int32_t expected) {
    int32_t v;
    bool ok = vm_pop(vm, &v);
    assert(ok);
    assert(v == expected);
}

/* The Cloister-binding builtins (vfs_open/fill_rect/poll_mouse/...) need the
 * System + device-bus wiring that plain vm_create() doesn't provide, so
 * those tests run through machine_create() instead. Fluxio's yield()
 * builtin goes through the SCI_CMD_YIELD syscall (sets system->yielded),
 * NOT the VM's own OP_YIELD opcode -- vm_run()/vm_yielded() don't stop for
 * it at all (a plain vm_run() runs straight through every yield() call to
 * HALT in one shot). machine_tick() is the primitive that actually checks
 * system->yielded, one call per "frame" -- it's what a real host (see
 * src/cloister.c's main loop) calls repeatedly to drive a program forward
 * one yield at a time, so pumping through it here is what actually
 * exercises per-frame yielding instead of accidentally no-op'ing it. */
static Machine* run_machine_pumped(const uint8_t* bc, size_t len, int max_frames) {
    Machine* m = machine_create(bc, (uint32_t) len, HEADLESS_BASE_ADDRESS, 32 * 1024 * 1024, false);
    assert(m != NULL);
    int frames = 0;
    while (machine_tick(m) && frames < max_frames) {
        frames++;
    }
    return m;
}

/* Compile+run a program whose main() leaves a single int on the stack at
 * HALT, and assert it equals `expected`. */
static void check_result(const char* source, int32_t expected) {
    size_t len;
    uint8_t* bc = must_compile(source, &len);
    VM* vm = run_and_capture(bc, len);
    assert(vm->halted);
    check_stack_top(vm, expected);
    vm_free(vm);
    free(bc);
}

/* Same as check_result, but via machine_create() (System + bus wired up)
 * and pumped across yields, for tests exercising the Cloister bindings. */
static void check_machine_result(const char* source, int32_t expected, int max_frames) {
    size_t len;
    uint8_t* bc = must_compile(source, &len);
    Machine* m = run_machine_pumped(bc, len, max_frames);
    assert(m->cpu->halted);
    check_stack_top(m->cpu, expected);
    machine_free(m);
    free(bc);
}

/* -----------------------------------------------------------------------
 * Filesystem helpers for `include` tests -- the include mechanism is
 * inherently file-based (paths resolve relative to the including file's
 * directory), so it can't be exercised through the in-memory-source
 * must_compile() path used by everything else in this file.
 * ----------------------------------------------------------------------- */

static void write_temp_file(const char* dir, const char* name, const char* content) {
    char path[1024];
    snprintf(path, sizeof(path), "%s/%s", dir, name);
    FILE* f = fopen(path, "w");
    assert(f != NULL);
    fputs(content, f);
    fclose(f);
}

/* Compiles `entry_name` (within `dir`, with includes resolved) and asserts
 * its main() returns `expected`. */
static void check_include_result(const char* dir, const char* entry_name, int32_t expected) {
    char path[1024];
    snprintf(path, sizeof(path), "%s/%s", dir, entry_name);
    FxTokenList* tokens = fx_load_with_includes(path);
    assert(tokens != NULL);
    FxProgram* program = fx_parse(tokens);
    fx_token_list_free(tokens);
    assert(program != NULL);
    size_t len;
    uint8_t* bc = fx_codegen(program, HEADLESS_BASE_ADDRESS, &len);
    fx_program_free(program);
    assert(bc != NULL);
    VM* vm = run_and_capture(bc, len);
    assert(vm->halted);
    check_stack_top(vm, expected);
    vm_free(vm);
    free(bc);
}

/* Returns true iff fx_load_with_includes() itself rejects `entry_name`
 * (I/O error, malformed directive, or circular include). */
static bool include_load_fails(const char* dir, const char* entry_name) {
    char path[1024];
    snprintf(path, sizeof(path), "%s/%s", dir, entry_name);
    FxTokenList* tokens = fx_load_with_includes(path);
    if (!tokens) return true;
    fx_token_list_free(tokens);
    return false;
}

/* -----------------------------------------------------------------------
 * Lexer
 * ----------------------------------------------------------------------- */

static void test_lexer_literals_and_operators(void) {
    printf("Testing lexer literals and operators...\n");
    FxTokenList* toks = fx_tokenize("42 0xFF 0x0a + - * / % & | ^ ~ ! && || << >> == != <= >= < >");
    assert(toks != NULL);
    assert(toks->tokens[0].type == FXTOK_INT_LIT && toks->tokens[0].int_value == 42);
    assert(toks->tokens[1].type == FXTOK_INT_LIT && toks->tokens[1].int_value == 255);
    assert(toks->tokens[2].type == FXTOK_INT_LIT && toks->tokens[2].int_value == 10);
    fx_token_list_free(toks);
}

static void test_lexer_comments(void) {
    printf("Testing lexer comments...\n");
    FxTokenList* toks = fx_tokenize("1 // line comment\n/* block\ncomment */ 2");
    assert(toks != NULL);
    assert(toks->tokens[0].type == FXTOK_INT_LIT && toks->tokens[0].int_value == 1);
    assert(toks->tokens[1].type == FXTOK_INT_LIT && toks->tokens[1].int_value == 2);
    fx_token_list_free(toks);
}

static void test_lexer_doc_comment_tracking(void) {
    printf("Testing lexer doc-comment tracking...\n");
    FxTokenList* toks = fx_tokenize("/** doc */\nint");
    assert(toks != NULL);
    assert(toks->tokens[0].type == FXTOK_KW_INT);
    assert(toks->tokens[0].has_doc_comment);
    fx_token_list_free(toks);

    /* A plain (non-doc) block comment must NOT be attached. */
    toks = fx_tokenize("/* plain */\nint");
    assert(toks != NULL);
    assert(!toks->tokens[0].has_doc_comment);
    fx_token_list_free(toks);
}

/* -----------------------------------------------------------------------
 * Expressions / precedence
 * ----------------------------------------------------------------------- */

static void test_arithmetic_precedence(void) {
    printf("Testing arithmetic precedence...\n");
    check_result("/** e */\nint main() { return 2 + 3 * 4; }", 14);
    check_result("/** e */\nint main() { return (2 + 3) * 4; }", 20);
    check_result("/** e */\nint main() { return 1 << 3; }", 8);
    check_result("/** e */\nint main() { return -5 % 3; }", -2);
    check_result("/** e */\nint main() { return ~0; }", -1);
    check_result("/** e */\nint main() { return !0; }", 1);
    check_result("/** e */\nint main() { return !5; }", 0);
    check_result("/** e */\nint main() { return 6 & 3 | 8; }", 10);
    check_result("/** e */\nint main() { return - -5; }", 5);
}

static void test_short_circuit(void) {
    printf("Testing short-circuit && and ||...\n");
    check_result(
        "int touched = 0;\n"
        "/** marks touched */\n"
        "int mark() { touched = 1; return 1; }\n"
        "/** e */\n"
        "int main() { int r = 0 && mark(); r = r; return touched; }", 0);
    check_result(
        "int touched = 0;\n"
        "/** marks touched */\n"
        "int mark() { touched = 1; return 1; }\n"
        "/** e */\n"
        "int main() { int r = 1 || mark(); r = r; return touched; }", 0);
    check_result(
        "/** e */\n"
        "int main() { return (1 && 1) + (0 || 1) + (1 && 0) + (0 || 0); }", 2);
}

/* -----------------------------------------------------------------------
 * Globals
 * ----------------------------------------------------------------------- */

static void test_globals(void) {
    printf("Testing globals...\n");
    check_result("int g = 7;\n/** e */\nint main() { return g; }", 7);
    check_result("int g;\n/** e */\nint main() { return g; }", 0); /* default zero */
    check_result("int g = 1;\n/** e */\nint main() { g = g + 41; return g; }", 42);
    check_result(
        "int g = 0;\n"
        "/** bumps g */\n"
        "int bump() { g = g + 1; return g; }\n"
        "/** e */\n"
        "int main() { bump(); bump(); return bump(); }", 3);
}

/* -----------------------------------------------------------------------
 * Locals / params / shadowing
 * ----------------------------------------------------------------------- */

static void test_locals_and_params(void) {
    printf("Testing locals and params...\n");
    check_result("/** adds */\nint add(int a, int b) { return a + b; }\n/** e */\nint main() { return add(3, 4); }", 7);
    check_result("/** e */\nint main() { int x = 3; int y = 4; return x * y; }", 12);
    check_result("/** e */\nint main() { int x = 1; { int x = 2; x = x + 1; } return x; }", 1);
    check_result(
        "/** e */\n"
        "int main() { int a = 1; int b = 2; int c = 3; return a + b + c; }", 6);
}

static void test_frame_offset_correctness(void) {
    printf("Testing frame offset correctness (params + locals mix)...\n");
    /* 3 params, 2 locals: exercises the K-1-j / L-1-i offset formulas together. */
    check_result(
        "/** weighted sum with locals */\n"
        "int calc(int a, int b, int c) {\n"
        "    int x = a * 2;\n"
        "    int y = b * 3;\n"
        "    return x + y + c;\n"
        "}\n"
        "/** e */\n"
        "int main() { return calc(1, 2, 3); }", 11);
}

/* -----------------------------------------------------------------------
 * Control flow
 * ----------------------------------------------------------------------- */

static void test_if_else(void) {
    printf("Testing if/else...\n");
    check_result("/** e */\nint main() { if (1) { return 1; } return 0; }", 1);
    check_result("/** e */\nint main() { if (0) { return 1; } return 0; }", 0);
    check_result("/** e */\nint main() { if (0) { return 1; } else { return 2; } }", 2);
    check_result("/** e */\nint main() { int r; if (3 > 5) { r = 1; } else { r = 2; } return r; }", 2);
}

static void test_while_loop(void) {
    printf("Testing while...\n");
    check_result(
        "/** e */\n"
        "int main() { int i = 0; int s = 0; while (i < 5) { s = s + i; i = i + 1; } return s; }", 10);
    check_result("/** e */\nint main() { int i = 5; while (0) { i = 99; } return i; }", 5);
}

static void test_for_loop(void) {
    printf("Testing for (incl. empty clauses)...\n");
    check_result(
        "/** e */\n"
        "int main() { int s = 0; for (int i = 0; i < 10; i = i + 1) { s = s + i; } return s; }", 45);
    check_result(
        "/** e */\n"
        "int main() { int i = 0; int s = 0; for (;;) { if (i >= 5) { return s; } s = s + i; i = i + 1; } }", 10);
}

/* -----------------------------------------------------------------------
 * Functions: forward calls, recursion, leaf/fall-off-end
 * ----------------------------------------------------------------------- */

static void test_forward_call(void) {
    printf("Testing forward function call...\n");
    check_result(
        "/** e */\n"
        "int main() { return helper(6); }\n"
        "/** doubles */\n"
        "int helper(int x) { return x * 2; }", 12);
}

static void test_leaf_and_falloff(void) {
    printf("Testing leaf functions (L==0) and fall-off-end...\n");
    check_result("/** returns constant */\nint five() { return 5; }\n/** e */\nint main() { return five(); }", 5);
    check_result("/** falls off, no explicit return */\nint noop() { int x = 1; }\n/** e */\nint main() { noop(); return 42; }", 42);
}

static void test_recursion_bounded(void) {
    printf("Testing recursive(N) plain and mutual recursion...\n");
    check_result(
        "/** fib */\n"
        "recursive(32) int fib(int n) { if (n < 2) { return n; } return fib(n - 1) + fib(n - 2); }\n"
        "/** e */\n"
        "int main() { return fib(10); }", 55);
    check_result(
        "/** even? */\n"
        "recursive(64) int is_even(int n) { if (n == 0) { return 1; } return is_odd(n - 1); }\n"
        "/** odd? */\n"
        "recursive(64) int is_odd(int n) { if (n == 0) { return 0; } return is_even(n - 1); }\n"
        "/** e */\n"
        "int main() { return is_even(10); }", 1);
}

static void test_recursion_guard_halts(void) {
    printf("Testing recursion depth guard halts cleanly past N...\n");
    size_t len;
    uint8_t* bc = must_compile(
        "/** unbounded by construction, bounded by the guard */\n"
        "recursive(5) int spin(int n) { return spin(n + 1); }\n"
        "/** e */\n"
        "int main() { return spin(0); }", &len);
    VM* vm = run_and_capture(bc, len);
    assert(vm->halted);
    check_stack_top(vm, -1); /* sentinel emitted by the guard's fault path */
    vm_free(vm);
    free(bc);
}

/* -----------------------------------------------------------------------
 * Builtins: emit()/print(), v1's entire I/O surface
 * ----------------------------------------------------------------------- */

static void test_builtin_emit_and_print(void) {
    printf("Testing emit()/print() builtins...\n");

    reset_output();
    {
        size_t len;
        uint8_t* bc = must_compile(
            "/** e */\n"
            "int main() { emit(72); emit(105); emit(10); return 0; }", &len);
        VM* vm = run_capturing_output(bc, len);
        assert(vm->halted);
        assert(strcmp(output_buffer, "Hi\n") == 0);
        vm_free(vm);
        free(bc);
    }

    reset_output();
    {
        size_t len;
        uint8_t* bc = must_compile(
            "/** e */\n"
            "int main() { print(42); return 0; }", &len);
        VM* vm = run_capturing_output(bc, len);
        assert(vm->halted);
        assert(strcmp(output_buffer, "42") == 0);
        vm_free(vm);
        free(bc);
    }

    /* emit()/print() compose as expressions (dummy 0 result), so a loop
     * calling emit() in a larger expression statement must still work. */
    reset_output();
    {
        size_t len;
        uint8_t* bc = must_compile(
            "/** prints digits 0..4 */\n"
            "int main() { for (int i = 0; i < 5; i = i + 1) { emit(48 + i); } return 0; }", &len);
        VM* vm = run_capturing_output(bc, len);
        assert(vm->halted);
        assert(strcmp(output_buffer, "01234") == 0);
        vm_free(vm);
        free(bc);
    }
}

/* -----------------------------------------------------------------------
 * v2: fixed-size arrays (global + local), bounds-checked indexing
 * ----------------------------------------------------------------------- */

static void test_arrays_global(void) {
    printf("Testing global arrays...\n");
    check_result(
        "int arr[5];\n"
        "/** e */\n"
        "int main() { arr[0] = 10; arr[4] = 20; return arr[0] + arr[4]; }", 30);
    check_result(
        "int arr[3];\n"
        "/** e */\n"
        "int main() { return arr[0] + arr[1] + arr[2]; }", 0); /* default zero */
    check_result(
        "int arr[5];\n"
        "/** e */\n"
        "int main() {\n"
        "    for (int i = 0; i < 5; i = i + 1) { arr[i] = i * i; }\n"
        "    int s = 0;\n"
        "    for (int i = 0; i < 5; i = i + 1) { s = s + arr[i]; }\n"
        "    return s;\n"
        "}", 30); /* 0+1+4+9+16 */
}

static void test_arrays_local(void) {
    printf("Testing local arrays...\n");
    check_result(
        "/** e */\n"
        "int main() { int arr[3]; arr[0] = 1; arr[1] = 2; arr[2] = 3; return arr[0]+arr[1]+arr[2]; }", 6);
}

static void test_arrays_bounds_check(void) {
    printf("Testing array bounds checks halt cleanly...\n");
    size_t len;
    uint8_t* bc;
    VM* vm;

    bc = must_compile("int arr[3];\n/** e */\nint main() { return arr[-1]; }", &len);
    vm = run_and_capture(bc, len);
    assert(vm->halted);
    check_stack_top(vm, -2); /* sentinel distinct from the recursion guard's -1 */
    vm_free(vm); free(bc);

    bc = must_compile("int arr[3];\n/** e */\nint main() { return arr[3]; }", &len);
    vm = run_and_capture(bc, len);
    assert(vm->halted);
    check_stack_top(vm, -2);
    vm_free(vm); free(bc);

    bc = must_compile("/** e */\nint main() { int arr[3]; return arr[10]; }", &len);
    vm = run_and_capture(bc, len);
    assert(vm->halted);
    check_stack_top(vm, -2);
    vm_free(vm); free(bc);
}

static void test_array_param_decay(void) {
    printf("Testing global arrays decay to a base address when passed to a function...\n");
    check_result(
        "int arr[4];\n"
        "/** sums the first n elements of a[] */\n"
        "int sum(int a[], int n) {\n"
        "    int total = 0;\n"
        "    for (int i = 0; i < n; i = i + 1) { total = total + a[i]; }\n"
        "    return total;\n"
        "}\n"
        "/** e */\n"
        "int main() { arr[0]=1; arr[1]=2; arr[2]=3; arr[3]=4; return sum(arr, 4); }", 10);
}

/* -----------------------------------------------------------------------
 * v2: string literals as array initializers
 * ----------------------------------------------------------------------- */

static void test_string_literal_init(void) {
    printf("Testing string literal array initializers...\n");

    reset_output();
    {
        size_t len;
        uint8_t* bc = must_compile(
            "int msg[] = \"Hi!\";\n"
            "/** e */\n"
            "int main() { for (int i = 0; i < 3; i = i + 1) { emit(msg[i]); } return msg[3]; }", &len);
        VM* vm = run_capturing_output(bc, len);
        assert(vm->halted);
        assert(strcmp(output_buffer, "Hi!") == 0);
        check_stack_top(vm, 0); /* NUL terminator */
        vm_free(vm);
        free(bc);
    }

    check_result("/** e */\nint main() { int msg[] = \"AB\"; return msg[0] + msg[1]; }", 65 + 66);

    /* explicit array size larger than the string: remaining bytes stay zero */
    check_result("int msg[10] = \"Hi\";\n/** e */\nint main() { return msg[9]; }", 0);
}

/* -----------------------------------------------------------------------
 * v2b: Cloister bindings (SCI/VFS/draw builtins)
 * ----------------------------------------------------------------------- */

static void test_vfs_open_draw(void) {
    printf("Testing vfs_open(\"/dev/draw\")...\n");
    check_machine_result(
        "/** e */\n"
        "int main() { int fd = vfs_open(\"/dev/draw\"); if (fd >= 0) { return 1; } return 0; }", 1, 1);
}

static void test_canvas_size(void) {
    printf("Testing canvas_size()...\n");
    check_machine_result(
        "/** e */\n"
        "int main() {\n"
        "    int fd = vfs_open(\"/dev/draw\");\n"
        "    int sz = canvas_size(fd);\n"
        "    int w = sz >> 16;\n"
        "    int h = sz & 0xFFFF;\n"
        "    if (w > 0) { if (h > 0) { return 1; } }\n"
        "    return 0;\n"
        "}", 1, 1);
}

static void test_draw_sequence_no_fault(void) {
    printf("Testing a full draw sequence does not fault...\n");
    check_machine_result(
        "/** e */\n"
        "int main() {\n"
        "    int fd = vfs_open(\"/dev/draw\");\n"
        "    set_window_title(\"Fluxio Demo\");\n"
        "    begin_frame(fd);\n"
        "    fill_rect(fd, 0, 0, 100, 100, 0x000000);\n"
        "    draw_str(fd, 10, 10, 0xFFFFFF, 12, \"Hello, Cloister!\");\n"
        "    end_frame(fd);\n"
        "    yield();\n"
        "    vfs_close(fd);\n"
        "    return 42;\n"
        "}", 42, 2);
}

static void test_fill_rect_pixel_exact(void) {
    printf("Testing fill_rect writes the exact packed color into the framebuffer...\n");
    size_t len;
    uint8_t* bc = must_compile(
        "/** e */\n"
        "int main() {\n"
        "    int fd = vfs_open(\"/dev/draw\");\n"
        "    begin_frame(fd);\n"
        "    fill_rect(fd, 5, 5, 10, 10, 0x00FF80);\n"
        "    end_frame(fd);\n"
        "    return 0;\n"
        "}", &len);
    Machine* m = run_machine_pumped(bc, len, 1);
    assert(m->cpu->halted);
    int sw = m->system->screen_width ? m->system->screen_width : 960;
    uint8_t* fb = m->system->screen_pixels;
    uint8_t* pixel = fb + (size_t) 10 * sw * 4 + (size_t) 10 * 4;
    assert(pixel[1] == 0x00 && pixel[2] == 0xFF && pixel[3] == 0x80); /* [0]=alpha,[1]=R,[2]=G,[3]=B */
    machine_free(m);
    free(bc);
}

static void test_poll_no_events(void) {
    printf("Testing poll_mouse/poll_kbd return 0 with no queued events...\n");
    /* A VFS read on an empty input queue implicitly sets system->yielded
     * (src/vfs.c: blocking-read-as-yield), so each poll_*() call here can
     * cost machine_tick() a full "frame" on its own even though the
     * program itself never calls yield() -- budget for both. */
    check_machine_result(
        "/** e */\n"
        "int main() {\n"
        "    int mfd = vfs_open(\"/dev/mouse\");\n"
        "    int kfd = vfs_open(\"/dev/kbd\");\n"
        "    return poll_mouse(mfd) + poll_kbd(kfd);\n"
        "}", 0, 5);
}

static void test_accessors_callable(void) {
    printf("Testing mouse_*/kbd_* accessors are callable without a prior poll...\n");
    check_machine_result(
        "/** e */\n"
        "int main() {\n"
        "    int a = mouse_x() + mouse_y() + mouse_type() + mouse_button();\n"
        "    int b = kbd_type() + kbd_key();\n"
        "    return a + b;\n"
        "}", 0, 1);
}

static void test_frame_loop_multi_yield(void) {
    printf("Testing a multi-frame loop with yield() each iteration...\n");
    check_machine_result(
        "/** e */\n"
        "int main() {\n"
        "    int fd = vfs_open(\"/dev/draw\");\n"
        "    int i = 0;\n"
        "    while (i < 5) {\n"
        "        begin_frame(fd);\n"
        "        fill_rect(fd, i * 10, 0, 20, 20, 0xFF0000);\n"
        "        end_frame(fd);\n"
        "        yield();\n"
        "        i = i + 1;\n"
        "    }\n"
        "    return i;\n"
        "}", 5, 10);
}

/* -----------------------------------------------------------------------
 * v2c: `include "path.fx";` -- splitting a program across files
 * ----------------------------------------------------------------------- */

static void test_include_basic(void) {
    printf("Testing basic single-level include...\n");
    char dir[] = "/tmp/fluxio_test_include_basic_XXXXXX";
    assert(mkdtemp(dir) != NULL);
    write_temp_file(dir, "mathlib.fx",
        "/** doubles a number */\n"
        "int double_it(int x) { return x * 2; }\n");
    write_temp_file(dir, "main.fx",
        "include \"mathlib.fx\";\n"
        "/** e */\n"
        "int main() { return double_it(21); }\n");
    check_include_result(dir, "main.fx", 42);
}

static void test_include_diamond_dedup(void) {
    printf("Testing diamond include is deduplicated (shared global state, no redefinition error)...\n");
    char dir[] = "/tmp/fluxio_test_include_diamond_XXXXXX";
    assert(mkdtemp(dir) != NULL);
    write_temp_file(dir, "base.fx",
        "int shared_counter = 0;\n"
        "/** bumps the shared counter */\n"
        "int bump() { shared_counter = shared_counter + 1; return shared_counter; }\n");
    write_temp_file(dir, "a.fx",
        "include \"base.fx\";\n"
        "/** e */\n"
        "int via_a() { return bump(); }\n");
    write_temp_file(dir, "b.fx",
        "include \"base.fx\";\n"
        "/** e */\n"
        "int via_b() { return bump(); }\n");
    write_temp_file(dir, "main.fx",
        "include \"a.fx\";\n"
        "include \"b.fx\";\n"
        "/** e */\n"
        "int main() { via_a(); via_b(); return via_a(); }\n");
    check_include_result(dir, "main.fx", 3);
}

static void test_include_circular_error(void) {
    printf("Testing error: circular include is rejected...\n");
    char dir[] = "/tmp/fluxio_test_include_circular_XXXXXX";
    assert(mkdtemp(dir) != NULL);
    write_temp_file(dir, "cyc1.fx", "include \"cyc2.fx\";\nint x = 1;\n");
    write_temp_file(dir, "cyc2.fx", "include \"cyc1.fx\";\nint y = 2;\n");
    write_temp_file(dir, "main.fx", "include \"cyc1.fx\";\n/** e */\nint main() { return 0; }\n");
    assert(include_load_fails(dir, "main.fx"));
}

static void test_include_missing_file_error(void) {
    printf("Testing error: including a nonexistent file is rejected...\n");
    char dir[] = "/tmp/fluxio_test_include_missing_XXXXXX";
    assert(mkdtemp(dir) != NULL);
    write_temp_file(dir, "main.fx", "include \"does_not_exist.fx\";\n/** e */\nint main() { return 0; }\n");
    assert(include_load_fails(dir, "main.fx"));
}

static void test_include_transitive(void) {
    printf("Testing transitive include (A includes B includes C)...\n");
    char dir[] = "/tmp/fluxio_test_include_transitive_XXXXXX";
    assert(mkdtemp(dir) != NULL);
    write_temp_file(dir, "c.fx", "/** e */\nint c_val() { return 7; }\n");
    write_temp_file(dir, "b.fx", "include \"c.fx\";\n/** e */\nint b_val() { return c_val() + 1; }\n");
    write_temp_file(dir, "main.fx", "include \"b.fx\";\n/** e */\nint main() { return b_val(); }\n");
    check_include_result(dir, "main.fx", 8);
}

/* -----------------------------------------------------------------------
 * v2d: structs -- UpperCamelCase type names, int-only fields, field access
 * via '.'; global structs decay to an address (with field offsets known
 * from the declared type), local structs cannot (no stable address, same
 * VM constraint as local arrays); whole-struct assignment is rejected.
 * ----------------------------------------------------------------------- */

static void test_struct_global_basic(void) {
    printf("Testing global struct field read/write...\n");
    check_result(
        "/** a point */\n"
        "struct Point { int x; int y; }\n"
        "Point p;\n"
        "/** e */\n"
        "int main() { p.x = 3; p.y = 4; return p.x + p.y; }", 7);
}

static void test_struct_local_basic(void) {
    printf("Testing local struct field read/write...\n");
    check_result(
        "/** a point */\n"
        "struct Point { int x; int y; }\n"
        "/** e */\n"
        "int main() { Point p; p.x = 10; p.y = 20; return p.x * p.y; }", 200);
}

static void test_struct_default_zero(void) {
    printf("Testing struct fields default to zero...\n");
    check_result(
        "/** a point */\n"
        "struct Point { int x; int y; }\n"
        "Point p;\n"
        "/** e */\n"
        "int main() { return p.x + p.y; }", 0);
}

static void test_struct_param_decay_and_write_through(void) {
    printf("Testing struct params decay from a global and write through by reference...\n");
    check_result(
        "/** a point */\n"
        "struct Point { int x; int y; }\n"
        "Point origin;\n"
        "/** sums fields */\n"
        "int sum_fields(Point a) { return a.x + a.y; }\n"
        "/** mutates via the passed reference */\n"
        "int set_xy(Point a, int x, int y) { a.x = x; a.y = y; return 0; }\n"
        "/** e */\n"
        "int main() {\n"
        "    origin.x = 5; origin.y = 6;\n"
        "    int s = sum_fields(origin);\n"
        "    set_xy(origin, 7, 8);\n"
        "    return s * 100 + origin.x * 10 + origin.y;\n"
        "}", 1178); /* s=11 -> 1100, then origin becomes (7,8) -> +78 */
}

static void test_struct_multiple_instances_and_types(void) {
    printf("Testing multiple struct instances and multiple struct types coexist...\n");
    check_result(
        "/** a point */\n"
        "struct Point { int x; int y; }\n"
        "/** an rgb color */\n"
        "struct Color { int r; int g; int b; }\n"
        "Point a;\n"
        "Point b;\n"
        "/** e */\n"
        "int main() {\n"
        "    a.x = 1; a.y = 2; b.x = 10; b.y = 20;\n"
        "    Color c; c.r = 100; c.g = 150; c.b = 200;\n"
        "    return a.x + a.y + b.x + b.y + c.r + c.g + c.b;\n"
        "}", 483);
}

/* -----------------------------------------------------------------------
 * lib/float.fx: fixed-point Float is a pure library on top of structs (no
 * codegen changes), so these tests compile the real library source
 * (read from disk -- `make test` runs from the repo root) concatenated
 * with a test-specific main(), exercising it exactly as user code would.
 * ----------------------------------------------------------------------- */

static char* float_lib_source(const char* extra) {
    FILE* f = fopen("lib/float.fx", "r");
    assert(f != NULL);
    fseek(f, 0, SEEK_END);
    long lib_len = ftell(f);
    fseek(f, 0, SEEK_SET);
    char* lib_src = malloc((size_t) lib_len + 1);
    assert(lib_src != NULL);
    size_t nread = fread(lib_src, 1, (size_t) lib_len, f);
    lib_src[nread] = '\0';
    fclose(f);

    size_t total = strlen(lib_src) + strlen(extra) + 1;
    char* combined = malloc(total);
    assert(combined != NULL);
    snprintf(combined, total, "%s%s", lib_src, extra);
    free(lib_src);
    return combined;
}

static void check_float_result(const char* extra, int32_t expected) {
    char* src = float_lib_source(extra);
    check_result(src, expected);
    free(src);
}

static void test_float_int_roundtrip(void) {
    printf("Testing int_to_float/float_to_int round-trip...\n");
    check_float_result(
        "Float f;\n"
        "/** e */\n"
        "int main() { int_to_float(f, 42); return float_to_int(f); }", 42);
}

static void test_float_add_sub_signs(void) {
    printf("Testing float_add/float_sub with mixed-sign operands...\n");
    check_float_result(
        "Float a; Float b; Float r;\n"
        "/** e */\n"
        "int main() {\n"
        "    int_to_float(a, 1); a.frac = 5000;\n"  /* a = 1.5 */
        "    int_to_float(b, 0); b.frac = 0 - 5000;\n" /* b = -0.5 */
        "    float_add(r, a, b);\n" /* 1.0 */
        "    int add_ok = r.whole == 1 && r.frac == 0;\n"
        "    float_sub(r, a, b);\n" /* 2.0 */
        "    int sub_ok = r.whole == 2 && r.frac == 0;\n"
        "    return add_ok * 10 + sub_ok;\n"
        "}", 11);
}

static void test_float_mul_div(void) {
    printf("Testing float_mul/float_div...\n");
    check_float_result(
        "Float a; Float b; Float r;\n"
        "/** e */\n"
        "int main() {\n"
        "    int_to_float(a, 1); a.frac = 5000;\n" /* a = 1.5 */
        "    int_to_float(b, 2);\n"                /* b = 2.0 */
        "    float_mul(r, a, b);\n"                /* 3.0 */
        "    int mul_ok = r.whole == 3 && r.frac == 0;\n"
        "    float_div(r, r, b);\n"                /* 1.5 */
        "    int div_ok = r.whole == 1 && r.frac == 5000;\n"
        "    return mul_ok * 10 + div_ok;\n"
        "}", 11);
}

static void test_float_neg_abs(void) {
    printf("Testing float_neg/float_abs...\n");
    check_float_result(
        "Float a; Float r;\n"
        "/** e */\n"
        "int main() {\n"
        "    int_to_float(a, 2); a.frac = 2500;\n" /* a = 2.25 */
        "    float_neg(r, a);\n"
        "    int neg_ok = r.whole == 0 - 2 && r.frac == 0 - 2500;\n"
        "    float_abs(r, r);\n"
        "    int abs_ok = r.whole == 2 && r.frac == 2500;\n"
        "    return neg_ok * 10 + abs_ok;\n"
        "}", 11);
}

static void test_float_comparisons(void) {
    printf("Testing float_eq/float_lt/float_gt...\n");
    check_float_result(
        "Float a; Float b;\n"
        "/** e */\n"
        "int main() {\n"
        "    int_to_float(a, 1); a.frac = 5000;\n" /* 1.5 */
        "    int_to_float(b, 3);\n"                /* 3.0 */
        "    return float_eq(a, a) * 100 + float_lt(a, b) * 10 + float_gt(a, b);\n"
        "}", 110);
}

static void test_float_print(void) {
    printf("Testing print_float() output formatting...\n");
    reset_output();
    {
        char* src = float_lib_source(
            "Float a; Float b;\n"
            "/** e */\n"
            "int main() {\n"
            "    int_to_float(a, 3); a.frac = 5000;\n" /* 3.5 */
            "    print_float(a);\n"
            "    emit(32);\n"
            "    int_to_float(b, 0); b.frac = 0 - 500;\n" /* -0.05 */
            "    print_float(b);\n"
            "    return 0;\n"
            "}");
        size_t len;
        uint8_t* bc = must_compile(src, &len);
        free(src);
        VM* vm = run_capturing_output(bc, len);
        assert(vm->halted);
        assert(strcmp(output_buffer, "3.5000 -0.0500") == 0);
        vm_free(vm);
        free(bc);
    }
}

static void test_error_local_struct_decay(void) {
    printf("Testing error: local struct used as a value (no stable address)...\n");
    assert(must_fail_compile(
        "/** a point */\n"
        "struct Point { int x; int y; }\n"
        "/** takes a point */\n"
        "int f(Point a) { return a.x; }\n"
        "/** e */\n"
        "int main() { Point p; return f(p); }"));
}

static void test_error_assign_whole_struct(void) {
    printf("Testing error: assignment to a struct as a whole...\n");
    assert(must_fail_compile(
        "/** a point */\n"
        "struct Point { int x; int y; }\n"
        "Point a;\nPoint b;\n"
        "/** e */\n"
        "int main() { a = b; return 0; }"));
}

static void test_error_unknown_field(void) {
    printf("Testing error: accessing an undeclared field...\n");
    assert(must_fail_compile(
        "/** a point */\n"
        "struct Point { int x; int y; }\n"
        "Point p;\n"
        "/** e */\n"
        "int main() { return p.z; }"));
}

static void test_error_struct_naming_and_shape(void) {
    printf("Testing error: struct naming convention and shape rules...\n");
    assert(must_fail_compile("/** e */\nstruct point { int x; }\n/** e */\nint main() { return 0; }")); /* not UpperCamelCase */
    assert(must_fail_compile("/** e */\nstruct Point { int x; int x; }\n/** e */\nint main() { return 0; }")); /* dup field */
    assert(must_fail_compile("/** e */\nstruct Point { int x; }\n/** e2 */\nstruct Point { int y; }\n/** e */\nint main() { return 0; }")); /* dup struct */
    assert(must_fail_compile("struct Point { int x; }\n/** e */\nint main() { return 0; }")); /* missing doc comment */
    assert(must_fail_compile("/** e */\nstruct Empty { }\n/** e */\nint main() { return 0; }")); /* no fields */
}

/* -----------------------------------------------------------------------
 * Compile-error paths
 * ----------------------------------------------------------------------- */

static void test_error_reserved_builtin_name(void) {
    printf("Testing error: redeclaring a reserved builtin name...\n");
    assert(must_fail_compile("/** shadows a builtin */\nint emit(int x) { return x; }\n/** e */\nint main() { return 0; }"));
    assert(must_fail_compile("int print = 1;\n/** e */\nint main() { return print; }"));
}

static void test_error_undefined_function(void) {
    printf("Testing error: undefined function...\n");
    assert(must_fail_compile("/** e */\nint main() { return ghost(1); }"));
}

static void test_error_undefined_variable(void) {
    printf("Testing error: undefined variable...\n");
    assert(must_fail_compile("/** e */\nint main() { return nope; }"));
}

static void test_error_syntax(void) {
    printf("Testing error: syntax errors...\n");
    assert(must_fail_compile("/** e */\nint main() { return 1 }"));       /* missing ; */
    assert(must_fail_compile("/** e */\nint main( { return 1; }"));       /* missing ) */
    assert(must_fail_compile("/** e */\nint main() { return 1 + ; }"));   /* missing operand */
}

static void test_error_redeclared_global(void) {
    printf("Testing error: redeclared global...\n");
    assert(must_fail_compile("int g = 1;\nint g = 2;\n/** e */\nint main() { return g; }"));
}

static void test_error_missing_main(void) {
    printf("Testing error: missing main...\n");
    assert(must_fail_compile("/** not main */\nint foo() { return 1; }"));
}

static void test_error_noncost_global_init(void) {
    printf("Testing error: non-constant global initializer...\n");
    assert(must_fail_compile("/** helper */\nint helper() { return 1; }\nint g = helper();\n/** e */\nint main() { return g; }"));
}

static void test_error_assign_to_nonlvalue(void) {
    printf("Testing error: assignment to non-lvalue...\n");
    assert(must_fail_compile("/** e */\nint main() { 1 = 2; return 0; }"));
}

static void test_error_naming_convention(void) {
    printf("Testing error: non-snake_case identifiers...\n");
    assert(must_fail_compile("/** e */\nint main() { int MyVar = 1; return MyVar; }"));
    assert(must_fail_compile("/** e */\nint MyFunc() { return 1; }\n/** e */\nint main() { return MyFunc(); }"));
}

static void test_error_missing_doc_comment(void) {
    printf("Testing error: function missing its doc comment...\n");
    assert(must_fail_compile("int add(int a, int b) { return a + b; }\n/** e */\nint main() { return add(1,2); }"));
}

static void test_error_arity_mismatch(void) {
    printf("Testing error: call arity mismatch...\n");
    assert(must_fail_compile("/** takes two */\nint add(int a, int b) { return a + b; }\n/** e */\nint main() { return add(1); }"));
}

static void test_error_unbounded_recursion(void) {
    printf("Testing error: recursion without recursive(N) annotation...\n");
    assert(must_fail_compile(
        "/** fib, not annotated */\n"
        "int fib(int n) { if (n < 2) { return n; } return fib(n - 1) + fib(n - 2); }\n"
        "/** e */\n"
        "int main() { return fib(5); }"));
    /* mutual recursion cycle without annotation on either side */
    assert(must_fail_compile(
        "/** even? */\n"
        "int is_even(int n) { if (n == 0) { return 1; } return is_odd(n - 1); }\n"
        "/** odd? */\n"
        "int is_odd(int n) { if (n == 0) { return 0; } return is_even(n - 1); }\n"
        "/** e */\n"
        "int main() { return is_even(4); }"));
}

static void test_error_main_with_params(void) {
    printf("Testing error: main() with parameters...\n");
    assert(must_fail_compile("/** e */\nint main(int argc) { return argc; }"));
}

static void test_error_recursive_main(void) {
    printf("Testing error: main() declared recursive...\n");
    assert(must_fail_compile("/** e */\nrecursive(2) int main() { return main(); }"));
}

static void test_error_local_array_decay(void) {
    printf("Testing error: local array used as a value (no stable address)...\n");
    assert(must_fail_compile(
        "/** takes array */\n"
        "int sum1(int a[]) { return a[0]; }\n"
        "/** e */\n"
        "int main() { int arr[3]; arr[0] = 5; return sum1(arr); }"));
}

static void test_error_assign_whole_array(void) {
    printf("Testing error: assignment to an array as a whole...\n");
    assert(must_fail_compile("int arr[3];\n/** e */\nint main() { arr = 5; return 0; }"));
}

static void test_error_string_init_misuse(void) {
    printf("Testing error: string-literal-initializer misuse...\n");
    assert(must_fail_compile("/** e */\nint main() { int x = \"nope\"; return x; }"));            /* scalar */
    assert(must_fail_compile("/** e */\nint main() { int arr[3] = 5; return arr[0]; }"));          /* array w/ non-string init */
    assert(must_fail_compile("int msg[2] = \"Hello\";\n/** e */\nint main() { return msg[0]; }")); /* too long */
}

static void test_error_builtin_string_arg_required(void) {
    printf("Testing error: builtin requires a string-literal argument...\n");
    /* vfs_open's path must be a literal, not a general int/array expression */
    assert(must_fail_compile(
        "int p = 0;\n/** e */\nint main() { int fd = vfs_open(p); return fd; }"));
    /* draw_str's text arg (position 6) must be a literal too */
    assert(must_fail_compile(
        "/** e */\nint main() { int fd = vfs_open(\"/dev/draw\"); draw_str(fd, 0, 0, 0, 12, 5); return 0; }"));
}

static void test_error_builtin_int_arg_required(void) {
    printf("Testing error: builtin requires an int argument, not a string literal...\n");
    assert(must_fail_compile("/** e */\nint main() { return vfs_close(\"nope\"); }"));
}

/* -----------------------------------------------------------------------
 * End-to-end fixture via the on-disk .bin path is covered by examples/fib.fx
 * + bin/fluxioc + bin/nux, exercised manually / in CI shell scripts rather
 * than here, since this binary only links the compiler + VM library code.
 * ----------------------------------------------------------------------- */

int main(void) {
    /* Unbuffered so progress lines interleave correctly with the compiler's
     * own stderr diagnostics (several tests below intentionally compile
     * invalid programs and expect a "fluxio: ... error ..." diagnostic —
     * that output is the test passing, not a failure). */
    setvbuf(stdout, NULL, _IONBF, 0);

    printf("\n=== Fluxio Compiler Tests ===\n\n");

    test_lexer_literals_and_operators();
    test_lexer_comments();
    test_lexer_doc_comment_tracking();

    test_arithmetic_precedence();
    test_short_circuit();

    test_globals();

    test_locals_and_params();
    test_frame_offset_correctness();

    test_if_else();
    test_while_loop();
    test_for_loop();

    test_forward_call();
    test_leaf_and_falloff();
    test_recursion_bounded();
    test_recursion_guard_halts();

    test_builtin_emit_and_print();

    test_arrays_global();
    test_arrays_local();
    test_arrays_bounds_check();
    test_array_param_decay();
    test_string_literal_init();

    test_vfs_open_draw();
    test_canvas_size();
    test_draw_sequence_no_fault();
    test_fill_rect_pixel_exact();
    test_poll_no_events();
    test_accessors_callable();
    test_frame_loop_multi_yield();

    test_include_basic();
    test_include_diamond_dedup();
    test_include_transitive();

    test_struct_global_basic();
    test_struct_local_basic();
    test_struct_default_zero();
    test_struct_param_decay_and_write_through();
    test_struct_multiple_instances_and_types();

    test_float_int_roundtrip();
    test_float_add_sub_signs();
    test_float_mul_div();
    test_float_neg_abs();
    test_float_comparisons();
    test_float_print();

    printf("\n--- Negative tests: each one below compiles an intentionally\n"
           "    invalid program and expects a \"fluxio: ... error ...\"\n"
           "    diagnostic on stderr. That diagnostic is the test passing. ---\n\n");

    test_error_reserved_builtin_name();
    test_error_undefined_function();
    test_error_undefined_variable();
    test_error_syntax();
    test_error_redeclared_global();
    test_error_missing_main();
    test_error_noncost_global_init();
    test_error_assign_to_nonlvalue();
    test_error_naming_convention();
    test_error_missing_doc_comment();
    test_error_arity_mismatch();
    test_error_unbounded_recursion();
    test_error_main_with_params();
    test_error_recursive_main();
    test_error_local_array_decay();
    test_error_assign_whole_array();
    test_error_string_init_misuse();
    test_error_builtin_string_arg_required();
    test_error_builtin_int_arg_required();
    test_include_circular_error();
    test_include_missing_file_error();
    test_error_local_struct_decay();
    test_error_assign_whole_struct();
    test_error_unknown_field();
    test_error_struct_naming_and_shape();

    printf("\nAll Fluxio compiler tests passed!\n");
    return 0;
}
