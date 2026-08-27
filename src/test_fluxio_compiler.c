#include "fluxio_token.h"
#include "fluxio_include.h"
#include "fluxio_parser.h"
#include "fluxio_codegen.h"
#include "machine.h"
#include "vfs.h"
#include "vm.h"
#include "opcodes.h"
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

/* True iff `bc` contains an OP_CALL whose 4-byte big-endian immediate is
 * `addr`. Used to prove extern codegen binds to the declared address
 * without needing a linked library to actually run the call. */
static bool bytecode_contains_call(const uint8_t* bc, size_t len, int32_t addr) {
    uint32_t u = (uint32_t) addr;
    for (size_t i = 0; i + 5 <= len; i++) {
        if (bc[i] == OP_CALL &&
            bc[i + 1] == (uint8_t) ((u >> 24) & 0xFF) &&
            bc[i + 2] == (uint8_t) ((u >> 16) & 0xFF) &&
            bc[i + 3] == (uint8_t) ((u >> 8) & 0xFF) &&
            bc[i + 4] == (uint8_t) (u & 0xFF)) {
            return true;
        }
    }
    return false;
}

/* True iff the OP_CALL to `addr` is immediately followed by OP_POP
 * (the trailing discard FX_EXPR_STMT emits for a value-producing call
 * used as a statement). `extern void` must NOT have this pop. */
static bool call_followed_by_pop(const uint8_t* bc, size_t len, int32_t addr) {
    uint32_t u = (uint32_t) addr;
    for (size_t i = 0; i + 5 <= len; i++) {
        if (bc[i] == OP_CALL &&
            bc[i + 1] == (uint8_t) ((u >> 24) & 0xFF) &&
            bc[i + 2] == (uint8_t) ((u >> 16) & 0xFF) &&
            bc[i + 3] == (uint8_t) ((u >> 8) & 0xFF) &&
            bc[i + 4] == (uint8_t) (u & 0xFF)) {
            return i + 5 < len && bc[i + 5] == OP_POP;
        }
    }
    return false;
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

static void test_lexer_new_keywords(void) {
    printf("Testing lexer keywords: extern, void, byte...\n");
    FxTokenList* toks = fx_tokenize("extern void byte int struct");
    assert(toks != NULL);
    assert(toks->tokens[0].type == FXTOK_KW_EXTERN);
    assert(toks->tokens[1].type == FXTOK_KW_VOID);
    assert(toks->tokens[2].type == FXTOK_KW_BYTE);
    assert(toks->tokens[3].type == FXTOK_KW_INT);
    assert(toks->tokens[4].type == FXTOK_KW_STRUCT);
    fx_token_list_free(toks);
}

static void test_lexer_string_escapes(void) {
    printf("Testing lexer string-literal escapes...\n");
    FxTokenList* toks = fx_tokenize("\"a\\nb\\tc\\\\d\\\"e\\0f\"");
    assert(toks != NULL);
    assert(toks->tokens[0].type == FXTOK_STRING_LIT);
    assert(toks->tokens[0].str_len == 11); /* a \n b \t c \\ d " e \0 f */
    assert(toks->tokens[0].value[0] == 'a');
    assert(toks->tokens[0].value[1] == '\n');
    assert(toks->tokens[0].value[2] == 'b');
    assert(toks->tokens[0].value[3] == '\t');
    assert(toks->tokens[0].value[4] == 'c');
    assert(toks->tokens[0].value[5] == '\\');
    assert(toks->tokens[0].value[6] == 'd');
    assert(toks->tokens[0].value[7] == '"');
    assert(toks->tokens[0].value[8] == 'e');
    assert(toks->tokens[0].value[9] == '\0');
    assert(toks->tokens[0].value[10] == 'f');
    fx_token_list_free(toks);

    /* Unknown escape and unterminated string are lex errors. */
    assert(fx_tokenize("\"\\q\"") == NULL);
    assert(fx_tokenize("\"unterminated") == NULL);
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
    check_result("/** e */\nint main() { return + +7; }", 7); /* unary plus is a no-op */
    check_result("/** e */\nint main() { return 0xF0 ^ 0x0F; }", 0xFF);
    check_result("/** e */\nint main() { return 16 >> 2; }", 4);
    check_result("/** e */\nint main() { return (0 - 8) >> 1; }", -4); /* >> is arithmetic */
    check_result("/** e */\nint main() { return 1 == 1 && 2 != 3 && 3 < 4 && 5 >= 5; }", 1);
    check_result("/** e */\nint main() { return 1 < 2 == 1; }", 1); /* comparisons bind tighter than == */
    check_result(
        "/** e */\n"
        "int main() { int a; int b; a = b = 7; return a + b; }", 14);
    check_result(
        "/** e */\n"
        "int main() { int msg[] = \"A\\nB\"; return msg[1]; }", 10);
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
    /* Nested if/else and else-if chain. */
    check_result(
        "/** e */\n"
        "int main() {\n"
        "    int x = 2;\n"
        "    int r = 0;\n"
        "    if (x == 0) { r = 10; } else {\n"
        "        if (x == 1) { r = 20; } else { r = 30; }\n"
        "    }\n"
        "    return r;\n"
        "}", 30);
    check_result(
        "/** classify */\n"
        "int classify(int n) {\n"
        "    if (n < 0) { return 0 - 1; } else { if (n == 0) { return 0; } else { return 1; } }\n"
        "}\n"
        "/** e */\n"
        "int main() { return classify(0 - 3) + classify(0) * 10 + classify(4) * 100; }", 99);
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
    /* Nested for; empty init/post. */
    check_result(
        "/** e */\n"
        "int main() {\n"
        "    int s = 0;\n"
        "    for (int i = 0; i < 3; i = i + 1) {\n"
        "        for (int j = 0; j < 3; j = j + 1) { s = s + 1; }\n"
        "    }\n"
        "    return s;\n"
        "}", 9);
    check_result(
        "/** e */\n"
        "int main() { int i = 0; int s = 0; for (; i < 4; ) { s = s + i; i = i + 1; } return s; }", 6);
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

/* Phase A3, docs/quill_fluxio.md: draw_bytes(fd,x,y,color,scale,buf,len)
 * has the same wire format as draw_str, sourced from a runtime byte[]
 * instead of a compile-time string literal (needed for Quill to draw live
 * file/line content). Draws the same text both ways at two different y
 * offsets in one frame and asserts the rendered pixels are byte-identical
 * -- a much stronger check than "it didn't fault", and doesn't require
 * hand-verifying glyph bitmaps. */
static void test_draw_bytes_matches_draw_str(void) {
    printf("Testing draw_bytes renders pixel-identical output to draw_str...\n");
    size_t len;
    uint8_t* bc = must_compile(
        "byte msg[4] = \"Hi\";\n"
        "/** e */\n"
        "int main() {\n"
        "    int fd = vfs_open(\"/dev/draw\");\n"
        "    begin_frame(fd);\n"
        "    fill_rect(fd, 0, 0, 60, 60, 0x000000);\n"
        "    draw_str(fd, 5, 5, 0xFFFFFF, 12, \"Hi\");\n"
        "    draw_bytes(fd, 5, 30, 0xFFFFFF, 12, msg, 2);\n"
        "    end_frame(fd);\n"
        "    return 0;\n"
        "}", &len);
    Machine* m = run_machine_pumped(bc, len, 1);
    assert(m->cpu->halted);
    int sw = m->system->screen_width ? m->system->screen_width : 960;
    uint8_t* fb = m->system->screen_pixels;
    bool any_ink = false;
    for (int y = 0; y < 20; y++) {
        for (int x = 0; x < 20; x++) {
            uint8_t* p1 = fb + (size_t) (5 + y) * sw * 4 + (size_t) (5 + x) * 4;
            uint8_t* p2 = fb + (size_t) (30 + y) * sw * 4 + (size_t) (5 + x) * 4;
            assert(p1[0] == p2[0] && p1[1] == p2[1] && p1[2] == p2[2] && p1[3] == p2[3]);
            if (p1[1] || p1[2] || p1[3]) any_ink = true;
        }
    }
    assert(any_ink); /* sanity: text actually drew ink, not just matching blanks */
    machine_free(m);
    free(bc);
}

static void test_draw_bytes_oversized_len_clamped(void) {
    printf("Testing draw_bytes clamps an oversized len instead of overrunning scratch memory...\n");
    check_machine_result(
        "byte msg[4] = \"Hi\";\n"
        "/** e */\n"
        "int main() {\n"
        "    int fd = vfs_open(\"/dev/draw\");\n"
        "    begin_frame(fd);\n"
        "    draw_bytes(fd, 5, 5, 0xFFFFFF, 12, msg, 999999);\n"
        "    end_frame(fd);\n"
        "    return 1;\n"
        "}", 1, 1);
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
 * Phase A2, docs/quill_fluxio.md: runtime-buffer VFS builtins
 * (vfs_open_buf/vfs_read/vfs_write/vfs_seek/vfs_stat/vfs_write_chunk) --
 * unlike vfs_open, none of these accept a literal, so they're exercised
 * through real host-backed files under /sys/file/ (src/vfs.c), not a /dev
 * pseudo-file.
 * ----------------------------------------------------------------------- */

static void test_vfs_write_read_roundtrip(void) {
    printf("Testing vfs_write/vfs_read round-trip on a real host file...\n");
    check_machine_result(
        "byte path[32] = \"/sys/file/fx_a2_rw.txt\";\n"
        "byte msg[16] = \"HelloA2\";\n"
        "byte rbuf[16];\n"
        "/** e */\n"
        "int main() {\n"
        "    int fd = vfs_open_buf(path, 22, 4);\n"
        "    int written = vfs_write(fd, msg, 7);\n"
        "    vfs_close(fd);\n"
        "    int fd2 = vfs_open_buf(path, 22, 0);\n"
        "    int read_n = vfs_read(fd2, rbuf, 16);\n"
        "    vfs_close(fd2);\n"
        "    if (written != 7) { return 0; }\n"
        "    if (read_n != 7) { return 0; }\n"
        "    if (rbuf[0] != 72) { return 0; }\n"  /* 'H' */
        "    if (rbuf[6] != 50) { return 0; }\n"  /* '2' */
        "    return 1;\n"
        "}", 1, 1);
    remove("fx_a2_rw.txt");
}

static void test_vfs_seek_and_stat(void) {
    printf("Testing vfs_seek/vfs_stat on a real host file...\n");
    check_machine_result(
        "byte path[32] = \"/sys/file/fx_a2_seek.txt\";\n"
        "byte msg[16] = \"abcdef\";\n"
        "byte rbuf[16];\n"
        "/** e */\n"
        "int main() {\n"
        "    int fd = vfs_open_buf(path, 24, 6);\n" /* 0x04 truncate | 0x02 read+write */
        "    vfs_write(fd, msg, 6);\n"
        "    int size = vfs_stat(fd);\n"
        "    vfs_seek(fd, 2);\n"
        "    int n = vfs_read(fd, rbuf, 16);\n"
        "    vfs_close(fd);\n"
        "    if (size != 6) { return 0; }\n"
        /* seeked past the first 2 bytes -- 4 of the original 6 remain */
        "    if (n != 4) { return 0; }\n"
        "    if (rbuf[0] != 99) { return 0; }\n" /* 'c' */
        "    return 1;\n"
        "}", 1, 1);
    remove("fx_a2_seek.txt");
}

static void test_vfs_write_chunk(void) {
    printf("Testing vfs_write_chunk on a real host file...\n");
    check_machine_result(
        "byte path[32] = \"/sys/file/fx_a2_chunk.txt\";\n"
        "byte msg[16] = \"chunked\";\n"
        "byte rbuf[16];\n"
        "/** e */\n"
        "int main() {\n"
        "    int fd = vfs_open_buf(path, 25, 4);\n"
        "    int written = vfs_write_chunk(fd, msg, 7, 0, 7);\n"
        "    vfs_close(fd);\n"
        "    int fd2 = vfs_open_buf(path, 25, 0);\n"
        "    int n = vfs_read(fd2, rbuf, 16);\n"
        "    vfs_close(fd2);\n"
        "    if (written != 7) { return 0; }\n"
        "    if (n != 7) { return 0; }\n"
        "    if (rbuf[0] != 99) { return 0; }\n" /* 'c' */
        "    return 1;\n"
        "}", 1, 1);
    remove("fx_a2_chunk.txt");
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

/* Phase A1, docs/quill_fluxio.md: `byte name[N]` -- global (real memory,
 * 1 byte/element), local (frame-relative, same codegen as int[] locals --
 * see the comment on FxNode.local_decl.is_byte), and array parameters
 * (decayed address, 1-byte index stride). */
static void test_byte_arrays(void) {
    printf("Testing byte arrays: global, local, string init, and array params...\n");
    /* global byte array: string-literal init + explicit stores, string vs
     * a mixed explicit-store byte to confirm 1-byte packing round-trips. */
    check_result(
        "byte msg[8] = \"hi\";\n"
        "byte buf[4];\n"
        "/** e */\n"
        "int main() { buf[0] = msg[0]; buf[1] = msg[1]; return buf[0] + buf[1]; }",
        'h' + 'i');
    /* local byte array, plain stores/reads. */
    check_result(
        "/** e */\n"
        "int main() { byte a[4]; a[0] = 10; a[1] = 20; a[2] = 30; return a[0] + a[1] + a[2]; }",
        60);
    /* byte array passed as a param, indexed with 1-byte stride inside the
     * callee -- the actual bug this phase's design work was checking for
     * (word-stride indexing on byte storage silently reading/writing the
     * wrong bytes). */
    check_result(
        "byte g[4] = \"ab\";\n"
        "/** sums first n bytes */\n"
        "int sum_bytes(byte b[], int n) {\n"
        "    int i = 0; int total = 0;\n"
        "    while (i < n) { total = total + b[i]; i = i + 1; }\n"
        "    return total;\n"
        "}\n"
        "/** e */\n"
        "int main() { return sum_bytes(g, 2); }",
        'a' + 'b');
    /* a global byte array large enough to require the bulk-globals band
     * (Phase 0's FX_BULK_GLOBAL_THRESHOLD) -- a byte array only needs the
     * band at 1KB+ elements, unlike an equivalent int[] which needs it at
     * 256+. MM_FX_BULK_GLOBALS_BASE (~13MB) is well past check_result's
     * fixed 4MB VM, so this one needs its own bigger vm_create() call. */
    {
        const char* src =
            "byte big[1048576];\n"
            "/** e */\n"
            "int main() { big[0] = 65; big[1048575] = 66; return big[0] + big[1048575]; }";
        size_t len;
        uint8_t* bc = must_compile(src, &len);
        VM* vm = vm_create(bc, (uint32_t) len, HEADLESS_BASE_ADDRESS, 16 * 1024 * 1024, false);
        assert(vm != NULL);
        vm_run(vm);
        assert(vm->halted);
        check_stack_top(vm, 65 + 66);
        vm_free(vm);
        free(bc);
    }
}

/* -----------------------------------------------------------------------
 * extern int / extern void (Phase B5/B6) -- compile-time shape, not the
 * linked end-to-end path in src/test_abi_conformance.c. These prove the
 * parser accepts the form, codegen emits OP_CALL to the bound address,
 * void calls skip the statement-level POP, and the various collision /
 * arity / naming errors reject.
 * ----------------------------------------------------------------------- */

static void test_extern_int_emits_call(void) {
    printf("Testing extern int compiles to OP_CALL at the bound address...\n");
    const int32_t addr = 0x00ABCDEF;
    char src[512];
    snprintf(src, sizeof(src),
             "extern int get_answer() = 0x%X;\n"
             "/** e */\n"
             "int main() { return get_answer(); }\n",
             (unsigned) addr);
    size_t len;
    uint8_t* bc = must_compile(src, &len);
    assert(bytecode_contains_call(bc, len, addr));
    free(bc);

    /* Multi-arg form still emits a single CALL (args are PUSHed first). */
    snprintf(src, sizeof(src),
             "extern int add2(int a, int b) = 0x%X;\n"
             "/** e */\n"
             "int main() { return add2(1, 2); }\n",
             (unsigned) addr);
    bc = must_compile(src, &len);
    assert(bytecode_contains_call(bc, len, addr));
    free(bc);
}

static void test_extern_void_skips_pop(void) {
    printf("Testing extern void as a statement does not emit a trailing POP...\n");
    const int32_t poke_addr = 0x00100000;
    const int32_t get_addr = 0x00ABCDEF;
    char src[512];

    snprintf(src, sizeof(src),
             "extern void poke(int x) = 0x%X;\n"
             "/** e */\n"
             "int main() { poke(7); return 2; }\n",
             (unsigned) poke_addr);
    size_t len;
    uint8_t* bc = must_compile(src, &len);
    assert(bytecode_contains_call(bc, len, poke_addr));
    assert(!call_followed_by_pop(bc, len, poke_addr));
    free(bc);

    /* Contrast: a value-returning extern used as a statement DOES pop. */
    snprintf(src, sizeof(src),
             "extern int get_answer() = 0x%X;\n"
             "/** e */\n"
             "int main() { get_answer(); return 0; }\n",
             (unsigned) get_addr);
    bc = must_compile(src, &len);
    assert(bytecode_contains_call(bc, len, get_addr));
    assert(call_followed_by_pop(bc, len, get_addr));
    free(bc);
}

static void test_call_as_statement_discards_result(void) {
    printf("Testing a Fluxio function used as a statement discards its result...\n");
    check_result(
        "int g = 0;\n"
        "/** bumps g and returns it */\n"
        "int bump() { g = g + 1; return g; }\n"
        "/** e */\n"
        "int main() { bump(); bump(); return g; }", 2);
}

static void test_error_extern_void_as_value(void) {
    printf("Testing error: using an extern void call as a value...\n");
    assert(must_fail_compile(
        "extern void poke(int x) = 0x1000;\n"
        "/** e */\n"
        "int main() { int r = poke(1); return r; }"));
    assert(must_fail_compile(
        "extern void poke(int x) = 0x1000;\n"
        "/** e */\n"
        "int main() { return poke(1); }"));
}

static void test_error_extern_arity_and_shape(void) {
    printf("Testing error: extern arity, naming, params, and missing address...\n");
    assert(must_fail_compile(
        "extern int add2(int a, int b) = 0x1000;\n"
        "/** e */\n"
        "int main() { return add2(1); }"));
    assert(must_fail_compile(
        "extern int Add2(int a) = 0x1000;\n" /* not snake_case */
        "/** e */\n"
        "int main() { return Add2(1); }"));
    assert(must_fail_compile(
        "extern int f(int a[]) = 0x1000;\n" /* arrays not allowed on externs */
        "/** e */\n"
        "int main() { return 0; }"));
    assert(must_fail_compile(
        "extern int f() ;\n" /* missing = addr */
        "/** e */\n"
        "int main() { return f(); }"));
    assert(must_fail_compile(
        "extern f() = 0x1000;\n" /* missing int/void */
        "/** e */\n"
        "int main() { return 0; }"));
}

static void test_error_extern_name_collisions(void) {
    printf("Testing error: extern name collisions with functions, globals, builtins, itself...\n");
    assert(must_fail_compile(
        "extern int emit(int x) = 0x1000;\n"
        "/** e */\n"
        "int main() { return emit(1); }"));
    assert(must_fail_compile(
        "/** already a function */\n"
        "int foo() { return 1; }\n"
        "extern int foo() = 0x1000;\n"
        "/** e */\n"
        "int main() { return 0; }"));
    assert(must_fail_compile(
        "int foo = 1;\n"
        "extern int foo() = 0x1000;\n"
        "/** e */\n"
        "int main() { return foo; }"));
    assert(must_fail_compile(
        "extern int foo() = 0x1000;\n"
        "extern int foo() = 0x2000;\n"
        "/** e */\n"
        "int main() { return foo(); }"));
}

static void test_error_redeclared_function_and_params(void) {
    printf("Testing error: redeclared function and duplicate parameters...\n");
    assert(must_fail_compile(
        "/** a */\nint foo() { return 1; }\n"
        "/** b */\nint foo() { return 2; }\n"
        "/** e */\nint main() { return foo(); }"));
    assert(must_fail_compile(
        "/** uses the same param name twice */\n"
        "int add(int a, int a) { return a; }\n"
        "/** e */\n"
        "int main() { return add(1, 2); }"));
}

static void test_error_byte_type_misuse(void) {
    printf("Testing error: 'byte' used outside array-declaration form...\n");
    assert(must_fail_compile("byte x;\n/** e */\nint main() { return 0; }"));                    /* scalar global */
    assert(must_fail_compile("/** e */\nint main() { byte x; return 0; }"));                     /* scalar local */
    assert(must_fail_compile(
        "/** f */\nint f(byte x) { return x; }\n/** e */\nint main() { return f(1); }"));        /* scalar param */
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
 * Phase C, docs/quill_fluxio.md: apps/fluxio/Quill.fx (v1, minimal loop).
 * Drives the real compiled app through a simulated typing session --
 * inject synthetic keydown packets over a bound /dev/kbd channel (same
 * technique as test_vfs.c's test_mouse_chan_packet), pump frames, quit,
 * then verify the saved file's actual content through a fresh System.
 * This is the strongest verification available short of running it under
 * bin/cloister by hand: it proves the real on-disk app compiles, the
 * keyboard-driven edit loop actually mutates file_buf correctly, and the
 * save path round-trips through the real host filesystem -- not just
 * "it didn't fault".
 * ----------------------------------------------------------------------- */

/* Builds apps/fluxio/Quill.bin (via the real Makefile rule -- see below)
 * and returns a freshly created Machine for it, or NULL if fluxioc isn't
 * built / the source isn't found. Shared by every Quill.fx test so none
 * of them duplicate the build step. */
static Machine* quill_fx_machine(const char* dir, char* out_bin_path, size_t out_bin_path_cap) {
    (void) dir; /* kept for call-site compatibility; the real build always writes to the canonical path below */
    FILE* probe = fopen("./bin/fluxioc", "rb");
    if (!probe) {
        printf("  (skipped: ./bin/fluxioc not built yet -- run from repo root after `make`)\n");
        return NULL;
    }
    fclose(probe);
    probe = fopen("apps/fluxio/Quill.fx", "rb");
    if (!probe) {
        printf("  (skipped: apps/fluxio/Quill.fx not found -- run from repo root)\n");
        return NULL;
    }
    fclose(probe);

    /* Quill.fx calls into the linked UI library (docs/quill_fluxio.md
     * Phase B7/C: UI::new, UI::sbar-*), so it can't just be compiled with
     * fluxioc alone -- it needs the same compile-lib / compile-app /
     * fluxlink pipeline the real Makefile rule for apps/fluxio/Quill.bin
     * runs. Shelling out to `make` for that target (rather than
     * reimplementing the pipeline here) means this test can never drift
     * from how Quill.bin is actually built. */
    assert(system("make apps/fluxio/Quill.bin >/tmp/nuxvm_test_quill_fx_build.log 2>&1") == 0);

    snprintf(out_bin_path, out_bin_path_cap, "apps/fluxio/Quill.bin");
    FILE* bf = fopen(out_bin_path, "rb");
    assert(bf != NULL);
    fseek(bf, 0, SEEK_END);
    long blen = ftell(bf);
    fseek(bf, 0, SEEK_SET);
    uint8_t* bc = malloc((size_t) blen);
    assert(fread(bc, 1, (size_t) blen, bf) == (size_t) blen);
    fclose(bf);

    Machine* m = machine_create(bc, (uint32_t) blen, GRAPHICAL_BASE_ADDRESS, 32 * 1024 * 1024, false);
    free(bc);
    return m;
}

static void test_quill_fx_type_and_save(void) {
    printf("Testing apps/fluxio/Quill.fx: type \"Hi\", save, verify file content...\n");

    const char* dir = "/tmp/nuxvm_test_quill_fx";
    char binpath[256];
    Machine* m = quill_fx_machine(dir, binpath, sizeof(binpath));
    if (!m) return;

    remove("quill_scratch.txt"); /* fresh start -- sandbox_root defaults to "." */

    int32_t cfd = vfs_open(m->system, "/sys/chan/new", 0);
    int32_t pfd = vfs_open(m->system, "/sys/chan/peer", 0);
    assert(cfd >= 100 && pfd >= 100);
    assert(vfs_bind(m->system, pfd, "/dev/kbd") == 0);
    vfs_close(m->system, pfd);

    /* Type(1)=0 (KEY_DOWN), pad(1), key:u16 LE, mods:u32 (unused, zero).
     * "Hi", then Tab (Quill.fx's placeholder save key), then Esc (quit). */
    int keys[] = { 'H', 'i', 9, 27 };
    for (int k = 0; k < 4; k++) {
        uint8_t pkt[8] = { 0, 0, (uint8_t) (keys[k] & 0xFF), (uint8_t) ((keys[k] >> 8) & 0xFF), 0, 0, 0, 0 };
        assert(vfs_write(m->system, cfd, pkt, 8) == 8);
        int frames = 0;
        while (!m->cpu->halted && frames < 3) {
            machine_tick(m);
            frames++;
        }
    }
    vfs_close(m->system, cfd);

    assert(m->cpu->halted); /* Esc returned from main() */
    machine_free(m);

    System* check = system_create();
    assert(check != NULL);
    int32_t rfd = vfs_open(check, "/sys/file/quill_scratch.txt", 0);
    assert(rfd >= 0);
    uint8_t got[16] = { 0 };
    int n = vfs_read(check, rfd, got, sizeof(got));
    vfs_close(check, rfd);
    system_free(check);

    assert(n == 2);
    assert(got[0] == 'H' && got[1] == 'i');

    remove("quill_scratch.txt");
}

/* Mouse click positioning (find_click_index) against the word-wrap line
 * cache (rebuild_lines): seeds a real 3-line file, clicks at the start
 * of line 2 (row 1, left edge), types one character, saves, and checks
 * the saved bytes land exactly where a click at that pixel position
 * should put the cursor -- proves the y->line and x->column mapping
 * are both right, not just "it didn't fault". */
static void test_quill_fx_click_positions_cursor(void) {
    printf("Testing apps/fluxio/Quill.fx: mouse click positions the cursor correctly...\n");

    const char* dir = "/tmp/nuxvm_test_quill_fx_click";
    char binpath[256];
    Machine* m = quill_fx_machine(dir, binpath, sizeof(binpath));
    if (!m) return;

    /* Seed the file before the first tick -- load_file() only runs once
     * main() actually starts executing, on the first machine_tick() call
     * below, so this is well before that. */
    remove("quill_scratch.txt");
    FILE* seed = fopen("quill_scratch.txt", "wb");
    assert(seed != NULL);
    const char* content = "AB\nCD\nEF\n";
    assert(fwrite(content, 1, strlen(content), seed) == strlen(content));
    fclose(seed);

    int32_t mc = vfs_open(m->system, "/sys/chan/new", 0);
    int32_t mp = vfs_open(m->system, "/sys/chan/peer", 0);
    assert(mc >= 100 && mp >= 100);
    assert(vfs_bind(m->system, mp, "/dev/mouse") == 0);
    vfs_close(m->system, mp);

    int32_t kc = vfs_open(m->system, "/sys/chan/new", 0);
    int32_t kp = vfs_open(m->system, "/sys/chan/peer", 0);
    assert(kc >= 100 && kp >= 100);
    assert(vfs_bind(m->system, kp, "/dev/kbd") == 0);
    vfs_close(m->system, kp);

    /* MOUSE_DOWN(3) at (16, 65): row 1 (pane_y=40 + 1*line_h=20 -> [60,80)),
     * x=pane_x -> the left edge of "CD", byte index 3 in "AB\nCD\nEF\n". */
    uint8_t mpkt[8] = { 3, 1, 16, 0, 65, 0, 0, 0 };
    assert(vfs_write(m->system, mc, mpkt, 8) == 8);
    int frames = 0;
    while (!m->cpu->halted && frames < 3) {
        machine_tick(m);
        frames++;
    }

    /* Type 'X' at the clicked position, then Tab (save) and Esc (quit). */
    int keys[] = { 'X', 9, 27 };
    for (int k = 0; k < 3; k++) {
        uint8_t kpkt[8] = { 0, 0, (uint8_t) (keys[k] & 0xFF), (uint8_t) ((keys[k] >> 8) & 0xFF), 0, 0, 0, 0 };
        assert(vfs_write(m->system, kc, kpkt, 8) == 8);
        frames = 0;
        while (!m->cpu->halted && frames < 3) {
            machine_tick(m);
            frames++;
        }
    }
    vfs_close(m->system, mc);
    vfs_close(m->system, kc);

    assert(m->cpu->halted);
    machine_free(m);

    System* check = system_create();
    assert(check != NULL);
    int32_t rfd = vfs_open(check, "/sys/file/quill_scratch.txt", 0);
    assert(rfd >= 0);
    uint8_t got[16] = { 0 };
    int n = vfs_read(check, rfd, got, sizeof(got));
    vfs_close(check, rfd);
    system_free(check);

    assert(n == 10);
    assert(memcmp(got, "AB\nXCD\nEF\n", 10) == 0);

    remove("quill_scratch.txt");
}

/* rebuild_lines' word-wrap: a single "word" with no spaces, much wider
 * than the pane, must still terminate (the hard-char-wrap fallback path)
 * instead of looping forever or faulting -- the real risk in a
 * from-scratch line-wrap implementation. Also confirms editing after a
 * forced wrap doesn't corrupt the buffer: appends one more character and
 * checks the total saved length grew by exactly one. */
static void test_quill_fx_wraps_long_word_without_fault(void) {
    printf("Testing apps/fluxio/Quill.fx: word-wrap handles an overlong word without faulting...\n");

    const char* dir = "/tmp/nuxvm_test_quill_fx_wrap";
    char binpath[256];
    Machine* m = quill_fx_machine(dir, binpath, sizeof(binpath));
    if (!m) return;
    machine_free(m);

    remove("quill_scratch.txt");
    FILE* seed = fopen("quill_scratch.txt", "wb");
    assert(seed != NULL);
    char content[300];
    memset(content, 'A', sizeof(content));
    int content_len = (int) sizeof(content);
    assert(fwrite(content, 1, (size_t) content_len, seed) == (size_t) content_len);
    fclose(seed);

    m = quill_fx_machine(dir, binpath, sizeof(binpath));
    assert(m != NULL);

    int32_t kc = vfs_open(m->system, "/sys/chan/new", 0);
    int32_t kp = vfs_open(m->system, "/sys/chan/peer", 0);
    assert(kc >= 100 && kp >= 100);
    assert(vfs_bind(m->system, kp, "/dev/kbd") == 0);
    vfs_close(m->system, kp);

    int32_t mc = vfs_open(m->system, "/sys/chan/new", 0);
    int32_t mp = vfs_open(m->system, "/sys/chan/peer", 0);
    assert(mc >= 100 && mp >= 100);
    assert(vfs_bind(m->system, mp, "/dev/mouse") == 0);
    vfs_close(m->system, mp);

    /* Run enough frames to load, rebuild_lines() the 300-char word, and
     * render several times -- the thing under test is that this doesn't
     * hang or fault, not any particular rendered layout. */
    int frames = 0;
    while (!m->cpu->halted && frames < 20) {
        machine_tick(m);
        frames++;
    }
    assert(!m->cpu->halted); /* still running the main loop, not crashed/exited */

    /* Click well below all wrapped lines -- find_click_index falls
     * through to file_len -- to move the cursor to end-of-buffer without
     * needing a dedicated "End" key (Quill.fx doesn't have one yet).
     * Then append one char, save, quit. */
    uint8_t mpkt[8] = { 3, 1, 16, 0, 232, 3, 0, 0 }; /* (16, 1000): 1000 = 0x03E8 LE */
    assert(vfs_write(m->system, mc, mpkt, 8) == 8);
    frames = 0;
    while (!m->cpu->halted && frames < content_len + 5) {
        machine_tick(m);
        frames++;
    }

    int keys[] = { 'Z', 9, 27 };
    for (int k = 0; k < 3; k++) {
        uint8_t kpkt[8] = { 0, 0, (uint8_t) (keys[k] & 0xFF), (uint8_t) ((keys[k] >> 8) & 0xFF), 0, 0, 0, 0 };
        assert(vfs_write(m->system, kc, kpkt, 8) == 8);
        frames = 0;
        while (!m->cpu->halted && frames < content_len + 5) {
            machine_tick(m);
            frames++;
        }
    }
    vfs_close(m->system, kc);
    vfs_close(m->system, mc);

    assert(m->cpu->halted);
    machine_free(m);

    System* check = system_create();
    assert(check != NULL);
    int32_t rfd = vfs_open(check, "/sys/file/quill_scratch.txt", 0);
    assert(rfd >= 0);
    uint8_t got[512] = { 0 };
    int n = vfs_read(check, rfd, got, sizeof(got));
    vfs_close(check, rfd);
    system_free(check);

    assert(n == content_len + 1);
    assert(got[content_len] == 'Z');

    remove("quill_scratch.txt");
}

/* Hex mode (view-only, Home key toggles): renders a hex dump for several
 * frames without faulting, moves the cursor with arrow keys while in hex
 * mode, toggles back to text mode, and confirms editing afterward still
 * lands at the right byte offset -- proves the mode toggle doesn't lose
 * or corrupt cursor/buffer state, not just "the hex view doesn't crash". */
static void test_quill_fx_hex_mode_toggle(void) {
    printf("Testing apps/fluxio/Quill.fx: hex mode toggle, render, and cursor move...\n");

    const char* dir = "/tmp/nuxvm_test_quill_fx_hex";
    char binpath[256];
    Machine* m = quill_fx_machine(dir, binpath, sizeof(binpath));
    if (!m) return;
    machine_free(m);

    remove("quill_scratch.txt");
    FILE* seed = fopen("quill_scratch.txt", "wb");
    assert(seed != NULL);
    const char* content = "Hello, Quill!";
    assert(fwrite(content, 1, strlen(content), seed) == strlen(content));
    fclose(seed);

    m = quill_fx_machine(dir, binpath, sizeof(binpath));
    assert(m != NULL);

    int32_t kc = vfs_open(m->system, "/sys/chan/new", 0);
    int32_t kp = vfs_open(m->system, "/sys/chan/peer", 0);
    assert(kc >= 100 && kp >= 100);
    assert(vfs_bind(m->system, kp, "/dev/kbd") == 0);
    vfs_close(m->system, kp);

    /* Home (23) -> hex mode, right-arrow (20) x3 -> cursor to byte 3,
     * Home again -> back to text mode, then 'X', Tab (save), Esc (quit). */
    int keys[] = { 23, 20, 20, 20, 23, 'X', 9, 27 };
    for (int k = 0; k < 8; k++) {
        uint8_t kpkt[8] = { 0, 0, (uint8_t) (keys[k] & 0xFF), (uint8_t) ((keys[k] >> 8) & 0xFF), 0, 0, 0, 0 };
        assert(vfs_write(m->system, kc, kpkt, 8) == 8);
        int frames = 0;
        while (!m->cpu->halted && frames < 3) {
            machine_tick(m);
            frames++;
        }
    }
    vfs_close(m->system, kc);

    assert(m->cpu->halted);
    machine_free(m);

    System* check = system_create();
    assert(check != NULL);
    int32_t rfd = vfs_open(check, "/sys/file/quill_scratch.txt", 0);
    assert(rfd >= 0);
    uint8_t got[32] = { 0 };
    int n = vfs_read(check, rfd, got, sizeof(got));
    vfs_close(check, rfd);
    system_free(check);

    assert(n == (int) strlen(content) + 1);
    assert(memcmp(got, "HelXlo, Quill!", (size_t) n) == 0);

    remove("quill_scratch.txt");
}

/* Phase B7/C: the first Quill.fx test that actually exercises the linked
 * UI library (UI::sbar-*) rather than pure self-contained Fluxio. Seeds
 * 200 short lines (way more than the default 640x480 canvas's ~22
 * visible rows), pages the scrollbar down via track clicks (default
 * screen size puts the vertical bar at x=[624,640), y=[40,480) --
 * system_create()'s default resolution, src/system.c), then clicks near
 * the top of the text pane and types a marker character. If the
 * scrollbar actually scrolled the view, that click lands deep into the
 * file (whatever line scrolled up to the top), not at line 0 -- proving
 * UI::sbar-press's track-paging and Quill.fx's scroll-aware
 * find_click_index/draw_buffer are both wired correctly, not just "the
 * scrollbar renders". */
static void test_quill_fx_scrollbar_scrolls_view(void) {
    printf("Testing apps/fluxio/Quill.fx: scrollbar paging scrolls the visible lines...\n");

    const char* dir = "/tmp/nuxvm_test_quill_fx_scroll";
    char binpath[256];
    Machine* m = quill_fx_machine(dir, binpath, sizeof(binpath));
    if (!m) return;
    machine_free(m);

    remove("quill_scratch.txt");
    FILE* seed = fopen("quill_scratch.txt", "wb");
    assert(seed != NULL);
    for (int i = 0; i < 200; i++) {
        char line[2];
        line[0] = (char) ('A' + (i % 26));
        line[1] = '\n';
        assert(fwrite(line, 1, 2, seed) == 2);
    }
    fclose(seed);

    m = quill_fx_machine(dir, binpath, sizeof(binpath));
    assert(m != NULL);

    int32_t mc = vfs_open(m->system, "/sys/chan/new", 0);
    int32_t mp = vfs_open(m->system, "/sys/chan/peer", 0);
    assert(mc >= 100 && mp >= 100);
    assert(vfs_bind(m->system, mp, "/dev/mouse") == 0);
    vfs_close(m->system, mp);

    int32_t kc = vfs_open(m->system, "/sys/chan/new", 0);
    int32_t kp = vfs_open(m->system, "/sys/chan/peer", 0);
    assert(kc >= 100 && kp >= 100);
    assert(vfs_bind(m->system, kp, "/dev/kbd") == 0);
    vfs_close(m->system, kp);

    int frames = 0;
    while (!m->cpu->halted && frames < 5) {
        machine_tick(m);
        frames++;
    }
    assert(!m->cpu->halted);

    /* Three clicks at (631, 260) -- inside the bar, well below the thumb
     * (which sits near the top at scroll 0) -- each pages the view down
     * per UI::sbar-press's track-click behavior. */
    uint8_t track_down[8] = { 3, 1, 119, 2, 4, 1, 0, 0 };
    uint8_t track_up[8] = { 4, 1, 119, 2, 4, 1, 0, 0 };
    for (int c = 0; c < 3; c++) {
        assert(vfs_write(m->system, mc, track_down, 8) == 8);
        frames = 0;
        while (!m->cpu->halted && frames < 3) {
            machine_tick(m);
            frames++;
        }
        assert(vfs_write(m->system, mc, track_up, 8) == 8);
        frames = 0;
        while (!m->cpu->halted && frames < 3) {
            machine_tick(m);
            frames++;
        }
    }

    /* Click near the top-left of the text pane -- after scrolling, this
     * should land on whatever line scrolled up into view there, not
     * line 0. */
    uint8_t pane_down[8] = { 3, 1, 16, 0, 45, 0, 0, 0 };
    uint8_t pane_up[8] = { 4, 1, 16, 0, 45, 0, 0, 0 };
    assert(vfs_write(m->system, mc, pane_down, 8) == 8);
    frames = 0;
    while (!m->cpu->halted && frames < 3) {
        machine_tick(m);
        frames++;
    }
    assert(vfs_write(m->system, mc, pane_up, 8) == 8);
    frames = 0;
    while (!m->cpu->halted && frames < 3) {
        machine_tick(m);
        frames++;
    }

    int keys[] = { 'Z', 9, 27 };
    for (int k = 0; k < 3; k++) {
        uint8_t kpkt[8] = { 0, 0, (uint8_t) (keys[k] & 0xFF), (uint8_t) ((keys[k] >> 8) & 0xFF), 0, 0, 0, 0 };
        assert(vfs_write(m->system, kc, kpkt, 8) == 8);
        frames = 0;
        while (!m->cpu->halted && frames < 3) {
            machine_tick(m);
            frames++;
        }
    }
    vfs_close(m->system, mc);
    vfs_close(m->system, kc);

    assert(m->cpu->halted);
    machine_free(m);

    System* check = system_create();
    assert(check != NULL);
    int32_t rfd = vfs_open(check, "/sys/file/quill_scratch.txt", 0);
    assert(rfd >= 0);
    uint8_t got[1024] = { 0 };
    int n = vfs_read(check, rfd, got, sizeof(got));
    vfs_close(check, rfd);
    system_free(check);

    assert(n == 401); /* 200 * 2 original bytes + 1 inserted 'Z' */
    int zpos = -1;
    for (int i = 0; i < n; i++) {
        if (got[i] == 'Z') {
            zpos = i;
            break;
        }
    }
    assert(zpos > 20); /* well past the first ~10 lines -- proves the view actually scrolled */

    remove("quill_scratch.txt");
}

/* Counts non-background ink pixels in the status bar's text row, at the
 * default 960x720 resolution (status bar at y=[696,720)). Used to prove
 * the status line's content actually changes (dirty marker appears/
 * disappears) rather than just "the bar renders" -- comparing exact
 * glyph pixels would be brittle, but ink *count* going up when a " *"
 * marker is added and back down when it's removed is a solid signal. */
static int quill_fx_status_ink_pixels(Machine* m) {
    int sw = m->system->screen_width;
    int sh = m->system->screen_height;
    uint8_t* fb = m->system->screen_pixels;
    int count = 0;
    /* Sum over the whole status bar's text band (not just one row): a
     * single sampled row can land on a part of the glyphs that happens
     * not to differ between two strings of unequal length, even though
     * plenty of other rows do -- this was caught for real (a single-row
     * version of this helper missed a confirmed, visible ink increase
     * from the dirty marker at the one row it happened to sample). */
    for (int y = sh - 24; y < sh; y++) {
        for (int x = 0; x < 300; x++) {
            uint8_t* p = fb + (size_t) y * (size_t) sw * 4 + (size_t) x * 4;
            if (p[1] < 0x80 && p[2] < 0x80 && p[3] < 0x80) {
                count++;
            }
        }
    }
    return count;
}

/* Status line (Quill.lux's draw-status-line, ported): filename + dirty
 * marker + row:col. Edits one character (setting `dirty`), confirms the
 * status bar's ink pixel count increases (the " *" marker appearing),
 * then saves (clearing `dirty`) and confirms it drops back down --
 * proving the status line actually reflects live editor state, not
 * just a static label. */
static void test_quill_fx_status_line_reflects_dirty_state(void) {
    printf("Testing apps/fluxio/Quill.fx: status line shows the dirty marker after an edit...\n");

    const char* dir = "/tmp/nuxvm_test_quill_fx_status";
    char binpath[256];
    Machine* m = quill_fx_machine(dir, binpath, sizeof(binpath));
    if (!m) return;
    machine_free(m);

    remove("quill_scratch.txt");
    FILE* seed = fopen("quill_scratch.txt", "wb");
    assert(seed != NULL);
    const char* content = "Hello\nWorld\n";
    assert(fwrite(content, 1, strlen(content), seed) == strlen(content));
    fclose(seed);

    m = quill_fx_machine(dir, binpath, sizeof(binpath));
    assert(m != NULL);
    system_set_resolution(m->system, 960, 720); /* match cloister.c's real window size */

    int32_t kc = vfs_open(m->system, "/sys/chan/new", 0);
    int32_t kp = vfs_open(m->system, "/sys/chan/peer", 0);
    assert(kc >= 100 && kp >= 100);
    assert(vfs_bind(m->system, kp, "/dev/kbd") == 0);
    vfs_close(m->system, kp);

    int frames = 0;
    while (!m->cpu->halted && frames < 5) {
        machine_tick(m);
        frames++;
    }
    assert(!m->cpu->halted);
    int clean_ink = quill_fx_status_ink_pixels(m);

    uint8_t type_x[8] = { 0, 0, 'X', 0, 0, 0, 0, 0 };
    assert(vfs_write(m->system, kc, type_x, 8) == 8);
    frames = 0;
    while (!m->cpu->halted && frames < 3) {
        machine_tick(m);
        frames++;
    }
    int dirty_ink = quill_fx_status_ink_pixels(m);
    assert(dirty_ink > clean_ink); /* " *" marker adds ink */

    uint8_t save[8] = { 0, 0, 9, 0, 0, 0, 0, 0 }; /* Tab */
    assert(vfs_write(m->system, kc, save, 8) == 8);
    frames = 0;
    while (!m->cpu->halted && frames < 3) {
        machine_tick(m);
        frames++;
    }
    int saved_ink = quill_fx_status_ink_pixels(m);
    assert(saved_ink < dirty_ink); /* " *" marker gone after save */

    uint8_t quit[8] = { 0, 0, 27, 0, 0, 0, 0, 0 }; /* Esc */
    assert(vfs_write(m->system, kc, quit, 8) == 8);
    frames = 0;
    while (!m->cpu->halted && frames < 3) {
        machine_tick(m);
        frames++;
    }
    vfs_close(m->system, kc);
    assert(m->cpu->halted);
    machine_free(m);

    remove("quill_scratch.txt");
}

/* Viewport auto-follow: moves the cursor to end-of-buffer *without ever
 * touching the scrollbar* (a click below the last on-screen line, the
 * same "click past everything" trick test_quill_fx_scrollbar_scrolls_view
 * uses to reach file_len), then clicks near the top of the pane again --
 * if ensure_cursor_visible() correctly scrolled the view to follow the
 * cursor, that second click lands on whatever line the auto-scroll
 * brought to the top, not line 0. Also confirms the earlier "scroll
 * without moving the cursor stays put" fix (`last_cursor` gating in
 * main()) by checking the same 3 track-page-down clicks from
 * test_quill_fx_scrollbar_scrolls_view aren't immediately snapped back
 * to the cursor's (unmoved) line before this test's own click. */
static void test_quill_fx_viewport_follows_cursor(void) {
    printf("Testing apps/fluxio/Quill.fx: viewport scrolls to follow the cursor past the bottom...\n");

    const char* dir = "/tmp/nuxvm_test_quill_fx_follow";
    char binpath[256];
    Machine* m = quill_fx_machine(dir, binpath, sizeof(binpath));
    if (!m) return;
    machine_free(m);

    remove("quill_scratch.txt");
    FILE* seed = fopen("quill_scratch.txt", "wb");
    assert(seed != NULL);
    for (int i = 0; i < 100; i++) {
        char line[2];
        line[0] = (char) ('A' + (i % 26));
        line[1] = '\n';
        assert(fwrite(line, 1, 2, seed) == 2);
    }
    fclose(seed);

    m = quill_fx_machine(dir, binpath, sizeof(binpath));
    assert(m != NULL);
    system_set_resolution(m->system, 960, 720); /* match cloister.c's real window size */

    int32_t mc = vfs_open(m->system, "/sys/chan/new", 0);
    int32_t mp = vfs_open(m->system, "/sys/chan/peer", 0);
    assert(mc >= 100 && mp >= 100);
    assert(vfs_bind(m->system, mp, "/dev/mouse") == 0);
    vfs_close(m->system, mp);

    int32_t kc = vfs_open(m->system, "/sys/chan/new", 0);
    int32_t kp = vfs_open(m->system, "/sys/chan/peer", 0);
    assert(kc >= 100 && kp >= 100);
    assert(vfs_bind(m->system, kp, "/dev/kbd") == 0);
    vfs_close(m->system, kp);

    int frames = 0;
    while (!m->cpu->halted && frames < 5) {
        machine_tick(m);
        frames++;
    }
    assert(!m->cpu->halted);

    /* Click well below every on-screen line (at scroll 0, only the first
     * ~32 lines are on-screen at all) -- find_click_index falls through
     * to file_len, moving the cursor to the very end without any
     * scrollbar interaction. */
    uint8_t end_down[8] = { 3, 1, 16, 0, 232, 3, 0, 0 }; /* (16, 1000) */
    uint8_t end_up[8] = { 4, 1, 16, 0, 232, 3, 0, 0 };
    assert(vfs_write(m->system, mc, end_down, 8) == 8);
    frames = 0;
    while (!m->cpu->halted && frames < 3) {
        machine_tick(m);
        frames++;
    }
    assert(vfs_write(m->system, mc, end_up, 8) == 8);
    frames = 0;
    while (!m->cpu->halted && frames < 3) {
        machine_tick(m);
        frames++;
    }

    /* Click near the top-left of the text pane again -- if the view
     * followed the cursor to the bottom, this now lands on whatever
     * line scrolled up into view there, not line 0. */
    uint8_t top_down[8] = { 3, 1, 16, 0, 45, 0, 0, 0 };
    uint8_t top_up[8] = { 4, 1, 16, 0, 45, 0, 0, 0 };
    assert(vfs_write(m->system, mc, top_down, 8) == 8);
    frames = 0;
    while (!m->cpu->halted && frames < 3) {
        machine_tick(m);
        frames++;
    }
    assert(vfs_write(m->system, mc, top_up, 8) == 8);
    frames = 0;
    while (!m->cpu->halted && frames < 3) {
        machine_tick(m);
        frames++;
    }

    /* '#' (35), not a letter -- the seed content cycles through 'A'-'Z'
     * (line i uses 'A' + i%26), so a letter marker like 'Z' would
     * collide with the *pre-existing* 'Z' the seed loop already placed
     * at line 25 (byte 50) and silently match the wrong occurrence. */
    int keys[] = { '#', 9, 27 };
    for (int k = 0; k < 3; k++) {
        uint8_t kpkt[8] = { 0, 0, (uint8_t) (keys[k] & 0xFF), (uint8_t) ((keys[k] >> 8) & 0xFF), 0, 0, 0, 0 };
        assert(vfs_write(m->system, kc, kpkt, 8) == 8);
        frames = 0;
        while (!m->cpu->halted && frames < 3) {
            machine_tick(m);
            frames++;
        }
    }
    vfs_close(m->system, mc);
    vfs_close(m->system, kc);

    assert(m->cpu->halted);
    machine_free(m);

    System* check = system_create();
    assert(check != NULL);
    int32_t rfd = vfs_open(check, "/sys/file/quill_scratch.txt", 0);
    assert(rfd >= 0);
    uint8_t got[1024] = { 0 };
    int n = vfs_read(check, rfd, got, sizeof(got));
    vfs_close(check, rfd);
    system_free(check);

    assert(n == 201); /* 100 * 2 original bytes + 1 inserted '#' */
    int zpos = -1;
    for (int i = 0; i < n; i++) {
        if (got[i] == '#') {
            zpos = i;
            break;
        }
    }
    /* Well past the first ~32 on-screen lines (64 bytes) -- proves the
     * view scrolled to follow the cursor to end-of-buffer on its own. */
    assert(zpos > 64);

    remove("quill_scratch.txt");
}

/* Phase C menu bar, the second thing (after the scrollbar) in this port
 * to actually call into the Phase B linked UI library: types a
 * character (dirtying the buffer), then drives the real menu -- click
 * the "File" title to open it (UI::mb-down-open), then click "Save" in
 * the dropdown to fire it (UI::mb-apply posts the click, drained via
 * UI::poll-next/poll-name in Quill.fx, matching by comparing the fired
 * name pointer directly against `menu_save_label`'s own address, since
 * Fluxio owns that label buffer and UI::item was given that exact
 * pointer). Confirms the save actually happened two ways: the on-disk
 * bytes match what was typed, and the status bar's dirty marker is gone
 * afterward (reusing the same whole-band ink-count technique
 * test_quill_fx_status_line_reflects_dirty_state uses) -- proving the
 * menu path exercises the same save_file(), not a different one.
 */
/* The menu bar can fire correctly (clicks reach UI::mb-apply, events
 * drain via poll-next/poll-name) while still being invisible: UI::draw's
 * menu-bar rendering (mb-draw-bar/mb-draw-drop) draws through
 * `DRAW::fd` (lib/draw.lux), a Lux-side global set only by `DRAW::use`
 * -- unlike UI::sbar-draw or Quill.fx's own draw_bytes/fill_rect calls,
 * which all take an fd argument directly. Without calling `DRAW::use`
 * once at startup, every menu-bar draw call silently targets fd 0
 * instead of the real /dev/draw fd. This was caught for real during
 * development (the menu fired correctly in every test above, but
 * genuinely didn't render), so it gets a dedicated pixel-presence check
 * -- functional tests alone would never catch a "fires but invisible"
 * regression here. */
static void test_quill_fx_menu_bar_renders(void) {
    printf("Testing apps/fluxio/Quill.fx: menu bar actually renders (not just fires)...\n");

    const char* dir = "/tmp/nuxvm_test_quill_fx_menu_render";
    char binpath[256];
    Machine* m = quill_fx_machine(dir, binpath, sizeof(binpath));
    if (!m) return;

    remove("quill_scratch.txt");
    FILE* seed = fopen("quill_scratch.txt", "wb");
    assert(seed != NULL);
    assert(fwrite("Hello\n", 1, 6, seed) == 6);
    fclose(seed);

    m = quill_fx_machine(dir, binpath, sizeof(binpath));
    assert(m != NULL);
    system_set_resolution(m->system, 960, 720); /* match cloister.c's real window size */

    int frames = 0;
    while (!m->cpu->halted && frames < 8) {
        machine_tick(m);
        frames++;
    }
    assert(!m->cpu->halted);

    int sw = m->system->screen_width;
    uint8_t* fb = m->system->screen_pixels;
    int ink = 0;
    for (int y = 0; y < 20; y++) { /* MENU_H, lib/ui.lux */
        for (int x = 0; x < 200; x++) {
            uint8_t* p = fb + (size_t) y * (size_t) sw * 4 + (size_t) x * 4;
            if (p[1] < 0x80 && p[2] < 0x80 && p[3] < 0x80) {
                ink++;
            }
        }
    }
    assert(ink > 0); /* the "File"/"View" titles must actually paint something */

    machine_free(m);
    remove("quill_scratch.txt");
}

/* Real bug reported after the menu bar was declared working: switching
 * to hex mode made the menu bar disappear and stop responding entirely.
 * Root cause was gating the *whole* mouse-feed/poll block and the
 * UI::draw call on `!hex_mode`, when only the scrollbar and text-pane
 * click handling are actually text-mode-specific -- the menu bar itself
 * has no such restriction in lib/ui.lux. Checks both halves of that
 * symptom: the menu bar still paints something while hex_mode is on
 * (same ink-presence technique as test_quill_fx_menu_bar_renders), and
 * it's still genuinely interactive there -- clicking View > Toggle Hex
 * while already in hex mode flips back to text mode, confirmed
 * behaviorally (a character typed afterward actually gets inserted and
 * saved, which the keyboard dispatch only does when hex_mode is false)
 * rather than by reading internal state directly. */
static void test_quill_fx_menu_bar_works_in_hex_mode(void) {
    printf("Testing apps/fluxio/Quill.fx: menu bar stays visible and interactive in hex mode...\n");

    const char* dir = "/tmp/nuxvm_test_quill_fx_menu_hex";
    char binpath[256];
    Machine* m = quill_fx_machine(dir, binpath, sizeof(binpath));
    if (!m) return;

    remove("quill_scratch.txt");
    FILE* seed = fopen("quill_scratch.txt", "wb");
    assert(seed != NULL);
    const char* content = "Hello\n";
    assert(fwrite(content, 1, strlen(content), seed) == strlen(content));
    fclose(seed);

    m = quill_fx_machine(dir, binpath, sizeof(binpath));
    assert(m != NULL);
    system_set_resolution(m->system, 960, 720); /* match cloister.c's real window size */

    int32_t mc = vfs_open(m->system, "/sys/chan/new", 0);
    int32_t mp = vfs_open(m->system, "/sys/chan/peer", 0);
    assert(mc >= 100 && mp >= 100);
    assert(vfs_bind(m->system, mp, "/dev/mouse") == 0);
    vfs_close(m->system, mp);

    int32_t kc = vfs_open(m->system, "/sys/chan/new", 0);
    int32_t kp = vfs_open(m->system, "/sys/chan/peer", 0);
    assert(kc >= 100 && kp >= 100);
    assert(vfs_bind(m->system, kp, "/dev/kbd") == 0);
    vfs_close(m->system, kp);

    int frames = 0;
    while (!m->cpu->halted && frames < 5) {
        machine_tick(m);
        frames++;
    }
    assert(!m->cpu->halted);

    /* Home (23) -> hex mode. */
    uint8_t home[8] = { 0, 0, 23, 0, 0, 0, 0, 0 };
    assert(vfs_write(m->system, kc, home, 8) == 8);
    frames = 0;
    while (!m->cpu->halted && frames < 3) {
        machine_tick(m);
        frames++;
    }

    int sw = m->system->screen_width;
    uint8_t* fb = m->system->screen_pixels;
    int ink = 0;
    for (int y = 0; y < 20; y++) { /* MENU_H, lib/ui.lux */
        for (int x = 0; x < 200; x++) {
            uint8_t* p = fb + (size_t) y * (size_t) sw * 4 + (size_t) x * 4;
            if (p[1] < 0x80 && p[2] < 0x80 && p[3] < 0x80) {
                ink++;
            }
        }
    }
    assert(ink > 0); /* menu bar still paints something while hex_mode is on */

    /* Click "View" (second menu title, roughly x=[58,100)), then click
     * "Toggle Hex" (its only dropdown item, row 0). */
    uint8_t view_down[8] = { 3, 1, 70, 0, 10, 0, 0, 0 };
    uint8_t view_up[8] = { 4, 1, 70, 0, 10, 0, 0, 0 };
    assert(vfs_write(m->system, mc, view_down, 8) == 8);
    frames = 0;
    while (!m->cpu->halted && frames < 3) {
        machine_tick(m);
        frames++;
    }
    assert(vfs_write(m->system, mc, view_up, 8) == 8);
    frames = 0;
    while (!m->cpu->halted && frames < 3) {
        machine_tick(m);
        frames++;
    }

    uint8_t item_down[8] = { 3, 1, 70, 0, 29, 0, 0, 0 };
    uint8_t item_up[8] = { 4, 1, 70, 0, 29, 0, 0, 0 };
    assert(vfs_write(m->system, mc, item_down, 8) == 8);
    frames = 0;
    while (!m->cpu->halted && frames < 3) {
        machine_tick(m);
        frames++;
    }
    assert(vfs_write(m->system, mc, item_up, 8) == 8);
    frames = 0;
    while (!m->cpu->halted && frames < 3) {
        machine_tick(m);
        frames++;
    }

    /* If the click actually flipped hex_mode back off, this insert and
     * the Tab save will actually run -- the keyboard dispatch only takes
     * this path when !hex_mode. */
    uint8_t marker[8] = { 0, 0, '#', 0, 0, 0, 0, 0 };
    assert(vfs_write(m->system, kc, marker, 8) == 8);
    frames = 0;
    while (!m->cpu->halted && frames < 3) {
        machine_tick(m);
        frames++;
    }
    uint8_t save[8] = { 0, 0, 9, 0, 0, 0, 0, 0 };
    assert(vfs_write(m->system, kc, save, 8) == 8);
    frames = 0;
    while (!m->cpu->halted && frames < 3) {
        machine_tick(m);
        frames++;
    }
    uint8_t quit[8] = { 0, 0, 27, 0, 0, 0, 0, 0 };
    assert(vfs_write(m->system, kc, quit, 8) == 8);
    frames = 0;
    while (!m->cpu->halted && frames < 3) {
        machine_tick(m);
        frames++;
    }
    vfs_close(m->system, mc);
    vfs_close(m->system, kc);
    assert(m->cpu->halted);
    machine_free(m);

    System* check = system_create();
    assert(check != NULL);
    int32_t rfd = vfs_open(check, "/sys/file/quill_scratch.txt", 0);
    assert(rfd >= 0);
    uint8_t got[32] = { 0 };
    int n = vfs_read(check, rfd, got, sizeof(got));
    vfs_close(check, rfd);
    system_free(check);

    assert(n == (int) strlen(content) + 1);
    assert(got[0] == '#'); /* only possible if the menu click really returned us to text mode */

    remove("quill_scratch.txt");
}

static void test_quill_fx_menu_save_via_click(void) {
    printf("Testing apps/fluxio/Quill.fx: File > Save via the linked menu bar...\n");

    const char* dir = "/tmp/nuxvm_test_quill_fx_menu";
    char binpath[256];
    Machine* m = quill_fx_machine(dir, binpath, sizeof(binpath));
    if (!m) return;

    remove("quill_scratch.txt");
    FILE* seed = fopen("quill_scratch.txt", "wb");
    assert(seed != NULL);
    const char* content = "Hello\nWorld\n";
    assert(fwrite(content, 1, strlen(content), seed) == strlen(content));
    fclose(seed);

    int32_t mc = vfs_open(m->system, "/sys/chan/new", 0);
    int32_t mp = vfs_open(m->system, "/sys/chan/peer", 0);
    assert(mc >= 100 && mp >= 100);
    assert(vfs_bind(m->system, mp, "/dev/mouse") == 0);
    vfs_close(m->system, mp);

    int32_t kc = vfs_open(m->system, "/sys/chan/new", 0);
    int32_t kp = vfs_open(m->system, "/sys/chan/peer", 0);
    assert(kc >= 100 && kp >= 100);
    assert(vfs_bind(m->system, kp, "/dev/kbd") == 0);
    vfs_close(m->system, kp);

    int frames = 0;
    while (!m->cpu->halted && frames < 5) {
        machine_tick(m);
        frames++;
    }
    assert(!m->cpu->halted);

    /* Type '#' at the start -- dirties the buffer without touching the
     * seed's own byte values (see the marker-collision note above). */
    uint8_t type_marker[8] = { 0, 0, '#', 0, 0, 0, 0, 0 };
    assert(vfs_write(m->system, kc, type_marker, 8) == 8);
    frames = 0;
    while (!m->cpu->halted && frames < 3) {
        machine_tick(m);
        frames++;
    }
    int dirty_ink = quill_fx_status_ink_pixels(m);

    /* Click "File" (title, MENU_PAD=10..~58, y within [0, MENU_H=20)). */
    uint8_t file_down[8] = { 3, 1, 20, 0, 10, 0, 0, 0 };
    uint8_t file_up[8] = { 4, 1, 20, 0, 10, 0, 0, 0 };
    assert(vfs_write(m->system, mc, file_down, 8) == 8);
    frames = 0;
    while (!m->cpu->halted && frames < 3) {
        machine_tick(m);
        frames++;
    }
    assert(vfs_write(m->system, mc, file_up, 8) == 8);
    frames = 0;
    while (!m->cpu->halted && frames < 3) {
        machine_tick(m);
        frames++;
    }

    /* Click "Save" (the dropdown's first item, ITEM_H=18, row 0 center). */
    uint8_t save_down[8] = { 3, 1, 20, 0, 29, 0, 0, 0 };
    uint8_t save_up[8] = { 4, 1, 20, 0, 29, 0, 0, 0 };
    assert(vfs_write(m->system, mc, save_down, 8) == 8);
    frames = 0;
    while (!m->cpu->halted && frames < 3) {
        machine_tick(m);
        frames++;
    }
    assert(vfs_write(m->system, mc, save_up, 8) == 8);
    frames = 0;
    while (!m->cpu->halted && frames < 3) {
        machine_tick(m);
        frames++;
    }
    int saved_ink = quill_fx_status_ink_pixels(m);
    assert(saved_ink < dirty_ink); /* dirty marker gone -- Save actually ran */

    uint8_t quit[8] = { 0, 0, 27, 0, 0, 0, 0, 0 };
    assert(vfs_write(m->system, kc, quit, 8) == 8);
    frames = 0;
    while (!m->cpu->halted && frames < 3) {
        machine_tick(m);
        frames++;
    }
    vfs_close(m->system, mc);
    vfs_close(m->system, kc);
    assert(m->cpu->halted);
    machine_free(m);

    System* check = system_create();
    assert(check != NULL);
    int32_t rfd = vfs_open(check, "/sys/file/quill_scratch.txt", 0);
    assert(rfd >= 0);
    uint8_t got[32] = { 0 };
    int n = vfs_read(check, rfd, got, sizeof(got));
    vfs_close(check, rfd);
    system_free(check);

    assert(n == (int) strlen(content) + 1);
    assert(got[0] == '#'); /* typed at cursor 0, saved via the menu click */

    remove("quill_scratch.txt");
}

/* -----------------------------------------------------------------------
 * End-to-end fixture via the on-disk .bin path is covered by examples/fluxio/fib.fx
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
    test_lexer_new_keywords();
    test_lexer_string_escapes();

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
    test_call_as_statement_discards_result();
    test_extern_int_emits_call();
    test_extern_void_skips_pop();

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
    test_draw_bytes_matches_draw_str();
    test_draw_bytes_oversized_len_clamped();
    test_poll_no_events();
    test_accessors_callable();
    test_frame_loop_multi_yield();

    test_vfs_write_read_roundtrip();
    test_vfs_seek_and_stat();
    test_vfs_write_chunk();

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
    test_byte_arrays();
    test_error_byte_type_misuse();
    test_error_extern_void_as_value();
    test_error_extern_arity_and_shape();
    test_error_extern_name_collisions();
    test_error_redeclared_function_and_params();
    test_error_builtin_string_arg_required();
    test_error_builtin_int_arg_required();

    test_quill_fx_type_and_save();
    test_quill_fx_click_positions_cursor();
    test_quill_fx_wraps_long_word_without_fault();
    test_quill_fx_hex_mode_toggle();
    test_quill_fx_scrollbar_scrolls_view();
    test_quill_fx_status_line_reflects_dirty_state();
    test_quill_fx_viewport_follows_cursor();
    test_quill_fx_menu_bar_renders();
    test_quill_fx_menu_bar_works_in_hex_mode();
    test_quill_fx_menu_save_via_click();
    test_include_circular_error();
    test_include_missing_file_error();
    test_error_local_struct_decay();
    test_error_assign_whole_struct();
    test_error_unknown_field();
    test_error_struct_naming_and_shape();

    printf("\nAll Fluxio compiler tests passed!\n");
    return 0;
}
