#ifndef BUS_H
#define BUS_H

#include <stdint.h>
#include <stdbool.h>

// Forward declaration (full definition comes from vm.h when needed)
struct VM;

// DeviceBus is the SCI trap: LOAD/STORE in the 0x10000 band reach the
// System so VFS syscalls can run. It is not a Varvara-style device HAL.
typedef struct DeviceBus {
    int32_t (*read)(struct DeviceBus* bus, uint32_t address, bool* success);
    bool (*write)(struct DeviceBus* bus, uint32_t address, int32_t value);
    
    // User data for the implementation (e.g., pointer to System)
    void* user_data;
} DeviceBus;

#endif // BUS_H
