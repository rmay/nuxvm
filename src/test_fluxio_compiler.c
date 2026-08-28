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
#include <sys/stat.h>

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
 * lib/escape_menu.fx -- generalized Esc-to-pause/quit menu, importable by
 * any Fluxio app via `include "lib/escape_menu.fx";`. Exercised against
 * the real repo file (not a copy), via an absolute include path built
 * from getcwd() -- these tests assume they're run from the repo root
 * (true for every other file-based test here, e.g. quill_fx_machine()).
 * ----------------------------------------------------------------------- */

static void escmenu_include_path(char* out, size_t cap) {
    char cwd[1024];
    assert(getcwd(cwd, sizeof(cwd)) != NULL);
    snprintf(out, cap, "%s/lib/escape_menu.fx", cwd);
}

/* Same as check_include_result, but leaves the compiled bytecode's length
 * out-param instead of running it -- callers that need machine_create()
 * (for fill_rect/draw_str's System dependency) rather than a bare VM use
 * this directly. */
static uint8_t* must_compile_with_includes(const char* dir, const char* entry_name, size_t* out_len) {
    char path[1024];
    snprintf(path, sizeof(path), "%s/%s", dir, entry_name);
    FxTokenList* tokens = fx_load_with_includes(path);
    assert(tokens != NULL);
    FxProgram* program = fx_parse(tokens);
    fx_token_list_free(tokens);
    assert(program != NULL);
    uint8_t* bc = fx_codegen(program, HEADLESS_BASE_ADDRESS, out_len);
    fx_program_free(program);
    assert(bc != NULL);
    return bc;
}

static void test_escape_menu_esc_toggles_open(void) {
    printf("Testing escape_menu: Esc opens, consumes; Esc again closes, consumes; other keys pass through only while closed...\n");
    char libpath[1024];
    escmenu_include_path(libpath, sizeof(libpath));
    char dir[] = "/tmp/fluxio_test_escmenu_toggle_XXXXXX";
    assert(mkdtemp(dir) != NULL);
    char main_src[2048];
    snprintf(main_src, sizeof(main_src),
        "include \"%s\";\n"
        "/** e */\n"
        "int main() {\n"
        "    escmenu_init(320, 240);\n"
        "    int c1 = escmenu_key(27);\n"
        "    int open1 = escmenu_is_open();\n"
        "    int c2 = escmenu_key(65);\n"
        "    int c3 = escmenu_key(27);\n"
        "    int open2 = escmenu_is_open();\n"
        "    int c4 = escmenu_key(65);\n"
        "    return c1*100000 + open1*10000 + c2*1000 + c3*100 + open2*10 + c4;\n"
        "}\n", libpath);
    write_temp_file(dir, "main.fx", main_src);
    /* c1=1 (Esc opens+consumes), open1=1, c2=1 ('A' consumed while open),
     * c3=1 (Esc closes+consumes), open2=0, c4=0 ('A' passes through once closed). */
    check_include_result(dir, "main.fx", 111100);
}

static void test_escape_menu_quit_click_sets_flag(void) {
    printf("Testing escape_menu: clicking Quit sets escmenu_wants_quit() and leaves the menu open...\n");
    char libpath[1024];
    escmenu_include_path(libpath, sizeof(libpath));
    char dir[] = "/tmp/fluxio_test_escmenu_quit_XXXXXX";
    assert(mkdtemp(dir) != NULL);
    char main_src[2048];
    snprintf(main_src, sizeof(main_src),
        "include \"%s\";\n"
        "/** e */\n"
        "int main() {\n"
        "    escmenu_init(320, 240);\n"
        "    escmenu_key(27);\n"
        "    int bx = escmenu_btn_x();\n"
        "    int by = escmenu_btn_y(1);\n"
        "    int consumed = escmenu_mouse(3, 1, bx + 5, by + 5);\n"
        "    int wants_quit = escmenu_wants_quit();\n"
        "    int still_open = escmenu_is_open();\n"
        "    return consumed*100 + wants_quit*10 + still_open;\n"
        "}\n", libpath);
    write_temp_file(dir, "main.fx", main_src);
    check_include_result(dir, "main.fx", 111);
}

static void test_escape_menu_resume_click_closes(void) {
    printf("Testing escape_menu: clicking Resume closes the menu without setting the quit flag...\n");
    char libpath[1024];
    escmenu_include_path(libpath, sizeof(libpath));
    char dir[] = "/tmp/fluxio_test_escmenu_resume_XXXXXX";
    assert(mkdtemp(dir) != NULL);
    char main_src[2048];
    snprintf(main_src, sizeof(main_src),
        "include \"%s\";\n"
        "/** e */\n"
        "int main() {\n"
        "    escmenu_init(320, 240);\n"
        "    escmenu_key(27);\n"
        "    int bx = escmenu_btn_x();\n"
        "    int by = escmenu_btn_y(0);\n"
        "    int consumed = escmenu_mouse(3, 1, bx + 5, by + 5);\n"
        "    int still_open = escmenu_is_open();\n"
        "    int wants_quit = escmenu_wants_quit();\n"
        "    return consumed*100 + still_open*10 + wants_quit;\n"
        "}\n", libpath);
    write_temp_file(dir, "main.fx", main_src);
    check_include_result(dir, "main.fx", 100);
}

static void test_escape_menu_click_outside_buttons_is_noop(void) {
    printf("Testing escape_menu: a click inside the panel but outside both buttons is consumed but does nothing...\n");
    char libpath[1024];
    escmenu_include_path(libpath, sizeof(libpath));
    char dir[] = "/tmp/fluxio_test_escmenu_missclick_XXXXXX";
    assert(mkdtemp(dir) != NULL);
    char main_src[2048];
    snprintf(main_src, sizeof(main_src),
        "include \"%s\";\n"
        "/** e */\n"
        "int main() {\n"
        "    escmenu_init(320, 240);\n"
        "    escmenu_key(27);\n"
        "    int consumed = escmenu_mouse(3, 1, 0, 0);\n"
        "    int still_open = escmenu_is_open();\n"
        "    int wants_quit = escmenu_wants_quit();\n"
        "    return consumed*100 + still_open*10 + wants_quit;\n"
        "}\n", libpath);
    write_temp_file(dir, "main.fx", main_src);
    check_include_result(dir, "main.fx", 110);
}

static void test_escape_menu_inert_while_closed(void) {
    printf("Testing escape_menu: key/mouse both pass through (return 0) while the menu has never been opened...\n");
    char libpath[1024];
    escmenu_include_path(libpath, sizeof(libpath));
    char dir[] = "/tmp/fluxio_test_escmenu_inert_XXXXXX";
    assert(mkdtemp(dir) != NULL);
    char main_src[2048];
    snprintf(main_src, sizeof(main_src),
        "include \"%s\";\n"
        "/** e */\n"
        "int main() {\n"
        "    escmenu_init(320, 240);\n"
        "    int k = escmenu_key(65);\n"
        "    int m = escmenu_mouse(3, 1, 10, 10);\n"
        "    return k*10 + m;\n"
        "}\n", libpath);
    write_temp_file(dir, "main.fx", main_src);
    check_include_result(dir, "main.fx", 0);
}

/* Renders the menu into a real framebuffer via machine_create() (needed
 * for fill_rect's System dependency, unlike the logic-only tests above)
 * and checks a pixel inside the panel: the app's own white background
 * shows through while closed, and the panel's dark fill color shows once
 * Esc opens it -- proving escmenu_draw actually paints something, not
 * just that the logic-only state machine above is self-consistent. */
static void test_escape_menu_renders_when_open(void) {
    printf("Testing escape_menu: escmenu_draw paints the panel only while open...\n");
    char libpath[1024];
    escmenu_include_path(libpath, sizeof(libpath));
    char dir[] = "/tmp/fluxio_test_escmenu_draw_XXXXXX";
    assert(mkdtemp(dir) != NULL);
    char main_src[2048];
    snprintf(main_src, sizeof(main_src),
        "include \"%s\";\n"
        "/** e */\n"
        "int main() {\n"
        "    int fd = vfs_open(\"/dev/draw\");\n"
        "    int size = canvas_size(fd);\n"
        "    int w = size >> 16;\n"
        "    int h = size & 0xFFFF;\n"
        "    escmenu_init(w, h);\n"
        "    begin_frame(fd);\n"
        "    fill_rect(fd, 0, 0, w, h, 0xFFFFFF);\n"
        "    %s\n"
        "    escmenu_draw(fd);\n"
        "    end_frame(fd);\n"
        "    return 0;\n"
        "}\n", libpath, "%s");

    /* Build both variants (menu opened vs. never opened) from the same
     * template, so a change to one can't accidentally drift from the
     * other. */
    char closed_src[2048];
    snprintf(closed_src, sizeof(closed_src), main_src, "");
    char open_src[2048];
    snprintf(open_src, sizeof(open_src), main_src, "escmenu_key(27);");

    write_temp_file(dir, "closed.fx", closed_src);
    write_temp_file(dir, "open.fx", open_src);

    size_t clen, olen;
    uint8_t* cbc = must_compile_with_includes(dir, "closed.fx", &clen);
    uint8_t* obc = must_compile_with_includes(dir, "open.fx", &olen);

    Machine* cm = run_machine_pumped(cbc, clen, 1);
    Machine* om = run_machine_pumped(obc, olen, 1);
    assert(cm->cpu->halted && om->cpu->halted);

    /* Default 640x480 canvas (src/system.c) -> panel at x=[210,430),
     * y=[175,305); sample 5px inside the top-left corner, past the
     * 1px border, well clear of any button/text glyph. */
    int sw = cm->system->screen_width ? cm->system->screen_width : 640;
    uint8_t* cfb = cm->system->screen_pixels;
    uint8_t* ofb = om->system->screen_pixels;
    uint8_t* cpix = cfb + (size_t) 180 * sw * 4 + (size_t) 215 * 4;
    uint8_t* opix = ofb + (size_t) 180 * sw * 4 + (size_t) 215 * 4;

    /* Closed: app's own white fill_rect shows through untouched. */
    assert(cpix[1] == 0xFF && cpix[2] == 0xFF && cpix[3] == 0xFF);
    /* Open: escmenu's panel color (0x303030) painted over it. */
    assert(opix[1] == 0x30 && opix[2] == 0x30 && opix[3] == 0x30);

    machine_free(cm);
    machine_free(om);
    free(cbc);
    free(obc);
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

/* Hex mode's own scrollbar+click support (v9): same shared sb_bar as text
 * mode, but re-ranged in 16-byte-row units (hex_row_count/max_scroll) and
 * with its own reverse pixel->offset mapping (hex_find_click_index). Seeds
 * a file with far more than one screen's worth of hex rows, enters hex
 * mode, pages the scrollbar down via track clicks, then clicks near the
 * top-left of the pane and moves one step with the right arrow. If hex
 * mode's scroll-follow and click mapping are both wired correctly, that
 * click+step lands deep into the file (not near offset 0) -- confirmed by
 * flipping back to text mode and typing a marker at the resulting cursor,
 * then checking where it landed in the saved file. */
static void test_quill_fx_hex_click_after_scroll(void) {
    printf("Testing apps/fluxio/Quill.fx: hex mode scrollbar and click-to-position after scrolling...\n");

    const char* dir = "/tmp/nuxvm_test_quill_fx_hex_scroll";
    char binpath[256];
    Machine* m = quill_fx_machine(dir, binpath, sizeof(binpath));
    if (!m) return;
    machine_free(m);

    remove("quill_scratch.txt");
    FILE* seed = fopen("quill_scratch.txt", "wb");
    assert(seed != NULL);
    const int seed_len = 2000; /* far more than one screen's ~20 hex rows (320 bytes) */
    for (int i = 0; i < seed_len; i++) {
        uint8_t b = (uint8_t) ('a' + (i % 26));
        assert(fwrite(&b, 1, 1, seed) == 1);
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

    /* Home -> hex mode. */
    uint8_t home[8] = { 0, 0, 23, 0, 0, 0, 0, 0 };
    assert(vfs_write(m->system, kc, home, 8) == 8);
    frames = 0;
    while (!m->cpu->halted && frames < 3) {
        machine_tick(m);
        frames++;
    }

    /* Page the (now hex-row-ranged) scrollbar down three times, same
     * track-click coordinates test_quill_fx_scrollbar_scrolls_view uses. */
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

    /* Click near the top-left of the hex pane -- after scrolling, this
     * should map (via hex_find_click_index) to whatever row scrolled up
     * into view there, not row 0. */
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
    vfs_close(m->system, mc);

    /* Home again -> back to text mode (cursor, a byte offset, carries
     * over unchanged), then type a marker at that offset, save, quit. */
    int keys[] = { 23, '#', 9, 27 };
    for (int k = 0; k < 4; k++) {
        uint8_t kpkt[8] = { 0, 0, (uint8_t) (keys[k] & 0xFF), (uint8_t) ((keys[k] >> 8) & 0xFF), 0, 0, 0, 0 };
        assert(vfs_write(m->system, kc, kpkt, 8) == 8);
        frames = 0;
        while (!m->cpu->halted && frames < 10) {
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
    uint8_t got[4096] = { 0 };
    int n = vfs_read(check, rfd, got, sizeof(got));
    vfs_close(check, rfd);
    system_free(check);

    assert(n == seed_len + 1);
    int marker_pos = -1;
    for (int i = 0; i < n; i++) {
        if (got[i] == '#') {
            marker_pos = i;
            break;
        }
    }
    assert(marker_pos > 320); /* past a single screen's worth of hex rows -- proves the view actually scrolled and the click mapped into it */

    remove("quill_scratch.txt");
}

/* Finds the pixel x where the widest gap in a hex-mode row's ink ends,
 * within a row's y band -- used below to locate where the ASCII column
 * starts (the biggest horizontal gap in a hex-dump row is always the one
 * between the last hex-pair digit and the first ASCII character). */
static int quill_fx_hex_row_ascii_x(Machine* m, int y0, int y1) {
    int sw = m->system->screen_width;
    uint8_t* fb = m->system->screen_pixels;
    int last_ink = -1;
    int best_gap = 0;
    int best_gap_end = -1;
    for (int x = 0; x < 500; x++) {
        int ink = 0;
        for (int y = y0; y < y1; y++) {
            uint8_t* p = fb + (size_t) y * (size_t) sw * 4 + (size_t) x * 4;
            if (p[1] < 0x80 && p[2] < 0x80 && p[3] < 0x80) {
                ink = 1;
                break;
            }
        }
        if (ink && last_ink >= 0 && (x - last_ink) > best_gap) {
            best_gap = x - last_ink;
            best_gap_end = x;
        }
        if (ink) {
            last_ink = x;
        }
    }
    return best_gap_end;
}

/* Real bug, caught from a user screenshot: a file whose last hex row has
 * fewer than 16 bytes showed that row's ASCII column noticeably out of
 * line with the ASCII column in the full rows above it. Root cause: the
 * unused hex-pair slots in a short row used to be rendered as literal
 * blank space characters (render_hex_row), and in Quill.fx's proportional
 * font a run of spaces doesn't measure the same as a run of real hex
 * digits -- so a single draw_bytes call over the whole 72-byte row buffer
 * put the ASCII column at a different accumulated x for a short row than
 * for a full one. Fixed by having render_hex_row always render all 16
 * columns, treating a column past end-of-file as a phantom zero byte
 * (matching hex_col_x's existing caret-positioning convention) instead of
 * leaving it blank -- every row now accumulates the same real-digit width
 * regardless of how many bytes it actually has. Seeds a 3-row file whose
 * last row has only 11 of 16 bytes (same shape as the reported
 * screenshot) and confirms all three rows' ASCII columns land within a
 * few pixels of each other -- the same small (~4px) row-to-row jitter
 * full rows already show each other, from real digit glyphs of different
 * values measuring slightly differently in the proportional font. */
static void test_quill_fx_hex_ascii_column_aligns_on_short_row(void) {
    printf("Testing apps/fluxio/Quill.fx: hex mode ASCII column stays aligned on a short last row...\n");

    const char* dir = "/tmp/nuxvm_test_quill_fx_hex_align";
    char binpath[256];
    Machine* m = quill_fx_machine(dir, binpath, sizeof(binpath));
    if (!m) return;
    machine_free(m);

    remove("quill_scratch.txt");
    FILE* seed = fopen("quill_scratch.txt", "wb");
    assert(seed != NULL);
    const char* content = "Line One\nLine Two\nLine Three\nskip\nLINE FOUR"; /* 43 bytes: two full 16-byte rows + an 11-byte last row */
    assert(fwrite(content, 1, strlen(content), seed) == strlen(content));
    fclose(seed);

    m = quill_fx_machine(dir, binpath, sizeof(binpath));
    assert(m != NULL);

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

    uint8_t home[8] = { 0, 0, 23, 0, 0, 0, 0, 0 }; /* Home -> hex mode */
    assert(vfs_write(m->system, kc, home, 8) == 8);
    frames = 0;
    while (!m->cpu->halted && frames < 3) {
        machine_tick(m);
        frames++;
    }

    int ascii_x_row0 = quill_fx_hex_row_ascii_x(m, 40, 60);
    int ascii_x_row1 = quill_fx_hex_row_ascii_x(m, 60, 80);
    int ascii_x_row2 = quill_fx_hex_row_ascii_x(m, 80, 100); /* the short (11-byte) row */
    assert(ascii_x_row0 > 0 && ascii_x_row1 > 0 && ascii_x_row2 > 0);
    /* Within a few pixels, not exact -- real digit glyphs of different
     * values measure slightly differently even between two full rows
     * (confirmed: row0 vs row1 alone differ by ~4px here). The bug this
     * guards against was a ~50-130px miss, far outside this tolerance. */
    int diff01 = ascii_x_row2 - ascii_x_row0;
    if (diff01 < 0) {
        diff01 = -diff01;
    }
    int diff12 = ascii_x_row2 - ascii_x_row1;
    if (diff12 < 0) {
        diff12 = -diff12;
    }
    assert(diff01 <= 10);
    assert(diff12 <= 10);

    vfs_close(m->system, kc);
    machine_free(m);
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

    /* Click "View" (third menu title now that Edit sits between File and
     * View -- File/Edit both clamp to MENU_MIN_TW=48px, lib/ui.lux, so
     * View starts at MENU_PAD(10) + 48 + 48 = 106, roughly x=[106,154)),
     * then click "Toggle Hex" (its only dropdown item, row 0). */
    uint8_t view_down[8] = { 3, 1, 120, 0, 10, 0, 0, 0 };
    uint8_t view_up[8] = { 4, 1, 120, 0, 10, 0, 0, 0 };
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

    uint8_t item_down[8] = { 3, 1, 120, 0, 29, 0, 0, 0 };
    uint8_t item_up[8] = { 4, 1, 120, 0, 29, 0, 0, 0 };
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

/* v10 addition: hex-mode nibble editing. Typing a hex digit ('0'-'9',
 * 'A'-'F') edits the nibble under the cursor in place -- high nibble
 * first, then low, auto-advancing to the next byte after the low nibble
 * (the usual two-keystrokes-per-byte hex-editor rhythm). Seeds "Hello,
 * Quill!" (first byte 'H' = 0x48), enters hex mode, types '4' then '1' at
 * cursor 0 to overwrite it with 0x41 ('A'), then flips back to text mode
 * (Home again, since hex mode itself has no save-relevant text path) and
 * saves -- confirming the edit landed in file_buf for real, not just in
 * some hex-only scratch state. */
static void test_quill_fx_hex_nibble_edit(void) {
    printf("Testing apps/fluxio/Quill.fx: hex mode nibble editing writes into the buffer...\n");

    const char* dir = "/tmp/nuxvm_test_quill_fx_hex_edit";
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

    int frames = 0;
    while (!m->cpu->halted && frames < 5) {
        machine_tick(m);
        frames++;
    }
    assert(!m->cpu->halted);

    /* Home -> hex mode, '4' '1' -> byte 0 becomes 0x41 ('A'), Tab -> save,
     * Esc -> quit. No second Home needed: save (Tab) and quit (Esc) both
     * work directly in hex mode already. */
    int keys[] = { 23, '4', '1', 9, 27 };
    for (int k = 0; k < 5; k++) {
        uint8_t kpkt[8] = { 0, 0, (uint8_t) (keys[k] & 0xFF), (uint8_t) ((keys[k] >> 8) & 0xFF), 0, 0, 0, 0 };
        assert(vfs_write(m->system, kc, kpkt, 8) == 8);
        frames = 0;
        while (!m->cpu->halted && frames < 10) {
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

    assert(n == (int) strlen(content)); /* nibble editing overwrites in place, doesn't change length */
    assert(got[0] == 'A'); /* 0x48 ('H') -> 0x41 ('A') via '4','1' */
    assert(memcmp(got + 1, content + 1, strlen(content) - 1) == 0); /* rest of the file untouched */

    remove("quill_scratch.txt");
}

/* v10.2 addition: Up/Down arrows in hex mode move the cursor by a whole
 * row (16 bytes), preserving column -- mirroring how Left/Right already
 * moved by one byte. Seeds 40 bytes and, at each step, edits the byte
 * under the cursor to a distinct marker value *immediately* after each
 * move, rather than only checking a final position -- a test that only
 * checks where a round trip nets out can't tell "Up/Down both moved
 * correctly" apart from "Up/Down are both no-ops", since 3 -> (no-op) ->
 * (no-op) -> 3 looks identical to 3 -> 19 -> 35 -> 19 -> 3 at the end
 * (a real mistake caught while writing this test: an earlier version did
 * exactly that and kept passing even with Up/Down handling deleted
 * entirely). Sequence: right x3 (cursor=3), down (->19, mark 0x55),
 * down (->36, since cursor auto-advanced to 20 after the edit, mark
 * 0x66), up x2 (->21->5, mark 0x77), up again (5-16<0, clamps at 5, mark
 * 0x88 there to confirm the clamp keeps the cursor sane rather than
 * wrapping or going negative). Four distinct offsets, four distinct
 * marker bytes, verified independently. */
static void test_quill_fx_hex_up_down_arrows(void) {
    printf("Testing apps/fluxio/Quill.fx: hex mode Up/Down arrows move by a row, preserving column...\n");

    const char* dir = "/tmp/nuxvm_test_quill_fx_hex_updown";
    char binpath[256];
    Machine* m = quill_fx_machine(dir, binpath, sizeof(binpath));
    if (!m) return;
    machine_free(m);

    remove("quill_scratch.txt");
    FILE* seed = fopen("quill_scratch.txt", "wb");
    assert(seed != NULL);
    const int seed_len = 40;
    for (int i = 0; i < seed_len; i++) {
        uint8_t b = (uint8_t) ('a' + (i % 26));
        assert(fwrite(&b, 1, 1, seed) == 1);
    }
    fclose(seed);

    m = quill_fx_machine(dir, binpath, sizeof(binpath));
    assert(m != NULL);

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

    /* Home, right x3 (cursor=3), down (->19, edit 0x55, cursor auto-
     * advances to 20), down (->36, edit 0x66, cursor -> 37), up x2
     * (->21->5, edit 0x77, cursor -> 6), up (5(sic: cursor is 6 here,
     * 6-16<0) clamps, edit 0x88), Tab save, Esc quit. */
    int keys[] = {
        23, 20, 20, 20,             /* Home, right x3: cursor=3 */
        18, '5', '5',               /* down: cursor=19; edit byte 19 = 0x55; cursor->20 */
        18, '6', '6',               /* down: cursor=36; edit byte 36 = 0x66; cursor->37 */
        17, 17, '7', '7',           /* up,up: cursor=37->21->5; edit byte 5 = 0x77; cursor->6 */
        17, '8', '8',               /* up: cursor 6-16<0, clamps at 6; edit byte 6 = 0x88 */
        9, 27
    };
    int nkeys = (int) (sizeof(keys) / sizeof(keys[0]));
    for (int k = 0; k < nkeys; k++) {
        uint8_t kpkt[8] = { 0, 0, (uint8_t) (keys[k] & 0xFF), (uint8_t) ((keys[k] >> 8) & 0xFF), 0, 0, 0, 0 };
        assert(vfs_write(m->system, kc, kpkt, 8) == 8);
        frames = 0;
        while (!m->cpu->halted && frames < 10) {
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
    uint8_t got[64] = { 0 };
    int n = vfs_read(check, rfd, got, sizeof(got));
    vfs_close(check, rfd);
    system_free(check);

    assert(n == seed_len);
    assert(got[19] == 0x55); /* first Down landed at 3+16, not a no-op leaving it at 3 */
    assert(got[36] == 0x66); /* second Down landed at 20+16 */
    assert(got[5] == 0x77);  /* two Ups landed back at 37-16-16, not stuck at 36 */
    assert(got[6] == 0x88);  /* clamped Up (6-16<0) kept the cursor at 6, didn't wrap/corrupt */
    for (int i = 0; i < n; i++) {
        if (i != 19 && i != 36 && i != 5 && i != 6) {
            assert(got[i] == (uint8_t) ('a' + (i % 26))); /* nothing else touched */
        }
    }

    remove("quill_scratch.txt");
}

/* v10 addition: the View > Toggle Hex menu item is now a check-item
 * (UI::check-item), and toggle_hex_mode() explicitly syncs its checkmark
 * via UI::item-set every time it runs -- not just when the toggle comes
 * from clicking the menu item itself (which UI::menu's own mb-apply would
 * auto-toggle on its own), but also when it comes from the Home-key
 * shortcut, which touches nothing in lib/ui.lux's menu state on its own.
 * Toggles via the Home key (never touching the menu at all), then opens
 * the View dropdown and confirms the checkmark's extra ink is present --
 * proving item-set is actually keeping the checkbox in sync, not just
 * that the auto-toggle-on-click path happens to work. */
static void test_quill_fx_hex_menu_checkbox_syncs_via_home_key(void) {
    printf("Testing apps/fluxio/Quill.fx: View > Toggle Hex checkbox syncs when toggled via Home key...\n");

    const char* dir = "/tmp/nuxvm_test_quill_fx_hex_checkbox";
    char binpath[256];
    Machine* m = quill_fx_machine(dir, binpath, sizeof(binpath));
    if (!m) return;
    machine_free(m);

    remove("quill_scratch.txt");
    FILE* seed = fopen("quill_scratch.txt", "wb");
    assert(seed != NULL);
    const char* content = "Hello\n";
    assert(fwrite(content, 1, strlen(content), seed) == strlen(content));
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

    /* Open View (title at x=120,y=10, same coordinates
     * test_quill_fx_menu_bar_works_in_hex_mode uses), measure the
     * dropdown's first row's ink while hex_mode is still off (unchecked). */
    uint8_t view_down[8] = { 3, 1, 120, 0, 10, 0, 0, 0 };
    uint8_t view_up[8] = { 4, 1, 120, 0, 10, 0, 0, 0 };
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
    int sw = m->system->screen_width;
    uint8_t* fb = m->system->screen_pixels;
    int unchecked_ink = 0;
    for (int y = 20; y < 38; y++) {
        for (int x = 106; x < 128; x++) {
            uint8_t* p = fb + (size_t) y * (size_t) sw * 4 + (size_t) x * 4;
            if (p[1] < 0x80 && p[2] < 0x80 && p[3] < 0x80) {
                unchecked_ink++;
            }
        }
    }

    /* Close the dropdown (click the title again). */
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

    /* Toggle hex mode via the Home key -- never touches the menu. */
    uint8_t home[8] = { 0, 0, 23, 0, 0, 0, 0, 0 };
    assert(vfs_write(m->system, kc, home, 8) == 8);
    frames = 0;
    while (!m->cpu->halted && frames < 3) {
        machine_tick(m);
        frames++;
    }

    /* Reopen View -- the checkbox should now show checked. */
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
    int checked_ink = 0;
    for (int y = 20; y < 38; y++) {
        for (int x = 106; x < 128; x++) {
            uint8_t* p = fb + (size_t) y * (size_t) sw * 4 + (size_t) x * 4;
            if (p[1] < 0x80 && p[2] < 0x80 && p[3] < 0x80) {
                checked_ink++;
            }
        }
    }

    assert(checked_ink > unchecked_ink); /* the checkmark glyph appeared, without ever clicking the menu item */

    vfs_close(m->system, mc);
    vfs_close(m->system, kc);
    machine_free(m);
    remove("quill_scratch.txt");
}

/* Counts blue-ish pixels (channel[3] high, channel[1]/[2] low, matching
 * clr_hex_caret = 0x0000FF's channel layout) in a screen region -- used
 * to confirm the hex-mode caret box actually renders in blue, not the
 * text-mode caret's red. */
static int quill_fx_blue_pixels(Machine* m, int x0, int x1, int y0, int y1) {
    int sw = m->system->screen_width;
    uint8_t* fb = m->system->screen_pixels;
    int count = 0;
    for (int y = y0; y < y1; y++) {
        for (int x = x0; x < x1; x++) {
            uint8_t* p = fb + (size_t) y * (size_t) sw * 4 + (size_t) x * 4;
            if (p[3] > 0xB0 && p[1] < 0x40 && p[2] < 0x40) {
                count++;
            }
        }
    }
    return count;
}

/* v10 follow-up: "make the cursor a hollow blue rectangle for hexmode" --
 * both hex-mode carets (the hex-digit one and its ASCII companion) switched
 * from the text-mode caret's solid 2px red bar (fill_rect) to a hollow
 * outline (draw_rect_outline, four thin fill_rect edges) in clr_hex_caret
 * (0x0000FF), sized to the actual glyph's width instead of a fixed 2px.
 * Confirms both: (a) the color is blue, not red, and (b) the box is
 * actually hollow -- comparing the ink count inside its bounding box
 * against the box's full area, since a solid fill would read close to
 * 100% and an outline reads far lower. */
static void test_quill_fx_hex_caret_is_hollow_blue_box(void) {
    printf("Testing apps/fluxio/Quill.fx: hex mode caret renders as a hollow blue box...\n");

    const char* dir = "/tmp/nuxvm_test_quill_fx_hex_caret_color";
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

    int frames = 0;
    while (!m->cpu->halted && frames < 5) {
        machine_tick(m);
        frames++;
    }
    assert(!m->cpu->halted);

    /* No blue caret should exist in text mode. */
    int text_mode_blue = quill_fx_blue_pixels(m, 0, 500, 40, 60);
    assert(text_mode_blue == 0);

    uint8_t home[8] = { 0, 0, 23, 0, 0, 0, 0, 0 }; /* Home -> hex mode */
    assert(vfs_write(m->system, kc, home, 8) == 8);
    frames = 0;
    while (!m->cpu->halted && frames < 3) {
        machine_tick(m);
        frames++;
    }

    int sw = m->system->screen_width;
    uint8_t* fb = m->system->screen_pixels;
    int minx = 99999, maxx = -1, miny = 99999, maxy = -1;
    for (int y = 40; y < 60; y++) {
        for (int x = 0; x < 300; x++) {
            uint8_t* p = fb + (size_t) y * (size_t) sw * 4 + (size_t) x * 4;
            if (p[3] > 0xB0 && p[1] < 0x40 && p[2] < 0x40) {
                if (x < minx) {
                    minx = x;
                }
                if (x > maxx) {
                    maxx = x;
                }
                if (y < miny) {
                    miny = y;
                }
                if (y > maxy) {
                    maxy = y;
                }
            }
        }
    }
    assert(maxx >= minx); /* a blue box exists at all */
    int box_w = maxx - minx + 1;
    int box_h = maxy - miny + 1;
    int box_area = box_w * box_h;
    int blue_count = quill_fx_blue_pixels(m, minx, maxx + 1, miny, maxy + 1);
    assert(blue_count * 2 < box_area); /* hollow: filled well under half its bounding box, not ~100% like a solid fill */

    vfs_close(m->system, kc);
    machine_free(m);
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

    /* Click "Save" -- row 2 of the dropdown now that "New" (v12) is row 0
     * and "Open..." (v11) is row 1; MENU_H=20 + ITEM_H=18, row 2 center =
     * 20 + 2*18 + 9 = 65. */
    uint8_t save_down[8] = { 3, 1, 20, 0, 65, 0, 0, 0 };
    uint8_t save_up[8] = { 4, 1, 20, 0, 65, 0, 0, 0 };
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

/* v12 addition: the Edit menu (Cut/Copy/Paste/Select All), sharing the
 * /sys/snarf clipboard file apps/Quill.lux uses. "Edit" is now the
 * second menu title (File, Edit, View); File/Edit both clamp to
 * lib/ui.lux's MENU_MIN_TW=48px, so Edit's title sits at x=[58,106) --
 * click x=70 (a title-x reused from before the Edit menu existed, back
 * when it happened to hit "View") lands inside it. Drags a mouse
 * selection over "AB\n" (indices [0,3) of the seeded "AB\nCD\nEF\n",
 * same click geometry test_quill_fx_click_positions_cursor documents:
 * row 0 y=45, row 1 y=65, x=16 is each row's left edge), Copies it via
 * the menu, repositions the cursor to the start of "EF" (row 2, clearing
 * the selection the same way an ordinary click would), then Pastes --
 * proving the whole path (selection geometry -> copy_selection() ->
 * /sys/snarf -> paste_snarf() -> insert_bytes()) rather than just "the
 * menu items fire". */
static void test_quill_fx_edit_copy_paste(void) {
    printf("Testing apps/fluxio/Quill.fx: Edit > Copy then Edit > Paste round-trips a selection through /sys/snarf...\n");

    const char* dir = "/tmp/nuxvm_test_quill_fx_edit_copypaste";
    char binpath[256];
    Machine* m = quill_fx_machine(dir, binpath, sizeof(binpath));
    if (!m) return;

    remove("quill_scratch.txt");
    FILE* seed = fopen("quill_scratch.txt", "wb");
    assert(seed != NULL);
    const char* content = "AB\nCD\nEF\n";
    assert(fwrite(content, 1, strlen(content), seed) == strlen(content));
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

    /* Drag-select "AB\n" (indices [0,3)): mouse down at row 0's left edge
     * (index 0), drag to row 1's left edge (index 3), release. */
    uint8_t sel_down[8] = { 3, 1, 16, 0, 45, 0, 0, 0 };
    uint8_t sel_move[8] = { 2, 0, 16, 0, 65, 0, 0, 0 };
    uint8_t sel_up[8] = { 4, 1, 16, 0, 65, 0, 0, 0 };
    assert(vfs_write(m->system, mc, sel_down, 8) == 8);
    frames = 0;
    while (!m->cpu->halted && frames < 3) {
        machine_tick(m);
        frames++;
    }
    assert(vfs_write(m->system, mc, sel_move, 8) == 8);
    frames = 0;
    while (!m->cpu->halted && frames < 3) {
        machine_tick(m);
        frames++;
    }
    assert(vfs_write(m->system, mc, sel_up, 8) == 8);
    frames = 0;
    while (!m->cpu->halted && frames < 3) {
        machine_tick(m);
        frames++;
    }

    /* Open Edit (x=70, y=10), click Copy (row 1 of Cut/Copy/Paste/Select
     * All: MENU_H=20 + ITEM_H=18 + 9 = 47). */
    uint8_t edit_down[8] = { 3, 1, 70, 0, 10, 0, 0, 0 };
    uint8_t edit_up[8] = { 4, 1, 70, 0, 10, 0, 0, 0 };
    uint8_t copy_down[8] = { 3, 1, 70, 0, 47, 0, 0, 0 };
    uint8_t copy_up[8] = { 4, 1, 70, 0, 47, 0, 0, 0 };
    assert(vfs_write(m->system, mc, edit_down, 8) == 8);
    frames = 0;
    while (!m->cpu->halted && frames < 3) {
        machine_tick(m);
        frames++;
    }
    assert(vfs_write(m->system, mc, edit_up, 8) == 8);
    frames = 0;
    while (!m->cpu->halted && frames < 3) {
        machine_tick(m);
        frames++;
    }
    assert(vfs_write(m->system, mc, copy_down, 8) == 8);
    frames = 0;
    while (!m->cpu->halted && frames < 3) {
        machine_tick(m);
        frames++;
    }
    assert(vfs_write(m->system, mc, copy_up, 8) == 8);
    frames = 0;
    while (!m->cpu->halted && frames < 3) {
        machine_tick(m);
        frames++;
    }

    /* Reposition the cursor to the start of "EF" (row 2, index 6) via an
     * ordinary click -- also clears the selection, same as a real user
     * clicking elsewhere before pasting. */
    uint8_t reposition_down[8] = { 3, 1, 16, 0, 85, 0, 0, 0 };
    uint8_t reposition_up[8] = { 4, 1, 16, 0, 85, 0, 0, 0 };
    assert(vfs_write(m->system, mc, reposition_down, 8) == 8);
    frames = 0;
    while (!m->cpu->halted && frames < 3) {
        machine_tick(m);
        frames++;
    }
    assert(vfs_write(m->system, mc, reposition_up, 8) == 8);
    frames = 0;
    while (!m->cpu->halted && frames < 3) {
        machine_tick(m);
        frames++;
    }

    /* Open Edit again, click Paste (row 2: 20 + 2*18 + 9 = 65). */
    uint8_t paste_down[8] = { 3, 1, 70, 0, 65, 0, 0, 0 };
    uint8_t paste_up[8] = { 4, 1, 70, 0, 65, 0, 0, 0 };
    assert(vfs_write(m->system, mc, edit_down, 8) == 8);
    frames = 0;
    while (!m->cpu->halted && frames < 3) {
        machine_tick(m);
        frames++;
    }
    assert(vfs_write(m->system, mc, edit_up, 8) == 8);
    frames = 0;
    while (!m->cpu->halted && frames < 3) {
        machine_tick(m);
        frames++;
    }
    assert(vfs_write(m->system, mc, paste_down, 8) == 8);
    frames = 0;
    while (!m->cpu->halted && frames < 3) {
        machine_tick(m);
        frames++;
    }
    assert(vfs_write(m->system, mc, paste_up, 8) == 8);
    frames = 0;
    while (!m->cpu->halted && frames < 3) {
        machine_tick(m);
        frames++;
    }

    int keys[] = { 9, 27 }; /* Tab (save), Esc (quit) */
    for (int k = 0; k < 2; k++) {
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
    uint8_t got[32] = { 0 };
    int n = vfs_read(check, rfd, got, sizeof(got));
    vfs_close(check, rfd);
    system_free(check);

    /* "AB\nCD\nEF\n" with "AB\n" pasted back in right before "EF". */
    assert(n == 12);
    assert(memcmp(got, "AB\nCD\nAB\nEF\n", 12) == 0);

    remove("quill_scratch.txt");
}

/* v12 addition: Edit > Select All followed by Edit > Cut -- proves
 * select_all() (no drag needed) and that Cut both writes the clipboard
 * and actually removes the selection, not just one or the other. */
static void test_quill_fx_edit_select_all_and_cut(void) {
    printf("Testing apps/fluxio/Quill.fx: Edit > Select All then Edit > Cut empties the buffer...\n");

    const char* dir = "/tmp/nuxvm_test_quill_fx_edit_cut";
    char binpath[256];
    Machine* m = quill_fx_machine(dir, binpath, sizeof(binpath));
    if (!m) return;

    remove("quill_scratch.txt");
    FILE* seed = fopen("quill_scratch.txt", "wb");
    assert(seed != NULL);
    const char* content = "Hi\n";
    assert(fwrite(content, 1, strlen(content), seed) == strlen(content));
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

    /* Open Edit (x=70, y=10), click Select All (row 3: 20 + 3*18 + 9 = 83). */
    uint8_t edit_down[8] = { 3, 1, 70, 0, 10, 0, 0, 0 };
    uint8_t edit_up[8] = { 4, 1, 70, 0, 10, 0, 0, 0 };
    uint8_t selall_down[8] = { 3, 1, 70, 0, 83, 0, 0, 0 };
    uint8_t selall_up[8] = { 4, 1, 70, 0, 83, 0, 0, 0 };
    assert(vfs_write(m->system, mc, edit_down, 8) == 8);
    frames = 0;
    while (!m->cpu->halted && frames < 3) {
        machine_tick(m);
        frames++;
    }
    assert(vfs_write(m->system, mc, edit_up, 8) == 8);
    frames = 0;
    while (!m->cpu->halted && frames < 3) {
        machine_tick(m);
        frames++;
    }
    assert(vfs_write(m->system, mc, selall_down, 8) == 8);
    frames = 0;
    while (!m->cpu->halted && frames < 3) {
        machine_tick(m);
        frames++;
    }
    assert(vfs_write(m->system, mc, selall_up, 8) == 8);
    frames = 0;
    while (!m->cpu->halted && frames < 3) {
        machine_tick(m);
        frames++;
    }

    /* Reopen Edit, click Cut (row 0: 20 + 9 = 29). */
    uint8_t cut_down[8] = { 3, 1, 70, 0, 29, 0, 0, 0 };
    uint8_t cut_up[8] = { 4, 1, 70, 0, 29, 0, 0, 0 };
    assert(vfs_write(m->system, mc, edit_down, 8) == 8);
    frames = 0;
    while (!m->cpu->halted && frames < 3) {
        machine_tick(m);
        frames++;
    }
    assert(vfs_write(m->system, mc, edit_up, 8) == 8);
    frames = 0;
    while (!m->cpu->halted && frames < 3) {
        machine_tick(m);
        frames++;
    }
    assert(vfs_write(m->system, mc, cut_down, 8) == 8);
    frames = 0;
    while (!m->cpu->halted && frames < 3) {
        machine_tick(m);
        frames++;
    }
    assert(vfs_write(m->system, mc, cut_up, 8) == 8);
    frames = 0;
    while (!m->cpu->halted && frames < 3) {
        machine_tick(m);
        frames++;
    }

    /* /sys/snarf is a per-System in-memory buffer (SnarfFileData,
     * src/vfs.c), not a real host file -- it has to be read back from
     * this same m->system, right now, before machine_free() tears it
     * down; a fresh system_create() afterward would see an empty
     * clipboard regardless of what Cut wrote. Proves Cut actually wrote
     * the clipboard before deleting, not just deleting. */
    int32_t sfd = vfs_open(m->system, "/sys/snarf", 0);
    assert(sfd >= 0);
    uint8_t snarf_got[16] = { 0 };
    int sn = vfs_read(m->system, sfd, snarf_got, sizeof(snarf_got));
    vfs_close(m->system, sfd);
    assert(sn == 3);
    assert(memcmp(snarf_got, "Hi\n", 3) == 0);

    int keys[] = { 9, 27 }; /* Tab (save), Esc (quit) */
    for (int k = 0; k < 2; k++) {
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

    assert(n == 0); /* the whole buffer was selected and cut */

    remove("quill_scratch.txt");
}

/* v12 addition: File > New. Clicking it with a clean buffer (dirty == 0)
 * discards and starts "new.quill" immediately, no dialog -- confirmed by
 * typing a marker into the fresh buffer, saving, and checking it landed
 * in a brand-new "new.quill" file while the original scratch file (never
 * reopened for writing) is untouched. File's dropdown is now New(row 0,
 * y=29)/Open(row 1, y=47)/Save(row 2, y=65)/Quit(row 3, y=83). */
static void test_quill_fx_file_new_without_changes_skips_confirm(void) {
    printf("Testing apps/fluxio/Quill.fx: File > New with a clean buffer starts a fresh document with no confirm dialog...\n");

    const char* dir = "/tmp/nuxvm_test_quill_fx_new_clean";
    char binpath[256];
    Machine* m = quill_fx_machine(dir, binpath, sizeof(binpath));
    if (!m) return;

    remove("quill_scratch.txt");
    remove("new.quill");
    FILE* seed = fopen("quill_scratch.txt", "wb");
    assert(seed != NULL);
    const char* content = "Original\n";
    assert(fwrite(content, 1, strlen(content), seed) == strlen(content));
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

    /* Click File (title), then New (row 0, y=29). Buffer is clean (never
     * edited), so this should reset immediately -- no confirm dialog. */
    uint8_t file_down[8] = { 3, 1, 20, 0, 10, 0, 0, 0 };
    uint8_t file_up[8] = { 4, 1, 20, 0, 10, 0, 0, 0 };
    uint8_t new_down[8] = { 3, 1, 20, 0, 29, 0, 0, 0 };
    uint8_t new_up[8] = { 4, 1, 20, 0, 29, 0, 0, 0 };
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
    assert(vfs_write(m->system, mc, new_down, 8) == 8);
    frames = 0;
    while (!m->cpu->halted && frames < 3) {
        machine_tick(m);
        frames++;
    }
    assert(vfs_write(m->system, mc, new_up, 8) == 8);
    frames = 0;
    while (!m->cpu->halted && frames < 3) {
        machine_tick(m);
        frames++;
    }

    /* No confirm dialog should be up -- a click at its Cancel button
     * position must be a plain miss-click on the (now empty) text pane,
     * not something the dialog intercepts. Cheaper proof: just type and
     * save directly, no dialog-dismissal clicks needed at all. */
    int keys[] = { 'Z', 9, 27 }; /* type 'Z', Tab (save), Esc (quit) */
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

    /* The original scratch file was never reopened for writing -- still
     * exactly what it was seeded with. */
    System* check = system_create();
    assert(check != NULL);
    int32_t rfd = vfs_open(check, "/sys/file/quill_scratch.txt", 0);
    assert(rfd >= 0);
    uint8_t got[32] = { 0 };
    int n = vfs_read(check, rfd, got, sizeof(got));
    vfs_close(check, rfd);
    assert(n == (int) strlen(content));
    assert(memcmp(got, content, n) == 0);

    /* The typed 'Z' landed in a brand-new "new.quill" file instead. */
    int32_t nfd = vfs_open(check, "/sys/file/new.quill", 0);
    assert(nfd >= 0);
    uint8_t ngot[8] = { 0 };
    int nn = vfs_read(check, nfd, ngot, sizeof(ngot));
    vfs_close(check, nfd);
    system_free(check);
    assert(nn == 1);
    assert(ngot[0] == 'Z');

    remove("quill_scratch.txt");
    remove("new.quill");
}

/* v12 addition: File > New with unsaved changes opens the confirm
 * dialog instead of discarding immediately. Covers all three buttons in
 * one machine run (each on its own dirtied buffer) since they're cheap,
 * independent checks against the same setup:
 *   - Save: writes the old content first, then starts the new document.
 *   - Don't Save: starts the new document, old content never written.
 *   - Cancel: closes the dialog, old buffer/content untouched.
 * Dialog geometry (default 640x480 canvas): panel at
 * x=[190,450), y=[155,325); the three stacked buttons share x=[230,410)
 * with centers at y=216 (Save), y=256 (Don't Save), y=296 (Cancel) --
 * see confirm_panel_x/y/confirm_btn_x/confirm_btn_y in Quill.fx. */
static void test_quill_fx_file_new_dirty_confirm_dialog_buttons(void) {
    printf("Testing apps/fluxio/Quill.fx: File > New with unsaved changes prompts, and each dialog button behaves correctly...\n");

    const char* dir = "/tmp/nuxvm_test_quill_fx_new_dirty";
    char binpath[256];

    /* ---- Save ---- */
    {
        Machine* m = quill_fx_machine(dir, binpath, sizeof(binpath));
        if (!m) return;
        remove("quill_scratch.txt");
        remove("new.quill");
        FILE* seed = fopen("quill_scratch.txt", "wb");
        assert(seed != NULL);
        assert(fwrite("Hi\n", 1, 3, seed) == 3);
        fclose(seed);

        m = quill_fx_machine(dir, binpath, sizeof(binpath));
        assert(m != NULL);
        int32_t mc = vfs_open(m->system, "/sys/chan/new", 0);
        int32_t mp = vfs_open(m->system, "/sys/chan/peer", 0);
        assert(vfs_bind(m->system, mp, "/dev/mouse") == 0);
        vfs_close(m->system, mp);
        int32_t kc = vfs_open(m->system, "/sys/chan/new", 0);
        int32_t kp = vfs_open(m->system, "/sys/chan/peer", 0);
        assert(vfs_bind(m->system, kp, "/dev/kbd") == 0);
        vfs_close(m->system, kp);

        int frames = 0;
        while (!m->cpu->halted && frames < 5) { machine_tick(m); frames++; }

        /* Dirty the buffer. */
        uint8_t marker[8] = { 0, 0, '#', 0, 0, 0, 0, 0 };
        assert(vfs_write(m->system, kc, marker, 8) == 8);
        frames = 0;
        while (!m->cpu->halted && frames < 3) { machine_tick(m); frames++; }

        /* Click File > New. */
        uint8_t file_down[8] = { 3, 1, 20, 0, 10, 0, 0, 0 };
        uint8_t file_up[8] = { 4, 1, 20, 0, 10, 0, 0, 0 };
        uint8_t new_down[8] = { 3, 1, 20, 0, 29, 0, 0, 0 };
        uint8_t new_up[8] = { 4, 1, 20, 0, 29, 0, 0, 0 };
        assert(vfs_write(m->system, mc, file_down, 8) == 8);
        frames = 0; while (!m->cpu->halted && frames < 3) { machine_tick(m); frames++; }
        assert(vfs_write(m->system, mc, file_up, 8) == 8);
        frames = 0; while (!m->cpu->halted && frames < 3) { machine_tick(m); frames++; }
        assert(vfs_write(m->system, mc, new_down, 8) == 8);
        frames = 0; while (!m->cpu->halted && frames < 3) { machine_tick(m); frames++; }
        assert(vfs_write(m->system, mc, new_up, 8) == 8);
        frames = 0; while (!m->cpu->halted && frames < 3) { machine_tick(m); frames++; }

        /* Confirm dialog should be visible -- ink present over its panel
         * region (well inside the border, away from any button text). */
        int sw = m->system->screen_width;
        uint8_t* fb = m->system->screen_pixels;
        int panel_ink = 0;
        for (int y = 155; y < 200; y++) {
            for (int x = 190; x < 450; x++) {
                uint8_t* p = fb + (size_t) y * (size_t) sw * 4 + (size_t) x * 4;
                if (p[1] < 0x40 && p[2] < 0x40 && p[3] < 0x40) { /* dark panel fill, not white bg */
                    panel_ink++;
                }
            }
        }
        assert(panel_ink > 0);

        /* Click Save (y=216). */
        uint8_t save_btn_down[8] = { 3, 1, 250, 0, 216, 0, 0, 0 };
        uint8_t save_btn_up[8] = { 4, 1, 250, 0, 216, 0, 0, 0 };
        assert(vfs_write(m->system, mc, save_btn_down, 8) == 8);
        frames = 0; while (!m->cpu->halted && frames < 3) { machine_tick(m); frames++; }
        assert(vfs_write(m->system, mc, save_btn_up, 8) == 8);
        frames = 0; while (!m->cpu->halted && frames < 3) { machine_tick(m); frames++; }

        uint8_t quit[8] = { 0, 0, 27, 0, 0, 0, 0, 0 };
        assert(vfs_write(m->system, kc, quit, 8) == 8);
        frames = 0; while (!m->cpu->halted && frames < 3) { machine_tick(m); frames++; }
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
        /* Save wrote the dirtied old content ("#Hi\n") before resetting. */
        assert(n == 4);
        assert(memcmp(got, "#Hi\n", 4) == 0);

        remove("quill_scratch.txt");
        remove("new.quill");
    }

    /* ---- Don't Save ---- */
    {
        Machine* m = quill_fx_machine(dir, binpath, sizeof(binpath));
        if (!m) return;
        remove("quill_scratch.txt");
        remove("new.quill");
        FILE* seed = fopen("quill_scratch.txt", "wb");
        assert(seed != NULL);
        assert(fwrite("Hi\n", 1, 3, seed) == 3);
        fclose(seed);

        m = quill_fx_machine(dir, binpath, sizeof(binpath));
        assert(m != NULL);
        int32_t mc = vfs_open(m->system, "/sys/chan/new", 0);
        int32_t mp = vfs_open(m->system, "/sys/chan/peer", 0);
        assert(vfs_bind(m->system, mp, "/dev/mouse") == 0);
        vfs_close(m->system, mp);
        int32_t kc = vfs_open(m->system, "/sys/chan/new", 0);
        int32_t kp = vfs_open(m->system, "/sys/chan/peer", 0);
        assert(vfs_bind(m->system, kp, "/dev/kbd") == 0);
        vfs_close(m->system, kp);

        int frames = 0;
        while (!m->cpu->halted && frames < 5) { machine_tick(m); frames++; }

        uint8_t marker[8] = { 0, 0, '#', 0, 0, 0, 0, 0 };
        assert(vfs_write(m->system, kc, marker, 8) == 8);
        frames = 0;
        while (!m->cpu->halted && frames < 3) { machine_tick(m); frames++; }

        uint8_t file_down[8] = { 3, 1, 20, 0, 10, 0, 0, 0 };
        uint8_t file_up[8] = { 4, 1, 20, 0, 10, 0, 0, 0 };
        uint8_t new_down[8] = { 3, 1, 20, 0, 29, 0, 0, 0 };
        uint8_t new_up[8] = { 4, 1, 20, 0, 29, 0, 0, 0 };
        assert(vfs_write(m->system, mc, file_down, 8) == 8);
        frames = 0; while (!m->cpu->halted && frames < 3) { machine_tick(m); frames++; }
        assert(vfs_write(m->system, mc, file_up, 8) == 8);
        frames = 0; while (!m->cpu->halted && frames < 3) { machine_tick(m); frames++; }
        assert(vfs_write(m->system, mc, new_down, 8) == 8);
        frames = 0; while (!m->cpu->halted && frames < 3) { machine_tick(m); frames++; }
        assert(vfs_write(m->system, mc, new_up, 8) == 8);
        frames = 0; while (!m->cpu->halted && frames < 3) { machine_tick(m); frames++; }

        /* Click Don't Save (y=256; split into lo/hi bytes -- mouse_y()
         * reads buf[4] | (buf[5]<<8), src/fluxio_codegen.c, so 256
         * doesn't fit the single low byte other click packets in this
         * file get away with). */
        uint8_t dont_down[8] = { 3, 1, 250, 0, 0, 1, 0, 0 };
        uint8_t dont_up[8] = { 4, 1, 250, 0, 0, 1, 0, 0 };
        assert(vfs_write(m->system, mc, dont_down, 8) == 8);
        frames = 0; while (!m->cpu->halted && frames < 3) { machine_tick(m); frames++; }
        assert(vfs_write(m->system, mc, dont_up, 8) == 8);
        frames = 0; while (!m->cpu->halted && frames < 3) { machine_tick(m); frames++; }

        /* Type into the fresh document and save it -- proves New actually
         * ran (the buffer is really the empty new.quill now), separately
         * from the "old content untouched" check below. */
        int keys[] = { 'Q', 9, 27 };
        for (int k = 0; k < 3; k++) {
            uint8_t kpkt[8] = { 0, 0, (uint8_t) (keys[k] & 0xFF), 0, 0, 0, 0, 0 };
            assert(vfs_write(m->system, kc, kpkt, 8) == 8);
            frames = 0;
            while (!m->cpu->halted && frames < 3) { machine_tick(m); frames++; }
        }
        vfs_close(m->system, mc);
        vfs_close(m->system, kc);
        assert(m->cpu->halted);
        machine_free(m);

        System* check = system_create();
        assert(check != NULL);
        /* Old scratch file was never reopened for writing -- still just
         * the original seed, no '#'. */
        int32_t rfd = vfs_open(check, "/sys/file/quill_scratch.txt", 0);
        assert(rfd >= 0);
        uint8_t got[16] = { 0 };
        int n = vfs_read(check, rfd, got, sizeof(got));
        vfs_close(check, rfd);
        assert(n == 3);
        assert(memcmp(got, "Hi\n", 3) == 0);

        int32_t nfd = vfs_open(check, "/sys/file/new.quill", 0);
        assert(nfd >= 0);
        uint8_t ngot[8] = { 0 };
        int nn = vfs_read(check, nfd, ngot, sizeof(ngot));
        vfs_close(check, nfd);
        system_free(check);
        assert(nn == 1);
        assert(ngot[0] == 'Q');

        remove("quill_scratch.txt");
        remove("new.quill");
    }

    /* ---- Cancel ---- */
    {
        Machine* m = quill_fx_machine(dir, binpath, sizeof(binpath));
        if (!m) return;
        remove("quill_scratch.txt");
        remove("new.quill");
        FILE* seed = fopen("quill_scratch.txt", "wb");
        assert(seed != NULL);
        assert(fwrite("Hi\n", 1, 3, seed) == 3);
        fclose(seed);

        m = quill_fx_machine(dir, binpath, sizeof(binpath));
        assert(m != NULL);
        int32_t mc = vfs_open(m->system, "/sys/chan/new", 0);
        int32_t mp = vfs_open(m->system, "/sys/chan/peer", 0);
        assert(vfs_bind(m->system, mp, "/dev/mouse") == 0);
        vfs_close(m->system, mp);
        int32_t kc = vfs_open(m->system, "/sys/chan/new", 0);
        int32_t kp = vfs_open(m->system, "/sys/chan/peer", 0);
        assert(vfs_bind(m->system, kp, "/dev/kbd") == 0);
        vfs_close(m->system, kp);

        int frames = 0;
        while (!m->cpu->halted && frames < 5) { machine_tick(m); frames++; }

        uint8_t marker[8] = { 0, 0, '#', 0, 0, 0, 0, 0 };
        assert(vfs_write(m->system, kc, marker, 8) == 8);
        frames = 0;
        while (!m->cpu->halted && frames < 3) { machine_tick(m); frames++; }

        uint8_t file_down[8] = { 3, 1, 20, 0, 10, 0, 0, 0 };
        uint8_t file_up[8] = { 4, 1, 20, 0, 10, 0, 0, 0 };
        uint8_t new_down[8] = { 3, 1, 20, 0, 29, 0, 0, 0 };
        uint8_t new_up[8] = { 4, 1, 20, 0, 29, 0, 0, 0 };
        assert(vfs_write(m->system, mc, file_down, 8) == 8);
        frames = 0; while (!m->cpu->halted && frames < 3) { machine_tick(m); frames++; }
        assert(vfs_write(m->system, mc, file_up, 8) == 8);
        frames = 0; while (!m->cpu->halted && frames < 3) { machine_tick(m); frames++; }
        assert(vfs_write(m->system, mc, new_down, 8) == 8);
        frames = 0; while (!m->cpu->halted && frames < 3) { machine_tick(m); frames++; }
        assert(vfs_write(m->system, mc, new_up, 8) == 8);
        frames = 0; while (!m->cpu->halted && frames < 3) { machine_tick(m); frames++; }

        /* Click Cancel (y=296 = 40 + 256, same lo/hi split as Don't Save
         * above). */
        uint8_t cancel_down[8] = { 3, 1, 250, 0, 40, 1, 0, 0 };
        uint8_t cancel_up[8] = { 4, 1, 250, 0, 40, 1, 0, 0 };
        assert(vfs_write(m->system, mc, cancel_down, 8) == 8);
        frames = 0; while (!m->cpu->halted && frames < 3) { machine_tick(m); frames++; }
        assert(vfs_write(m->system, mc, cancel_up, 8) == 8);
        frames = 0; while (!m->cpu->halted && frames < 3) { machine_tick(m); frames++; }

        /* Dialog should be gone -- the panel region now shows plain pane
         * background, not the dark panel fill. */
        int sw = m->system->screen_width;
        uint8_t* fb = m->system->screen_pixels;
        int panel_ink = 0;
        for (int y = 155; y < 200; y++) {
            for (int x = 190; x < 450; x++) {
                uint8_t* p = fb + (size_t) y * (size_t) sw * 4 + (size_t) x * 4;
                if (p[1] < 0x40 && p[2] < 0x40 && p[3] < 0x40) {
                    panel_ink++;
                }
            }
        }
        assert(panel_ink == 0);

        /* Save via Tab -- if Cancel had discarded the buffer, this would
         * write an empty new.quill instead of the still-dirty "#Hi\n". */
        uint8_t save_key[8] = { 0, 0, 9, 0, 0, 0, 0, 0 };
        assert(vfs_write(m->system, kc, save_key, 8) == 8);
        frames = 0;
        while (!m->cpu->halted && frames < 3) { machine_tick(m); frames++; }
        uint8_t quit[8] = { 0, 0, 27, 0, 0, 0, 0, 0 };
        assert(vfs_write(m->system, kc, quit, 8) == 8);
        frames = 0; while (!m->cpu->halted && frames < 3) { machine_tick(m); frames++; }
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
        assert(n == 4);
        assert(memcmp(got, "#Hi\n", 4) == 0);

        remove("quill_scratch.txt");
        remove("new.quill");
    }
}

/* v11 addition: the file picker (File > Open..., lib/sf.lux's SF module).
 * Uses an isolated sandbox directory (system_set_sandbox_root) rather
 * than the real repo root the other Quill.fx tests implicitly run
 * against, so the directory listing the picker shows is exactly two
 * known files in a known sort order (VFS's own directory listing is
 * already alphabetically sorted, src/vfs.c's create_dir_file) --
 * "quill_scratch.txt" (the file Quill starts with) and "second.txt" (the
 * file this test picks), instead of whatever the real working directory
 * happens to contain at test time.
 *
 * Opens the picker, double-clicks "second.txt" (row 1 of the list -- a
 * single click only selects; SF::click-list's own double-click-within-
 * 500ms rule is what actually opens/chooses a file, same as a real Mac
 * file dialog), types a marker character, saves, and quits. If the pick
 * worked, the marker lands in second.txt (not quill_scratch.txt, which
 * must be untouched) -- proving both that SF::show's dialog is actually
 * reachable and clickable (which needed APP::win-set!, since Quill.fx
 * bypasses APP::init/loop entirely and SF::show centers on
 * APP::width/height) and that save_file() now targets the newly picked
 * path. */
static void test_quill_fx_file_picker_opens_and_picks(void) {
    printf("Testing apps/fluxio/Quill.fx: File > Open... picks a different file via the SF picker...\n");

    const char* dir = "/tmp/nuxvm_test_quill_fx_picker";
    char binpath[256];
    Machine* m = quill_fx_machine(dir, binpath, sizeof(binpath));
    if (!m) return;
    machine_free(m);

    const char* sandbox = "/tmp/nuxvm_test_quill_fx_picker_sandbox";
    mkdir(sandbox, 0755); /* ignore EEXIST -- reused/overwritten below */

    char scratch_path[512];
    snprintf(scratch_path, sizeof(scratch_path), "%s/quill_scratch.txt", sandbox);
    FILE* seed1 = fopen(scratch_path, "wb");
    assert(seed1 != NULL);
    const char* content1 = "Hello\n";
    assert(fwrite(content1, 1, strlen(content1), seed1) == strlen(content1));
    fclose(seed1);

    char second_path[512];
    snprintf(second_path, sizeof(second_path), "%s/second.txt", sandbox);
    FILE* seed2 = fopen(second_path, "wb");
    assert(seed2 != NULL);
    const char* content2 = "World\n";
    assert(fwrite(content2, 1, strlen(content2), seed2) == strlen(content2));
    fclose(seed2);

    m = quill_fx_machine(dir, binpath, sizeof(binpath));
    assert(m != NULL);
    system_set_sandbox_root(m->system, sandbox);

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

    /* Click "File" (title), then "Open..." (now File's row 1, since v12's
     * "New" is row 0 ahead of it -- MENU_H=20 + ITEM_H=18, row 1 center =
     * 20 + 18 + 9 = 47). */
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

    uint8_t open_down[8] = { 3, 1, 20, 0, 47, 0, 0, 0 };
    uint8_t open_up[8] = { 4, 1, 20, 0, 47, 0, 0, 0 };
    assert(vfs_write(m->system, mc, open_down, 8) == 8);
    frames = 0;
    while (!m->cpu->halted && frames < 3) {
        machine_tick(m);
        frames++;
    }
    assert(vfs_write(m->system, mc, open_up, 8) == 8);
    frames = 0;
    while (!m->cpu->halted && frames < 10) {
        machine_tick(m);
        frames++;
    }

    /* Double-click "second.txt", the list's row 1 (alphabetically after
     * "quill_scratch.txt", row 0) -- default 640x480 resolution puts the
     * dialog at dlg_x=(640-464)/2=88, dlg_y=(480-268)/2=106; the list
     * starts at lx=dlg_x+16=104, ly=dlg_y+56=162; SF's own ITEM_H=16, so
     * row 1's center y = 162 + 16 + 8 = 186. */
    uint8_t row_down[8] = { 3, 1, 124, 0, 186, 0, 0, 0 };
    uint8_t row_up[8] = { 4, 1, 124, 0, 186, 0, 0, 0 };
    for (int click = 0; click < 2; click++) {
        assert(vfs_write(m->system, mc, row_down, 8) == 8);
        frames = 0;
        while (!m->cpu->halted && frames < 3) {
            machine_tick(m);
            frames++;
        }
        assert(vfs_write(m->system, mc, row_up, 8) == 8);
        frames = 0;
        while (!m->cpu->halted && frames < 3) {
            machine_tick(m);
            frames++;
        }
    }
    /* Let sf_picked()/open_picked_file() settle. */
    frames = 0;
    while (!m->cpu->halted && frames < 10) {
        machine_tick(m);
        frames++;
    }
    vfs_close(m->system, mc);

    /* Marker at cursor 0 (load_file() resets cursor to 0), save, quit. */
    int keys[] = { '#', 9, 27 };
    for (int k = 0; k < 3; k++) {
        uint8_t kpkt[8] = { 0, 0, (uint8_t) (keys[k] & 0xFF), (uint8_t) ((keys[k] >> 8) & 0xFF), 0, 0, 0, 0 };
        assert(vfs_write(m->system, kc, kpkt, 8) == 8);
        frames = 0;
        while (!m->cpu->halted && frames < 10) {
            machine_tick(m);
            frames++;
        }
    }
    vfs_close(m->system, kc);

    assert(m->cpu->halted);
    machine_free(m);

    FILE* rf1 = fopen(scratch_path, "rb");
    assert(rf1 != NULL);
    char got1[32] = { 0 };
    size_t n1 = fread(got1, 1, sizeof(got1), rf1);
    fclose(rf1);
    assert(n1 == strlen(content1));
    assert(memcmp(got1, content1, n1) == 0); /* quill_scratch.txt untouched -- save targeted second.txt, not the original */

    FILE* rf2 = fopen(second_path, "rb");
    assert(rf2 != NULL);
    char got2[32] = { 0 };
    size_t n2 = fread(got2, 1, sizeof(got2), rf2);
    fclose(rf2);
    assert(n2 == strlen(content2) + 1);
    assert(got2[0] == '#'); /* marker landed in the picked file */
    assert(memcmp(got2 + 1, content2, strlen(content2)) == 0);

    remove(scratch_path);
    remove(second_path);
    rmdir(sandbox);
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

    test_escape_menu_esc_toggles_open();
    test_escape_menu_quit_click_sets_flag();
    test_escape_menu_resume_click_closes();
    test_escape_menu_click_outside_buttons_is_noop();
    test_escape_menu_inert_while_closed();
    test_escape_menu_renders_when_open();

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
    test_quill_fx_hex_click_after_scroll();
    test_quill_fx_hex_ascii_column_aligns_on_short_row();
    test_quill_fx_status_line_reflects_dirty_state();
    test_quill_fx_viewport_follows_cursor();
    test_quill_fx_menu_bar_renders();
    test_quill_fx_menu_bar_works_in_hex_mode();
    test_quill_fx_hex_nibble_edit();
    test_quill_fx_hex_up_down_arrows();
    test_quill_fx_hex_menu_checkbox_syncs_via_home_key();
    test_quill_fx_hex_caret_is_hollow_blue_box();
    test_quill_fx_menu_save_via_click();
    test_quill_fx_edit_copy_paste();
    test_quill_fx_edit_select_all_and_cut();
    test_quill_fx_file_new_without_changes_skips_confirm();
    test_quill_fx_file_new_dirty_confirm_dialog_buttons();
    test_quill_fx_file_picker_opens_and_picks();
    test_include_circular_error();
    test_include_missing_file_error();
    test_error_local_struct_decay();
    test_error_assign_whole_struct();
    test_error_unknown_field();
    test_error_struct_naming_and_shape();

    printf("\nAll Fluxio compiler tests passed!\n");
    return 0;
}
