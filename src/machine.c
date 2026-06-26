#include "machine.h"
#include <stdlib.h>
#include <stdio.h>

static uint32_t get_vector_cb(System* sys, int index) {
    if (!sys->vm_ptr) return 0;
    VM* vm = (VM*)sys->vm_ptr;
    // Assuming vector addresses are mapped to DEVICE_MEMORY_OFFSET + index * 16.
    // In vm.c, it's just a memory read if we don't intercept it.
    // We'll just read from VM memory at that location.
    uint32_t addr = DEVICE_MEMORY_OFFSET + (index * 16);
    if (addr + 4 > vm->memory_size) return 0;
    uint32_t val = 0;
    val |= (uint32_t)vm->memory[addr] << 24;
    val |= (uint32_t)vm->memory[addr+1] << 16;
    val |= (uint32_t)vm->memory[addr+2] << 8;
    val |= (uint32_t)vm->memory[addr+3];
    return val;
}

static void set_vector_cb(System* sys, int index, uint32_t addr) {
    if (!sys->vm_ptr) return;
    VM* vm = (VM*)sys->vm_ptr;
    uint32_t mem_addr = DEVICE_MEMORY_OFFSET + (index * 16);
    if (mem_addr + 4 > vm->memory_size) return;
    vm->memory[mem_addr] = (addr >> 24) & 0xFF;
    vm->memory[mem_addr+1] = (addr >> 16) & 0xFF;
    vm->memory[mem_addr+2] = (addr >> 8) & 0xFF;
    vm->memory[mem_addr+3] = addr & 0xFF;
}

Machine* machine_create(const uint8_t* program, uint32_t program_size, uint32_t base_address, uint32_t mem_size, bool trace) {
    Machine* machine = (Machine*)calloc(1, sizeof(Machine));
    if (!machine) return NULL;

    machine->cpu = vm_create(program, program_size, base_address, mem_size, trace);
    if (!machine->cpu) {
        free(machine);
        return NULL;
    }

    machine->system = system_create();
    if (!machine->system) {
        vm_free(machine->cpu);
        free(machine);
        return NULL;
    }

    system_set_memory(machine->system, machine->cpu->memory, machine->cpu->memory_size);
    system_set_vector_callbacks(machine->system, get_vector_cb, set_vector_cb, machine->cpu);

    // Attach system bus to VM
    // We need to implement this in vm.h and vm.c
    vm_set_bus(machine->cpu, &machine->system->bus);

    return machine;
}

void machine_free(Machine* machine) {
    if (machine) {
        if (machine->system) system_free(machine->system);
        if (machine->cpu) vm_free(machine->cpu);
        free(machine);
    }
}

bool machine_tick(Machine* machine) {
    if (!machine || !machine->cpu) return false;

    if (machine->cpu->halted) return false;

    machine->cpu->running = true;
    vm_clear_yield(machine->cpu);
    machine->system->yielded = false;

    int cycles = 0;
    while (machine->cpu->running && !vm_yielded(machine->cpu) && !machine->system->yielded && cycles < 100000) {
        vm_tick(machine->cpu);
        if (machine->cpu->halted) {
            return false;
        }
        if (!machine->cpu->running && !vm_yielded(machine->cpu) && !machine->system->yielded) {
            return false; // Runtime error
        }
        cycles++;
    }

    return !machine->cpu->halted;
}
