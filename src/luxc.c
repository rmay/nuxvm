#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include "compiler.h"
#include "opcodes.h"
#include "vm.h"

static void usage(const char* prog) {
    fprintf(stderr,
            "Usage: %s [-trace] [-o out.bin] [-target graphical|headless] [-base 0xADDR] "
            "[-symbols out.symtab.json] [-dumpAt 0xADDR] [-dumpRange N] <file.lux>\n",
            prog);
}

/* Writes every dictionary entry (name -> final compiled address) as JSON.
 * Consumed by fluxlink (docs/quill_fluxio.md Phase B3) to build a linked
 * library's trampoline table; see abi/nux-abi.json's append_only_policy
 * for why this is a *committed* file, not a throwaway build artifact, once
 * a library starts being linked against. */
static bool write_symtab(const char* path, Compiler* c) {
    FILE* f = fopen(path, "w");
    if (!f) return false;
    fprintf(f, "{\n  \"symbols\": [\n");
    for (size_t i = 0; i < c->dict_count; i++) {
        fprintf(f, "    { \"name\": \"%s\", \"address\": %d }%s\n",
                c->dictionary[i].name, c->dictionary[i].address,
                (i + 1 < c->dict_count) ? "," : "");
    }
    fprintf(f, "  ]\n}\n");
    fclose(f);
    return true;
}

// Parse a decimal or 0x-prefixed hex address. Returns false on garbage.
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

// Encoded byte length of an opcode: 5 for opcodes with a 4-byte immediate,
// 1 otherwise (unknown opcodes resync byte-by-byte).
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

// Print a hex+disassembly window of `bytecode` centered on absolute address
// `pc`. The window starts at pc-radius and resyncs on each emitted instruction.
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
    bool trace = false;
    int base_address = GRAPHICAL_BASE_ADDRESS;
    const char* base_override_arg = NULL;
    const char* filename = NULL;
    const char* out_filename_arg = NULL;
    const char* symbols_out_arg = NULL;
    const char* dump_at_arg = NULL;
    int dump_range = 64;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-trace") == 0) {
            trace = true;
        } else if (strcmp(argv[i], "-target") == 0 && i + 1 < argc) {
            if (strcmp(argv[i+1], "headless") == 0) {
                base_address = HEADLESS_BASE_ADDRESS;
            } else if (strcmp(argv[i+1], "graphical") == 0) {
                base_address = GRAPHICAL_BASE_ADDRESS;
            } else {
                fprintf(stderr, "luxc: bad -target %s (use graphical or headless)\n", argv[i+1]);
                return 1;
            }
            i++;
        } else if (strcmp(argv[i], "-base") == 0 && i + 1 < argc) {
            /* Overrides -target's base address. For a Lux "library build"
             * meant to be linked by fluxlink (docs/quill_fluxio.md Phase B),
             * which needs to target the ABI library-link band
             * (MM_ABI_LIBRARY_LINK_BASE, include/memory_map.h) instead of
             * colliding with GRAPHICAL_BASE_ADDRESS, where the Fluxio
             * program it gets linked into already loads. Applied after the
             * full argument scan below, so it always wins regardless of
             * whether -target appears before or after it. */
            base_override_arg = argv[i+1];
            i++;
        } else if (strcmp(argv[i], "-o") == 0 && i + 1 < argc) {
            out_filename_arg = argv[i+1];
            i++;
        } else if (strcmp(argv[i], "-symbols") == 0 && i + 1 < argc) {
            symbols_out_arg = argv[i+1];
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

    if (base_override_arg) {
        long long override_val;
        if (!parse_address(base_override_arg, &override_val)) {
            fprintf(stderr, "luxc: bad -base \"%s\"\n", base_override_arg);
            return 1;
        }
        base_address = (int)override_val;
    }

    FILE* f = fopen(filename, "rb");
    if (!f) {
        fprintf(stderr, "luxc: read %s: cannot open\n", filename);
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

    /* Compiled via the lower-level compiler_create/compiler_compile API
     * (rather than the compile_source one-shot helper) so the dictionary
     * is still reachable afterward for -symbols -- compile_source frees
     * its Compiler internally before returning. */
    TokenList* token_list = tokenize(source);
    if (!token_list) {
        fprintf(stderr, "Compilation failed\n");
        free(source);
        return 1;
    }
    Compiler* compiler = compiler_create(token_list, base_address, trace);
    size_t code_len = 0;
    uint8_t* bytecode = compiler_compile(compiler, &code_len);

    if (!bytecode) {
        fprintf(stderr, "Compilation failed\n");
        compiler_free(compiler);
        token_list_free(token_list);
        free(source);
        return 1;
    }

    /* Every app build must declare `VERSION <n>` (Kelvin versioning,
     * AGENTS.md). Library builds (-base, linked into a Fluxio host rather
     * than run standalone) are exempt -- they don't own a runnable app. */
    if (!base_override_arg && !compiler->version_seen) {
        fprintf(stderr, "luxc: %s: missing required 'VERSION <n>' directive\n", filename);
        free(bytecode);
        compiler_free(compiler);
        token_list_free(token_list);
        free(source);
        return 1;
    }

    if (symbols_out_arg) {
        if (!write_symtab(symbols_out_arg, compiler)) {
            fprintf(stderr, "luxc: could not write symbol table to \"%s\"\n", symbols_out_arg);
            free(bytecode);
            compiler_free(compiler);
            token_list_free(token_list);
            free(source);
            return 1;
        }
    }

    if (dump_at_arg) {
        long long pc;
        if (!parse_address(dump_at_arg, &pc)) {
            fprintf(stderr, "luxc: bad -dumpAt \"%s\"\n", dump_at_arg);
            free(bytecode);
            compiler_free(compiler);
            token_list_free(token_list);
            free(source);
            return 1;
        }
        dump_around(bytecode, code_len, base_address, pc, dump_range);
        free(bytecode);
        compiler_free(compiler);
        token_list_free(token_list);
        free(source);
        return 0;
    }
    compiler_free(compiler);
    token_list_free(token_list);

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
