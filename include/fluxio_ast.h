#ifndef FLUXIO_AST_H
#define FLUXIO_AST_H

#include <stdint.h>
#include <stdbool.h>

typedef enum {
    FX_INT_LIT,
    FX_STR_LIT,       /* string literal; legal ONLY as a direct argument to a
                        * builtin that declares a string-typed parameter --
                        * codegen rejects it anywhere else (see fluxio_codegen.c) */
    FX_VAR_REF,
    FX_ASSIGN,
    FX_BINARY,
    FX_UNARY,
    FX_CALL,
    FX_INDEX,         /* array/string element read: name[index] */
    FX_INDEX_ASSIGN,  /* array/string element write: name[index] = value */
    FX_FIELD,         /* struct field read: name.field */
    FX_FIELD_ASSIGN,  /* struct field write: name.field = value */

    FX_LOCAL_DECL,
    FX_EXPR_STMT,
    FX_IF,
    FX_WHILE,
    FX_FOR,
    FX_RETURN,
    FX_BLOCK,
    FX_EMPTY
} FxNodeKind;

typedef enum {
    FX_OP_ADD, FX_OP_SUB, FX_OP_MUL, FX_OP_DIV, FX_OP_MOD,
    FX_OP_AND, FX_OP_OR, FX_OP_XOR, FX_OP_SHL, FX_OP_SAR,
    FX_OP_EQ, FX_OP_NEQ, FX_OP_LT, FX_OP_LTE, FX_OP_GT, FX_OP_GTE,
    FX_OP_LAND, FX_OP_LOR,
    FX_OP_NEG, FX_OP_LNOT, FX_OP_BNOT, FX_OP_PLUS
} FxOp;

typedef struct FxNode FxNode;

struct FxNode {
    FxNodeKind kind;
    int line, col;

    union {
        int32_t int_lit;

        struct { char* value; int32_t len; } str_lit; /* decoded bytes, excludes NUL */

        struct { char* name; } var;

        struct { char* name; FxNode* value; } assign;

        struct { FxOp op; FxNode* l; FxNode* r; } binary;

        struct { FxOp op; FxNode* operand; } unary;

        struct { char* name; FxNode** args; int nargs; } call;

        struct { char* name; FxNode* index; } index; /* FX_INDEX */

        struct { char* name; FxNode* index; FxNode* value; } index_assign; /* FX_INDEX_ASSIGN */

        struct { char* name; char* field; } field; /* FX_FIELD */

        struct { char* name; char* field; FxNode* value; } field_assign; /* FX_FIELD_ASSIGN */

        /* FX_LOCAL_DECL. Exactly one of {scalar, array, struct}:
         *   array_len == 0 && struct_type_name == NULL: scalar (init may be NULL)
         *   array_len > 0: fixed-size array, zero-initialized (init==NULL,
         *     has_string_init==false) or from a string literal
         *     (has_string_init==true; string_value/string_len hold the
         *     decoded bytes, array_len == string_len + 1 for the NUL)
         *   struct_type_name != NULL: a struct instance (array_len is set
         *     to the struct's field count for storage-sizing purposes;
         *     init/has_string_init are unused)
         *
         * is_byte (Phase A1, docs/quill_fluxio.md): only meaningful when
         * array_len > 0. A *local* byte array still lives in the VM's
         * word-sized frame-relative locals region (locals aren't
         * byte-addressable), so it costs identical codegen to a local
         * `int[]` -- one full word per element either way. is_byte only
         * changes anything for a *global* byte array (see FxGlobal below),
         * where it halves-to-quarters real memory use. Kept here anyway so
         * a local declared `byte buf[N]` reads correctly and a future
         * pointer-decay path (if locals ever get one) has the type to key
         * off of. */
        struct {
            char* name;
            int32_t array_len;
            FxNode* init;
            bool has_string_init;
            char* string_value;
            int32_t string_len;
            char* struct_type_name;
            bool is_byte;
        } local_decl;

        struct { FxNode* expr; } expr_stmt;

        struct { FxNode* cond; FxNode* then_s; FxNode* else_s; } if_s; /* else_s may be NULL */

        struct { FxNode* cond; FxNode* body; } while_s;

        struct { FxNode* init; FxNode* cond; FxNode* post; FxNode* body; } for_s; /* any may be NULL */

        struct { FxNode* expr; } ret; /* expr may be NULL */

        struct { FxNode** stmts; int nstmts; } block;
    } as;
};

/* is_array and struct_type_name are mutually exclusive. Neither set means
 * a plain scalar int param. */
typedef struct {
    char* name;
    bool is_array;          /* declared as "int name[]" -- receives a decayed base address */
    bool is_byte;            /* is_array only: declared "byte name[]" -- 1-byte element stride */
    char* struct_type_name; /* declared as "TypeName name" -- receives a decayed base address, field offsets known */
    int line;
} FxParam;

typedef struct {
    char* name;
    FxParam* params;
    int nparams;
    FxNode* body; /* FX_BLOCK */
    bool is_recursive;
    int32_t max_depth; /* meaningful only when is_recursive */
    int line;
    bool has_doc_comment;
} FxFunc;

/* Exactly one of {scalar, array, struct} -- see FX_LOCAL_DECL's comment
 * above, same three-way split applies to globals.
 *
 * is_byte (Phase A1, docs/quill_fluxio.md): only meaningful when
 * array_len > 0. Global storage is real, byte-addressable VM memory
 * (unlike locals), so a byte array actually packs 1 byte/element instead
 * of 4 -- this is what makes a 1MB `byte file_buf[1048576];` fit in a
 * quarter of the memory a `int[]` of the same element count would need.
 * See the global-allocation loop and string-init codegen in
 * src/fluxio_codegen.c. */
typedef struct {
    char* name;
    int32_t array_len;
    bool has_init;
    int32_t init_value;
    bool has_string_init;
    char* string_value;
    int32_t string_len;
    char* struct_type_name;
    int line;
    bool is_byte;
} FxGlobal;

typedef struct {
    char* name;
    int line;
} FxStructField;

typedef struct {
    char* name;             /* UpperCamelCase, enforced by the parser */
    FxStructField* fields;  /* int-only; field i lives at word offset i */
    int nfields;
    int line;
} FxStructDef;

/* `extern int name(int a, int b, ...) = 0xADDR;` -- binds a name + arity
 * to a fixed absolute address instead of a compiled body (Phase B5,
 * docs/quill_fluxio.md). Used to call into a `fluxlink`-produced
 * trampoline slot (abi/nux-abi.json): the address is always known at
 * parse time (there's no dynamic linking), so codegen emits a plain
 * `OP_CALL <address>` at the call site, no different from calling a
 * compiled Fluxio function once its own address is known. Only plain
 * `int` parameters are supported (no arrays/structs) -- arity is all
 * that's checked against a call site, param names exist only for
 * readability in source.
 *
 * `is_void` (docs/quill_fluxio.md Phase B6/B7): plain Fluxio functions
 * always leave exactly one value on the stack, so FX_EXPR_STMT always
 * emits a trailing OP_POP after any call used as a statement. Most of
 * the Lux UI/SF library words this binds against are `( ... -- )` --
 * they push nothing -- so blindly popping after them would consume
 * whatever the *next* instruction leaves instead. `extern void f(...)`
 * marks that case: codegen skips the POP, and using its result as a
 * value (assignment, argument, ...) is a compile error instead of
 * silently reading garbage. */
typedef struct {
    char* name;
    int nparams;
    int32_t address;
    int line;
    bool is_void;
} FxExtern;

typedef struct {
    FxGlobal* globals;
    int nglobals;
    FxFunc* funcs;
    int nfuncs;
    FxStructDef* structs;
    int nstructs;
    FxExtern* externs;
    int nexterns;
    bool version_seen;
    int32_t version_value;
} FxProgram;

/* Looks up a struct definition by name, or NULL if none exists. */
const FxStructDef* fx_find_struct(const FxProgram* program, const char* name);

/* Every Fluxio app build must declare `version <n>;` (Kelvin versioning,
 * AGENTS.md). Fluxio has no library-build mode (that's a luxc -base concept
 * -- it only produces apps, linking against already-compiled Lux libraries
 * via fluxlink), so this is unconditional for every caller that turns a
 * parsed program into a runnable image: fluxioc and cloister both call it.
 * Returns false, having printed a "<tool>: <path>: ..." diagnostic, when
 * absent. */
bool fx_require_version(const FxProgram* program, const char* tool, const char* path);

FxNode* fx_node_new(FxNodeKind kind, int line, int col);
void fx_node_free(FxNode* node);

void fx_program_free(FxProgram* program);

#endif /* FLUXIO_AST_H */
