#include "machine.h"
#include <stdlib.h>
#include <stdio.h>

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
    while (machine->cpu->running && !vm_yielded(machine->cpu) && !machine->system->yielded && cycles < 1000000) {
        vm_tick(machine->cpu);
        if (machine->cpu->halted) {
            break;
        }
        if (!machine->cpu->running && !vm_yielded(machine->cpu) && !machine->system->yielded) {
            return false; // Runtime error
        }
        cycles++;
    }
    machine->system->last_tick_cycles = cycles;

    // Tick children after the parent's slice, including the tick where the
    // parent halts (Go: Machine.Tick ticks childMachines before returning).
    for (int i = 0; i < SYS_MAX_CHILD_VMS; i++) {
        if (machine->system->child_vms[i]) {
            machine_tick(machine->system->child_vms[i]);
        }
    }

    return !machine->cpu->halted;
}
