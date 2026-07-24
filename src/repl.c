#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "compiler.h"
#include "vm.h"

// Basic REPL implementation.
// Reads a line, compiles it against the accumulated definition history,
// and runs it. Stack, history, and word list persist between evaluations.

#define MAX_DEFINITIONS 128
#define MAX_DEF_NAME 64

static int32_t stack[MAX_STACK_SIZE];
static int stack_count = 0;
static char history[16384];
static char definitions[MAX_DEFINITIONS][MAX_DEF_NAME];
static int def_count = 0;

static void print_stack(void) {
    if (stack_count == 0) {
        printf("  Stack: []\n");
        return;
    }
    printf("  Stack: [");
    for (int i = 0; i < stack_count; i++) {
        printf("%d%s", stack[i], i < stack_count - 1 ? " " : "");
    }
    printf("]\n");
}

static void print_help(void) {
    printf("\n=== LUX REPL Commands ===\n");
    printf("  help, ?          - Show this help\n");
    printf("  exit, quit, q    - Exit REPL\n");
    printf("  clear, reset     - Clear word definitions\n");
    printf("  clearstack, cs   - Clear the stack\n");
    printf("  stack, .s        - Show current stack\n");
    printf("  drop             - Drop top stack value\n");
    printf("  words            - List defined words\n");
    printf("  history          - Show definition history\n");
}

// Handle a word definition line ("@name ... ;"): record it in history and the
// word list without compiling — errors surface on first use, matching the Go
// REPL.
static void handle_word_def(const char* line) {
    if (!strchr(line, ';')) {
        printf("Error: Word definition must end with ';'\n");
        return;
    }
    if (strlen(history) + strlen(line) + 2 >= sizeof(history)) {
        printf("Error: history full\n");
        return;
    }
    strcat(history, line);
    strcat(history, "\n");

    // Extract the word name: first whitespace-delimited token after '@'.
    const char* p = line + 1;
    while (*p == ' ' || *p == '\t') p++;
    char name[MAX_DEF_NAME] = {0};
    int n = 0;
    while (p[n] && p[n] != ' ' && p[n] != '\t' && n < MAX_DEF_NAME - 1) {
        name[n] = p[n];
        n++;
    }
    if (n == 0) return;
    if (def_count < MAX_DEFINITIONS) {
        strncpy(definitions[def_count], name, MAX_DEF_NAME - 1);
        def_count++;
    }
    printf("Defined word '%s'\n", name);
}

static void evaluate(const char* line) {
    // Build the source: history + stack + line
    char source[16384 + 1024];
    strcpy(source, history);

    for (int i = 0; i < stack_count; i++) {
        char num_buf[32];
        snprintf(num_buf, sizeof(num_buf), "%d ", stack[i]);
        strcat(source, num_buf);
    }

    strcat(source, line);

    size_t code_len = 0;
    uint8_t* bytecode = compile_source(source, HEADLESS_BASE_ADDRESS, &code_len, false);
    if (!bytecode) {
        printf("Compile error\n");
        return;
    }

    uint32_t total_memory = 32 * 1024 * 1024;
    VM* vm = vm_create(bytecode, code_len, HEADLESS_BASE_ADDRESS, total_memory, false);
    if (vm) {
        vm_run(vm);

        stack_count = vm->stack_ptr;
        if (stack_count > MAX_STACK_SIZE) stack_count = MAX_STACK_SIZE;
        for (int i = 0; i < stack_count; i++) {
            stack[i] = vm->stack[i];
        }
        print_stack();

        vm_free(vm);
    }
    free(bytecode);
}

int main(void) {
    printf("LUX REPL (C Port). Type 'exit' to quit.\n");

    while (1) {
        printf("lux> ");
        char line[1024];
        if (!fgets(line, sizeof(line), stdin)) {
            break;
        }

        // Remove trailing newline
        line[strcspn(line, "\r\n")] = 0;

        if (strlen(line) == 0) continue;

        if (strcmp(line, "exit") == 0 || strcmp(line, "quit") == 0 || strcmp(line, "q") == 0) {
            break;
        }

        if (strcmp(line, "help") == 0 || strcmp(line, "?") == 0) {
            print_help();
            continue;
        }

        if (strcmp(line, "clear") == 0 || strcmp(line, "reset") == 0) {
            history[0] = '\0';
            def_count = 0;
            printf("History cleared\n");
            continue;
        }

        if (strcmp(line, "clearstack") == 0 || strcmp(line, "cs") == 0) {
            stack_count = 0;
            printf("Stack cleared\n");
            continue;
        }

        if (strcmp(line, "stack") == 0 || strcmp(line, ".s") == 0) {
            print_stack();
            continue;
        }

        if (strcmp(line, "drop") == 0) {
            if (stack_count > 0) stack_count--;
            print_stack();
            continue;
        }

        if (strcmp(line, "words") == 0) {
            if (def_count == 0) {
                printf("No words defined\n");
            } else {
                printf("Defined words: ");
                for (int i = 0; i < def_count; i++) {
                    printf("%s%s", definitions[i], i < def_count - 1 ? ", " : "");
                }
                printf("\n");
            }
            continue;
        }

        if (strcmp(line, "history") == 0) {
            if (history[0] == '\0') {
                printf("No history\n");
            } else {
                printf("%s\n", history);
            }
            continue;
        }

        if (line[0] == '@') {
            handle_word_def(line);
            continue;
        }

        evaluate(line);
    }

    return 0;
}
