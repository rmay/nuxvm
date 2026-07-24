#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include "vm.h"
#include "machine.h"
#include "compiler.h"

static bool has_suffix_ci(const char* s, const char* suffix) {
    size_t sl = strlen(s), xl = strlen(suffix);
    return sl >= xl && strncasecmp(s + sl - xl, suffix, xl) == 0;
}

static uint8_t* load_program(const char* filename, long* out_size) {
    FILE* f = fopen(filename, "rb");
    if (!f) {
        fprintf(stderr, "Error opening file: %s\n", filename);
        return NULL;
    }

    if (fseek(f, 0, SEEK_END) != 0) {
        fprintf(stderr, "Error seeking file: %s\n", filename);
        fclose(f);
        return NULL;
    }
    long fsize = ftell(f);
    if (fsize < 0) {
        fprintf(stderr, "Error determining file size: %s\n", filename);
        fclose(f);
        return NULL;
    }
    fseek(f, 0, SEEK_SET);

    uint8_t* program = (uint8_t*)malloc((size_t)fsize);
    if (!program) {
        fprintf(stderr, "Memory allocation failed\n");
        fclose(f);
        return NULL;
    }

    if (fread(program, 1, fsize, f) != (size_t)fsize) {
        fprintf(stderr, "Error reading file\n");
        free(program);
        fclose(f);
        return NULL;
    }
    fclose(f);
    *out_size = fsize;
    return program;
}

static void print_stack(VM* vm) {
    int count = vm_get_stack_count(vm);
    int32_t buf[64];
    if (count > 64) count = 64;
    vm_get_stack_copy(vm, buf, count);
    printf("[");
    for (int i = 0; i < count; i++) {
        printf("%d%s", buf[i], i < count - 1 ? " " : "");
    }
    printf("]");
}

static void run_debug(Machine* machine) {
    VM* vm = machine->cpu;
    printf("=== NUX Debugger ===\n");
    printf("Press Enter to step, 'q' to quit, 'c' to continue\n\n");

    char input[64];
    while (1) {
        printf("PC: %u, Stack: ", vm_get_pc(vm));
        print_stack(vm);
        printf("\n> ");
        fflush(stdout);

        if (!fgets(input, sizeof(input), stdin)) {
            break;
        }
        input[strcspn(input, "\r\n")] = '\0';

        if (strcmp(input, "q") == 0) {
            break;
        }
        if (strcmp(input, "c") == 0) {
            vm_run(vm);
            break;
        }

        vm->running = true;
        if (!vm_tick(vm)) {
            if (vm->halted) {
                printf("Program halted\n");
            }
            break;
        }
    }

    printf("\nFinal stack: ");
    print_stack(vm);
    printf("\n");
}

static void run_trace(Machine* machine) {
    VM* vm = machine->cpu;
    printf("=== Execution Trace ===\n\n");

    while (1) {
        printf("PC=%u Stack=", vm_get_pc(vm));
        print_stack(vm);
        printf("\n");

        if (!vm_tick(vm)) {
            if (!vm->halted && !vm_yielded(vm)) {
                fprintf(stderr, "Runtime error at PC=%u\n", vm_get_pc(vm));
            }
            break;
        }
    }

    printf("\nFinal stack: ");
    print_stack(vm);
    printf("\n");
}

static void usage(const char* prog) {
    fprintf(stderr, "Usage: %s [options] <program.bin|program.lux>\n\n", prog);
    fprintf(stderr, "Options:\n");
    fprintf(stderr, "  -debug    Enable step-by-step debugging\n");
    fprintf(stderr, "  -trace    Show execution trace\n");
}

int main(int argc, char** argv) {
    bool debug = false;
    bool trace = false;
    const char* filename = NULL;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-debug") == 0) {
            debug = true;
        } else if (strcmp(argv[i], "-trace") == 0) {
            trace = true;
        } else if (argv[i][0] == '-') {
            usage(argv[0]);
            return 1;
        } else {
            filename = argv[i];
        }
    }

    if (!filename) {
        usage(argv[0]);
        return 1;
    }

    long fsize = 0;
    uint8_t* program = NULL;
    if (has_suffix_ci(filename, ".lux")) {
        long src_len = 0;
        uint8_t* raw = load_program(filename, &src_len);
        if (!raw) {
            return 1;
        }
        char* source = malloc((size_t)src_len + 1);
        memcpy(source, raw, (size_t)src_len);
        source[src_len] = '\0';
        free(raw);

        size_t code_len = 0;
        program = compile_source(source, HEADLESS_BASE_ADDRESS, &code_len, false);
        free(source);
        if (!program) {
            fprintf(stderr, "Error: compilation failed for %s\n", filename);
            return 1;
        }
        fsize = (long)code_len;
    } else {
        program = load_program(filename, &fsize);
        if (!program) {
            return 1;
        }
    }

    uint32_t total_memory = 32 * 1024 * 1024;
    Machine* machine = machine_create(program, (uint32_t)fsize, HEADLESS_BASE_ADDRESS, total_memory, trace);
    if (!machine) {
        fprintf(stderr, "Failed to create VM\n");
        free(program);
        return 1;
    }
    VM* vm = machine->cpu;

    if (debug) {
        run_debug(machine);
    } else if (trace) {
        run_trace(machine);
    } else {
        vm_run(vm);
        if (!vm->halted && !vm_yielded(vm) && !vm->running) {
            fprintf(stderr, "---Runtime error---\n");
            fprintf(stderr, "PC=%u Stack=", vm_get_pc(vm));
            print_stack(vm);
            fprintf(stderr, "\n");
            machine_free(machine);
            free(program);
            return 1;
        }
    }

    machine_free(machine);
    free(program);
    return 0;
}
