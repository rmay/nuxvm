#ifndef BUS_H
#define BUS_H

#include <stdint.h>
#include <stdbool.h>

// Forward declaration (full definition comes from vm.h when needed)
struct VM;

// Bus defines the interface for communicating with external devices via MMIO.
typedef struct DeviceBus {
    // Read returns the value at the specified device address.
    int32_t (*read)(struct DeviceBus* bus, uint32_t address, bool* success);
    // Write sets the value at the specified device address.
    bool (*write)(struct DeviceBus* bus, uint32_t address, int32_t value);
    
    // User data for the implementation (e.g., pointer to System)
    void* user_data;
} DeviceBus;

#endif // BUS_H
