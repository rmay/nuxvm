#ifndef VM_H
#define VM_H

#include <stdint.h>
#include <stdbool.h>

#define MAX_STACK_SIZE 8192
#define MAX_RETURN_STACK_SIZE 1024
#define MAX_LOCALS_SIZE 4096
#define MAX_LOOP_STACK_SIZE 1024

#define RESERVED_MEMORY_SIZE 0x4000
#define DEVICE_MEMORY_OFFSET 0x10000
#define DEVICE_MEMORY_SIZE 0x1000

#define HEADLESS_BASE_ADDRESS 0x11000
#define GRAPHICAL_BASE_ADDRESS 0x600000

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
    
    uint32_t reserved_memory_size;
    uint32_t user_memory_start;
    
    bool trace;
    
    int32_t locals[MAX_LOCALS_SIZE];
    int32_t fp; // Frame pointer
    
    int32_t loop_stack[MAX_LOOP_STACK_SIZE];
    int32_t loop_stack_ptr;
    
    uint8_t last_opcode;
    
    // Callbacks
    void (*output_handler)(int32_t value, int32_t format);
} VM;

// Initialize a new VM. memory_size must be >= base_address + program_size
VM* vm_create(const uint8_t* program, uint32_t program_size, uint32_t base_address, uint32_t memory_size, bool trace);

// Free the VM
void vm_free(VM* vm);

// Run the VM until halted or an error occurs
void vm_run(VM* vm);

// Execute a single instruction. Returns true if still running, false if halted/error.
bool vm_tick(VM* vm);

// Push a value to the main stack
bool vm_push(VM* vm, int32_t value);

// Pop a value from the main stack
bool vm_pop(VM* vm, int32_t* value);

#endif // VM_H
