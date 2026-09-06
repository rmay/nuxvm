#include "vm.h"
#include "opcodes.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

const char* nux_trap_name(NuxTrap trap) {
    switch (trap) {
        case TRAP_NONE: return "none";
        case TRAP_EXEC_OUTSIDE_IMAGE: return "execute outside program";
        case TRAP_JUMP_OUTSIDE_IMAGE: return "jump/call outside program";
        case TRAP_PC_OUT_OF_BOUNDS: return "PC out of bounds";
        case TRAP_TRUNCATED_IMMEDIATE: return "truncated immediate";
        case TRAP_UNKNOWN_OPCODE: return "unknown opcode";
        case TRAP_STACK_UNDERFLOW: return "stack underflow";
        case TRAP_STACK_OVERFLOW: return "data stack overflow";
        case TRAP_RETURN_STACK_UNDERFLOW: return "return stack underflow";
        case TRAP_RETURN_STACK_OVERFLOW: return "return stack overflow";
        case TRAP_LOOP_STACK_UNDERFLOW: return "loop stack underflow";
        case TRAP_LOOP_STACK_OVERFLOW: return "loop stack overflow";
        case TRAP_PICK_RANGE: return "PICK index out of range";
        case TRAP_ROLL_RANGE: return "ROLL index out of range";
        case TRAP_FRAME_RANGE: return "frame index out of range";
        case TRAP_LOCAL_RANGE: return "local index out of range";
        case TRAP_DIVIDE_BY_ZERO: return "division by zero";
        case TRAP_UNALIGNED_READ: return "unaligned memory read";
        case TRAP_UNALIGNED_WRITE: return "unaligned memory write";
        case TRAP_READ_OUT_OF_BOUNDS: return "memory read out of bounds";
        case TRAP_WRITE_OUT_OF_BOUNDS: return "memory write out of bounds";
        case TRAP_WRITE_INTO_IMAGE: return "write into program";
        case TRAP_DEVICE_READ_FAILED: return "device read failed";
        case TRAP_NO_BUS: return "no bus";
        default: return "unknown trap";
    }
}

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
    vm->trap = TRAP_NONE;
    vm->reserved_memory_size = RESERVED_MEMORY_SIZE;
    vm->user_memory_start = base_address;
    vm->image_base = base_address;
    vm->image_end = base_address + program_size;
    vm->op_pc = base_address;
    vm->trace = trace;
    vm->fp = -1;
    
    return vm;
}

/* Record a trap and stop the machine. The cause is stored in vm->trap so it
 * is machine state, not merely a line on stderr. The first trap wins: a
 * stopped machine never re-faults, so vm->trap always names the original
 * cause. */
static void vm_fault(VM* vm, NuxTrap trap, const char* fmt, ...) {
    if (vm->trap == TRAP_NONE) vm->trap = (uint8_t)trap;
    fprintf(stderr, "Fault at PC 0x%08X SP:%d: ", vm->op_pc, vm->stack_ptr);
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fprintf(stderr, "\n");
    vm->running = false;
}

static bool in_image(const VM* vm, uint32_t addr) {
    return addr >= vm->image_base && addr < vm->image_end;
}

static bool check_exec_target(VM* vm, uint32_t addr) {
    if (!in_image(vm, addr)) {
        vm_fault(vm, TRAP_JUMP_OUTSIDE_IMAGE,
                 "jump/call outside program to 0x%08X (image 0x%08X-0x%08X)",
                 addr, vm->image_base, vm->image_end);
        return false;
    }
    return true;
}

static bool mem_in_range(const VM* vm, uint32_t addr) {
    return addr < vm->memory_size && vm->memory_size - addr >= 4;
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

bool vm_push(VM* vm, int32_t value) {
    if (vm->stack_ptr >= MAX_STACK_SIZE) {
        vm_fault(vm, TRAP_STACK_OVERFLOW, "data stack overflow");
        return false;
    }
    vm->stack[vm->stack_ptr++] = value;
    return true;
}

bool vm_pop(VM* vm, int32_t* value) {
    if (vm->stack_ptr <= 0) {
        vm_fault(vm, TRAP_STACK_UNDERFLOW, "stack underflow");
        return false;
    }
    *value = vm->stack[--vm->stack_ptr];
    return true;
}

/* ---- Operand guards -------------------------------------------------
 * Every opcode checks its operand count and ranges through these BEFORE
 * mutating any state, so a trapping instruction commits nothing. This
 * fault-atomicity is what makes a small-step semantics expressible: a
 * step either succeeds completely or leaves the state untouched. */

/* Data stack holds at least n operands. */
static bool need(VM* vm, int n) {
    if (vm->stack_ptr < n) {
        vm_fault(vm, TRAP_STACK_UNDERFLOW, "stack underflow (needs %d, SP:%d)",
                 n, vm->stack_ptr);
        return false;
    }
    return true;
}

/* Data stack has room to grow by n. */
static bool room(VM* vm, int n) {
    if (vm->stack_ptr > MAX_STACK_SIZE - n) {
        vm_fault(vm, TRAP_STACK_OVERFLOW, "data stack overflow");
        return false;
    }
    return true;
}

/* S(1) is the top of the data stack, S(2) the next one down. */
#define S(i) (vm->stack[vm->stack_ptr - (i)])

/* ---- Defined arithmetic ---------------------------------------------
 * NUX integers are 32-bit two's complement and every operation is total.
 * Arithmetic wraps modulo 2^32; C signed overflow is undefined, so the
 * wrapping ones are computed in uint32_t and converted back. Only
 * division by zero traps. */

static inline int32_t wrap_add(int32_t a, int32_t b) { return (int32_t)((uint32_t)a + (uint32_t)b); }
static inline int32_t wrap_sub(int32_t a, int32_t b) { return (int32_t)((uint32_t)a - (uint32_t)b); }
static inline int32_t wrap_mul(int32_t a, int32_t b) { return (int32_t)((uint32_t)a * (uint32_t)b); }
static inline int32_t wrap_neg(int32_t a)            { return (int32_t)(0u - (uint32_t)a); }
static inline int32_t wrap_abs(int32_t a)            { return a < 0 ? wrap_neg(a) : a; }

/* Truncating division. INT32_MIN / -1 overflows (and traps on x86), so it
 * is defined here to wrap: quotient INT32_MIN, remainder 0. Callers must
 * have already rejected b == 0. */
static inline int32_t div_trunc(int32_t a, int32_t b) {
    if (a == INT32_MIN && b == -1) return INT32_MIN;
    return a / b;
}
static inline int32_t mod_trunc(int32_t a, int32_t b) {
    if (a == INT32_MIN && b == -1) return 0;
    return a % b;
}

/* Shift counts are masked to 5 bits. C's % truncates toward zero, so a
 * negative count would yield a negative (undefined) shift distance; & 31
 * is total and matches the hardware most guests expect. */
static inline int32_t shl32(int32_t a, int32_t b) {
    return (int32_t)((uint32_t)a << (uint32_t)(b & 31));
}
static inline int32_t shr32(int32_t a, int32_t b) {
    return (int32_t)((uint32_t)a >> (uint32_t)(b & 31));
}
static inline int32_t sar32(int32_t a, int32_t b) {
    unsigned s = (unsigned)(b & 31);
    uint32_t r = (uint32_t)a >> s;
    /* Sign-fill explicitly; >> on a negative signed value is only
     * implementation-defined. s == 0 would make the shift below UB. */
    if (a < 0 && s != 0) r |= ~(uint32_t)0 << (32 - s);
    return (int32_t)r;
}

static bool fetch_u32(VM* vm, uint32_t* out) {
    if (vm->pc < vm->image_base || vm->pc >= vm->image_end ||
        vm->image_end - vm->pc < 4) {
        vm_fault(vm, TRAP_TRUNCATED_IMMEDIATE, "truncated immediate");
        return false;
    }
    uint32_t val = 0;
    val |= (uint32_t)vm->memory[vm->pc++] << 24;
    val |= (uint32_t)vm->memory[vm->pc++] << 16;
    val |= (uint32_t)vm->memory[vm->pc++] << 8;
    val |= (uint32_t)vm->memory[vm->pc++];
    *out = val;
    return true;
}

static bool fetch_i32(VM* vm, int32_t* out) {
    uint32_t u;
    if (!fetch_u32(vm, &u)) return false;
    *out = (int32_t)u;
    return true;
}

static bool is_device_addr(uint32_t addr) {
    /* SCI trap only. Framebuffer and app RAM are ordinary memory;
     * guests draw through /dev/draw, not MMIO. */
    return addr >= DEVICE_MEMORY_OFFSET &&
           addr < DEVICE_MEMORY_OFFSET + DEVICE_MEMORY_SIZE;
}


static bool write_mem32(VM* vm, uint32_t addr, int32_t val) {
    if (addr & 3) {
        vm_fault(vm, TRAP_UNALIGNED_WRITE, "unaligned memory write at 0x%08X", addr);
        return false;
    }
    if (!mem_in_range(vm, addr)) {
        vm_fault(vm, TRAP_WRITE_OUT_OF_BOUNDS, "memory write out of bounds at 0x%08X", addr);
        return false;
    }
    if (addr < vm->image_end && addr + 4 > vm->image_base) {
        vm_fault(vm, TRAP_WRITE_INTO_IMAGE,
                 "write into program at 0x%08X (image 0x%08X-0x%08X)",
                 addr, vm->image_base, vm->image_end);
        return false;
    }
    vm->memory[addr] = (val >> 24) & 0xFF;
    vm->memory[addr+1] = (val >> 16) & 0xFF;
    vm->memory[addr+2] = (val >> 8) & 0xFF;
    vm->memory[addr+3] = val & 0xFF;
    
    if (is_device_addr(addr)) {
        if (vm->bus && vm->bus->write) {
            vm->bus->write(vm->bus, addr, val);
        }
    }
    return true;
}

static bool read_mem32(VM* vm, uint32_t addr, int32_t* out) {
    if (addr & 3) {
        vm_fault(vm, TRAP_UNALIGNED_READ, "unaligned memory read at 0x%08X", addr);
        return false;
    }
    if (is_device_addr(addr)) {
        if (vm->bus && vm->bus->read) {
            bool success = false;
            int32_t val = vm->bus->read(vm->bus, addr, &success);
            if (success) {
                *out = val;
                return true;
            }
            vm_fault(vm, TRAP_DEVICE_READ_FAILED, "device read failed at 0x%08X", addr);
            return false;
        }
        vm_fault(vm, TRAP_NO_BUS, "no bus: device read at 0x%08X", addr);
        return false;
    }
    if (!mem_in_range(vm, addr)) {
        vm_fault(vm, TRAP_READ_OUT_OF_BOUNDS, "memory read out of bounds at 0x%08X", addr);
        return false;
    }
    uint32_t val = 0;
    val |= (uint32_t)vm->memory[addr] << 24;
    val |= (uint32_t)vm->memory[addr+1] << 16;
    val |= (uint32_t)vm->memory[addr+2] << 8;
    val |= (uint32_t)vm->memory[addr+3];
    *out = (int32_t)val;
    return true;
}

bool vm_tick(VM* vm) {
    if (!vm->running) return false;
    if (vm->pc < vm->image_base || vm->pc >= vm->image_end) {
        vm->op_pc = vm->pc;
        vm_fault(vm, TRAP_EXEC_OUTSIDE_IMAGE,
                 "execute outside program (image 0x%08X-0x%08X)",
                 vm->image_base, vm->image_end);
        return false;
    }
    if (vm->pc >= vm->memory_size) {
        vm->op_pc = vm->pc;
        vm_fault(vm, TRAP_PC_OUT_OF_BOUNDS, "PC out of bounds");
        return false;
    }
    
    vm->op_pc = vm->pc;
    uint8_t op = vm->memory[vm->pc++];
    vm->last_opcode = op;
    
    if (vm->trace) {
        fprintf(stderr, "TRACE PC:0x%08X OP:%s(%02X) SP:%d\n", vm->op_pc, opcode_name(op), op, vm->stack_ptr);
    }
    
    int32_t a, b;
    uint32_t addr;
    
    switch (op) {
        case OP_PUSH:
            if (room(vm, 1) && fetch_i32(vm, &a)) {
                vm->stack[vm->stack_ptr++] = a;
            }
            break;
        case OP_POP:
            if (need(vm, 1)) vm->stack_ptr--;
            break;
        case OP_DUP:
            if (need(vm, 1) && room(vm, 1)) {
                vm->stack[vm->stack_ptr] = S(1);
                vm->stack_ptr++;
            }
            break;
        case OP_SWAP:
            if (need(vm, 2)) {
                a = S(2);
                S(2) = S(1);
                S(1) = a;
            }
            break;
        case OP_OVER:
            if (need(vm, 2) && room(vm, 1)) {
                vm->stack[vm->stack_ptr] = S(2);
                vm->stack_ptr++;
            }
            break;
        case OP_ROT:
            if (need(vm, 3)) {
                a = S(3);
                S(3) = S(2);
                S(2) = S(1);
                S(1) = a;
            }
            break;
        case OP_PICK:
            /* Pops the index and pushes the picked value: net zero, so the
             * index slot is reused and no overflow check is needed. */
            if (need(vm, 1)) {
                a = S(1);
                int32_t depth = vm->stack_ptr - 1;
                if (a >= 0 && a < depth) {
                    S(1) = vm->stack[depth - 1 - a];
                } else {
                    vm_fault(vm, TRAP_PICK_RANGE, "PICK index %d out of range (SP:%d)", a, depth);
                }
            }
            break;
        case OP_ROLL:
            if (need(vm, 1)) {
                a = S(1);
                int32_t depth = vm->stack_ptr - 1;
                if (a < 0 || a >= depth) {
                    vm_fault(vm, TRAP_ROLL_RANGE, "ROLL index %d out of range (SP:%d)", a, depth);
                } else {
                    vm->stack_ptr--;            /* drop the index */
                    if (a > 0) {                /* ROLL 0 is a no-op */
                        int32_t src = vm->stack_ptr - 1 - a;
                        int32_t val = vm->stack[src];
                        for (int32_t i = src; i < vm->stack_ptr - 1; i++) {
                            vm->stack[i] = vm->stack[i+1];
                        }
                        vm->stack[vm->stack_ptr - 1] = val;
                    }
                }
            }
            break;
        case OP_ADD:
            if (need(vm, 2)) { a = wrap_add(S(2), S(1)); vm->stack_ptr--; S(1) = a; }
            break;
        case OP_SUB:
            if (need(vm, 2)) { a = wrap_sub(S(2), S(1)); vm->stack_ptr--; S(1) = a; }
            break;
        case OP_MUL:
            if (need(vm, 2)) { a = wrap_mul(S(2), S(1)); vm->stack_ptr--; S(1) = a; }
            break;
        case OP_DIV:
            if (need(vm, 2)) {
                a = S(2); b = S(1);
                if (b == 0) vm_fault(vm, TRAP_DIVIDE_BY_ZERO, "division by zero");
                else { vm->stack_ptr--; S(1) = div_trunc(a, b); }
            }
            break;
        case OP_MOD:
            if (need(vm, 2)) {
                a = S(2); b = S(1);
                if (b == 0) vm_fault(vm, TRAP_DIVIDE_BY_ZERO, "division by zero");
                else { vm->stack_ptr--; S(1) = mod_trunc(a, b); }
            }
            break;
        case OP_INC:
            if (need(vm, 1)) S(1) = wrap_add(S(1), 1);
            break;
        case OP_DEC:
            if (need(vm, 1)) S(1) = wrap_sub(S(1), 1);
            break;
        case OP_NEG:
            if (need(vm, 1)) S(1) = wrap_neg(S(1));
            break;
        case OP_ABS:
            if (need(vm, 1)) S(1) = wrap_abs(S(1));
            break;
        case OP_DIVMOD:
            /* Pops 2, pushes 2: net zero, no overflow check needed. */
            if (need(vm, 2)) {
                a = S(2); b = S(1);
                if (b == 0) vm_fault(vm, TRAP_DIVIDE_BY_ZERO, "division by zero");
                else { S(2) = div_trunc(a, b); S(1) = mod_trunc(a, b); }
            }
            break;
        case OP_MIN:
            if (need(vm, 2)) { a = S(2) < S(1) ? S(2) : S(1); vm->stack_ptr--; S(1) = a; }
            break;
        case OP_MAX:
            if (need(vm, 2)) { a = S(2) > S(1) ? S(2) : S(1); vm->stack_ptr--; S(1) = a; }
            break;
        case OP_AND:
            if (need(vm, 2)) { a = S(2) & S(1); vm->stack_ptr--; S(1) = a; }
            break;
        case OP_OR:
            if (need(vm, 2)) { a = S(2) | S(1); vm->stack_ptr--; S(1) = a; }
            break;
        case OP_XOR:
            if (need(vm, 2)) { a = S(2) ^ S(1); vm->stack_ptr--; S(1) = a; }
            break;
        case OP_NOT:
            if (need(vm, 1)) S(1) = ~S(1);
            break;
        case OP_SHL:
            if (need(vm, 2)) { a = shl32(S(2), S(1)); vm->stack_ptr--; S(1) = a; }
            break;
        case OP_SHR:
            if (need(vm, 2)) { a = shr32(S(2), S(1)); vm->stack_ptr--; S(1) = a; }
            break;
        case OP_SAR:
            if (need(vm, 2)) { a = sar32(S(2), S(1)); vm->stack_ptr--; S(1) = a; }
            break;
        case OP_EQ:
            if (need(vm, 2)) { a = S(2) == S(1); vm->stack_ptr--; S(1) = a; }
            break;
        case OP_NEQ:
            if (need(vm, 2)) { a = S(2) != S(1); vm->stack_ptr--; S(1) = a; }
            break;
        case OP_LT:
            if (need(vm, 2)) { a = S(2) < S(1); vm->stack_ptr--; S(1) = a; }
            break;
        case OP_LTE:
            if (need(vm, 2)) { a = S(2) <= S(1); vm->stack_ptr--; S(1) = a; }
            break;
        case OP_GT:
            if (need(vm, 2)) { a = S(2) > S(1); vm->stack_ptr--; S(1) = a; }
            break;
        case OP_GTE:
            if (need(vm, 2)) { a = S(2) >= S(1); vm->stack_ptr--; S(1) = a; }
            break;
        case OP_JMP:
            if (fetch_u32(vm, &addr) && check_exec_target(vm, addr)) vm->pc = addr;
            break;
        case OP_JZ:
            /* The target is fetched and range-checked before the condition
             * is popped, so a bad target traps even on a not-taken branch. */
            if (fetch_u32(vm, &addr) && check_exec_target(vm, addr) && need(vm, 1)) {
                a = S(1);
                vm->stack_ptr--;
                if (a == 0) vm->pc = addr;
            }
            break;
        case OP_JNZ:
            if (fetch_u32(vm, &addr) && check_exec_target(vm, addr) && need(vm, 1)) {
                a = S(1);
                vm->stack_ptr--;
                if (a != 0) vm->pc = addr;
            }
            break;
        case OP_CALL:
            if (fetch_u32(vm, &addr)) {
                if (vm->return_stack_ptr >= MAX_RETURN_STACK_SIZE) {
                    vm_fault(vm, TRAP_RETURN_STACK_OVERFLOW, "return stack overflow");
                } else if (check_exec_target(vm, addr)) {
                    vm->return_stack[vm->return_stack_ptr++] = vm->pc;
                    vm->pc = addr;
                }
            }
            break;
        case OP_RET:
            if (vm->return_stack_ptr <= 0) {
                vm_fault(vm, TRAP_RETURN_STACK_UNDERFLOW, "return stack underflow");
            } else {
                addr = vm->return_stack[vm->return_stack_ptr - 1];
                if (check_exec_target(vm, addr)) {
                    vm->return_stack_ptr--;
                    vm->pc = addr;
                }
            }
            break;
        case OP_CALLSTACK:
            if (need(vm, 1)) {
                addr = (uint32_t)S(1);
                if (vm->return_stack_ptr >= MAX_RETURN_STACK_SIZE) {
                    vm_fault(vm, TRAP_RETURN_STACK_OVERFLOW, "return stack overflow");
                } else if (check_exec_target(vm, addr)) {
                    vm->stack_ptr--;
                    vm->return_stack[vm->return_stack_ptr++] = vm->pc;
                    vm->pc = addr;
                }
            }
            break;
        case OP_JMPSTACK:
            if (need(vm, 1)) {
                addr = (uint32_t)S(1);
                if (check_exec_target(vm, addr)) {
                    vm->stack_ptr--;
                    vm->pc = addr;
                }
            }
            break;
        case OP_LOAD:
            if (room(vm, 1) && fetch_u32(vm, &addr)) {
                int32_t v;
                if (read_mem32(vm, addr, &v)) vm->stack[vm->stack_ptr++] = v;
            }
            break;
        case OP_STORE:
            if (fetch_u32(vm, &addr) && need(vm, 1)) {
                if (write_mem32(vm, addr, S(1))) vm->stack_ptr--;
            }
            break;
        case OP_LOADI:
            if (need(vm, 1)) {
                int32_t v;
                if (read_mem32(vm, (uint32_t)S(1), &v)) S(1) = v;
            }
            break;
        case OP_STOREI:
            /* [val, addr] with addr on top. */
            if (need(vm, 2)) {
                if (write_mem32(vm, (uint32_t)S(1), S(2))) vm->stack_ptr -= 2;
            }
            break;
        case OP_PUSHR:
            if (need(vm, 1)) {
                if (vm->loop_stack_ptr >= MAX_LOOP_STACK_SIZE) {
                    vm_fault(vm, TRAP_LOOP_STACK_OVERFLOW, "loop stack overflow");
                } else {
                    vm->loop_stack[vm->loop_stack_ptr++] = S(1);
                    vm->stack_ptr--;
                }
            }
            break;
        case OP_POPR:
            if (vm->loop_stack_ptr <= 0) {
                vm_fault(vm, TRAP_LOOP_STACK_UNDERFLOW, "loop stack underflow");
            } else if (room(vm, 1)) {
                vm->stack[vm->stack_ptr++] = vm->loop_stack[--vm->loop_stack_ptr];
            }
            break;
        case OP_PEEKR:
            if (vm->loop_stack_ptr <= 0) {
                vm_fault(vm, TRAP_LOOP_STACK_UNDERFLOW, "loop stack underflow");
            } else if (room(vm, 1)) {
                vm->stack[vm->stack_ptr++] = vm->loop_stack[vm->loop_stack_ptr - 1];
            }
            break;
        case OP_PEEKR2:
            if (vm->loop_stack_ptr <= 1) {
                vm_fault(vm, TRAP_LOOP_STACK_UNDERFLOW, "loop stack underflow (PEEKR2)");
            } else if (room(vm, 1)) {
                vm->stack[vm->stack_ptr++] = vm->loop_stack[vm->loop_stack_ptr - 2];
            }
            break;
        case OP_FRAME:
            /* [v_{n-1} ... v_0, n] -> []  (v_0, the top, becomes local 0).
             * Locals live in locals[], not on the data stack: the saved FP
             * goes at base = old_fp + 1 and the n values above it, so
             * local 0 sits at the new fp and LOCALGET indexes downward. */
            if (need(vm, 1)) {
                a = S(1);
                /* 64-bit so a huge n cannot overflow the bound check.
                 * Both ends are bounded: checking only `top` left the
                 * low end open, and a corrupt fp then made base negative
                 * and locals[base] an out-of-bounds write. */
                int64_t base64 = (int64_t)vm->fp + 1;
                int64_t top = base64 + (int64_t)a;
                if (a < 0 || base64 < 0 || top >= MAX_LOCALS_SIZE) {
                    vm_fault(vm, TRAP_FRAME_RANGE, "frame overflow (n=%d fp=%d)", a, vm->fp);
                } else if (vm->stack_ptr - 1 < a) {
                    vm_fault(vm, TRAP_STACK_UNDERFLOW, "stack underflow (FRAME needs %d)", a);
                } else {
                    int32_t old_fp = vm->fp;
                    int32_t base = old_fp + 1;
                    vm->stack_ptr--;               /* drop n */
                    vm->locals[base] = old_fp;
                    for (int32_t i = 0; i < a; i++) {
                        /* First pop (top of stack) becomes local 0. */
                        vm->locals[base + a - i] = S(1);
                        vm->stack_ptr--;
                    }
                    vm->fp = base + a;
                }
            }
            break;
        case OP_UNFRAME:
            if (need(vm, 1)) {
                a = S(1);
                int64_t idx = (int64_t)vm->fp - (int64_t)a;
                if (a < 0 || idx < 0 || idx >= MAX_LOCALS_SIZE) {
                    vm_fault(vm, TRAP_FRAME_RANGE, "bad UNFRAME n=%d fp=%d", a, vm->fp);
                } else {
                    /* The saved-FP slot is an ordinary local, so a guest can
                     * overwrite it with LOCALSET. Restoring it unchecked let
                     * fp leave [-1, MAX_LOCALS_SIZE) entirely, which broke
                     * the machine's representation invariant and handed the
                     * next FRAME an out-of-bounds index. Validate it. */
                    int32_t restored = vm->locals[idx];
                    if (restored < -1 || restored >= MAX_LOCALS_SIZE) {
                        vm_fault(vm, TRAP_FRAME_RANGE,
                                 "UNFRAME restored an invalid frame pointer %d", restored);
                    } else {
                        vm->stack_ptr--;
                        vm->fp = restored;
                    }
                }
            }
            break;
        case OP_LOCALGET:
            /* Pops the offset and pushes the local: net zero. */
            if (need(vm, 1)) {
                a = S(1);
                int64_t idx = (int64_t)vm->fp - (int64_t)a;
                if (idx < 0 || idx >= MAX_LOCALS_SIZE) {
                    vm_fault(vm, TRAP_LOCAL_RANGE, "LOCALGET offset %d out of range", a);
                } else {
                    S(1) = vm->locals[idx];
                }
            }
            break;
        case OP_LOCALSET:
            /* [val, offset] with offset on top. */
            if (need(vm, 2)) {
                b = S(1);
                int64_t idx = (int64_t)vm->fp - (int64_t)b;
                if (idx < 0 || idx >= MAX_LOCALS_SIZE) {
                    vm_fault(vm, TRAP_LOCAL_RANGE, "LOCALSET offset %d out of range", b);
                } else {
                    vm->locals[idx] = S(2);
                    vm->stack_ptr -= 2;
                }
            }
            break;
        case OP_OUT:
            /* [value, format] with format on top. */
            if (need(vm, 2)) {
                a = S(2); b = S(1);
                vm->stack_ptr -= 2;
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
            vm_fault(vm, TRAP_UNKNOWN_OPCODE, "unknown opcode 0x%02X", op);
            break;
    }
    
    return vm->running;
}

#undef S

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
