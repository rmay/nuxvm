/* Phase 0.5 / Phase B1 (docs/quill_fluxio.md): a permanent conformance
 * check for the Lux<->Fluxio calling ABI, not a one-off manual smoke test.
 *
 * Both compilers already share the VM's calling convention (stack args,
 * OP_FRAME/OP_UNFRAME, docs/fluxio-language-plan.md), but nothing had
 * actually proven that a *foreign* caller -- hand-built bytecode standing
 * in for what Fluxio's codegen will eventually emit for an `extern` call
 * (Phase B5) -- can correctly invoke a real `luxc`-compiled word with
 * matching argument push order and get its return value back. This file
 * proves exactly that, plus the architectural assumption Phase B4's
 * "dual-image loader" originally assumed needed a VM change: it doesn't.
 *
 * The VM restricts PC execution to a single contiguous
 * [image_base, image_end) range (src/vm.c: vm_create sets image_end =
 * base_address + program_size; OP_* dispatch faults with "execute outside
 * program" otherwise, src/vm.c:266-269). So a Lux library loaded at a
 * different base address than an app's own code cannot be a *second*
 * image -- it has to be part of the *same* one contiguous blob passed to
 * vm_create. Concretely: fluxlink's job is to produce ONE merged binary
 * (app bytes, zero-padded gap, linked-library bytes) that `cloister`/`nux`
 * load exactly as they already load any single .bin today -- not a new
 * loader code path. test_dual_image_single_blob() below proves that
 * merged-single-image execution actually works before Phase B3/B4 commit
 * to building fluxlink around it.
 */

#include "compiler.h"
#include "vm.h"
#include "opcodes.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

static int tests_run = 0;
static int tests_failed = 0;

#define CHECK(cond, msg) do { \
    tests_run++; \
    if (!(cond)) { \
        tests_failed++; \
        fprintf(stderr, "  FAIL: %s (%s:%d)\n", msg, __FILE__, __LINE__); \
    } \
} while (0)

/* Compiles `source` as a Lux library targeting `base`, returning the raw
 * bytecode (caller must free) and its length. Mirrors what `luxc -base`
 * (Phase 0 deliverable 5) does from the command line. */
static uint8_t* compile_lux_lib(const char* source, int32_t base, size_t* out_len) {
    uint8_t* bytecode = compile_source(source, base, out_len, false);
    if (!bytecode) {
        fprintf(stderr, "  (helper) failed to compile Lux library snippet: %s\n", source);
    }
    return bytecode;
}

/* Runs `caller` bytecode (loaded at `caller_base`) merged into one VM
 * image with `lib` bytecode (loaded at `lib_base`), gap zero-filled, and
 * returns the top of the data stack after HALT. Fails the calling test via
 * CHECK() if the VM faults or the stack is empty. */
static bool run_merged(const uint8_t* caller, size_t caller_len, int32_t caller_base,
                        const uint8_t* lib, size_t lib_len, int32_t lib_base,
                        int32_t* out_result) {
    assert(lib_base >= caller_base);
    size_t total_len = (size_t)(lib_base - caller_base) + lib_len;
    uint8_t* merged = calloc(1, total_len);
    memcpy(merged, caller, caller_len);
    memcpy(merged + (lib_base - caller_base), lib, lib_len);

    VM* vm = vm_create(merged, (uint32_t)total_len, (uint32_t)caller_base, 4 * 1024 * 1024, false);
    vm_run(vm);
    bool ok = vm->halted;
    int32_t result = 0;
    bool popped = vm_pop(vm, &result);
    if (out_result) *out_result = result;
    vm_free(vm);
    free(merged);
    return ok && popped;
}

/* --- Test 1: 0-arg call, return value only ---
 * Lux: @answer ( -- n ) 42 ;
 * Caller (hand-built, standing in for Fluxio's extern-call codegen):
 *   CALL <answer's body address>; HALT
 * Proves: a bare CALL into a Lux word's body address (no args) round-trips
 * a return value through OP_RET the same way Fluxio's own CALL codegen
 * would expect. */
static void test_zero_arg_call(void) {
    printf("Testing ABI: 0-arg call, return value...\n");
    int32_t lib_base = 0x700000;
    size_t lib_len;
    uint8_t* lib = compile_lux_lib("@answer ( -- n ) 42 ;\n", lib_base, &lib_len);
    CHECK(lib != NULL, "compiled the 0-arg Lux library");
    if (!lib) return;

    /* `@answer` is the only word in the file, so luxc emits a leading
     * 5-byte JMP (to a trailing HALT for standalone execution) followed
     * immediately by the word's own body -- see docs/quill_fluxio.md
     * Phase B1's derivation. Body starts right after the JMP. */
    int32_t answer_body = lib_base + 5;

    int32_t caller_base = 0x600000;
    uint8_t caller[6];
    caller[0] = OP_CALL;
    caller[1] = (uint8_t)((answer_body >> 24) & 0xFF);
    caller[2] = (uint8_t)((answer_body >> 16) & 0xFF);
    caller[3] = (uint8_t)((answer_body >> 8) & 0xFF);
    caller[4] = (uint8_t)(answer_body & 0xFF);
    caller[5] = OP_HALT;

    int32_t result = -1;
    bool ok = run_merged(caller, sizeof(caller), caller_base, lib, lib_len, lib_base, &result);
    CHECK(ok, "merged program ran to HALT without faulting");
    CHECK(result == 42, "answer() returned 42");
    free(lib);
}

/* --- Test 2: multi-arg call, verifies PUSH ORDER compatibility ---
 * Lux: @sub ( a b -- a-b ) - ;   (order-sensitive on purpose: a-b, not
 * commutative, so a push-order bug shows up as a wrong answer, not a
 * coincidentally-right one the way ADD would let slip through)
 * Caller: PUSH 10; PUSH 3; CALL <sub's body>; HALT
 * A Forth `( a b -- )` signature means: a was pushed first (deeper),
 * b was pushed last (top of stack) -- OP_FRAME's own doc comment
 * (src/vm.c:526) confirms "v_0 (top of stack) -> local 0", so local 0
 * inside sub is `b`, local 1 is `a`. This test proves Fluxio-style
 * left-to-right argument pushing (`sub(10, 3)` -> PUSH 10; PUSH 3) lines
 * up with that convention without any translation layer. */
static void test_multi_arg_call_push_order(void) {
    printf("Testing ABI: multi-arg call, push-order compatibility...\n");
    int32_t lib_base = 0x700000;
    size_t lib_len;
    uint8_t* lib = compile_lux_lib("@sub ( a b -- a-b ) - ;\n", lib_base, &lib_len);
    CHECK(lib != NULL, "compiled the 2-arg Lux library");
    if (!lib) return;

    int32_t sub_body = lib_base + 5;

    int32_t caller_base = 0x600000;
    uint8_t caller[16];
    int off = 0;
    caller[off++] = OP_PUSH;
    caller[off++] = 0; caller[off++] = 0; caller[off++] = 0; caller[off++] = 10; /* a = 10 */
    caller[off++] = OP_PUSH;
    caller[off++] = 0; caller[off++] = 0; caller[off++] = 0; caller[off++] = 3;  /* b = 3 */
    caller[off++] = OP_CALL;
    caller[off++] = (uint8_t)((sub_body >> 24) & 0xFF);
    caller[off++] = (uint8_t)((sub_body >> 16) & 0xFF);
    caller[off++] = (uint8_t)((sub_body >> 8) & 0xFF);
    caller[off++] = (uint8_t)(sub_body & 0xFF);
    caller[off++] = OP_HALT;

    int32_t result = -999;
    bool ok = run_merged(caller, (size_t)off, caller_base, lib, lib_len, lib_base, &result);
    CHECK(ok, "merged program ran to HALT without faulting");
    CHECK(result == 7, "sub(10, 3) == 7 -- push order matches Lux's Forth convention (a b -- a-b)");
    free(lib);
}

/* --- Test 3: single merged image spans a large address gap ---
 * Proves the Phase B4 assumption directly: one vm_create() image_base/
 * image_end window can cover both a "caller" region and a "library"
 * region separated by a large gap (here: the real ABI library-link band
 * distance, MM_GRAPHICAL_CODE_BASE to MM_ABI_LIBRARY_LINK_BASE, both from
 * include/memory_map.h), as long as they're one contiguous blob -- no VM
 * loader change needed, just what fluxlink's output format has to be. */
static void test_dual_image_single_blob(void) {
    printf("Testing ABI: single image spans the real code<->library-link gap...\n");
    int32_t lib_base = MM_ABI_LIBRARY_LINK_BASE;
    size_t lib_len;
    uint8_t* lib = compile_lux_lib("@triple ( n -- 3n ) 3 * ;\n", lib_base, &lib_len);
    CHECK(lib != NULL, "compiled the library at MM_ABI_LIBRARY_LINK_BASE");
    if (!lib) return;

    int32_t triple_body = lib_base + 5;
    int32_t caller_base = MM_GRAPHICAL_CODE_BASE;
    uint8_t caller[11];
    int off = 0;
    caller[off++] = OP_PUSH;
    caller[off++] = 0; caller[off++] = 0; caller[off++] = 0; caller[off++] = 14;
    caller[off++] = OP_CALL;
    caller[off++] = (uint8_t)((triple_body >> 24) & 0xFF);
    caller[off++] = (uint8_t)((triple_body >> 16) & 0xFF);
    caller[off++] = (uint8_t)((triple_body >> 8) & 0xFF);
    caller[off++] = (uint8_t)(triple_body & 0xFF);
    caller[off++] = OP_HALT;

    int32_t result = -1;
    bool ok = run_merged(caller, (size_t)off, caller_base, lib, lib_len, lib_base, &result);
    CHECK(ok, "merged program spanning the real code<->library-link gap ran without faulting");
    CHECK(result == 42, "triple(14) == 42 across the full-size gap");
    free(lib);
}

/* --- Test 4: fluxlink end-to-end (Phase B3) ---
 * Exercises the real bin/luxc + bin/fluxlink CLI pipeline (not just the
 * in-process compiler calls the other tests use), the way a real build
 * would: compile a library with -base + -symbols, curate an exports list,
 * link it against a hand-built "app" that calls through the resulting
 * trampoline slots, and run the merged output. Also proves the Phase 0.5
 * append-only enforcement actually rejects a reordered export list, not
 * just that fluxlink's happy path works. Requires bin/luxc and
 * bin/fluxlink to already be built (true under `make test`, which
 * depends on the whole TARGETS list). Skips gracefully if run standalone
 * before those exist, rather than failing confusingly. */
static void test_fluxlink_end_to_end(void) {
    printf("Testing ABI: fluxlink end-to-end (real luxc + fluxlink CLI)...\n");

    FILE* probe = fopen("./bin/luxc", "rb");
    if (!probe) {
        printf("  (skipped: ./bin/luxc not built yet -- run from repo root after `make`)\n");
        return;
    }
    fclose(probe);

    const char* dir = "/tmp/nuxvm_test_fluxlink_e2e";
    char cmd[1024];
    snprintf(cmd, sizeof(cmd), "rm -rf %s && mkdir -p %s", dir, dir);
    CHECK(system(cmd) == 0, "created scratch dir");

    FILE* lib_src = fopen("/tmp/nuxvm_test_fluxlink_e2e/lib.lux", "w");
    CHECK(lib_src != NULL, "opened scratch lib.lux for writing");
    if (!lib_src) return;
    fprintf(lib_src, "MODULE E2E\n@double ( n -- 2n ) 2 * ;\n@triple ( n -- 3n ) 3 * ;\n");
    fclose(lib_src);

    snprintf(cmd, sizeof(cmd),
        "./bin/luxc -base 0x701000 -symbols %s/lib.symtab.json -o %s/lib.bin %s/lib.lux >/tmp/nuxvm_test_fluxlink_e2e/luxc.log 2>&1",
        dir, dir, dir);
    CHECK(system(cmd) == 0, "compiled the scratch library with luxc -base -symbols");

    FILE* exports_v1 = fopen("/tmp/nuxvm_test_fluxlink_e2e/exports_v1.json", "w");
    fprintf(exports_v1, "[\"E2E::double\"]\n");
    fclose(exports_v1);
    FILE* exports_v2 = fopen("/tmp/nuxvm_test_fluxlink_e2e/exports_v2.json", "w");
    fprintf(exports_v2, "[\"E2E::double\", \"E2E::triple\"]\n");
    fclose(exports_v2);
    FILE* exports_bad = fopen("/tmp/nuxvm_test_fluxlink_e2e/exports_reordered.json", "w");
    fprintf(exports_bad, "[\"E2E::triple\", \"E2E::double\"]\n");
    fclose(exports_bad);

    /* Hand-built "app": PUSH 21; CALL trampoline_slot0 (double); CALL
     * trampoline_slot1 (triple); HALT -- slot addresses are fixed by the
     * trampoline layout (12-byte header + 5 bytes/slot), independent of
     * where the library's own code ends up. */
    int32_t lib_link_base = MM_ABI_LIBRARY_LINK_BASE;
    int32_t slot0 = lib_link_base + 12;
    int32_t slot1 = lib_link_base + 17;
    uint8_t app[16];
    int off = 0;
    app[off++] = OP_PUSH;
    app[off++] = 0; app[off++] = 0; app[off++] = 0; app[off++] = 21;
    app[off++] = OP_CALL;
    app[off++] = (uint8_t)((slot0 >> 24) & 0xFF); app[off++] = (uint8_t)((slot0 >> 16) & 0xFF);
    app[off++] = (uint8_t)((slot0 >> 8) & 0xFF);  app[off++] = (uint8_t)(slot0 & 0xFF);
    app[off++] = OP_CALL;
    app[off++] = (uint8_t)((slot1 >> 24) & 0xFF); app[off++] = (uint8_t)((slot1 >> 16) & 0xFF);
    app[off++] = (uint8_t)((slot1 >> 8) & 0xFF);  app[off++] = (uint8_t)(slot1 & 0xFF);
    app[off++] = OP_HALT;
    FILE* app_f = fopen("/tmp/nuxvm_test_fluxlink_e2e/app.bin", "wb");
    CHECK(app_f != NULL, "opened scratch app.bin for writing");
    if (app_f) { fwrite(app, 1, (size_t)off, app_f); fclose(app_f); }

    snprintf(cmd, sizeof(cmd),
        "./bin/fluxlink --lib %s/lib.bin --symtab %s/lib.symtab.json --exports %s/exports_v2.json "
        "--app %s/app.bin --app-base 0x%X --lib-base 0x%X -o %s/merged.bin >%s/fluxlink.log 2>&1",
        dir, dir, dir, dir, (unsigned)MM_GRAPHICAL_CODE_BASE, (unsigned)lib_link_base, dir, dir);
    CHECK(system(cmd) == 0, "fluxlink linked the merged image");

    size_t merged_len;
    FILE* mf = fopen("/tmp/nuxvm_test_fluxlink_e2e/merged.bin", "rb");
    CHECK(mf != NULL, "merged.bin was produced");
    if (mf) {
        fseek(mf, 0, SEEK_END);
        merged_len = (size_t)ftell(mf);
        fseek(mf, 0, SEEK_SET);
        uint8_t* merged = malloc(merged_len);
        size_t rd = fread(merged, 1, merged_len, mf);
        fclose(mf);
        CHECK(rd == merged_len, "read the merged image back");

        VM* vm = vm_create(merged, (uint32_t)merged_len, (uint32_t)MM_GRAPHICAL_CODE_BASE, 4 * 1024 * 1024, false);
        vm_run(vm);
        int32_t result = -1;
        bool popped = vm_pop(vm, &result);
        CHECK(vm->halted && popped, "merged image ran to HALT via bin/nux-equivalent load path");
        CHECK(result == 126, "double(21) then triple(...) through real trampoline slots == 126");
        vm_free(vm);
        free(merged);
    }

    /* Append-only enforcement: a reordered export list must be rejected. */
    snprintf(cmd, sizeof(cmd),
        "./bin/fluxlink --lib %s/lib.bin --symtab %s/lib.symtab.json --exports %s/exports_reordered.json "
        "--check-append-only %s/exports_v1.json "
        "--app %s/app.bin --app-base 0x%X --lib-base 0x%X -o %s/merged_bad.bin >/dev/null 2>&1",
        dir, dir, dir, dir, dir, (unsigned)MM_GRAPHICAL_CODE_BASE, (unsigned)lib_link_base, dir);
    int reorder_exit = system(cmd);
    CHECK(reorder_exit != 0, "fluxlink rejects a reordered/renamed export slot (Phase 0.5 append-only policy)");

    /* And a genuine append (v1 -> v2) must be accepted. */
    snprintf(cmd, sizeof(cmd),
        "./bin/fluxlink --lib %s/lib.bin --symtab %s/lib.symtab.json --exports %s/exports_v2.json "
        "--check-append-only %s/exports_v1.json "
        "--app %s/app.bin --app-base 0x%X --lib-base 0x%X -o %s/merged_ok.bin >/dev/null 2>&1",
        dir, dir, dir, dir, dir, (unsigned)MM_GRAPHICAL_CODE_BASE, (unsigned)lib_link_base, dir);
    CHECK(system(cmd) == 0, "fluxlink accepts a genuine append to the export list");
}

/* --- Test 5: Fluxio `extern` end-to-end (Phase B5) ---
 * The realistic pipeline, not a stand-in: real Fluxio source using
 * `extern` declarations, compiled by the real fluxioc, linked by fluxlink
 * against a real luxc-compiled library, run. Multi-arg + chained calls
 * (double -> triple -> add2) to also re-confirm push-order correctness
 * through the actual extern codegen path (not just the hand-built bytecode
 * test_multi_arg_call_push_order uses). */
static void test_fluxio_extern_end_to_end(void) {
    printf("Testing ABI: Fluxio extern end-to-end (real fluxioc + luxc + fluxlink)...\n");

    FILE* probe = fopen("./bin/fluxioc", "rb");
    if (!probe) {
        printf("  (skipped: ./bin/fluxioc not built yet -- run from repo root after `make`)\n");
        return;
    }
    fclose(probe);

    const char* dir = "/tmp/nuxvm_test_extern_e2e";
    char cmd[1024];
    snprintf(cmd, sizeof(cmd), "rm -rf %s && mkdir -p %s", dir, dir);
    CHECK(system(cmd) == 0, "created scratch dir");

    FILE* lib_src = fopen("/tmp/nuxvm_test_extern_e2e/lib.lux", "w");
    CHECK(lib_src != NULL, "opened scratch lib.lux for writing");
    if (!lib_src) return;
    fprintf(lib_src, "MODULE MATHLIB\n@double ( n -- 2n ) 2 * ;\n@triple ( n -- 3n ) 3 * ;\n@add2 ( a b -- sum ) + ;\n");
    fclose(lib_src);

    int32_t lib_link_base = MM_ABI_LIBRARY_LINK_BASE;
    snprintf(cmd, sizeof(cmd),
        "./bin/luxc -base 0x%X -symbols %s/lib.symtab.json -o %s/lib.bin %s/lib.lux >%s/luxc.log 2>&1",
        (unsigned)(lib_link_base + MM_ABI_TRAMPOLINE_RESERVE), dir, dir, dir, dir);
    CHECK(system(cmd) == 0, "compiled the scratch Lux library");

    FILE* exports = fopen("/tmp/nuxvm_test_extern_e2e/lib.exports.json", "w");
    fprintf(exports, "[\"MATHLIB::double\", \"MATHLIB::triple\", \"MATHLIB::add2\"]\n");
    fclose(exports);

    /* Trampoline slot addresses are fully determined by declaration order
     * in the exports file: header (12 bytes) + 5 bytes per preceding slot. */
    int32_t slot_double = lib_link_base + 12;
    int32_t slot_triple = lib_link_base + 17;
    int32_t slot_add2 = lib_link_base + 22;

    FILE* app_src = fopen("/tmp/nuxvm_test_extern_e2e/app.fx", "w");
    CHECK(app_src != NULL, "opened scratch app.fx for writing");
    if (!app_src) return;
    fprintf(app_src,
        "version 400000;\n\n"
        "extern int lib_double(int n) = 0x%X;\n"
        "extern int lib_triple(int n) = 0x%X;\n"
        "extern int lib_add2(int a, int b) = 0x%X;\n\n"
        "/** Entry point. */\n"
        "int main() {\n"
        "    int x = lib_double(21);\n"
        "    int y = lib_triple(x);\n"
        "    return lib_add2(y, 4);\n"
        "}\n",
        (unsigned)slot_double, (unsigned)slot_triple, (unsigned)slot_add2);
    fclose(app_src);

    snprintf(cmd, sizeof(cmd),
        "./bin/fluxioc -target graphical -o %s/app.bin %s/app.fx >%s/fluxioc.log 2>&1", dir, dir, dir);
    CHECK(system(cmd) == 0, "compiled the scratch Fluxio app with extern declarations");

    snprintf(cmd, sizeof(cmd),
        "./bin/fluxlink --lib %s/lib.bin --symtab %s/lib.symtab.json --exports %s/lib.exports.json "
        "--app %s/app.bin --app-base 0x%X --lib-base 0x%X -o %s/merged.bin >%s/fluxlink.log 2>&1",
        dir, dir, dir, dir, (unsigned)MM_GRAPHICAL_CODE_BASE, (unsigned)lib_link_base, dir, dir);
    CHECK(system(cmd) == 0, "fluxlink linked the Fluxio app against the Lux library");

    FILE* mf = fopen("/tmp/nuxvm_test_extern_e2e/merged.bin", "rb");
    CHECK(mf != NULL, "merged.bin was produced");
    if (mf) {
        fseek(mf, 0, SEEK_END);
        size_t merged_len = (size_t)ftell(mf);
        fseek(mf, 0, SEEK_SET);
        uint8_t* merged = malloc(merged_len);
        size_t rd = fread(merged, 1, merged_len, mf);
        fclose(mf);
        CHECK(rd == merged_len, "read the merged image back");

        VM* vm = vm_create(merged, (uint32_t)merged_len, (uint32_t)MM_GRAPHICAL_CODE_BASE, 4 * 1024 * 1024, false);
        vm_run(vm);
        int32_t result = -1;
        bool popped = vm_pop(vm, &result);
        CHECK(vm->halted && popped, "Fluxio app calling through extern ran to HALT");
        CHECK(result == 130, "lib_add2(lib_triple(lib_double(21)), 4) == 130 via real extern codegen");
        vm_free(vm);
        free(merged);
    }
}

/* --- Test 6: `extern void` end-to-end (Phase B6/B7) ---
 * Most of lib/ui.lux and lib/sf.lux's exported words are `( ... -- )` --
 * they push nothing back. FX_EXPR_STMT unconditionally emits OP_POP after
 * an ordinary call, so calling one of these through a plain `extern int`
 * would pop whatever the *next* instruction produces instead -- silent
 * stack corruption, not a crash. `extern void` (added alongside this test)
 * skips that POP. Two consecutive void calls followed by a value-returning
 * call proves the stack stays balanced across all three, not just one. */
static void test_fluxio_extern_void_end_to_end(void) {
    printf("Testing ABI: Fluxio 'extern void' end-to-end (no spurious stack pop)...\n");

    FILE* probe = fopen("./bin/fluxioc", "rb");
    if (!probe) {
        printf("  (skipped: ./bin/fluxioc not built yet -- run from repo root after `make`)\n");
        return;
    }
    fclose(probe);

    const char* dir = "/tmp/nuxvm_test_extern_void_e2e";
    char cmd[1024];
    snprintf(cmd, sizeof(cmd), "rm -rf %s && mkdir -p %s", dir, dir);
    CHECK(system(cmd) == 0, "created scratch dir");

    FILE* lib_src = fopen("/tmp/nuxvm_test_extern_void_e2e/lib.lux", "w");
    CHECK(lib_src != NULL, "opened scratch lib.lux for writing");
    if (!lib_src) return;
    fprintf(lib_src,
        "MODULE VLIB\n@STASH 0x800200 ;\n"
        "@stash ( v -- ) STASH STOREI ;\n@peek ( -- v ) STASH LOADI ;\n");
    fclose(lib_src);

    int32_t lib_link_base = MM_ABI_LIBRARY_LINK_BASE;
    snprintf(cmd, sizeof(cmd),
        "./bin/luxc -base 0x%X -symbols %s/lib.symtab.json -o %s/lib.bin %s/lib.lux >%s/luxc.log 2>&1",
        (unsigned)(lib_link_base + MM_ABI_TRAMPOLINE_RESERVE), dir, dir, dir, dir);
    CHECK(system(cmd) == 0, "compiled the scratch Lux library");

    FILE* exports = fopen("/tmp/nuxvm_test_extern_void_e2e/lib.exports.json", "w");
    fprintf(exports, "[\"VLIB::stash\", \"VLIB::peek\"]\n");
    fclose(exports);

    int32_t slot_stash = lib_link_base + 12;
    int32_t slot_peek = lib_link_base + 17;

    FILE* app_src = fopen("/tmp/nuxvm_test_extern_void_e2e/app.fx", "w");
    CHECK(app_src != NULL, "opened scratch app.fx for writing");
    if (!app_src) return;
    fprintf(app_src,
        "version 400000;\n\n"
        "extern void v_stash(int x) = 0x%X;\n"
        "extern int v_peek() = 0x%X;\n\n"
        "/** Entry point. */\n"
        "int main() {\n"
        "    v_stash(11);\n"
        "    v_stash(22);\n"
        "    return v_peek();\n"
        "}\n",
        (unsigned)slot_stash, (unsigned)slot_peek);
    fclose(app_src);

    snprintf(cmd, sizeof(cmd),
        "./bin/fluxioc -target graphical -o %s/app.bin %s/app.fx >%s/fluxioc.log 2>&1", dir, dir, dir);
    CHECK(system(cmd) == 0, "compiled the scratch Fluxio app with an extern void declaration");

    snprintf(cmd, sizeof(cmd),
        "./bin/fluxlink --lib %s/lib.bin --symtab %s/lib.symtab.json --exports %s/lib.exports.json "
        "--app %s/app.bin --app-base 0x%X --lib-base 0x%X -o %s/merged.bin >%s/fluxlink.log 2>&1",
        dir, dir, dir, dir, (unsigned)MM_GRAPHICAL_CODE_BASE, (unsigned)lib_link_base, dir, dir);
    CHECK(system(cmd) == 0, "fluxlink linked the Fluxio app against the Lux library");

    FILE* mf = fopen("/tmp/nuxvm_test_extern_void_e2e/merged.bin", "rb");
    CHECK(mf != NULL, "merged.bin was produced");
    if (mf) {
        fseek(mf, 0, SEEK_END);
        size_t merged_len = (size_t)ftell(mf);
        fseek(mf, 0, SEEK_SET);
        uint8_t* merged = malloc(merged_len);
        size_t rd = fread(merged, 1, merged_len, mf);
        fclose(mf);
        CHECK(rd == merged_len, "read the merged image back");

        VM* vm = vm_create(merged, (uint32_t)merged_len, (uint32_t)MM_GRAPHICAL_CODE_BASE, 16 * 1024 * 1024, false);
        vm_run(vm);
        int32_t result = -1;
        bool popped = vm_pop(vm, &result);
        CHECK(vm->halted && popped, "app calling two void externs then a value-returning extern ran to HALT");
        CHECK(result == 22, "v_peek() == 22 -- both void calls executed with no stack corruption between them");
        vm_free(vm);
        free(merged);
    }

    /* Using a void extern's result as a value must be rejected at compile
     * time, not silently read garbage. */
    FILE* bad_src = fopen("/tmp/nuxvm_test_extern_void_e2e/bad.fx", "w");
    CHECK(bad_src != NULL, "opened scratch bad.fx for writing");
    if (bad_src) {
        fprintf(bad_src,
            "version 400000;\n\n"
            "extern void v_stash(int x) = 0x%X;\n\n"
            "/** Entry point. */\n"
            "int main() {\n"
            "    int y = v_stash(5);\n"
            "    return y;\n"
            "}\n",
            (unsigned)slot_stash);
        fclose(bad_src);
        snprintf(cmd, sizeof(cmd),
            "./bin/fluxioc -target graphical -o %s/bad.bin %s/bad.fx >%s/fluxioc_bad.log 2>&1", dir, dir, dir);
        int rc = system(cmd);
        CHECK(rc != 0, "fluxioc rejects using an 'extern void' call as a value");
    }
}

/* --- Test 7: lib/ui.lux + lib/sf.lux link against a real Fluxio app
 * (Phase B7) --- Compiles the actual UI/SF library (not a scratch stand-in)
 * at MM_ABI_LIBRARY_CODE_BASE, links it via fluxlink against the real,
 * committed abi/uisf.exports.json (the append-only contract Quill.fx's
 * `extern` declarations will target in Phase C), and calls two of the
 * exported words through a placeholder Fluxio app: UI::new (void, slot 0)
 * to initialize UI's internal state, then UI::menu-open? (int, slot 15) to
 * confirm the init actually ran (MB_ACTIVE == -1 right after `new`, so
 * menu-open? must read back 0). Slot indices are fixed by
 * abi/uisf.exports.json's current order -- append-only, so once assigned
 * they don't move. */
static void test_uisf_library_link(void) {
    printf("Testing ABI: lib/ui.lux + lib/sf.lux link against a real Fluxio app...\n");

    FILE* probe = fopen("./bin/fluxioc", "rb");
    if (!probe) {
        printf("  (skipped: ./bin/fluxioc not built yet -- run from repo root after `make`)\n");
        return;
    }
    fclose(probe);
    probe = fopen("lib/sf.lux", "rb");
    if (!probe) {
        printf("  (skipped: lib/sf.lux not found -- run from repo root)\n");
        return;
    }
    fclose(probe);

    const char* dir = "/tmp/nuxvm_test_uisf_link";
    char cmd[1024];
    snprintf(cmd, sizeof(cmd), "rm -rf %s && mkdir -p %s", dir, dir);
    CHECK(system(cmd) == 0, "created scratch dir");

    int32_t lib_link_base = MM_ABI_LIBRARY_LINK_BASE;
    snprintf(cmd, sizeof(cmd),
        "./bin/luxc -base 0x%X -symbols %s/uisf.symtab.json -o %s/uisf.bin lib/sf.lux >%s/luxc.log 2>&1",
        (unsigned)(lib_link_base + MM_ABI_TRAMPOLINE_RESERVE), dir, dir, dir);
    CHECK(system(cmd) == 0, "compiled lib/sf.lux (+ lib/ui.lux) as a library");

    /* abi/uisf.exports.json order: 0 = UI::new, 15 = UI::menu-open?. */
    int32_t slot_new = lib_link_base + 12 + 5 * 0;
    int32_t slot_menu_open = lib_link_base + 12 + 5 * 15;

    FILE* app_src = fopen("/tmp/nuxvm_test_uisf_link/app.fx", "w");
    CHECK(app_src != NULL, "opened scratch app.fx for writing");
    if (!app_src) return;
    fprintf(app_src,
        "version 400000;\n\n"
        "extern void ui_new() = 0x%X;\n"
        "extern int ui_menu_open() = 0x%X;\n\n"
        "/** Entry point. */\n"
        "int main() {\n"
        "    ui_new();\n"
        "    return ui_menu_open();\n"
        "}\n",
        (unsigned)slot_new, (unsigned)slot_menu_open);
    fclose(app_src);

    snprintf(cmd, sizeof(cmd),
        "./bin/fluxioc -target graphical -o %s/app.bin %s/app.fx >%s/fluxioc.log 2>&1", dir, dir, dir);
    int fluxioc_rc = system(cmd);
    if (fluxioc_rc != 0) {
        snprintf(cmd, sizeof(cmd), "cat %s/fluxioc.log 1>&2", dir);
        system(cmd);
    }
    CHECK(fluxioc_rc == 0, "compiled the placeholder Fluxio app with extern declarations for the linked library");

    snprintf(cmd, sizeof(cmd),
        "./bin/fluxlink --lib %s/uisf.bin --symtab %s/uisf.symtab.json --exports abi/uisf.exports.json "
        "--app %s/app.bin --app-base 0x%X --lib-base 0x%X -o %s/merged.bin >%s/fluxlink.log 2>&1",
        dir, dir, dir, (unsigned)MM_GRAPHICAL_CODE_BASE, (unsigned)lib_link_base, dir, dir);
    CHECK(system(cmd) == 0, "fluxlink linked the placeholder app against the real UI/SF library and abi/uisf.exports.json");

    FILE* mf = fopen("/tmp/nuxvm_test_uisf_link/merged.bin", "rb");
    CHECK(mf != NULL, "merged.bin was produced");
    if (mf) {
        fseek(mf, 0, SEEK_END);
        size_t merged_len = (size_t)ftell(mf);
        fseek(mf, 0, SEEK_SET);
        uint8_t* merged = malloc(merged_len);
        size_t rd = fread(merged, 1, merged_len, mf);
        fclose(mf);
        CHECK(rd == merged_len, "read the merged image back");

        VM* vm = vm_create(merged, (uint32_t)merged_len, (uint32_t)MM_GRAPHICAL_CODE_BASE, 16 * 1024 * 1024, false);
        vm_run(vm);
        int32_t result = -1;
        bool popped = vm_pop(vm, &result);
        CHECK(vm->halted && popped, "app calling UI::new then UI::menu-open? through the real library ran to HALT");
        CHECK(result == 0, "UI::menu-open? == 0 right after UI::new -- library state actually initialized");
        vm_free(vm);
        free(merged);
    }
}

int main(void) {
    printf("\n=== ABI Conformance Tests (docs/quill_fluxio.md Phase 0.5 / B1) ===\n\n");

    test_zero_arg_call();
    test_multi_arg_call_push_order();
    test_dual_image_single_blob();
    test_fluxlink_end_to_end();
    test_fluxio_extern_end_to_end();
    test_fluxio_extern_void_end_to_end();
    test_uisf_library_link();

    printf("\n%d/%d ABI conformance checks passed.\n", tests_run - tests_failed, tests_run);
    if (tests_failed > 0) {
        fprintf(stderr, "ABI CONFORMANCE FAILED\n");
        return 1;
    }
    printf("All ABI conformance tests passed!\n");
    return 0;
}
