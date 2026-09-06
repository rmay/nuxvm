#ifndef VM_H
#define VM_H

#include <stdint.h>
#include <stdbool.h>
#include "bus.h"
#include "memory_map.h"

/* Machine bounds. These are the real, normative sizes (docs/semantics.md
 * section 2). They are overridable only so the CBMC harness in verify/ can
 * instantiate a small model of the same machine -- the proofs are about the
 * shape of the code, which does not depend on the constants, and a 8192-slot
 * stack of nondeterministic words is not tractable for a solver. Nothing in
 * the shipping build ever overrides them. */
#ifndef MAX_STACK_SIZE
#define MAX_STACK_SIZE 8192
#endif
#ifndef MAX_RETURN_STACK_SIZE
#define MAX_RETURN_STACK_SIZE 1024
#endif
#ifndef MAX_LOCALS_SIZE
#define MAX_LOCALS_SIZE 4096
#endif
#ifndef MAX_LOOP_STACK_SIZE
#define MAX_LOOP_STACK_SIZE 1024
#endif

#define RESERVED_MEMORY_SIZE 0x4000
#define DEVICE_MEMORY_OFFSET MM_DEVICE_BASE
#define DEVICE_MEMORY_SIZE (MM_DEVICE_END - MM_DEVICE_BASE)

#define HEADLESS_BASE_ADDRESS MM_HEADLESS_CODE_BASE
#define GRAPHICAL_BASE_ADDRESS MM_GRAPHICAL_CODE_BASE

/* Size of the host buffer covering guest addresses [0, size). Graphical
 * ROMs write reserved bands via STOREI, so they get MM_TOTAL_MEMORY.
 * Headless ROMs only need the loaded image (and everything below it). */
static inline uint32_t nux_guest_memory_size(uint32_t base_address, uint32_t program_size) {
    uint64_t need = (uint64_t)base_address + (uint64_t)program_size;
    if (base_address >= MM_GRAPHICAL_CODE_BASE && need < (uint64_t)MM_TOTAL_MEMORY) {
        need = MM_TOTAL_MEMORY;
    }
    if (need > 0xffffffffu) {
        need = 0xffffffffu;
    }
    return (uint32_t)need;
}

/* Trap causes. A trap is machine state, not just a message on stderr:
 * vm_fault() records the cause here so "the machine faulted, and why" is a
 * proposition a caller (or a proof) can state. TRAP_NONE means the machine
 * has not faulted -- it is either still running, halted, or yielded. */
typedef enum {
    TRAP_NONE = 0,
    TRAP_EXEC_OUTSIDE_IMAGE,
    TRAP_JUMP_OUTSIDE_IMAGE,
    TRAP_PC_OUT_OF_BOUNDS,
    TRAP_TRUNCATED_IMMEDIATE,
    TRAP_UNKNOWN_OPCODE,
    TRAP_STACK_UNDERFLOW,
    TRAP_STACK_OVERFLOW,
    TRAP_RETURN_STACK_UNDERFLOW,
    TRAP_RETURN_STACK_OVERFLOW,
    TRAP_LOOP_STACK_UNDERFLOW,
    TRAP_LOOP_STACK_OVERFLOW,
    TRAP_PICK_RANGE,
    TRAP_ROLL_RANGE,
    TRAP_FRAME_RANGE,
    TRAP_LOCAL_RANGE,
    TRAP_DIVIDE_BY_ZERO,
    TRAP_UNALIGNED_READ,
    TRAP_UNALIGNED_WRITE,
    TRAP_READ_OUT_OF_BOUNDS,
    TRAP_WRITE_OUT_OF_BOUNDS,
    TRAP_WRITE_INTO_IMAGE,
    TRAP_DEVICE_READ_FAILED,
    TRAP_NO_BUS,
    TRAP__COUNT
} NuxTrap;

/* Human-readable name for a trap cause. */
const char* nux_trap_name(NuxTrap trap);

typedef struct {
    int32_t stack[MAX_STACK_SIZE];
    int32_t stack_ptr; // Points to the next free slot
    
    uint32_t return_stack[MAX_RETURN_STACK_SIZE];
    int32_t return_stack_ptr;
    
    uint8_t* memory;
    uint32_t memory_size;
    
    uint32_t pc;
    bool running;
    bool halted;
    uint8_t trap; // NuxTrap: cause of the fault that stopped the machine
    
    uint32_t reserved_memory_size;
    uint32_t user_memory_start;

    // Guest program image [image_base, image_end). Execution and writes
    // into this range fault; reads (T" strings) are allowed.
    uint32_t image_base;
    uint32_t image_end;
    uint32_t op_pc; // address of the opcode being executed (fault reports)
    
    bool trace;
    
    int32_t locals[MAX_LOCALS_SIZE];
    int32_t fp; // Frame pointer: index of saved-FP slot for current frame, -1 if none

    int32_t loop_stack[MAX_LOOP_STACK_SIZE];
    int32_t loop_stack_ptr;

    uint8_t last_opcode;

    // SCI result register, read via SCI_PORT (the VFS syscall trap).
    int32_t sci_result;

    // Callbacks
    void (*output_handler)(int32_t value, int32_t format);

    DeviceBus* bus;
} VM;

// Initialize a new VM. memory_size must be >= base_address + program_size
VM* vm_create(const uint8_t* program, uint32_t program_size, uint32_t base_address, uint32_t memory_size, bool trace);

// Free the VM
void vm_free(VM* vm);

// Set device bus
void vm_set_bus(VM* vm, DeviceBus* bus);

// Run the VM until halted or an error occurs
void vm_run(VM* vm);

// Execute a single instruction. Returns true if still running, false if halted/error.
bool vm_tick(VM* vm);

// Push a value to the main stack
bool vm_push(VM* vm, int32_t value);

// Pop a value from the main stack
bool vm_pop(VM* vm, int32_t* value);

// Introspection helpers (debug/trace CLI)
uint32_t vm_get_pc(const VM* vm);
int vm_get_stack_count(const VM* vm);
void vm_get_stack_copy(const VM* vm, int32_t* out, int max_count);

// Yield state: OP_YIELD sets running=false and records yield via last_opcode.
bool vm_yielded(const VM* vm);
void vm_clear_yield(VM* vm);

#endif // VM_H
