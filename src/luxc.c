#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "compiler.h"
#include "vm.h"

int main(int argc, char** argv) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <program.lux>\n", argv[0]);
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
    
    char* source = (char*)malloc(fsize + 1);
    if (!source) {
        fprintf(stderr, "Memory allocation failed\n");
        fclose(f);
        return 1;
    }
    
    if (fread(source, 1, fsize, f) != (size_t)fsize) {
        fprintf(stderr, "Error reading file\n");
        free(source);
        fclose(f);
        return 1;
    }
    source[fsize] = '\0';
    fclose(f);
    
    size_t code_len = 0;
    uint8_t* bytecode = compile_source(source, HEADLESS_BASE_ADDRESS, &code_len, false);
    
    if (!bytecode) {
        fprintf(stderr, "Compilation failed\n");
        free(source);
        return 1;
    }
    
    char out_filename[256];
    strncpy(out_filename, filename, sizeof(out_filename) - 5);
    out_filename[sizeof(out_filename) - 5] = '\0';
    
    char* dot = strrchr(out_filename, '.');
    if (dot) *dot = '\0';
    strcat(out_filename, ".bin");
    
    FILE* out_f = fopen(out_filename, "wb");
    if (!out_f) {
        fprintf(stderr, "Error opening output file: %s\n", out_filename);
        free(bytecode);
        free(source);
        return 1;
    }
    
    fwrite(bytecode, 1, code_len, out_f);
    fclose(out_f);
    
    printf("Compiled %s to %s (%zu bytes)\n", filename, out_filename, code_len);
    
    free(bytecode);
    free(source);
    return 0;
}
