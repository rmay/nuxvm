#include "vm.h"
#include "opcodes.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

const char* opcode_name(uint8_t op) {
    switch (op) {
        case OP_PUSH: return "PUSH";
        case OP_POP: return "POP";
        case OP_DUP: return "DUP";
        case OP_SWAP: return "SWAP";
        case OP_OVER: return "OVER";
        case OP_ROT: return "ROT";
        case OP_PICK: return "PICK";
        case OP_ROLL: return "ROLL";
        case OP_ADD: return "ADD";
        case OP_SUB: return "SUB";
        case OP_MUL: return "MUL";
        case OP_DIV: return "DIV";
        case OP_MOD: return "MOD";
        case OP_INC: return "INC";
        case OP_DEC: return "DEC";
        case OP_NEG: return "NEG";
        case OP_ABS: return "ABS";
        case OP_DIVMOD: return "DIVMOD";
        case OP_MIN: return "MIN";
        case OP_MAX: return "MAX";
        case OP_AND: return "AND";
        case OP_OR: return "OR";
        case OP_XOR: return "XOR";
        case OP_NOT: return "NOT";
        case OP_SHL: return "SHL";
        case OP_SHR: return "SHR";
        case OP_SAR: return "SAR";
        case OP_EQ: return "EQ";
        case OP_NEQ: return "NEQ";
        case OP_LT: return "LT";
        case OP_LTE: return "LTE";
        case OP_GT: return "GT";
        case OP_GTE: return "GTE";
        case OP_JMP: return "JMP";
        case OP_JZ: return "JZ";
        case OP_JNZ: return "JNZ";
        case OP_CALL: return "CALL";
        case OP_RET: return "RET";
        case OP_CALLSTACK: return "CALLSTACK";
        case OP_JMPSTACK: return "JMPSTACK";
        case OP_LOAD: return "LOAD";
        case OP_STORE: return "STORE";
        case OP_LOADI: return "LOADI";
        case OP_STOREI: return "STOREI";
        case OP_PUSHR: return "PUSHR";
        case OP_POPR: return "POPR";
        case OP_PEEKR: return "PEEKR";
        case OP_PEEKR2: return "PEEKR2";
        case OP_FRAME: return "FRAME";
        case OP_UNFRAME: return "UNFRAME";
        case OP_LOCALGET: return "LOCALGET";
        case OP_LOCALSET: return "LOCALSET";
        case OP_OUT: return "OUT";
        case OP_HALT: return "HALT";
        case OP_YIELD: return "YIELD";
        default: return "UNKNOWN";
    }
}

VM* vm_create(const uint8_t* program, uint32_t program_size, uint32_t base_address, uint32_t memory_size, bool trace) {
    if (memory_size < base_address + program_size) {
        memory_size = base_address + program_size;
    }
    
    VM* vm = (VM*)calloc(1, sizeof(VM));
    if (!vm) return NULL;
    
    vm->memory = (uint8_t*)calloc(1, memory_size);
    if (!vm->memory) {
        free(vm);
        return NULL;
    }
    vm->memory_size = memory_size;
    
    if (program && program_size > 0) {
        memcpy(vm->memory + base_address, program, program_size);
    }
    
    vm->pc = base_address;
    vm->running = true;
    vm->halted = false;
    vm->reserved_memory_size = RESERVED_MEMORY_SIZE;
    vm->user_memory_start = base_address;
    vm->trace = trace;
    vm->fp = -1;
    
    return vm;
}

void vm_free(VM* vm) {
    if (vm) {
        if (vm->memory) free(vm->memory);
        free(vm);
    }
}

void vm_set_bus(VM* vm, DeviceBus* bus) {
    if (vm) {
        vm->bus = bus;
    }
}

bool vm_call_vector(VM* vm, uint32_t addr) {
    if (vm->return_stack_ptr >= MAX_RETURN_STACK_SIZE) {
        fprintf(stderr, "Return stack overflow\n");
        vm->running = false;
        return false;
    }
    vm->return_stack[vm->return_stack_ptr++] = vm->pc;
    vm->pc = addr - vm->user_memory_start;
    return true;
}

bool vm_push(VM* vm, int32_t value) {
    if (vm->stack_ptr >= MAX_STACK_SIZE) {
        fprintf(stderr, "Data stack overflow at PC 0x%08X\n", vm->pc);
        vm->running = false;
        return false;
    }
    vm->stack[vm->stack_ptr++] = value;
    return true;
}

bool vm_pop(VM* vm, int32_t* value) {
    if (vm->stack_ptr <= 0) {
        fprintf(stderr, "Stack underflow at PC 0x%08X\n", vm->pc);
        vm->running = false;
        return false;
    }
    *value = vm->stack[--vm->stack_ptr];
    return true;
}

static uint32_t read_uint32(VM* vm) {
    uint32_t val = 0;
    val |= (uint32_t)vm->memory[vm->pc++] << 24;
    val |= (uint32_t)vm->memory[vm->pc++] << 16;
    val |= (uint32_t)vm->memory[vm->pc++] << 8;
    val |= (uint32_t)vm->memory[vm->pc++];
    return val;
}

static int32_t read_int32(VM* vm) {
    return (int32_t)read_uint32(vm);
}

static bool is_device_addr(VM* vm, uint32_t addr) {
    if (addr >= vm->user_memory_start) {
        return false;
    }
    return (addr >= DEVICE_MEMORY_OFFSET && addr < DEVICE_MEMORY_OFFSET + DEVICE_MEMORY_SIZE)
        || (addr >= VIDEO_FRAMEBUFFER_START && addr < VIDEO_FRAMEBUFFER_END);
}


static void write_mem32(VM* vm, uint32_t addr, int32_t val) {
    if (addr > vm->memory_size || addr + 4 > vm->memory_size) {
        fprintf(stderr, "Memory write out of bounds at 0x%08X\n", addr);
        vm->running = false;
        return;
    }
    vm->memory[addr] = (val >> 24) & 0xFF;
    vm->memory[addr+1] = (val >> 16) & 0xFF;
    vm->memory[addr+2] = (val >> 8) & 0xFF;
    vm->memory[addr+3] = val & 0xFF;
    
    if (is_device_addr(vm, addr)) {
        if (vm->bus && vm->bus->write) {
            vm->bus->write(vm->bus, addr, val);
        }
    }
}

static int32_t read_mem32(VM* vm, uint32_t addr) {
    if (is_device_addr(vm, addr)) {
        if (vm->bus && vm->bus->read) {
            bool success = false;
            int32_t val = vm->bus->read(vm->bus, addr, &success);
            if (success) {
                return val;
            }
            fprintf(stderr, "Device read failed at 0x%08X\n", addr);
            vm->running = false;
            return 0;
        }
        fprintf(stderr, "No bus: device read at 0x%08X\n", addr);
        vm->running = false;
        return 0;
    }
    if (addr > vm->memory_size || addr + 4 > vm->memory_size) {
        fprintf(stderr, "Memory read out of bounds at 0x%08X\n", addr);
        vm->running = false;
        return 0;
    }
    int32_t val = 0;
    val |= (int32_t)vm->memory[addr] << 24;
    val |= (int32_t)vm->memory[addr+1] << 16;
    val |= (int32_t)vm->memory[addr+2] << 8;
    val |= (int32_t)vm->memory[addr+3];
    return val;
}

bool vm_tick(VM* vm) {
    if (!vm->running) return false;
    if (vm->pc >= vm->memory_size) {
        fprintf(stderr, "PC out of bounds\n");
        vm->running = false;
        return false;
    }
    
    uint8_t op = vm->memory[vm->pc++];
    vm->last_opcode = op;
    
    if (vm->trace) {
        fprintf(stderr, "TRACE PC:0x%08X OP:%s(%02X) SP:%d\n", vm->pc-1, opcode_name(op), op, vm->stack_ptr);
    }
    
    int32_t a, b, c;
    uint32_t addr;
    
    switch (op) {
        case OP_PUSH:
            vm_push(vm, read_int32(vm));
            break;
        case OP_POP:
            vm_pop(vm, &a);
            break;
        case OP_DUP:
            if (vm_pop(vm, &a)) {
                vm_push(vm, a);
                vm_push(vm, a);
            }
            break;
        case OP_SWAP:
            if (vm_pop(vm, &b) && vm_pop(vm, &a)) {
                vm_push(vm, b);
                vm_push(vm, a);
            }
            break;
        case OP_OVER:
            if (vm->stack_ptr >= 2) {
                vm_push(vm, vm->stack[vm->stack_ptr - 2]);
            } else {
                vm->running = false;
            }
            break;
        case OP_ROT:
            if (vm->stack_ptr >= 3) {
                a = vm->stack[vm->stack_ptr - 3];
                b = vm->stack[vm->stack_ptr - 2];
                c = vm->stack[vm->stack_ptr - 1];
                vm->stack[vm->stack_ptr - 3] = b;
                vm->stack[vm->stack_ptr - 2] = c;
                vm->stack[vm->stack_ptr - 1] = a;
            } else {
                vm->running = false;
            }
            break;
        case OP_PICK:
            if (vm_pop(vm, &a)) {
                if (a >= 0 && a < vm->stack_ptr) {
                    vm_push(vm, vm->stack[vm->stack_ptr - 1 - a]);
                } else {
                    vm->running = false;
                }
            }
            break;
        case OP_ROLL:
            if (vm_pop(vm, &a)) {
                if (a > 0 && a < vm->stack_ptr) {
                    int real_idx = vm->stack_ptr - 1 - a;
                    int32_t val = vm->stack[real_idx];
                    for (int i = real_idx; i < vm->stack_ptr - 1; i++) {
                        vm->stack[i] = vm->stack[i+1];
                    }
                    vm->stack[vm->stack_ptr - 1] = val;
                } else if (a < 0 || a >= vm->stack_ptr) {
                    vm->running = false;
                }
            }
            break;
        case OP_ADD:
            if (vm_pop(vm, &b) && vm_pop(vm, &a)) vm_push(vm, a + b);
            break;
        case OP_SUB:
            if (vm_pop(vm, &b) && vm_pop(vm, &a)) vm_push(vm, a - b);
            break;
        case OP_MUL:
            if (vm_pop(vm, &b) && vm_pop(vm, &a)) vm_push(vm, a * b);
            break;
        case OP_DIV:
            if (vm_pop(vm, &b) && vm_pop(vm, &a)) {
                if (b == 0) vm->running = false;
                else vm_push(vm, a / b);
            }
            break;
        case OP_MOD:
            if (vm_pop(vm, &b) && vm_pop(vm, &a)) {
                if (b == 0) vm->running = false;
                else vm_push(vm, a % b);
            }
            break;
        case OP_INC:
            if (vm_pop(vm, &a)) vm_push(vm, a + 1);
            break;
        case OP_DEC:
            if (vm_pop(vm, &a)) vm_push(vm, a - 1);
            break;
        case OP_NEG:
            if (vm_pop(vm, &a)) vm_push(vm, -a);
            break;
        case OP_ABS:
            if (vm_pop(vm, &a)) vm_push(vm, a < 0 ? -a : a);
            break;
        case OP_DIVMOD:
            if (vm_pop(vm, &b) && vm_pop(vm, &a)) {
                if (b == 0) vm->running = false;
                else {
                    vm_push(vm, a / b);
                    vm_push(vm, a % b);
                }
            }
            break;
        case OP_MIN:
            if (vm_pop(vm, &b) && vm_pop(vm, &a)) vm_push(vm, a < b ? a : b);
            break;
        case OP_MAX:
            if (vm_pop(vm, &b) && vm_pop(vm, &a)) vm_push(vm, a > b ? a : b);
            break;
        case OP_AND:
            if (vm_pop(vm, &b) && vm_pop(vm, &a)) vm_push(vm, a & b);
            break;
        case OP_OR:
            if (vm_pop(vm, &b) && vm_pop(vm, &a)) vm_push(vm, a | b);
            break;
        case OP_XOR:
            if (vm_pop(vm, &b) && vm_pop(vm, &a)) vm_push(vm, a ^ b);
            break;
        case OP_NOT:
            if (vm_pop(vm, &a)) vm_push(vm, ~a);
            break;
        case OP_SHL:
            if (vm_pop(vm, &b) && vm_pop(vm, &a)) vm_push(vm, a << (b % 32));
            break;
        case OP_SHR:
            if (vm_pop(vm, &b) && vm_pop(vm, &a)) {
                uint32_t ua = (uint32_t)a;
                vm_push(vm, (int32_t)(ua >> (b % 32)));
            }
            break;
        case OP_SAR:
            if (vm_pop(vm, &b) && vm_pop(vm, &a)) vm_push(vm, a >> (b % 32));
            break;
        case OP_EQ:
            if (vm_pop(vm, &b) && vm_pop(vm, &a)) vm_push(vm, a == b ? 1 : 0);
            break;
        case OP_NEQ:
            if (vm_pop(vm, &b) && vm_pop(vm, &a)) vm_push(vm, a != b ? 1 : 0);
            break;
        case OP_LT:
            if (vm_pop(vm, &b) && vm_pop(vm, &a)) vm_push(vm, a < b ? 1 : 0);
            break;
        case OP_LTE:
            if (vm_pop(vm, &b) && vm_pop(vm, &a)) vm_push(vm, a <= b ? 1 : 0);
            break;
        case OP_GT:
            if (vm_pop(vm, &b) && vm_pop(vm, &a)) vm_push(vm, a > b ? 1 : 0);
            break;
        case OP_GTE:
            if (vm_pop(vm, &b) && vm_pop(vm, &a)) vm_push(vm, a >= b ? 1 : 0);
            break;
        case OP_JMP:
            vm->pc = read_uint32(vm);
            break;
        case OP_JZ:
            addr = read_uint32(vm);
            if (vm_pop(vm, &a) && a == 0) vm->pc = addr;
            break;
        case OP_JNZ:
            addr = read_uint32(vm);
            if (vm_pop(vm, &a) && a != 0) vm->pc = addr;
            break;
        case OP_CALL:
            addr = read_uint32(vm);
            if (vm->return_stack_ptr >= MAX_RETURN_STACK_SIZE) {
                fprintf(stderr, "Return stack overflow at PC 0x%08X\n", vm->pc);
                vm->running = false;
            } else {
                vm->return_stack[vm->return_stack_ptr++] = vm->pc;
                vm->pc = addr;
            }
            break;
        case OP_RET:
            if (vm->return_stack_ptr <= 0) {
                vm->running = false;
            } else {
                vm->pc = vm->return_stack[--vm->return_stack_ptr];
            }
            break;
        case OP_CALLSTACK:
            if (vm_pop(vm, &a)) {
                if (vm->return_stack_ptr >= MAX_RETURN_STACK_SIZE) {
                    fprintf(stderr, "Return stack overflow at PC 0x%08X\n", vm->pc);
                    vm->running = false;
                } else {
                    vm->return_stack[vm->return_stack_ptr++] = vm->pc;
                    vm->pc = (uint32_t)a;
                }
            }
            break;
        case OP_JMPSTACK:
            if (vm_pop(vm, &a)) vm->pc = (uint32_t)a;
            break;
        case OP_LOAD:
            addr = read_uint32(vm);
            vm_push(vm, read_mem32(vm, addr));
            break;
        case OP_STORE:
            addr = read_uint32(vm);
            if (vm_pop(vm, &a)) write_mem32(vm, addr, a);
            break;
        case OP_LOADI:
            if (vm_pop(vm, &a)) vm_push(vm, read_mem32(vm, (uint32_t)a));
            break;
        case OP_STOREI:
            if (vm_pop(vm, &a) && vm_pop(vm, &b)) write_mem32(vm, (uint32_t)a, b); // [val, addr]
            break;
        case OP_PUSHR:
            if (vm_pop(vm, &a)) {
                if (vm->loop_stack_ptr >= MAX_LOOP_STACK_SIZE) {
                    fprintf(stderr, "Loop stack overflow at PC 0x%08X\n", vm->pc);
                    vm->running = false;
                }
                else vm->loop_stack[vm->loop_stack_ptr++] = a;
            }
            break;
        case OP_POPR:
            if (vm->loop_stack_ptr <= 0) vm->running = false;
            else vm_push(vm, vm->loop_stack[--vm->loop_stack_ptr]);
            break;
        case OP_PEEKR:
            if (vm->loop_stack_ptr <= 0) vm->running = false;
            else vm_push(vm, vm->loop_stack[vm->loop_stack_ptr - 1]);
            break;
        case OP_PEEKR2:
            if (vm->loop_stack_ptr <= 1) vm->running = false;
            else vm_push(vm, vm->loop_stack[vm->loop_stack_ptr - 2]);
            break;
        case OP_FRAME:
            // [v_{n-1} ... v_0, n] → []  (v_0 is top of stack → local 0)
            if (vm_pop(vm, &a)) {
                if (a < 0 || vm->fp + 1 + a >= MAX_LOCALS_SIZE) {
                    vm->running = false;
                } else if (vm->stack_ptr < a) {
                    fprintf(stderr, "Stack underflow at PC 0x%08X (FRAME needs %d)\n",
                            vm->pc, a);
                    vm->running = false;
                } else {
                    int32_t old_fp = vm->fp;
                    int32_t base = old_fp + 1;
                    vm->locals[base] = old_fp;
                    for (int32_t i = 0; i < a; i++) {
                        int32_t val;
                        vm_pop(vm, &val); // guarded by stack_ptr check above
                        // First pop (top) → local 0 at base+a
                        vm->locals[base + a - i] = val;
                    }
                    vm->fp = base + a;
                }
            }
            break;
        case OP_UNFRAME:
            if (vm_pop(vm, &a)) {
                if (a < 0 || vm->fp - a < 0 || vm->fp - a >= MAX_LOCALS_SIZE) vm->running = false;
                else {
                    vm->fp = vm->locals[vm->fp - a];
                }
            }
            break;
        case OP_LOCALGET:
            if (vm_pop(vm, &a)) {
                int32_t base = vm->fp; // We need to be careful with Go's implementation
                int32_t idx = base - a;
                if (idx < 0 || idx >= MAX_LOCALS_SIZE) vm->running = false;
                else vm_push(vm, vm->locals[idx]);
            }
            break;
        case OP_LOCALSET:
            if (vm_pop(vm, &b) && vm_pop(vm, &a)) { // [val, offset] (b=offset, a=val)
                int32_t base = vm->fp;
                int32_t idx = base - b;
                if (idx < 0 || idx >= MAX_LOCALS_SIZE) vm->running = false;
                else vm->locals[idx] = a;
            }
            break;
        case OP_OUT:
            if (vm_pop(vm, &b) && vm_pop(vm, &a)) { // format, value
                if (vm->output_handler) {
                    vm->output_handler(a, b);
                } else {
                    if (b == 0) printf("%d", a);
                    else if (b == 1) printf("%c", (char)a);
                    fflush(stdout);
                }
            }
            break;
        case OP_HALT:
            vm->halted = true;
            vm->running = false;
            break;
        case OP_YIELD:
            vm->running = false;
            break;
        default:
            fprintf(stderr, "Unknown opcode 0x%02X at 0x%08X\n", op, vm->pc - 1);
            vm->running = false;
            break;
    }
    
    return vm->running;
}


uint32_t vm_get_pc(const VM* vm) {
    return vm ? vm->pc : 0;
}

int vm_get_stack_count(const VM* vm) {
    return vm ? vm->stack_ptr : 0;
}

void vm_get_stack_copy(const VM* vm, int32_t* out, int max_count) {
    if (!vm || !out || max_count <= 0) return;
    int n = vm->stack_ptr;
    if (n > max_count) n = max_count;
    for (int i = 0; i < n; i++) {
        out[i] = vm->stack[i];
    }
}

bool vm_yielded(const VM* vm) {
    return vm && vm->last_opcode == OP_YIELD;
}

void vm_clear_yield(VM* vm) {
    if (vm) vm->last_opcode = 0xFF;
}

void vm_run(VM* vm) {
    while (vm->running && !vm->halted) {
        if (!vm_tick(vm)) {
            break;
        }
    }
}
