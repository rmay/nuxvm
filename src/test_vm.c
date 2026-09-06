#include "vm.h"
#include "opcodes.h"
#include "machine.h"
#include "system.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

void write_int32(uint8_t* p, int32_t val) {
    p[0] = (val >> 24) & 0xFF;
    p[1] = (val >> 16) & 0xFF;
    p[2] = (val >> 8) & 0xFF;
    p[3] = val & 0xFF;
}

void test_push_pop() {
    printf("Testing PUSH and POP...\n");
    uint8_t prog[] = {
        OP_PUSH, 0, 0, 0, 42,
        OP_PUSH, 0, 0, 0, 99,
        OP_HALT
    };
    
    VM* vm = vm_create(prog, sizeof(prog), 0, 1024, false);
    assert(vm != NULL);
    
    vm_run(vm);
    
    int32_t v1, v2;
    assert(vm_pop(vm, &v1) == true);
    assert(vm_pop(vm, &v2) == true);
    
    assert(v1 == 99);
    assert(v2 == 42);
    
    vm_free(vm);
}

void test_stack_manipulation() {
    printf("Testing DUP, SWAP, OVER, ROT, PICK, ROLL...\n");
    uint8_t prog[] = {
        // [10]
        OP_PUSH, 0, 0, 0, 10,
        // DUP -> [10, 10]
        OP_DUP,
        // [10, 10, 20]
        OP_PUSH, 0, 0, 0, 20,
        // SWAP -> [10, 20, 10]
        OP_SWAP,
        // OVER -> [10, 20, 10, 20]
        OP_OVER,
        // ROT -> [10, 10, 20, 20]  (top 3 elements rotated, was 20,10,20 -> 10,20,20)
        OP_ROT,
        OP_HALT
    };
    VM* vm = vm_create(prog, sizeof(prog), 0, 1024, false);
    vm_run(vm);
    
    int32_t v;
    assert(vm_pop(vm, &v) == true); assert(v == 20);
    assert(vm_pop(vm, &v) == true); assert(v == 20);
    assert(vm_pop(vm, &v) == true); assert(v == 10);
    assert(vm_pop(vm, &v) == true); assert(v == 10);
    vm_free(vm);
    
    // Test PICK and ROLL
    uint8_t prog2[] = {
        OP_PUSH, 0, 0, 0, 1,
        OP_PUSH, 0, 0, 0, 2,
        OP_PUSH, 0, 0, 0, 3,
        // Stack: [1, 2, 3]
        OP_PUSH, 0, 0, 0, 2,
        OP_PICK, // Pick element at index 2 from top (0-indexed). Top is 2 (index 0). So index 2 is 1. -> [1, 2, 3, 1]
        OP_PUSH, 0, 0, 0, 2,
        OP_ROLL, // Roll element at index 2 (which is 2) to top. Stack was [1, 2, 3, 1]. index 2 from top is 2. Stack -> [1, 3, 1, 2]
        OP_HALT
    };
    vm = vm_create(prog2, sizeof(prog2), 0, 1024, false);
    vm_run(vm);
    assert(vm_pop(vm, &v) == true); assert(v == 2);
    assert(vm_pop(vm, &v) == true); assert(v == 1);
    assert(vm_pop(vm, &v) == true); assert(v == 3);
    assert(vm_pop(vm, &v) == true); assert(v == 1);
    vm_free(vm);
}

void test_arithmetic() {
    printf("Testing ADD, SUB, MUL, DIV, MOD, DIVMOD...\n");
    uint8_t prog[] = {
        OP_PUSH, 0, 0, 0, 10,
        OP_PUSH, 0, 0, 0, 20,
        OP_ADD,   // 30
        OP_PUSH, 0, 0, 0, 5,
        OP_SUB,  // 25
        OP_PUSH, 0, 0, 0, 4,
        OP_MUL,  // 100
        OP_PUSH, 0, 0, 0, 3,
        OP_DIVMOD, // 100 / 3 -> [33, 1] (quotient, remainder)
        OP_HALT
    };
    
    VM* vm = vm_create(prog, sizeof(prog), 0, 1024, false);
    vm_run(vm);
    
    int32_t rem, quot;
    assert(vm_pop(vm, &rem) == true);
    assert(vm_pop(vm, &quot) == true);
    assert(rem == 1);
    assert(quot == 33);
    vm_free(vm);
    
    printf("Testing INC, DEC, NEG, ABS, MIN, MAX...\n");
    uint8_t prog2[] = {
        OP_PUSH, 0, 0, 0, 10,
        OP_INC, // 11
        OP_DEC, // 10
        OP_NEG, // -10
        OP_ABS, // 10
        OP_PUSH, 0, 0, 0, 15,
        OP_MAX, // 15
        OP_PUSH, 0, 0, 0, 12,
        OP_MIN, // 12
        OP_HALT
    };
    vm = vm_create(prog2, sizeof(prog2), 0, 1024, false);
    vm_run(vm);
    int32_t res;
    assert(vm_pop(vm, &res) == true);
    assert(res == 12);
    vm_free(vm);
}

void test_bitwise() {
    printf("Testing AND, OR, XOR, NOT, SHL, SHR, SAR...\n");
    uint8_t prog[] = {
        OP_PUSH, 0, 0, 0, 0b1100,
        OP_PUSH, 0, 0, 0, 0b1010,
        OP_AND, // 0b1000 = 8
        OP_PUSH, 0, 0, 0, 0b0011,
        OP_OR,  // 0b1011 = 11
        OP_PUSH, 0, 0, 0, 0b0110,
        OP_XOR, // 0b1101 = 13
        OP_PUSH, 0, 0, 0, 1,
        OP_SHL, // 26
        OP_PUSH, 0, 0, 0, 1,
        OP_SHR, // 13
        OP_HALT
    };
    VM* vm = vm_create(prog, sizeof(prog), 0, 1024, false);
    vm_run(vm);
    int32_t res;
    assert(vm_pop(vm, &res) == true);
    assert(res == 13);
    vm_free(vm);
    
    // Test SAR
    uint8_t prog2[] = {
        OP_PUSH, 0xFF, 0xFF, 0xFF, 0xF0, // -16
        OP_PUSH, 0, 0, 0, 1,
        OP_SAR, // -8
        OP_HALT
    };
    vm = vm_create(prog2, sizeof(prog2), 0, 1024, false);
    vm_run(vm);
    assert(vm_pop(vm, &res) == true);
    assert(res == -8);
    vm_free(vm);
}

void test_comparison() {
    printf("Testing EQ, NEQ, LT, LTE, GT, GTE...\n");
    uint8_t prog[] = {
        OP_PUSH, 0, 0, 0, 10,
        OP_PUSH, 0, 0, 0, 10,
        OP_EQ, // 1
        
        OP_PUSH, 0, 0, 0, 10,
        OP_PUSH, 0, 0, 0, 20,
        OP_LT, // 1
        
        OP_PUSH, 0, 0, 0, 20,
        OP_PUSH, 0, 0, 0, 10,
        OP_LTE, // 0
        
        OP_PUSH, 0, 0, 0, 20,
        OP_PUSH, 0, 0, 0, 10,
        OP_GT, // 1
        
        OP_HALT
    };
    VM* vm = vm_create(prog, sizeof(prog), 0, 1024, false);
    vm_run(vm);
    int32_t res;
    assert(vm_pop(vm, &res) == true); assert(res == 1); // GT
    assert(vm_pop(vm, &res) == true); assert(res == 0); // LTE
    assert(vm_pop(vm, &res) == true); assert(res == 1); // LT
    assert(vm_pop(vm, &res) == true); assert(res == 1); // EQ
    vm_free(vm);
}

void test_control_flow() {
    printf("Testing JMP, JZ, JNZ, CALL, RET, JMPSTACK, CALLSTACK...\n");
    uint8_t prog[100];
    int pc = 0;
    
    // PUSH target offset (32) for CALLSTACK
    prog[pc++] = OP_PUSH; write_int32(&prog[pc], 32); pc += 4; // pc = 5
    // CALLSTACK
    prog[pc++] = OP_CALLSTACK; // pc = 6
    // HALT (offset 6)
    prog[pc++] = OP_HALT; // pc = 7
    
    // Pad to 32
    while(pc < 32) prog[pc++] = 0;
    
    assert(pc == 32);
    // PUSH 99
    prog[pc++] = OP_PUSH; write_int32(&prog[pc], 99); pc += 4; // pc = 37
    // RET
    prog[pc++] = OP_RET; // pc = 38
    
    VM* vm = vm_create(prog, pc, 0, 1024, false);
    vm_run(vm);
    int32_t res;
    assert(vm_pop(vm, &res) == true);
    assert(res == 99);
    vm_free(vm);
}

void test_memory() {
    printf("Testing LOAD, STORE, LOADI, STOREI...\n");
    // We will write 42 to address 500
    uint8_t prog[] = {
        // PUSH 42
        OP_PUSH, 0, 0, 0, 42,
        // STORE at 500
        OP_STORE, 0, 0, 0x01, 0xF4, // 500
        // LOAD from 500
        OP_LOAD, 0, 0, 0x01, 0xF4, // 500
        OP_HALT
    };
    VM* vm = vm_create(prog, sizeof(prog), 0, 1024, false);
    vm_run(vm);
    int32_t res;
    assert(vm_pop(vm, &res) == true);
    assert(res == 42);
    
    // Test that the memory was actually written correctly
    // write_mem32 writes big-endian: 500=0, 501=0, 502=0, 503=42
    assert(vm->memory[500] == 0);
    assert(vm->memory[501] == 0);
    assert(vm->memory[502] == 0);
    assert(vm->memory[503] == 42);
    
    vm_free(vm);
    
    // Test LOADI / STOREI (byte level)
    uint8_t prog2[] = {
        // STOREI 0xAB at 600
        OP_PUSH, 0, 0, 0, 0xAB,
        OP_PUSH, 0, 0, 0x02, 0x58, // 600
        OP_STOREI,
        // LOADI from 600
        OP_PUSH, 0, 0, 0x02, 0x58, // 600
        OP_LOADI,
        OP_HALT
    };
    vm = vm_create(prog2, sizeof(prog2), 0, 1024, false);
    vm_run(vm);
    assert(vm_pop(vm, &res) == true);
    assert(res == 0xAB);
    // write_mem32 writes big-endian: 600=0, 601=0, 602=0, 603=0xAB
    assert(vm->memory[600] == 0);
    assert(vm->memory[603] == 0xAB);
    vm_free(vm);
}

void test_loop_stack() {
    printf("Testing PUSHR, POPR, PEEKR, PEEKR2...\n");
    uint8_t prog[] = {
        OP_PUSH, 0, 0, 0, 10,
        OP_PUSHR,
        OP_PUSH, 0, 0, 0, 20,
        OP_PUSHR,
        OP_PEEKR, // 20
        OP_PEEKR2, // 10
        OP_POPR, // pop 20
        OP_POPR, // pop 10
        OP_HALT
    };
    VM* vm = vm_create(prog, sizeof(prog), 0, 1024, false);
    vm_run(vm);
    int32_t res;
    // Main stack after execution (bottom to top):
    // PEEKR: 20
    // PEEKR2: 10
    // POPR: 20
    // POPR: 10
    // Pop order: 10, 20, 10, 20
    assert(vm_pop(vm, &res) == true); assert(res == 10); // from POPR
    assert(vm_pop(vm, &res) == true); assert(res == 20); // from POPR
    assert(vm_pop(vm, &res) == true); assert(res == 10); // from PEEKR2
    assert(vm_pop(vm, &res) == true); assert(res == 20); // from PEEKR
    vm_free(vm);
}

void test_frames() {
    printf("Testing FRAME, UNFRAME, LOCALGET, LOCALSET...\n");
    uint8_t prog[] = {
        OP_PUSH, 0, 0, 0, 11, // val 1
        OP_PUSH, 0, 0, 0, 22, // val 0
        OP_PUSH, 0, 0, 0, 2, // size = 2
        OP_FRAME,
        OP_PUSH, 0, 0, 0, 88,
        OP_PUSH, 0, 0, 0, 0, // idx 0
        OP_LOCALSET,
        OP_PUSH, 0, 0, 0, 99,
        OP_PUSH, 0, 0, 0, 1, // idx 1
        OP_LOCALSET,
        
        OP_PUSH, 0, 0, 0, 0,
        OP_LOCALGET, // 88
        OP_PUSH, 0, 0, 0, 1,
        OP_LOCALGET, // 99
        
        OP_PUSH, 0, 0, 0, 2, // size = 2 for UNFRAME
        OP_UNFRAME,
        OP_HALT
    };
    VM* vm = vm_create(prog, sizeof(prog), 0, 1024, false);
    vm_run(vm);
    int32_t res;
    assert(vm_pop(vm, &res) == true); assert(res == 99);
    assert(vm_pop(vm, &res) == true); assert(res == 88);
    vm_free(vm);
}

void test_breaking_calls() {
    printf("Testing breaking calls (stack overflow/underflow)...\n");
    // Test OP_CALL stack overflow
    uint8_t prog_call_overflow[] = {
        OP_CALL, 0, 0, 0, 0 // Call address 0 repeatedly
    };
    VM* vm = vm_create(prog_call_overflow, sizeof(prog_call_overflow), 0, 1024, false);
    vm_run(vm);
    assert(vm->running == false); // Should halt due to overflow
    assert(vm->return_stack_ptr == MAX_RETURN_STACK_SIZE);
    vm_free(vm);

    // Test OP_CALLSTACK stack overflow
    uint8_t prog_callstack_overflow[] = {
        OP_PUSH, 0, 0, 0, 0, // Push address 0
        OP_CALLSTACK
    };
    vm = vm_create(prog_callstack_overflow, sizeof(prog_callstack_overflow), 0, 1024, false);
    vm_run(vm);
    assert(vm->running == false);
    assert(vm->return_stack_ptr == MAX_RETURN_STACK_SIZE);
    vm_free(vm);

    // Test OP_RET stack underflow
    uint8_t prog_ret_underflow[] = {
        OP_RET
    };
    vm = vm_create(prog_ret_underflow, sizeof(prog_ret_underflow), 0, 1024, false);
    vm_run(vm);
    assert(vm->running == false);
    assert(vm->return_stack_ptr == 0);
    vm_free(vm);
}


void test_bus_read() {
    printf("Testing SCI result port...\n");
    uint8_t prog[] = {
        OP_LOAD, 0x00, 0x01, 0x00, 0xD0,  /* SCI_PORT */
        OP_HALT
    };

    Machine* m = machine_create(prog, sizeof(prog), HEADLESS_BASE_ADDRESS, 1024 * 1024, false);
    assert(m != NULL);
    m->system->sci_result = 12345;

    vm_run(m->cpu);

    int32_t v;
    assert(vm_pop(m->cpu, &v) == true);
    assert(v == 12345);
    machine_free(m);
}

/* Guest scratch buffers are ordinary RAM, not device ports -- the property
 * that had to hold when I/O moved from MMIO to the Plan 9 VFS. Checked at
 * two addresses: the shared Lux flags band, and the reservation band, which
 * is where lib/time.lux's /dev/time snapshot actually lives now that it is
 * compiler-allocated (docs/reserve-directive.md). */
static void assert_addr_is_ram(uint32_t addr) {
    uint8_t prog[] = {
        OP_PUSH,
        (uint8_t) ((addr >> 24) & 0xFF), (uint8_t) ((addr >> 16) & 0xFF),
        (uint8_t) ((addr >> 8) & 0xFF),  (uint8_t) (addr & 0xFF),
        OP_LOADI,
        OP_HALT
    };
    Machine* m = machine_create(prog, sizeof(prog), GRAPHICAL_BASE_ADDRESS,
                                32 * 1024 * 1024, false);
    assert(m != NULL);
    vm_run(m->cpu);
    assert(m->cpu->halted == true);
    int32_t v;
    assert(vm_pop(m->cpu, &v) == true);
    assert(v == 0);
    machine_free(m);
}

void test_time_scratch_is_ram() {
    printf("Testing guest scratch is ordinary RAM...\n");
    assert_addr_is_ram(MM_SHARED_LUX_FLAGS_BASE + 0x100);
    assert_addr_is_ram(MM_LUX_RESERVE_BASE);
    assert_addr_is_ram(MM_LUX_RESERVE_END - 4);
    printf("  scratch RAM: OK\n");
}

void test_yield() {
    printf("Testing OP_YIELD...\n");
    uint8_t prog[] = {
        OP_YIELD,
        OP_PUSH, 0, 0, 0, 99,
        OP_HALT
    };

    VM* vm = vm_create(prog, sizeof(prog), HEADLESS_BASE_ADDRESS, 1024 * 1024, false);
    assert(vm != NULL);
    vm_run(vm);
    assert(vm_yielded(vm) == true);
    assert(vm->halted == false);
    assert(vm_get_stack_count(vm) == 0);
    vm_free(vm);

    Machine* m = machine_create(prog, sizeof(prog), HEADLESS_BASE_ADDRESS, 1024 * 1024, false);
    assert(m != NULL);
    bool still_running = machine_tick(m);
    assert(still_running == true);
    assert(vm_yielded(m->cpu) == true);
    assert(m->cpu->halted == false);
    machine_free(m);
}


void test_memory_faults() {
    printf("Testing aggressive memory faults...\n");
    int32_t res;

    // Fetch past image_end: PUSH then implicit zeros must not become PUSH 0 forever.
    uint8_t prog_runoff[] = {
        OP_PUSH, 0, 0, 0, 1
    };
    VM* vm = vm_create(prog_runoff, sizeof(prog_runoff), 0, 1024, false);
    vm_run(vm);
    assert(vm->running == false);
    assert(vm->halted == false);
    assert(vm->pc >= vm->image_end);
    assert(vm_get_stack_count(vm) == 1);
    assert(vm_pop(vm, &res) == true && res == 1);
    vm_free(vm);

    // JMP past the image
    uint8_t prog_jmp[] = {
        OP_JMP, 0, 0, 0, 100,
        OP_HALT
    };
    vm = vm_create(prog_jmp, sizeof(prog_jmp), 0, 1024, false);
    vm_run(vm);
    assert(vm->running == false);
    assert(vm->halted == false);
    vm_free(vm);

    // CALLSTACK to an address past the image
    uint8_t prog_cs[] = {
        OP_PUSH, 0, 0, 1, 0, // 256
        OP_CALLSTACK,
        OP_HALT
    };
    vm = vm_create(prog_cs, sizeof(prog_cs), 0, 1024, false);
    vm_run(vm);
    assert(vm->running == false);
    assert(vm->halted == false);
    vm_free(vm);

    // STOREI into the program image must not modify bytecode
    uint8_t prog_store[] = {
        OP_PUSH, 0xAA, 0xBB, 0xCC, 0xDD,
        OP_PUSH, 0, 0, 0, 0,
        OP_STOREI,
        OP_HALT
    };
    uint8_t first = prog_store[0];
    vm = vm_create(prog_store, sizeof(prog_store), 0, 1024, false);
    vm_run(vm);
    assert(vm->running == false);
    assert(vm->halted == false);
    assert(vm->memory[0] == first);
    vm_free(vm);

    // Unaligned LOADI
    uint8_t prog_unaligned[] = {
        OP_PUSH, 0, 0, 0, 1,
        OP_LOADI,
        OP_HALT
    };
    vm = vm_create(prog_unaligned, sizeof(prog_unaligned), 0, 1024, false);
    vm_run(vm);
    assert(vm->running == false);
    assert(vm->halted == false);
    vm_free(vm);

    // Partial word: aligned address with fewer than 4 bytes left
    uint8_t prog_partial[] = {
        OP_PUSH, 0, 0, 0, 100,
        OP_LOADI,
        OP_HALT
    };
    vm = vm_create(prog_partial, sizeof(prog_partial), 0, 103, false);
    vm_run(vm);
    assert(vm->running == false);
    assert(vm->halted == false);
    vm_free(vm);

    // Wrap: 0xFFFFFFFC is aligned and far past memory_size
    uint8_t prog_wrap[] = {
        OP_PUSH, 0xFF, 0xFF, 0xFF, 0xFC,
        OP_LOADI,
        OP_HALT
    };
    vm = vm_create(prog_wrap, sizeof(prog_wrap), 0, 1024, false);
    vm_run(vm);
    assert(vm->running == false);
    assert(vm->halted == false);
    vm_free(vm);

    printf("  memory faults: OK\n");
}

void test_guest_memory_size() {
    printf("Testing nux_guest_memory_size...\n");
    uint32_t headless = nux_guest_memory_size(HEADLESS_BASE_ADDRESS, 100);
    assert(headless == HEADLESS_BASE_ADDRESS + 100);

    uint32_t graphical = nux_guest_memory_size(GRAPHICAL_BASE_ADDRESS, 100);
    assert(graphical == MM_TOTAL_MEMORY);

    uint8_t prog[] = { OP_HALT };
    VM* vm = vm_create(prog, sizeof(prog), HEADLESS_BASE_ADDRESS,
                       nux_guest_memory_size(HEADLESS_BASE_ADDRESS, sizeof(prog)), false);
    assert(vm != NULL);
    assert(vm->memory_size == HEADLESS_BASE_ADDRESS + sizeof(prog));
    vm_run(vm);
    assert(vm->halted);
    vm_free(vm);

    Machine* m = machine_create(prog, sizeof(prog), GRAPHICAL_BASE_ADDRESS,
                                nux_guest_memory_size(GRAPHICAL_BASE_ADDRESS, sizeof(prog)), false);
    assert(m != NULL);
    assert(m->cpu->memory_size == MM_TOTAL_MEMORY);
    machine_free(m);
    printf("  guest memory size: OK\n");
}


/* --- Tier 1: every arithmetic edge case that used to be C undefined
 * behaviour now has a defined, tested answer. See docs/semantics.md. --- */

/* Run a binary op over two literals and return the resulting top of stack. */
static int32_t eval_binop(int32_t x, int32_t y, uint8_t op) {
    uint8_t prog[] = {
        OP_PUSH, 0, 0, 0, 0,
        OP_PUSH, 0, 0, 0, 0,
        op,
        OP_HALT
    };
    write_int32(prog + 1, x);
    write_int32(prog + 6, y);
    VM* vm = vm_create(prog, sizeof(prog), 0, 1024, false);
    vm_run(vm);
    assert(vm->halted == true);
    assert(vm->trap == TRAP_NONE);
    int32_t r;
    assert(vm_get_stack_count(vm) == 1);
    assert(vm_pop(vm, &r) == true);
    vm_free(vm);
    return r;
}

/* Run a unary op over one literal and return the resulting top of stack. */
static int32_t eval_unop(int32_t x, uint8_t op) {
    uint8_t prog[] = {
        OP_PUSH, 0, 0, 0, 0,
        op,
        OP_HALT
    };
    write_int32(prog + 1, x);
    VM* vm = vm_create(prog, sizeof(prog), 0, 1024, false);
    vm_run(vm);
    assert(vm->halted == true);
    assert(vm->trap == TRAP_NONE);
    int32_t r;
    assert(vm_get_stack_count(vm) == 1);
    assert(vm_pop(vm, &r) == true);
    vm_free(vm);
    return r;
}

void test_defined_arithmetic() {
    printf("Testing defined arithmetic at the 32-bit boundaries...\n");

    /* Overflow wraps modulo 2^32 rather than being undefined. */
    assert(eval_binop(INT32_MAX, 1, OP_ADD) == INT32_MIN);
    assert(eval_binop(INT32_MIN, -1, OP_ADD) == INT32_MAX);
    assert(eval_binop(INT32_MIN, 1, OP_SUB) == INT32_MAX);
    assert(eval_binop(INT32_MAX, -1, OP_SUB) == INT32_MIN);
    assert(eval_binop(INT32_MIN, -1, OP_MUL) == INT32_MIN);
    assert(eval_binop(65536, 65536, OP_MUL) == 0);
    assert(eval_unop(INT32_MAX, OP_INC) == INT32_MIN);
    assert(eval_unop(INT32_MIN, OP_DEC) == INT32_MAX);

    /* NEG and ABS of INT32_MIN wrap to INT32_MIN; they do not trap. */
    assert(eval_unop(INT32_MIN, OP_NEG) == INT32_MIN);
    assert(eval_unop(INT32_MIN, OP_ABS) == INT32_MIN);
    assert(eval_unop(-5, OP_ABS) == 5);
    assert(eval_unop(5, OP_ABS) == 5);

    /* INT32_MIN / -1 overflows the quotient and traps on x86 hardware.
     * NUX defines it: quotient wraps, remainder is zero. */
    assert(eval_binop(INT32_MIN, -1, OP_DIV) == INT32_MIN);
    assert(eval_binop(INT32_MIN, -1, OP_MOD) == 0);

    /* Division truncates toward zero, and the remainder takes the sign of
     * the dividend. */
    assert(eval_binop(-7, 2, OP_DIV) == -3);
    assert(eval_binop(-7, 2, OP_MOD) == -1);
    assert(eval_binop(7, -2, OP_DIV) == -3);
    assert(eval_binop(7, -2, OP_MOD) == 1);

    printf("  defined arithmetic: OK\n");
}

void test_shift_counts() {
    printf("Testing shift counts are masked to 5 bits...\n");

    /* Counts are masked with & 31, so 32 is a no-op and 33 shifts by one. */
    assert(eval_binop(1, 0, OP_SHL) == 1);
    assert(eval_binop(1, 31, OP_SHL) == INT32_MIN);
    assert(eval_binop(1, 32, OP_SHL) == 1);
    assert(eval_binop(1, 33, OP_SHL) == 2);

    /* A negative count would have been an undefined negative shift
     * distance; masking makes it total. -1 & 31 == 31. */
    assert(eval_binop(1, -1, OP_SHL) == INT32_MIN);
    assert(eval_binop(INT32_MIN, -1, OP_SHR) == 1);
    assert(eval_binop(INT32_MIN, -1, OP_SAR) == -1);

    /* SHR is logical, SAR is arithmetic. */
    assert(eval_binop(INT32_MIN, 31, OP_SHR) == 1);
    assert(eval_binop(INT32_MIN, 31, OP_SAR) == -1);
    assert(eval_binop(-8, 1, OP_SAR) == -4);
    assert(eval_binop(-1, 31, OP_SAR) == -1);
    assert(eval_binop(-1, 0, OP_SAR) == -1);   /* shift by zero sign-fills nothing */
    assert(eval_binop(-1, 0, OP_SHR) == -1);
    assert(eval_binop(-1, 32, OP_SAR) == -1);  /* 32 & 31 == 0 */

    printf("  shift counts: OK\n");
}

void test_trap_causes() {
    printf("Testing traps are recorded as machine state...\n");

    /* A clean HALT leaves no trap. */
    uint8_t prog_ok[] = { OP_HALT };
    VM* vm = vm_create(prog_ok, sizeof(prog_ok), 0, 1024, false);
    vm_run(vm);
    assert(vm->halted == true && vm->trap == TRAP_NONE);
    vm_free(vm);

    /* Division by zero names itself. */
    uint8_t prog_div[] = {
        OP_PUSH, 0, 0, 0, 1,
        OP_PUSH, 0, 0, 0, 0,
        OP_DIV,
        OP_HALT
    };
    vm = vm_create(prog_div, sizeof(prog_div), 0, 1024, false);
    vm_run(vm);
    assert(vm->running == false && vm->halted == false);
    assert(vm->trap == TRAP_DIVIDE_BY_ZERO);
    /* Fault atomicity: the trapping DIV committed nothing, so both
     * operands are still on the stack. */
    assert(vm_get_stack_count(vm) == 2);
    vm_free(vm);

    /* Underflow on an empty stack. */
    uint8_t prog_under[] = { OP_ADD, OP_HALT };
    vm = vm_create(prog_under, sizeof(prog_under), 0, 1024, false);
    vm_run(vm);
    assert(vm->trap == TRAP_STACK_UNDERFLOW);
    assert(vm_get_stack_count(vm) == 0);
    vm_free(vm);

    /* A partial pop must not happen: SWAP with one operand leaves it. */
    uint8_t prog_swap[] = {
        OP_PUSH, 0, 0, 0, 7,
        OP_SWAP,
        OP_HALT
    };
    vm = vm_create(prog_swap, sizeof(prog_swap), 0, 1024, false);
    vm_run(vm);
    assert(vm->trap == TRAP_STACK_UNDERFLOW);
    assert(vm_get_stack_count(vm) == 1);
    int32_t v;
    assert(vm_pop(vm, &v) == true && v == 7);
    vm_free(vm);

    /* Unknown opcode: 0x37 is the first byte past the frozen ISA. */
    uint8_t prog_bad[] = { 0x37, OP_HALT };
    vm = vm_create(prog_bad, sizeof(prog_bad), 0, 1024, false);
    vm_run(vm);
    assert(vm->trap == TRAP_UNKNOWN_OPCODE);
    vm_free(vm);

    /* Memory traps carry their specific cause. */
    uint8_t prog_unal[] = {
        OP_PUSH, 0, 0, 0, 1,
        OP_LOADI,
        OP_HALT
    };
    vm = vm_create(prog_unal, sizeof(prog_unal), 0, 1024, false);
    vm_run(vm);
    assert(vm->trap == TRAP_UNALIGNED_READ);
    vm_free(vm);

    uint8_t prog_img[] = {
        OP_PUSH, 0xAA, 0xBB, 0xCC, 0xDD,
        OP_PUSH, 0, 0, 0, 0,
        OP_STOREI,
        OP_HALT
    };
    vm = vm_create(prog_img, sizeof(prog_img), 0, 1024, false);
    vm_run(vm);
    assert(vm->trap == TRAP_WRITE_INTO_IMAGE);
    vm_free(vm);

    uint8_t prog_jmp[] = { OP_JMP, 0, 0, 0, 100, OP_HALT };
    vm = vm_create(prog_jmp, sizeof(prog_jmp), 0, 1024, false);
    vm_run(vm);
    assert(vm->trap == TRAP_JUMP_OUTSIDE_IMAGE);
    vm_free(vm);

    /* Running off the end of the image. */
    uint8_t prog_run[] = { OP_PUSH, 0, 0, 0, 1 };
    vm = vm_create(prog_run, sizeof(prog_run), 0, 1024, false);
    vm_run(vm);
    assert(vm->trap == TRAP_EXEC_OUTSIDE_IMAGE);
    vm_free(vm);

    /* Every trap cause has a name. */
    for (int t = 0; t < TRAP__COUNT; t++) {
        const char* n = nux_trap_name((NuxTrap)t);
        assert(n != NULL && strcmp(n, "unknown trap") != 0);
    }

    printf("  trap causes: OK\n");
}

void test_stack_edges() {
    printf("Testing PICK/ROLL/DIVMOD/MIN/MAX edges...\n");
    int32_t v;

    /* PICK 0 duplicates the top; PICK past the bottom traps. */
    uint8_t prog_pick[] = {
        OP_PUSH, 0, 0, 0, 11,
        OP_PUSH, 0, 0, 0, 22,
        OP_PUSH, 0, 0, 0, 0,
        OP_PICK,
        OP_HALT
    };
    VM* vm = vm_create(prog_pick, sizeof(prog_pick), 0, 1024, false);
    vm_run(vm);
    assert(vm->trap == TRAP_NONE);
    assert(vm_get_stack_count(vm) == 3);
    assert(vm_pop(vm, &v) && v == 22);
    vm_free(vm);

    uint8_t prog_pick_bad[] = {
        OP_PUSH, 0, 0, 0, 11,
        OP_PUSH, 0, 0, 0, 5,
        OP_PICK,
        OP_HALT
    };
    vm = vm_create(prog_pick_bad, sizeof(prog_pick_bad), 0, 1024, false);
    vm_run(vm);
    assert(vm->trap == TRAP_PICK_RANGE);
    vm_free(vm);

    /* ROLL 0 is a no-op that still consumes its index. */
    uint8_t prog_roll0[] = {
        OP_PUSH, 0, 0, 0, 11,
        OP_PUSH, 0, 0, 0, 0,
        OP_ROLL,
        OP_HALT
    };
    vm = vm_create(prog_roll0, sizeof(prog_roll0), 0, 1024, false);
    vm_run(vm);
    assert(vm->trap == TRAP_NONE);
    assert(vm_get_stack_count(vm) == 1);
    assert(vm_pop(vm, &v) && v == 11);
    vm_free(vm);

    /* ROLL 2 brings the third element to the top: [1,2,3] -> [2,3,1]. */
    uint8_t prog_roll2[] = {
        OP_PUSH, 0, 0, 0, 1,
        OP_PUSH, 0, 0, 0, 2,
        OP_PUSH, 0, 0, 0, 3,
        OP_PUSH, 0, 0, 0, 2,
        OP_ROLL,
        OP_HALT
    };
    vm = vm_create(prog_roll2, sizeof(prog_roll2), 0, 1024, false);
    vm_run(vm);
    assert(vm->trap == TRAP_NONE);
    assert(vm_get_stack_count(vm) == 3);
    assert(vm_pop(vm, &v) && v == 1);
    assert(vm_pop(vm, &v) && v == 3);
    assert(vm_pop(vm, &v) && v == 2);
    vm_free(vm);

    /* ROLL past the bottom traps, and commits nothing. */
    uint8_t prog_roll_bad[] = {
        OP_PUSH, 0, 0, 0, 1,
        OP_PUSH, 0, 0, 0, 4,
        OP_ROLL,
        OP_HALT
    };
    vm = vm_create(prog_roll_bad, sizeof(prog_roll_bad), 0, 1024, false);
    vm_run(vm);
    assert(vm->trap == TRAP_ROLL_RANGE);
    assert(vm_get_stack_count(vm) == 2);
    vm_free(vm);

    /* DIVMOD leaves quotient then remainder. */
    uint8_t prog_dm[] = {
        OP_PUSH, 0, 0, 0, 17,
        OP_PUSH, 0, 0, 0, 5,
        OP_DIVMOD,
        OP_HALT
    };
    vm = vm_create(prog_dm, sizeof(prog_dm), 0, 1024, false);
    vm_run(vm);
    assert(vm->trap == TRAP_NONE);
    assert(vm_get_stack_count(vm) == 2);
    assert(vm_pop(vm, &v) && v == 2);   /* remainder on top */
    assert(vm_pop(vm, &v) && v == 3);   /* quotient below */
    vm_free(vm);

    /* DIVMOD by zero traps and commits nothing. */
    uint8_t prog_dm0[] = {
        OP_PUSH, 0, 0, 0, 17,
        OP_PUSH, 0, 0, 0, 0,
        OP_DIVMOD,
        OP_HALT
    };
    vm = vm_create(prog_dm0, sizeof(prog_dm0), 0, 1024, false);
    vm_run(vm);
    assert(vm->trap == TRAP_DIVIDE_BY_ZERO);
    assert(vm_get_stack_count(vm) == 2);
    vm_free(vm);

    /* MIN/MAX at the boundaries. */
    assert(eval_binop(INT32_MIN, INT32_MAX, OP_MIN) == INT32_MIN);
    assert(eval_binop(INT32_MIN, INT32_MAX, OP_MAX) == INT32_MAX);
    assert(eval_binop(-1, 0, OP_MIN) == -1);
    assert(eval_binop(-1, 0, OP_MAX) == 0);

    printf("  stack edges: OK\n");
}


void test_frame_pointer_cannot_escape() {
    printf("Testing a guest cannot corrupt the frame pointer...\n");

    /* The saved-FP slot is an ordinary local, so LOCALSET can overwrite it.
     * Restoring it unchecked used to let FP leave [-1, MAX_LOCALS_SIZE),
     * and the next FRAME then indexed locals[] out of bounds with a
     * negative subscript -- a guest-triggerable write outside the VM's own
     * arrays. Found by the CBMC harness in verify/; see docs/semantics.md
     * section 9. UNFRAME must reject a corrupt frame pointer instead. */
    uint8_t prog[] = {
        OP_PUSH, 0, 0, 0, 7,                 /* a local value             */
        OP_PUSH, 0, 0, 0, 1,                 /* n = 1                     */
        OP_FRAME,                            /* fp = 1, saved fp at L[0]  */
        OP_PUSH, 0xFF, 0xF0, 0xBD, 0xC0,     /* -1000000                  */
        OP_PUSH, 0, 0, 0, 1,                 /* offset 1 -> the saved-FP slot */
        OP_LOCALSET,                         /* poison it                 */
        OP_PUSH, 0, 0, 0, 1,
        OP_UNFRAME,                          /* must trap, not restore it */
        OP_PUSH, 0, 0, 0, 0,
        OP_FRAME,                            /* would have written L[-999999] */
        OP_HALT
    };
    VM* vm = vm_create(prog, sizeof(prog), 0, 4096, false);
    vm_run(vm);
    assert(vm->running == false);
    assert(vm->halted == false);
    assert(vm->trap == TRAP_FRAME_RANGE);
    /* The machine stopped at the UNFRAME, with the frame pointer still
     * whatever the last legitimate FRAME made it. */
    assert(vm->fp >= -1 && vm->fp < MAX_LOCALS_SIZE);
    vm_free(vm);

    /* A well-behaved frame still nests and unwinds correctly. */
    uint8_t ok[] = {
        OP_PUSH, 0, 0, 0, 11,
        OP_PUSH, 0, 0, 0, 1,
        OP_FRAME,                            /* frame A: fp = 1 */
        OP_PUSH, 0, 0, 0, 22,
        OP_PUSH, 0, 0, 0, 1,
        OP_FRAME,                            /* frame B: fp = 3 */
        OP_PUSH, 0, 0, 0, 0,
        OP_LOCALGET,                         /* B's local 0 -> 22 */
        OP_PUSH, 0, 0, 0, 1,
        OP_UNFRAME,                          /* back to frame A */
        OP_PUSH, 0, 0, 0, 0,
        OP_LOCALGET,                         /* A's local 0 -> 11 */
        OP_HALT
    };
    vm = vm_create(ok, sizeof(ok), 0, 4096, false);
    vm_run(vm);
    assert(vm->halted == true && vm->trap == TRAP_NONE);
    int32_t v;
    assert(vm_pop(vm, &v) && v == 11);
    assert(vm_pop(vm, &v) && v == 22);
    vm_free(vm);

    printf("  frame pointer integrity: OK\n");
}

int main() {
    test_push_pop();
    test_stack_manipulation();
    test_arithmetic();
    test_bitwise();
    test_comparison();
    test_control_flow();
    test_memory();
    test_loop_stack();
    test_frames();
    test_breaking_calls();
    test_bus_read();
    test_time_scratch_is_ram();
    test_yield();
    test_memory_faults();
    test_guest_memory_size();
    test_defined_arithmetic();
    test_shift_counts();
    test_trap_causes();
    test_stack_edges();
    test_frame_pointer_cannot_escape();
    printf("All VM opcode tests passed!\n");
    return 0;
}
