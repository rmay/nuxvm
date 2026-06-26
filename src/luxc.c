#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "compiler.h"
#include "vm.h"

int main(int argc, char** argv) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s [-target graphical|headless] [-o out.bin] <program.lux>\n", argv[0]);
        return 1;
    }
    
    int base_address = HEADLESS_BASE_ADDRESS;
    const char* filename = NULL;
    const char* out_filename_arg = NULL;
    
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-target") == 0 && i + 1 < argc) {
            if (strcmp(argv[i+1], "graphical") == 0) {
                base_address = GRAPHICAL_BASE_ADDRESS;
            }
            i++;
        } else if (strcmp(argv[i], "-o") == 0 && i + 1 < argc) {
            out_filename_arg = argv[i+1];
            i++;
        } else {
            filename = argv[i];
        }
    }
    
    if (!filename) {
        fprintf(stderr, "Error: no input file specified\n");
        return 1;
    }

    FILE* f = fopen(filename, "rb");
    if (!f) {
        fprintf(stderr, "Error opening file: %s\n", filename);
        return 1;
    }
    
    if (fseek(f, 0, SEEK_END) != 0) {
        fprintf(stderr, "Error seeking file: %s\n", filename);
        fclose(f);
        return 1;
    }
    long fsize = ftell(f);
    if (fsize < 0) {
        fprintf(stderr, "Error determining file size: %s\n", filename);
        fclose(f);
        return 1;
    }
    fseek(f, 0, SEEK_SET);

    char* source = (char*)malloc((size_t)fsize + 1);
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
    uint8_t* bytecode = compile_source(source, base_address, &code_len, false);
    
    if (!bytecode) {
        fprintf(stderr, "Compilation failed\n");
        free(source);
        return 1;
    }
    
    char out_filename[256];
    if (out_filename_arg) {
        strncpy(out_filename, out_filename_arg, sizeof(out_filename) - 1);
        out_filename[sizeof(out_filename) - 1] = '\0';
    } else {
        strncpy(out_filename, filename, sizeof(out_filename) - 5);
        out_filename[sizeof(out_filename) - 5] = '\0';
        
        char* dot = strrchr(out_filename, '.');
        if (dot) *dot = '\0';
        strcat(out_filename, ".bin");
    }
    
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
