/* nuxstep -- step-trace harness for differential testing.
 *
 * Reads a hex-encoded program image on stdin, runs it one instruction at a
 * time, and prints a complete state digest after every step. tools/nuxref.py
 * implements the same machine straight from docs/semantics.md, and
 * tools/difftest.py compares the two traces. Any divergence is a bug in the
 * C interpreter, in the Python model, or in the specification -- all three
 * are worth finding.
 *
 * No bus and no output handler are attached beyond the recorder below, so
 * the only external interaction a traced run can raise is OUT. Device reads
 * trap with NoBus, which is itself a specified outcome.
 */
#include "vm.h"
#include "opcodes.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint64_t out_digest = 1469598103934665603ULL; /* FNV-1a offset basis */
static long out_count = 0;

static void fnv_byte(uint64_t* h, uint8_t b) {
    *h ^= b;
    *h *= 1099511628211ULL;
}

static void fnv_i32(uint64_t* h, int32_t v) {
    uint32_t u = (uint32_t)v;
    fnv_byte(h, (uint8_t)(u & 0xFF));
    fnv_byte(h, (uint8_t)((u >> 8) & 0xFF));
    fnv_byte(h, (uint8_t)((u >> 16) & 0xFF));
    fnv_byte(h, (uint8_t)((u >> 24) & 0xFF));
}

/* Records OUT interactions into a digest instead of printing them, so the
 * external interaction sequence is part of what gets compared. */
static void record_out(int32_t value, int32_t format) {
    fnv_i32(&out_digest, value);
    fnv_i32(&out_digest, format);
    out_count++;
}

static uint64_t hash_bytes(const uint8_t* p, uint32_t n) {
    uint64_t h = 1469598103934665603ULL;
    for (uint32_t i = 0; i < n; i++) fnv_byte(&h, p[i]);
    return h;
}

static uint64_t hash_i32s(const int32_t* p, uint32_t n) {
    uint64_t h = 1469598103934665603ULL;
    for (uint32_t i = 0; i < n; i++) fnv_i32(&h, p[i]);
    return h;
}

static void print_state(const VM* vm, long step) {
    printf("STEP %ld pc=%u run=%d halt=%d trap=%u last=%u sp=%d fp=%d rsp=%d ksp=%d",
           step, vm->pc, vm->running ? 1 : 0, vm->halted ? 1 : 0,
           (unsigned)vm->trap, (unsigned)vm->last_opcode,
           vm->stack_ptr, vm->fp, vm->return_stack_ptr, vm->loop_stack_ptr);
    printf(" D=");
    for (int i = 0; i < vm->stack_ptr; i++) printf("%s%d", i ? "," : "", vm->stack[i]);
    printf(" R=");
    for (int i = 0; i < vm->return_stack_ptr; i++) printf("%s%u", i ? "," : "", vm->return_stack[i]);
    printf(" K=");
    for (int i = 0; i < vm->loop_stack_ptr; i++) printf("%s%d", i ? "," : "", vm->loop_stack[i]);
    printf(" L=%llu M=%llu OUT=%ld/%llu\n",
           (unsigned long long)hash_i32s(vm->locals, MAX_LOCALS_SIZE),
           (unsigned long long)hash_bytes(vm->memory, vm->memory_size),
           out_count, (unsigned long long)out_digest);
}

int main(int argc, char** argv) {
    uint32_t base = 0, mem = 4096;
    long max_steps = 200;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--base") && i + 1 < argc) base = (uint32_t)strtoul(argv[++i], NULL, 0);
        else if (!strcmp(argv[i], "--mem") && i + 1 < argc) mem = (uint32_t)strtoul(argv[++i], NULL, 0);
        else if (!strcmp(argv[i], "--steps") && i + 1 < argc) max_steps = strtol(argv[++i], NULL, 0);
        else { fprintf(stderr, "usage: nuxstep [--base N] [--mem N] [--steps N] < image.hex\n"); return 2; }
    }

    static uint8_t prog[1 << 20];
    uint32_t n = 0;
    int c;
    unsigned byte = 0;
    int nib = 0;
    while ((c = getchar()) != EOF) {
        int v;
        if (c >= '0' && c <= '9') v = c - '0';
        else if (c >= 'a' && c <= 'f') v = c - 'a' + 10;
        else if (c >= 'A' && c <= 'F') v = c - 'A' + 10;
        else continue;
        byte = (byte << 4) | (unsigned)v;
        if (++nib == 2) {
            if (n < sizeof(prog)) prog[n++] = (uint8_t)byte;
            nib = 0; byte = 0;
        }
    }

    VM* vm = vm_create(prog, n, base, mem, false);
    if (!vm) { fprintf(stderr, "vm_create failed\n"); return 1; }
    vm->output_handler = record_out;

    print_state(vm, 0);
    for (long s = 1; s <= max_steps; s++) {
        if (!vm->running) break;
        vm_tick(vm);
        print_state(vm, s);
    }
    vm_free(vm);
    return 0;
}
