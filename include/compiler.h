#ifndef COMPILER_H
#define COMPILER_H

#include "lexer.h"
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#define COMPILER_MAX_LOCAL_FRAMES 8
#define COMPILER_MAX_LOCAL_NAMES  16
#define COMPILER_MAX_LOCAL_NAME   32

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
    int parent_quot_idx;
    int32_t address;
    uint8_t* code;
    size_t code_len;
    size_t code_cap;
    InternalJump* jumps;
    size_t jumps_count;
    size_t jumps_cap;
    int saved_local_depth;
} Quotation;

typedef struct {
    int quot_idx;
    int32_t offset;
    int32_t temp_addr;
    int parent_quot_idx;
} PatchRequest;

typedef struct {
    char* word;
    int32_t offset;
    int line;
    int column;
    int quot_idx;
    char* module;
    bool is_address;
    bool is_tail_call;
} UnresolvedRef;

typedef struct {
    int32_t offset;
    int quot_idx;
    char* str;
} StringPatch;

typedef struct {
    int* stack;
    size_t count;
    size_t cap;
    int active_quot_idx;
} QuotStackFrame;

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

    QuotStackFrame* quot_saved_frames;
    size_t quot_saved_count;
    size_t quot_saved_cap;
    
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
    

    char** included_files;
    size_t included_count;
    size_t included_cap;

    StringPatch* string_patches;
    size_t string_patches_count;
    size_t string_patches_cap;

    struct {
        char* alias;
        char* module;
    }* imports;
    size_t import_count;
    size_t import_cap;

    /* Named locals: { a b -- } ... }  compiles to FRAME!/LOCAL@/UNFRAME! */
    struct {
        char names[COMPILER_MAX_LOCAL_NAMES][COMPILER_MAX_LOCAL_NAME];
        int count;
    } local_frames[COMPILER_MAX_LOCAL_FRAMES];
    int local_depth;

    bool trace;

    /* Set by the top-level `VERSION <n>` directive (Kelvin versioning,
     * AGENTS.md). Required for app builds; luxc.c enforces presence
     * since compiler.c is also used for REPL/boot/library compiles that
     * should not be forced to declare one. */
    bool version_seen;
    int32_t version_value;

    /* Best-effort duplicate-base-address check (docs/memory-map.md):
     * every `@NAME 0xHEX ;` constant across the whole compiled unit
     * (including transitively-included files) that isn't named like a
     * color constant gets recorded here, then compiler_compile() warns
     * on any value shared by more than one name. See record_addr_const()
     * in compiler.c. */
    struct {
        char* name;
        int32_t value;
        int line;
    } *addr_consts;
    size_t addr_const_count;
    size_t addr_const_cap;

    /* `RESERVE <name> <bytes> ;` (docs/reserve-directive.md): the compiler
     * bump-allocates guest RAM out of MM_LUX_RESERVE_BASE..END instead of
     * the author hand-picking a hex address, so reserved state cannot
     * collide. reserve_next is the bump pointer (4-byte aligned);
     * reservations records every span so warn_duplicate_addr_consts() can
     * flag a hand-picked constant that lands inside one, and so luxc's
     * -symbols dump can report the data addresses (the dictionary holds
     * only the address of each reservation's PUSH/RET stub). */
    int32_t reserve_next;
    struct {
        char* name;
        int32_t addr;
        int32_t size;
        int line;
    } *reservations;
    size_t reservation_count;
    size_t reservation_cap;
} Compiler;

Compiler* compiler_create(TokenList* list, int32_t base_addr, bool trace);
void compiler_free(Compiler* c);

// Compiles the tokens and returns the bytecode array. *out_len receives the size.
// Returns NULL on error. The caller must free the returned array.
uint8_t* compiler_compile(Compiler* c, size_t* out_len);

// High-level function to compile a source string
uint8_t* compile_source(const char* source, int32_t base_addr, size_t* out_len, bool trace);

#endif // COMPILER_H
