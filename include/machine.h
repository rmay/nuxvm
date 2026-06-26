#ifndef MACHINE_H
#define MACHINE_H

#include "vm.h"
#include "system.h"

typedef struct Machine {
    VM* cpu;
    System* system;
} Machine;

Machine* machine_create(const uint8_t* program, uint32_t program_size, uint32_t base_address, uint32_t mem_size, bool trace);
void machine_free(Machine* machine);
bool machine_tick(Machine* machine);

#endif // MACHINE_H
