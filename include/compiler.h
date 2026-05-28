#ifndef COMPILER_H
#define COMPILER_H

#include "lexer.h"
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

typedef struct {
    char* name;
    int32_t address;
    char* module;
} WordDef;

typedef struct {
    int32_t placeholder_at;
    int32_t target_offset;
} InternalJump;

typedef struct {
    int32_t temp_addr;
    int32_t address;
    uint8_t* code;
    size_t code_len;
    size_t code_cap;
    InternalJump* jumps;
    size_t jumps_count;
    size_t jumps_cap;
} Quotation;

typedef struct {
    int quot_idx;
    int32_t offset;
    int32_t temp_addr;
} PatchRequest;

typedef struct {
    char* word;
    int32_t offset;
    int line;
    int column;
    int quot_idx;
    char* module;
    bool is_address;
} UnresolvedRef;

typedef struct {
    TokenList* token_list;
    int pos;
    
    uint8_t* bytecode;
    size_t bytecode_len;
    size_t bytecode_cap;
    
    WordDef* dictionary;
    size_t dict_count;
    size_t dict_cap;
    
    Quotation* quotations;
    size_t quot_count;
    size_t quot_cap;
    
    int* quot_stack;
    size_t quot_stack_count;
    size_t quot_stack_cap;
    
    int active_quot_idx;
    char* current_module;
    char* current_word;
    
    int32_t base_addr;
    int32_t temp_alloc;
    
    UnresolvedRef* unresolved;
    size_t unresolved_count;
    size_t unresolved_cap;
    
    PatchRequest* patches;
    size_t patches_count;
    size_t patches_cap;
    
    bool trace;
} Compiler;

Compiler* compiler_create(TokenList* list, int32_t base_addr, bool trace);
void compiler_free(Compiler* c);

// Compiles the tokens and returns the bytecode array. *out_len receives the size.
// Returns NULL on error. The caller must free the returned array.
uint8_t* compiler_compile(Compiler* c, size_t* out_len);

// High-level function to compile a source string
uint8_t* compile_source(const char* source, int32_t base_addr, size_t* out_len, bool trace);

#endif // COMPILER_H
