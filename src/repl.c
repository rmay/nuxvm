#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "compiler.h"
#include "vm.h"

// Basic REPL implementation.
// Reads a line, appends to history, compiles, and runs.
// We maintain a stack and history between evaluations.

int main() {
    char history[16384] = {0};
    int32_t stack[MAX_STACK_SIZE];
    int stack_count = 0;
    
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
        
        if (strcmp(line, "clear") == 0 || strcmp(line, "reset") == 0) {
            history[0] = '\0';
            printf("History cleared\n");
            continue;
        }
        
        if (strcmp(line, "clearstack") == 0 || strcmp(line, "cs") == 0) {
            stack_count = 0;
            printf("Stack cleared\n");
            continue;
        }
        
        if (strcmp(line, "stack") == 0 || strcmp(line, ".s") == 0) {
            printf("  Stack: [");
            for (int i = 0; i < stack_count; i++) {
                printf("%d%s", stack[i], i < stack_count - 1 ? " " : "");
            }
            printf("]\n");
            continue;
        }
        
        if (strcmp(line, "drop") == 0) {
            if (stack_count > 0) stack_count--;
            printf("  Stack: [");
            for (int i = 0; i < stack_count; i++) {
                printf("%d%s", stack[i], i < stack_count - 1 ? " " : "");
            }
            printf("]\n");
            continue;
        }
        
        // Build the source: history + stack + line
        char source[16384 + 1024];
        strcpy(source, history);
        
        for (int i = 0; i < stack_count; i++) {
            char num_buf[32];
            snprintf(num_buf, sizeof(num_buf), "%d ", stack[i]);
            strcat(source, num_buf);
        }
        
        strcat(source, line);
        
        // Is it a word definition?
        bool is_word_def = false;
        if (line[0] == '@') {
            is_word_def = true;
            // Append to history
            strcat(history, line);
            strcat(history, "\n");
        }
        
        size_t code_len = 0;
        uint8_t* bytecode = compile_source(source, HEADLESS_BASE_ADDRESS, &code_len, false);
        
        if (!bytecode) {
            printf("Compile error\n");
            continue;
        }
        
        // Only run if not just defining a word
        if (!is_word_def) {
            uint32_t total_memory = 32 * 1024 * 1024;
            VM* vm = vm_create(bytecode, code_len, HEADLESS_BASE_ADDRESS, total_memory, false);
            if (vm) {
                vm_run(vm);
                
                // Copy resulting stack
                stack_count = vm->stack_ptr;
                for (int i = 0; i < stack_count; i++) {
                    stack[i] = vm->stack[i];
                }
                
                printf("  Stack: [");
                for (int i = 0; i < stack_count; i++) {
                    printf("%d%s", stack[i], i < stack_count - 1 ? " " : "");
                }
                printf("]\n");
                
                vm_free(vm);
            }
        } else {
            // Minimal feedback for word definition
            char* space = strchr(line, ' ');
            if (space) {
                *space = '\0';
                printf("Defined word '%s'\n", line + 1);
            }
        }
        
        free(bytecode);
    }
    
    return 0;
}
