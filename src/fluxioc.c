#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include "fluxio_token.h"
#include "fluxio_include.h"
#include "fluxio_parser.h"
#include "fluxio_codegen.h"
#include "opcodes.h"
#include "vm.h"

static void usage(const char* prog) {
    fprintf(stderr,
            "Usage: %s [-o out.bin] [-target graphical|headless] "
            "[-dumpAt 0xADDR] [-dumpRange N] <file.fx>\n",
            prog);
}

/* Parse a decimal or 0x-prefixed hex address. Returns false on garbage. */
static bool parse_address(const char* s, long long* out) {
    while (*s == ' ' || *s == '\t') s++;
    char* end = NULL;
    long long v;
    if (strncmp(s, "0x", 2) == 0 || strncmp(s, "0X", 2) == 0) {
        v = strtoll(s + 2, &end, 16);
    } else {
        v = strtoll(s, &end, 10);
    }
    if (end == s || (end && *end != '\0')) return false;
    *out = v;
    return true;
}

static int opcode_size(uint8_t op) {
    switch (op) {
        case OP_PUSH:
        case OP_JMP:
        case OP_JZ:
        case OP_JNZ:
        case OP_CALL:
        case OP_LOAD:
        case OP_STORE:
            return 5;
        default:
            return 1;
    }
}

/* Print a hex+disassembly window of `bytecode` centered on absolute address
 * `pc`. The window starts at pc-radius and resyncs on each emitted instruction. */
static void dump_around(const uint8_t* bytecode, size_t code_len, int32_t base_addr,
                        long long pc, int radius) {
    long long start_abs = pc - radius;
    long long end_abs = pc + radius;
    int start_off = (int)(start_abs - base_addr);
    int end_off = (int)(end_abs - base_addr);
    if (start_off < 0) start_off = 0;
    if (end_off > (int)code_len) end_off = (int)code_len;

    printf("Bytecode dump: total length %zu (0x%zX) bytes, baseAddr 0x%X\n",
           code_len, code_len, base_addr);
    printf("Target PC 0x%llX (offset %d). Window: offset %d..%d (abs 0x%X..0x%X)\n\n",
           pc, (int)(pc - base_addr), start_off, end_off,
           base_addr + start_off, base_addr + end_off);

    printf("Linear hex (16 bytes/line):\n");
    for (int i = start_off; i < end_off; i += 16) {
        int end = i + 16;
        if (end > end_off) end = end_off;
        printf("  0x%08X: ", base_addr + i);
        for (int j = i; j < end; j++) {
            const char* mark = ((long long)(base_addr + j) == pc) ? "*" : " ";
            printf("%s%02X", mark, bytecode[j]);
        }
        printf("\n");
    }

    printf("\nDisassembly (best-effort, starting from window start):\n");
    int off = start_off;
    while (off < end_off) {
        long long abs = (long long)base_addr + off;
        uint8_t op = bytecode[off];
        const char* name = opcode_name(op);
        const char* marker = (abs == pc) ? ">>" : "  ";
        int size = opcode_size(op);
        if (off + size > (int)code_len) size = (int)code_len - off;
        if (size == 5) {
            int32_t imm = (int32_t)(((uint32_t)bytecode[off+1] << 24) |
                                    ((uint32_t)bytecode[off+2] << 16) |
                                    ((uint32_t)bytecode[off+3] << 8) |
                                    (uint32_t)bytecode[off+4]);
            printf("  %s 0x%08llX: %02X %02X%02X%02X%02X  %s 0x%X (%d)\n",
                   marker, abs, op, bytecode[off+1], bytecode[off+2],
                   bytecode[off+3], bytecode[off+4], name, (uint32_t)imm, imm);
        } else if (size == 1) {
            printf("  %s 0x%08llX: %02X            %s\n", marker, abs, op, name);
        } else {
            printf("  %s 0x%08llX: %02X            %s (size=%d)\n", marker, abs, op, name, size);
        }
        if (size <= 0) size = 1;
        off += size;
    }
}

int main(int argc, char** argv) {
    int base_address = GRAPHICAL_BASE_ADDRESS;
    const char* filename = NULL;
    const char* out_filename_arg = NULL;
    const char* dump_at_arg = NULL;
    int dump_range = 64;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-target") == 0 && i + 1 < argc) {
            if (strcmp(argv[i+1], "headless") == 0) {
                base_address = HEADLESS_BASE_ADDRESS;
            } else if (strcmp(argv[i+1], "graphical") == 0) {
                base_address = GRAPHICAL_BASE_ADDRESS;
            } else {
                fprintf(stderr, "fluxioc: bad -target %s (use graphical or headless)\n", argv[i+1]);
                return 1;
            }
            i++;
        } else if (strcmp(argv[i], "-o") == 0 && i + 1 < argc) {
            out_filename_arg = argv[i+1];
            i++;
        } else if (strcmp(argv[i], "-dumpAt") == 0 && i + 1 < argc) {
            dump_at_arg = argv[i+1];
            i++;
        } else if (strcmp(argv[i], "-dumpRange") == 0 && i + 1 < argc) {
            dump_range = atoi(argv[i+1]);
            i++;
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

    FxTokenList* tokens = fx_load_with_includes(filename);
    if (!tokens) {
        fprintf(stderr, "Lexing failed\n");
        return 1;
    }

    FxProgram* program = fx_parse(tokens);
    fx_token_list_free(tokens);
    if (!program) {
        fprintf(stderr, "Parsing failed\n");
        return 1;
    }

    /* Every app build must declare `version <n>;` (Kelvin versioning,
     * AGENTS.md). fluxioc has no library-build mode (that's a luxc -base
     * concept -- Fluxio only produces apps, linking against already-
     * compiled Lux libraries via fluxlink), so this check is unconditional. */
    if (!program->version_seen) {
        fprintf(stderr, "fluxioc: %s: missing required 'version <n>;' directive\n", filename);
        fx_program_free(program);
        return 1;
    }

    size_t code_len = 0;
    uint8_t* bytecode = fx_codegen(program, base_address, &code_len);
    fx_program_free(program);

    if (!bytecode) {
        fprintf(stderr, "Compilation failed\n");
        return 1;
    }

    if (dump_at_arg) {
        long long pc;
        if (!parse_address(dump_at_arg, &pc)) {
            fprintf(stderr, "fluxioc: bad -dumpAt \"%s\"\n", dump_at_arg);
            free(bytecode);
            return 1;
        }
        dump_around(bytecode, code_len, base_address, pc, dump_range);
        free(bytecode);
        return 0;
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
        return 1;
    }

    fwrite(bytecode, 1, code_len, out_f);
    fclose(out_f);

    printf("Compiled %s to %s (%zu bytes)\n", filename, out_filename, code_len);

    free(bytecode);
    return 0;
}
