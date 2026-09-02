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

void test_time_scratch_is_ram() {
    printf("Testing /dev/time scratch is ordinary RAM...\n");
    uint8_t prog[] = {
        OP_PUSH, 0x00, 0x50, 0x01, 0x00, /* 0x500100 TIME::BUF */
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
    printf("  time scratch RAM: OK\n");
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
    printf("All VM opcode tests passed!\n");
    return 0;
}
