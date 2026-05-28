#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "vm.h"

int main(int argc, char** argv) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <program.bin>\n", argv[0]);
        return 1;
    }
    
    const char* filename = argv[1];
    FILE* f = fopen(filename, "rb");
    if (!f) {
        fprintf(stderr, "Error opening file: %s\n", filename);
        return 1;
    }
    
    fseek(f, 0, SEEK_END);
    long fsize = ftell(f);
    fseek(f, 0, SEEK_SET);
    
    uint8_t* program = (uint8_t*)malloc(fsize);
    if (!program) {
        fprintf(stderr, "Memory allocation failed\n");
        fclose(f);
        return 1;
    }
    
    if (fread(program, 1, fsize, f) != (size_t)fsize) {
        fprintf(stderr, "Error reading file\n");
        free(program);
        fclose(f);
        return 1;
    }
    fclose(f);
    
    // 32MB Total memory, program loaded at HEADLESS_BASE_ADDRESS
    uint32_t total_memory = 32 * 1024 * 1024;
    
    VM* vm = vm_create(program, fsize, HEADLESS_BASE_ADDRESS, total_memory, false);
    if (!vm) {
        fprintf(stderr, "Failed to create VM\n");
        free(program);
        return 1;
    }
    
    vm_run(vm);
    
    vm_free(vm);
    free(program);
    return 0;
}
