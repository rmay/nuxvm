#include "fluxio_codegen.h"
#include "fluxio_ast.h"
#include "opcodes.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <setjmp.h>

#define FX_DEVICE_BOUNDARY MM_FX_GLOBALS_END

/* Globals larger than this (in bytes) bump-allocate from the bulk-globals
 * band (MM_FX_BULK_GLOBALS_BASE) instead of the small-scalar band -- see
 * the allocation loop in fx_codegen() and docs/memory-map.md. 1KB comfortably
 * fits many small fixed-size arrays in the ~60KB small-scalar budget while
 * still routing anything meaningfully large (a 1MB file buffer, etc) to the
 * band that's actually sized for it. */
#define FX_BULK_GLOBAL_THRESHOLD 1024

/* SCI (System Call Interface) protocol -- see src/system.c:11-14. A call is
 * issued by STORE-ing cmd/arg1/(arg3), then STORE-ing arg2 LAST (that write
 * is what fires the syscall), then LOADI-ing SCI_PORT for the result. */
#define FX_SCI_PORT      0x100D0
#define FX_SCI_CMD_ADDR  0x100D4
#define FX_SCI_ARG1_ADDR 0x100D8
#define FX_SCI_ARG2_ADDR 0x100DC
#define FX_SCI_ARG3_ADDR 0x10124

#define FX_SCI_CMD_VFS_OPEN          10
#define FX_SCI_CMD_VFS_CLOSE         11
#define FX_SCI_CMD_VFS_READ          12
#define FX_SCI_CMD_VFS_WRITE         13
#define FX_SCI_CMD_YIELD             16
#define FX_SCI_CMD_SET_WINDOW_TITLE  22
#define FX_SCI_CMD_VFS_SEEK          23
#define FX_SCI_CMD_VFS_STAT          24
#define FX_SCI_CMD_VFS_WRITE_CHUNK   25

/* /dev/draw command-buffer byte layout (src/vfs.c draw_write) */
#define FX_DRAW_CMD_FILL_RECT   0
#define FX_DRAW_CMD_DRAW_STRING 2
#define FX_DRAW_CMD_BEGIN_FRAME 6
#define FX_DRAW_CMD_END_FRAME   7

typedef struct { size_t imm_offset; char* name; int line; } FxFixup;

typedef struct {
    char* name;
    int32_t offset;
    int32_t array_len;              /* 0 = scalar/struct-via-pointer; >0 = own array/struct storage size in words */
    const FxStructDef* struct_def;   /* non-NULL if struct-typed (own storage or via-pointer) */
    bool struct_via_pointer;         /* true for struct params: `offset` is a LOCALGET slot holding an address */
    bool is_byte;                    /* array_len==0 array params only: holds a decayed byte[] address, 1-byte stride */
} FxScopeBinding;

typedef struct {
    uint8_t* code;
    size_t len, cap;
    int32_t base_addr;

    int32_t* global_addrs;   /* parallel to program->globals */
    int nglobals;

    FxFunc* funcs;           /* borrowed from program */
    int nfuncs;
    int32_t* func_addrs;      /* -1 until emitted */
    int32_t* recursion_slot;  /* -1 if not recursive */

    FxExtern* externs;        /* borrowed from program */
    int nexterns;

    FxFixup* fixups;
    int nfixups, fixups_cap;

    FxScopeBinding* scope;
    int scope_len, scope_cap;
    int32_t next_local_slot;
    int32_t frame_k;          /* K (body-local count) for the function currently being emitted */
    int32_t frame_l;          /* L = P+K for the function currently being emitted */
    bool cur_is_recursive;
    int32_t cur_recursion_slot;

    /* Reserved scratch memory for the SCI/VFS/draw builtins (see
     * "Cloister bindings" section below). Fixed addresses allocated once,
     * after user globals and recursion slots, regardless of whether the
     * program actually uses any of these builtins. */
    int32_t scratch_addr;   /* holds a byte address mid-computation (store_byte/load_byte) */
    int32_t scratch_shift;  /* holds a bit-shift amount mid-computation */
    int32_t scratch_word;   /* holds a partially-masked word mid-computation */
    int32_t scratch_field;  /* holds a field value being decomposed into bytes */
    int32_t scratch_copy_src; /* draw_bytes: runtime byte-copy loop source cursor */
    int32_t scratch_copy_dst; /* draw_bytes: runtime byte-copy loop dest cursor */
    int32_t mouse_buf_addr; /* 8-byte /dev/mouse event, persists across a frame */
    int32_t kbd_buf_addr;   /* 8-byte /dev/kbd event, persists across a frame */
    int32_t sci_buf_addr;   /* transient scratch: draw-command packing, string args, canvas-size reads */

    jmp_buf error_jmp;
} Codegen;

#define FX_SCI_BUF_SIZE 256

static void cg_error(Codegen* cg, int line, const char* fmt, ...) {
    fprintf(stderr, "fluxio: codegen error at line %d: ", line);
    va_list args;
    va_start(args, fmt);
    vfprintf(stderr, fmt, args);
    va_end(args);
    fprintf(stderr, "\n");
    longjmp(cg->error_jmp, 1);
}

/* ---- code buffer ---- */

static void emit_byte(Codegen* cg, uint8_t b) {
    if (cg->len == cg->cap) {
        cg->cap = cg->cap ? cg->cap * 2 : 256;
        cg->code = realloc(cg->code, cg->cap);
    }
    cg->code[cg->len++] = b;
}

static void emit_op(Codegen* cg, uint8_t op) { emit_byte(cg, op); }

static size_t emit_imm_op(Codegen* cg, uint8_t op, int32_t value) {
    emit_byte(cg, op);
    size_t off = cg->len;
    uint32_t u = (uint32_t) value;
    emit_byte(cg, (u >> 24) & 0xFF);
    emit_byte(cg, (u >> 16) & 0xFF);
    emit_byte(cg, (u >> 8) & 0xFF);
    emit_byte(cg, u & 0xFF);
    return off;
}

static void patch_i32(Codegen* cg, size_t off, int32_t value) {
    uint32_t u = (uint32_t) value;
    cg->code[off]     = (u >> 24) & 0xFF;
    cg->code[off + 1] = (u >> 16) & 0xFF;
    cg->code[off + 2] = (u >> 8) & 0xFF;
    cg->code[off + 3] = u & 0xFF;
}

static int32_t cur_addr(Codegen* cg) { return cg->base_addr + (int32_t) cg->len; }

/* ---- symbol resolution ---- */

typedef enum {
    FX_BINDING_NONE,
    FX_BINDING_LOCAL_SCALAR,
    FX_BINDING_LOCAL_ARRAY,    /* frame-relative; offset_or_addr is index-0's LOCALGET/LOCALSET offset */
    FX_BINDING_LOCAL_STRUCT,   /* frame-relative, own storage; offset_or_addr is field-0's LOCALGET/LOCALSET offset */
    FX_BINDING_PARAM_STRUCT,   /* frame-relative scalar HOLDING an address; offset_or_addr is that slot's own LOCALGET offset */
    FX_BINDING_GLOBAL_SCALAR,
    FX_BINDING_GLOBAL_ARRAY,   /* memory-addressable; offset_or_addr is a real base address */
    FX_BINDING_GLOBAL_STRUCT   /* memory-addressable; offset_or_addr is a real base address */
} FxBindingKind;

typedef struct {
    FxBindingKind kind;
    int32_t offset_or_addr;
    int32_t array_len;              /* >0 for array/struct bindings (struct: field count) */
    const FxStructDef* struct_def;   /* non-NULL for *_STRUCT bindings */
    bool is_byte;                    /* array bindings only: 1-byte element stride instead of 4 */
} FxBinding;

static FxBinding resolve_binding(Codegen* cg, FxProgram* program, const char* name) {
    for (int i = cg->scope_len - 1; i >= 0; i--) {
        if (strcmp(cg->scope[i].name, name) == 0) {
            FxBinding b;
            b.offset_or_addr = cg->scope[i].offset;
            b.array_len = cg->scope[i].array_len;
            b.struct_def = cg->scope[i].struct_def;
            b.is_byte = cg->scope[i].is_byte;
            if (cg->scope[i].struct_def) {
                b.kind = cg->scope[i].struct_via_pointer ? FX_BINDING_PARAM_STRUCT : FX_BINDING_LOCAL_STRUCT;
            } else {
                b.kind = cg->scope[i].array_len > 0 ? FX_BINDING_LOCAL_ARRAY : FX_BINDING_LOCAL_SCALAR;
            }
            return b;
        }
    }
    for (int i = 0; i < program->nglobals; i++) {
        if (strcmp(program->globals[i].name, name) == 0) {
            FxBinding b;
            b.offset_or_addr = cg->global_addrs[i];
            b.array_len = program->globals[i].array_len;
            b.is_byte = program->globals[i].is_byte;
            if (program->globals[i].struct_type_name) {
                b.kind = FX_BINDING_GLOBAL_STRUCT;
                b.struct_def = fx_find_struct(program, program->globals[i].struct_type_name);
            } else {
                b.kind = program->globals[i].array_len > 0 ? FX_BINDING_GLOBAL_ARRAY : FX_BINDING_GLOBAL_SCALAR;
                b.struct_def = NULL;
            }
            return b;
        }
    }
    FxBinding none = { FX_BINDING_NONE, 0, 0, NULL, false };
    return none;
}

/* Looks up a field's 0-based index within a struct, or -1 if not found. */
static int find_field_index(const FxStructDef* sd, const char* field_name) {
    for (int i = 0; i < sd->nfields; i++) {
        if (strcmp(sd->fields[i].name, field_name) == 0) return i;
    }
    return -1;
}

static int find_func_index(Codegen* cg, const char* name) {
    for (int i = 0; i < cg->nfuncs; i++) {
        if (strcmp(cg->funcs[i].name, name) == 0) return i;
    }
    return -1;
}

/* externs (Phase B5, docs/quill_fluxio.md) bind a name+arity to an address
 * known at parse time -- no backpatching needed, unlike a regular function
 * call, since the address never depends on anything this compilation unit
 * emits. */
static const FxExtern* find_extern(Codegen* cg, const char* name) {
    for (int i = 0; i < cg->nexterns; i++) {
        if (strcmp(cg->externs[i].name, name) == 0) return &cg->externs[i];
    }
    return NULL;
}

/* Builtins: v1's console I/O (emit/print, wrapping OP_OUT) plus v2b's
 * Cloister bindings (wrapping the SCI/VFS/draw protocol). Not user-definable
 * (reserved names, checked in fx_codegen); not part of the user call graph
 * (skipped by the recursion-cycle check). Each arg is either a plain int
 * expression or -- for path/title/text arguments -- a string literal
 * (FX_STR_LIT), required because Fluxio has no general string-as-value
 * support yet: the literal's bytes are packed into a compiler-owned scratch
 * buffer at the call site, entirely at compile time. */
typedef enum { FX_ARG_INT, FX_ARG_STRING } FxArgKind;

#define FX_BUILTIN_MAX_ARGS 7

typedef struct {
    const char* name;
    int argc;
    FxArgKind arg_kinds[FX_BUILTIN_MAX_ARGS];
} FxBuiltinSpec;

static const FxBuiltinSpec FX_BUILTINS[] = {
    { "emit",              1, { FX_ARG_INT } },
    { "print",             1, { FX_ARG_INT } },

    /* VFS */
    { "vfs_open",          1, { FX_ARG_STRING } },
    { "vfs_close",         1, { FX_ARG_INT } },

    /* Phase A2, docs/quill_fluxio.md: runtime buffer+length variants, for
     * a path/data that's only known at runtime (a file-picker result, a
     * lazily-loaded chunk) -- vfs_open only accepts a compile-time string
     * literal, which can't express that. */
    { "vfs_open_buf",      3, { FX_ARG_INT, FX_ARG_INT, FX_ARG_INT } },      /* path_buf, path_len, flags -> fd */
    { "vfs_read",          3, { FX_ARG_INT, FX_ARG_INT, FX_ARG_INT } },      /* fd, buf, maxlen -> bytes_read */
    { "vfs_write",         3, { FX_ARG_INT, FX_ARG_INT, FX_ARG_INT } },      /* fd, buf, len -> bytes_written */
    { "vfs_seek",          2, { FX_ARG_INT, FX_ARG_INT } },                  /* fd, pos -> result */
    { "vfs_stat",          1, { FX_ARG_INT } },                              /* fd -> size */
    { "vfs_write_chunk",   5, { FX_ARG_INT, FX_ARG_INT, FX_ARG_INT, FX_ARG_INT, FX_ARG_INT } }, /* fd,buf,len,offset,orig_len -> result */

    { "yield",             0, { 0 } },
    { "set_window_title",  1, { FX_ARG_STRING } },
    { "canvas_size",       1, { FX_ARG_INT } }, /* fd -> (w<<16)|h */

    /* /dev/draw */
    { "begin_frame",       1, { FX_ARG_INT } },
    { "end_frame",         1, { FX_ARG_INT } },
    { "fill_rect",         6, { FX_ARG_INT, FX_ARG_INT, FX_ARG_INT, FX_ARG_INT, FX_ARG_INT, FX_ARG_INT } }, /* fd,x,y,w,h,color */
    { "draw_str",          6, { FX_ARG_INT, FX_ARG_INT, FX_ARG_INT, FX_ARG_INT, FX_ARG_INT, FX_ARG_STRING } }, /* fd,x,y,color,scale,text */
    /* Phase A3, docs/quill_fluxio.md: same wire format as draw_str, but the
     * text comes from a runtime buffer+length (e.g. a byte[] holding live
     * file/line content) instead of a compile-time string literal. */
    { "draw_bytes",        7, { FX_ARG_INT, FX_ARG_INT, FX_ARG_INT, FX_ARG_INT, FX_ARG_INT, FX_ARG_INT, FX_ARG_INT } }, /* fd,x,y,color,scale,buf,len */

    /* /dev/mouse, /dev/kbd -- poll_*() fills a persistent buffer; the
     * accessor builtins read fields out of whichever buffer was last polled. */
    { "poll_mouse",        1, { FX_ARG_INT } }, /* fd -> 1 if an event was read, else 0 */
    { "mouse_type",        0, { 0 } },
    { "mouse_button",      0, { 0 } },
    { "mouse_x",           0, { 0 } },
    { "mouse_y",           0, { 0 } },
    { "poll_kbd",          1, { FX_ARG_INT } }, /* fd -> 1 if an event was read, else 0 */
    { "kbd_type",          0, { 0 } },
    { "kbd_key",           0, { 0 } },
};
#define FX_NUM_BUILTINS (sizeof(FX_BUILTINS) / sizeof(FX_BUILTINS[0]))

static const FxBuiltinSpec* find_builtin(const char* name) {
    for (size_t i = 0; i < FX_NUM_BUILTINS; i++) {
        if (strcmp(FX_BUILTINS[i].name, name) == 0) return &FX_BUILTINS[i];
    }
    return NULL;
}

static void push_scope_ex(Codegen* cg, const char* name, int32_t offset, int32_t array_len,
                           const FxStructDef* struct_def, bool struct_via_pointer) {
    if (cg->scope_len == cg->scope_cap) {
        cg->scope_cap = cg->scope_cap ? cg->scope_cap * 2 : 16;
        cg->scope = realloc(cg->scope, sizeof(FxScopeBinding) * cg->scope_cap);
    }
    cg->scope[cg->scope_len].name = (char*) name; /* borrowed from AST, not owned */
    cg->scope[cg->scope_len].offset = offset;
    cg->scope[cg->scope_len].array_len = array_len;
    cg->scope[cg->scope_len].struct_def = struct_def;
    cg->scope[cg->scope_len].struct_via_pointer = struct_via_pointer;
    cg->scope[cg->scope_len].is_byte = false;
    cg->scope_len++;
}

static void push_scope(Codegen* cg, const char* name, int32_t offset, int32_t array_len) {
    push_scope_ex(cg, name, offset, array_len, NULL, false);
}

/* Array parameter holding a decayed `byte[]` address (Phase A1,
 * docs/quill_fluxio.md) -- same FX_BINDING_LOCAL_SCALAR shape as an
 * `int[]` param (no compile-time-known length either way), but indexing
 * must use 1-byte stride instead of 4. */
static void push_scope_byte_array_param(Codegen* cg, const char* name, int32_t offset) {
    push_scope_ex(cg, name, offset, 0, NULL, false);
    cg->scope[cg->scope_len - 1].is_byte = true;
}

/* ---- generic AST walker (used for the analysis passes only) ---- */

typedef void (*FxVisitFn)(void* ctx, FxNode* node);

static void walk_node(FxNode* n, FxVisitFn visit, void* ctx) {
    if (!n) return;
    visit(ctx, n);
    switch (n->kind) {
        case FX_INT_LIT:
        case FX_STR_LIT:
        case FX_VAR_REF:
        case FX_EMPTY:
            break;
        case FX_ASSIGN:
            walk_node(n->as.assign.value, visit, ctx);
            break;
        case FX_BINARY:
            walk_node(n->as.binary.l, visit, ctx);
            walk_node(n->as.binary.r, visit, ctx);
            break;
        case FX_UNARY:
            walk_node(n->as.unary.operand, visit, ctx);
            break;
        case FX_CALL:
            for (int i = 0; i < n->as.call.nargs; i++) walk_node(n->as.call.args[i], visit, ctx);
            break;
        case FX_INDEX:
            walk_node(n->as.index.index, visit, ctx);
            break;
        case FX_INDEX_ASSIGN:
            walk_node(n->as.index_assign.index, visit, ctx);
            walk_node(n->as.index_assign.value, visit, ctx);
            break;
        case FX_FIELD:
            break; /* no sub-expressions */
        case FX_FIELD_ASSIGN:
            walk_node(n->as.field_assign.value, visit, ctx);
            break;
        case FX_LOCAL_DECL:
            walk_node(n->as.local_decl.init, visit, ctx);
            break;
        case FX_EXPR_STMT:
            walk_node(n->as.expr_stmt.expr, visit, ctx);
            break;
        case FX_IF:
            walk_node(n->as.if_s.cond, visit, ctx);
            walk_node(n->as.if_s.then_s, visit, ctx);
            walk_node(n->as.if_s.else_s, visit, ctx);
            break;
        case FX_WHILE:
            walk_node(n->as.while_s.cond, visit, ctx);
            walk_node(n->as.while_s.body, visit, ctx);
            break;
        case FX_FOR:
            walk_node(n->as.for_s.init, visit, ctx);
            walk_node(n->as.for_s.cond, visit, ctx);
            walk_node(n->as.for_s.post, visit, ctx);
            walk_node(n->as.for_s.body, visit, ctx);
            break;
        case FX_RETURN:
            walk_node(n->as.ret.expr, visit, ctx);
            break;
        case FX_BLOCK:
            for (int i = 0; i < n->as.block.nstmts; i++) walk_node(n->as.block.stmts[i], visit, ctx);
            break;
    }
}

static int count_locals(FxNode* n) {
    if (!n) return 0;
    switch (n->kind) {
        case FX_LOCAL_DECL: return n->as.local_decl.array_len > 0 ? n->as.local_decl.array_len : 1;
        case FX_BLOCK: {
            int c = 0;
            for (int i = 0; i < n->as.block.nstmts; i++) c += count_locals(n->as.block.stmts[i]);
            return c;
        }
        case FX_IF: return count_locals(n->as.if_s.then_s) + count_locals(n->as.if_s.else_s);
        case FX_WHILE: return count_locals(n->as.while_s.body);
        case FX_FOR: return count_locals(n->as.for_s.init) + count_locals(n->as.for_s.body);
        default: return 0;
    }
}

/* ---- call-site validation + call-graph edge collection ---- */

typedef struct {
    Codegen* cg;
    int caller_idx;
    bool* adj; /* nfuncs x nfuncs */
} CallVisitCtx;

static void visit_call(void* ctxv, FxNode* n) {
    if (n->kind != FX_CALL) return;
    CallVisitCtx* ctx = ctxv;
    Codegen* cg = ctx->cg;

    const FxBuiltinSpec* builtin = find_builtin(n->as.call.name);
    if (builtin) {
        if (n->as.call.nargs != builtin->argc) {
            cg_error(cg, n->line, "builtin '%s' expects %d argument(s) but %d given",
                      n->as.call.name, builtin->argc, n->as.call.nargs);
        }
        for (int i = 0; i < builtin->argc; i++) {
            bool is_str_lit = n->as.call.args[i]->kind == FX_STR_LIT;
            if (builtin->arg_kinds[i] == FX_ARG_STRING && !is_str_lit) {
                cg_error(cg, n->as.call.args[i]->line,
                    "builtin '%s' requires argument %d to be a string literal "
                    "(Fluxio has no general string-value support yet)", n->as.call.name, i + 1);
            }
            if (builtin->arg_kinds[i] == FX_ARG_INT && is_str_lit) {
                cg_error(cg, n->as.call.args[i]->line,
                    "builtin '%s' requires argument %d to be an int expression, not a string literal",
                    n->as.call.name, i + 1);
            }
        }
        return; /* builtins are not part of the user call graph */
    }

    const FxExtern* ext = find_extern(cg, n->as.call.name);
    if (ext) {
        if (ext->nparams != n->as.call.nargs) {
            cg_error(cg, n->line, "extern '%s' expects %d argument(s) but %d given",
                      n->as.call.name, ext->nparams, n->as.call.nargs);
        }
        return; /* externs are not part of the user call graph, same as builtins */
    }

    int callee_idx = find_func_index(cg, n->as.call.name);
    if (callee_idx < 0) {
        cg_error(cg, n->line, "call to undefined function '%s'", n->as.call.name);
    }
    if (cg->funcs[callee_idx].nparams != n->as.call.nargs) {
        cg_error(cg, n->line, "function '%s' expects %d argument(s) but %d given",
                  n->as.call.name, cg->funcs[callee_idx].nparams, n->as.call.nargs);
    }
    ctx->adj[ctx->caller_idx * cg->nfuncs + callee_idx] = true;
}

typedef enum { FX_WHITE, FX_GRAY, FX_BLACK } FxColor;

static void dfs_cycle(Codegen* cg, bool* adj, FxColor* color, int* stack, int* stack_len,
                       int u, bool* in_cycle) {
    color[u] = FX_GRAY;
    stack[(*stack_len)++] = u;
    for (int v = 0; v < cg->nfuncs; v++) {
        if (!adj[u * cg->nfuncs + v]) continue;
        if (color[v] == FX_GRAY) {
            int idx = -1;
            for (int k = 0; k < *stack_len; k++) if (stack[k] == v) { idx = k; break; }
            for (int k = idx; k < *stack_len; k++) in_cycle[stack[k]] = true;
        } else if (color[v] == FX_WHITE) {
            dfs_cycle(cg, adj, color, stack, stack_len, v, in_cycle);
        }
    }
    (*stack_len)--;
    color[u] = FX_BLACK;
}

static void validate_calls_and_recursion(Codegen* cg) {
    bool* adj = calloc((size_t) cg->nfuncs * (size_t) cg->nfuncs, sizeof(bool));
    for (int i = 0; i < cg->nfuncs; i++) {
        CallVisitCtx ctx = { cg, i, adj };
        walk_node(cg->funcs[i].body, visit_call, &ctx);
    }

    FxColor* color = calloc(cg->nfuncs, sizeof(FxColor));
    bool* in_cycle = calloc(cg->nfuncs, sizeof(bool));
    int* stack = malloc(sizeof(int) * cg->nfuncs);
    for (int i = 0; i < cg->nfuncs; i++) {
        if (color[i] == FX_WHITE) {
            int stack_len = 0;
            dfs_cycle(cg, adj, color, stack, &stack_len, i, in_cycle);
        }
    }

    for (int i = 0; i < cg->nfuncs; i++) {
        if (in_cycle[i] && !cg->funcs[i].is_recursive) {
            free(adj); free(color); free(in_cycle); free(stack);
            cg_error(cg, cg->funcs[i].line,
                "function '%s' participates in recursion but is not declared "
                "'recursive(N)' (unbounded recursion is not permitted)", cg->funcs[i].name);
        }
    }

    free(adj); free(color); free(in_cycle); free(stack);
}

/* ---- fixups (forward function-call references) ---- */

static void add_fixup(Codegen* cg, size_t imm_offset, const char* name, int line) {
    if (cg->nfixups == cg->fixups_cap) {
        cg->fixups_cap = cg->fixups_cap ? cg->fixups_cap * 2 : 16;
        cg->fixups = realloc(cg->fixups, sizeof(FxFixup) * cg->fixups_cap);
    }
    cg->fixups[cg->nfixups].imm_offset = imm_offset;
    cg->fixups[cg->nfixups].name = strdup(name);
    cg->fixups[cg->nfixups].line = line;
    cg->nfixups++;
}

/* ---- expression codegen ---- */

static void codegen_expr(Codegen* cg, FxProgram* program, FxNode* n);
static void codegen_builtin_call(Codegen* cg, FxProgram* program, const FxBuiltinSpec* builtin, FxNode* call_node);

static uint8_t binop_opcode(FxOp op) {
    switch (op) {
        case FX_OP_ADD: return OP_ADD;
        case FX_OP_SUB: return OP_SUB;
        case FX_OP_MUL: return OP_MUL;
        case FX_OP_DIV: return OP_DIV;
        case FX_OP_MOD: return OP_MOD;
        case FX_OP_AND: return OP_AND;
        case FX_OP_OR:  return OP_OR;
        case FX_OP_XOR: return OP_XOR;
        case FX_OP_SHL: return OP_SHL;
        case FX_OP_SAR: return OP_SAR;
        case FX_OP_EQ:  return OP_EQ;
        case FX_OP_NEQ: return OP_NEQ;
        case FX_OP_LT:  return OP_LT;
        case FX_OP_LTE: return OP_LTE;
        case FX_OP_GT:  return OP_GT;
        case FX_OP_GTE: return OP_GTE;
        default: return 0xFF;
    }
}

/* Restricted-pointer-arithmetic discipline: `[]` is the only access syntax
 * Fluxio ever exposes for arrays/strings, and every access whose length is
 * known at compile time (locals, globals) is bounds-checked at runtime,
 * halting cleanly with a distinct sentinel rather than corrupting memory.
 * Assumes the index value is on top of the stack; leaves it there unchanged
 * if the check passes. */
static void emit_bounds_check(Codegen* cg, int32_t array_len) {
    emit_op(cg, OP_DUP);
    emit_imm_op(cg, OP_PUSH, 0);
    emit_op(cg, OP_LT);
    size_t jnz_lo = emit_imm_op(cg, OP_JNZ, 0);
    emit_op(cg, OP_DUP);
    emit_imm_op(cg, OP_PUSH, array_len);
    emit_op(cg, OP_GTE);
    size_t jnz_hi = emit_imm_op(cg, OP_JNZ, 0);
    size_t jmp_ok = emit_imm_op(cg, OP_JMP, 0);
    int32_t fault_addr = cur_addr(cg);
    patch_i32(cg, jnz_lo, fault_addr);
    patch_i32(cg, jnz_hi, fault_addr);
    emit_imm_op(cg, OP_PUSH, -2); /* sentinel: out-of-bounds array access (distinct from the recursion guard's -1) */
    emit_op(cg, OP_HALT);
    patch_i32(cg, jmp_ok, cur_addr(cg));
}

static void codegen_var_load(Codegen* cg, FxProgram* program, FxNode* n) {
    FxBinding b = resolve_binding(cg, program, n->as.var.name);
    switch (b.kind) {
        case FX_BINDING_LOCAL_SCALAR:
            emit_imm_op(cg, OP_PUSH, b.offset_or_addr);
            emit_op(cg, OP_LOCALGET);
            return;
        case FX_BINDING_GLOBAL_SCALAR:
            emit_imm_op(cg, OP_LOAD, b.offset_or_addr);
            return;
        case FX_BINDING_GLOBAL_ARRAY:
        case FX_BINDING_GLOBAL_STRUCT:
            /* Arrays/structs decay to their base address when used as a plain
             * value (e.g. passed to a function) -- legal because global
             * storage is a real, stable memory address. */
            emit_imm_op(cg, OP_PUSH, b.offset_or_addr);
            return;
        case FX_BINDING_PARAM_STRUCT:
            /* Already holds a decayed address (received from the caller) --
             * loading it just forwards that same address onward. */
            emit_imm_op(cg, OP_PUSH, b.offset_or_addr);
            emit_op(cg, OP_LOCALGET);
            return;
        case FX_BINDING_LOCAL_ARRAY:
            cg_error(cg, n->line,
                "cannot use local array '%s' as a value: local arrays live in the VM's "
                "frame-relative locals region, which has no stable memory address outside "
                "their own function. Index it directly ('%s[i]'), or declare it as a global "
                "if it needs to be passed to another function.",
                n->as.var.name, n->as.var.name);
            return;
        case FX_BINDING_LOCAL_STRUCT:
            cg_error(cg, n->line,
                "cannot use local struct '%s' as a value: local structs live in the VM's "
                "frame-relative locals region, which has no stable memory address outside "
                "their own function. Access its fields directly ('%s.field'), or declare it "
                "as a global if it needs to be passed to another function.",
                n->as.var.name, n->as.var.name);
            return;
        case FX_BINDING_NONE:
        default:
            cg_error(cg, n->line, "undefined variable '%s'", n->as.var.name);
    }
}

/* Forward decls: emit_store_byte/emit_load_byte are compiler-internal
 * byte-level memory helpers defined further down (see the "Cloister
 * bindings" section), needed here already for byte-array [] codegen
 * (Phase A1, docs/quill_fluxio.md). */
static void emit_store_byte(Codegen* cg);
static void emit_load_byte(Codegen* cg);

/* Emits the index-computation + bounds-check prefix shared by array reads
 * and writes, for a binding known to be an array (local or global). Leaves
 * the checked index value on top of the stack. */
static void codegen_checked_index(Codegen* cg, FxProgram* program, FxNode* index_expr, FxBinding b) {
    codegen_expr(cg, program, index_expr);
    emit_bounds_check(cg, b.array_len);
}

static void codegen_expr(Codegen* cg, FxProgram* program, FxNode* n) {
    switch (n->kind) {
        case FX_INT_LIT:
            emit_imm_op(cg, OP_PUSH, n->as.int_lit);
            return;
        case FX_VAR_REF:
            codegen_var_load(cg, program, n);
            return;
        case FX_ASSIGN: {
            FxBinding b = resolve_binding(cg, program, n->as.assign.name);
            if (b.kind == FX_BINDING_NONE) {
                cg_error(cg, n->line, "undefined variable '%s'", n->as.assign.name);
            }
            if (b.kind == FX_BINDING_LOCAL_ARRAY || b.kind == FX_BINDING_GLOBAL_ARRAY) {
                cg_error(cg, n->line,
                    "cannot assign to array '%s' as a whole; assign to individual elements "
                    "with '%s[i] = ...'", n->as.assign.name, n->as.assign.name);
            }
            if (b.kind == FX_BINDING_LOCAL_STRUCT || b.kind == FX_BINDING_GLOBAL_STRUCT ||
                b.kind == FX_BINDING_PARAM_STRUCT) {
                cg_error(cg, n->line,
                    "cannot assign to struct '%s' as a whole; assign to individual fields "
                    "with '%s.field = ...'", n->as.assign.name, n->as.assign.name);
            }
            codegen_expr(cg, program, n->as.assign.value);
            emit_op(cg, OP_DUP);
            if (b.kind == FX_BINDING_LOCAL_SCALAR) {
                emit_imm_op(cg, OP_PUSH, b.offset_or_addr);
                emit_op(cg, OP_LOCALSET);
            } else { /* FX_BINDING_GLOBAL_SCALAR */
                emit_imm_op(cg, OP_STORE, b.offset_or_addr);
            }
            return;
        }
        case FX_INDEX: {
            FxBinding b = resolve_binding(cg, program, n->as.index.name);
            switch (b.kind) {
                case FX_BINDING_LOCAL_ARRAY:
                    codegen_checked_index(cg, program, n->as.index.index, b);
                    emit_imm_op(cg, OP_PUSH, b.offset_or_addr);
                    emit_op(cg, OP_SWAP);
                    emit_op(cg, OP_SUB);
                    emit_op(cg, OP_LOCALGET);
                    return;
                case FX_BINDING_LOCAL_SCALAR:
                    /* Unchecked: this is an array parameter or other local holding a raw
                     * base address, with no compile-time-known length to check against. */
                    emit_imm_op(cg, OP_PUSH, b.offset_or_addr);
                    emit_op(cg, OP_LOCALGET);
                    codegen_expr(cg, program, n->as.index.index);
                    if (b.is_byte) {
                        emit_op(cg, OP_ADD);
                        emit_load_byte(cg);
                    } else {
                        emit_imm_op(cg, OP_PUSH, 4);
                        emit_op(cg, OP_MUL);
                        emit_op(cg, OP_ADD);
                        emit_op(cg, OP_LOADI);
                    }
                    return;
                case FX_BINDING_GLOBAL_ARRAY:
                    codegen_checked_index(cg, program, n->as.index.index, b);
                    if (b.is_byte) {
                        emit_imm_op(cg, OP_PUSH, b.offset_or_addr);
                        emit_op(cg, OP_ADD);
                        emit_load_byte(cg);
                    } else {
                        emit_imm_op(cg, OP_PUSH, 4);
                        emit_op(cg, OP_MUL);
                        emit_imm_op(cg, OP_PUSH, b.offset_or_addr);
                        emit_op(cg, OP_ADD);
                        emit_op(cg, OP_LOADI);
                    }
                    return;
                case FX_BINDING_GLOBAL_SCALAR:
                    emit_imm_op(cg, OP_LOAD, b.offset_or_addr);
                    codegen_expr(cg, program, n->as.index.index);
                    emit_imm_op(cg, OP_PUSH, 4);
                    emit_op(cg, OP_MUL);
                    emit_op(cg, OP_ADD);
                    emit_op(cg, OP_LOADI);
                    return;
                case FX_BINDING_NONE:
                default:
                    cg_error(cg, n->line, "undefined variable '%s'", n->as.index.name);
            }
            return;
        }
        case FX_INDEX_ASSIGN: {
            FxBinding b = resolve_binding(cg, program, n->as.index_assign.name);
            if (b.kind == FX_BINDING_NONE) {
                cg_error(cg, n->line, "undefined variable '%s'", n->as.index_assign.name);
            }
            codegen_expr(cg, program, n->as.index_assign.value);
            emit_op(cg, OP_DUP);
            switch (b.kind) {
                case FX_BINDING_LOCAL_ARRAY:
                    codegen_checked_index(cg, program, n->as.index_assign.index, b);
                    emit_imm_op(cg, OP_PUSH, b.offset_or_addr);
                    emit_op(cg, OP_SWAP);
                    emit_op(cg, OP_SUB);
                    emit_op(cg, OP_LOCALSET);
                    return;
                case FX_BINDING_LOCAL_SCALAR:
                    emit_imm_op(cg, OP_PUSH, b.offset_or_addr);
                    emit_op(cg, OP_LOCALGET);
                    codegen_expr(cg, program, n->as.index_assign.index);
                    if (b.is_byte) {
                        emit_op(cg, OP_ADD);
                        emit_store_byte(cg);
                    } else {
                        emit_imm_op(cg, OP_PUSH, 4);
                        emit_op(cg, OP_MUL);
                        emit_op(cg, OP_ADD);
                        emit_op(cg, OP_STOREI);
                    }
                    return;
                case FX_BINDING_GLOBAL_ARRAY:
                    codegen_checked_index(cg, program, n->as.index_assign.index, b);
                    if (b.is_byte) {
                        emit_imm_op(cg, OP_PUSH, b.offset_or_addr);
                        emit_op(cg, OP_ADD);
                        emit_store_byte(cg);
                    } else {
                        emit_imm_op(cg, OP_PUSH, 4);
                        emit_op(cg, OP_MUL);
                        emit_imm_op(cg, OP_PUSH, b.offset_or_addr);
                        emit_op(cg, OP_ADD);
                        emit_op(cg, OP_STOREI);
                    }
                    return;
                case FX_BINDING_GLOBAL_SCALAR:
                    emit_imm_op(cg, OP_LOAD, b.offset_or_addr);
                    codegen_expr(cg, program, n->as.index_assign.index);
                    emit_imm_op(cg, OP_PUSH, 4);
                    emit_op(cg, OP_MUL);
                    emit_op(cg, OP_ADD);
                    emit_op(cg, OP_STOREI);
                    return;
                case FX_BINDING_NONE:
                default:
                    return; /* unreachable: handled above */
            }
        }
        case FX_FIELD: {
            FxBinding b = resolve_binding(cg, program, n->as.field.name);
            if (b.kind != FX_BINDING_LOCAL_STRUCT && b.kind != FX_BINDING_GLOBAL_STRUCT &&
                b.kind != FX_BINDING_PARAM_STRUCT) {
                cg_error(cg, n->line, "'%s' is not a struct instance", n->as.field.name);
            }
            int field_index = find_field_index(b.struct_def, n->as.field.field);
            if (field_index < 0) {
                cg_error(cg, n->line, "struct '%s' has no field '%s'", b.struct_def->name, n->as.field.field);
            }
            switch (b.kind) {
                case FX_BINDING_LOCAL_STRUCT:
                    emit_imm_op(cg, OP_PUSH, b.offset_or_addr - field_index);
                    emit_op(cg, OP_LOCALGET);
                    return;
                case FX_BINDING_GLOBAL_STRUCT:
                    emit_imm_op(cg, OP_LOAD, b.offset_or_addr + field_index * 4);
                    return;
                case FX_BINDING_PARAM_STRUCT:
                    emit_imm_op(cg, OP_PUSH, b.offset_or_addr);
                    emit_op(cg, OP_LOCALGET);
                    emit_imm_op(cg, OP_PUSH, field_index * 4);
                    emit_op(cg, OP_ADD);
                    emit_op(cg, OP_LOADI);
                    return;
                default:
                    return; /* unreachable */
            }
        }
        case FX_FIELD_ASSIGN: {
            FxBinding b = resolve_binding(cg, program, n->as.field_assign.name);
            if (b.kind != FX_BINDING_LOCAL_STRUCT && b.kind != FX_BINDING_GLOBAL_STRUCT &&
                b.kind != FX_BINDING_PARAM_STRUCT) {
                cg_error(cg, n->line, "'%s' is not a struct instance", n->as.field_assign.name);
            }
            int field_index = find_field_index(b.struct_def, n->as.field_assign.field);
            if (field_index < 0) {
                cg_error(cg, n->line, "struct '%s' has no field '%s'", b.struct_def->name, n->as.field_assign.field);
            }
            codegen_expr(cg, program, n->as.field_assign.value);
            emit_op(cg, OP_DUP);
            switch (b.kind) {
                case FX_BINDING_LOCAL_STRUCT:
                    emit_imm_op(cg, OP_PUSH, b.offset_or_addr - field_index);
                    emit_op(cg, OP_LOCALSET);
                    return;
                case FX_BINDING_GLOBAL_STRUCT:
                    emit_imm_op(cg, OP_STORE, b.offset_or_addr + field_index * 4);
                    return;
                case FX_BINDING_PARAM_STRUCT:
                    emit_imm_op(cg, OP_PUSH, b.offset_or_addr);
                    emit_op(cg, OP_LOCALGET);
                    emit_imm_op(cg, OP_PUSH, field_index * 4);
                    emit_op(cg, OP_ADD);
                    emit_op(cg, OP_STOREI);
                    return;
                default:
                    return; /* unreachable */
            }
        }
        case FX_BINARY: {
            if (n->as.binary.op == FX_OP_LAND) {
                codegen_expr(cg, program, n->as.binary.l);
                size_t jz1 = emit_imm_op(cg, OP_JZ, 0);
                codegen_expr(cg, program, n->as.binary.r);
                size_t jz2 = emit_imm_op(cg, OP_JZ, 0);
                emit_imm_op(cg, OP_PUSH, 1);
                size_t jmp_end = emit_imm_op(cg, OP_JMP, 0);
                int32_t false_addr = cur_addr(cg);
                patch_i32(cg, jz1, false_addr);
                patch_i32(cg, jz2, false_addr);
                emit_imm_op(cg, OP_PUSH, 0);
                patch_i32(cg, jmp_end, cur_addr(cg));
                return;
            }
            if (n->as.binary.op == FX_OP_LOR) {
                codegen_expr(cg, program, n->as.binary.l);
                size_t jnz1 = emit_imm_op(cg, OP_JNZ, 0);
                codegen_expr(cg, program, n->as.binary.r);
                size_t jnz2 = emit_imm_op(cg, OP_JNZ, 0);
                emit_imm_op(cg, OP_PUSH, 0);
                size_t jmp_end = emit_imm_op(cg, OP_JMP, 0);
                int32_t true_addr = cur_addr(cg);
                patch_i32(cg, jnz1, true_addr);
                patch_i32(cg, jnz2, true_addr);
                emit_imm_op(cg, OP_PUSH, 1);
                patch_i32(cg, jmp_end, cur_addr(cg));
                return;
            }
            codegen_expr(cg, program, n->as.binary.l);
            codegen_expr(cg, program, n->as.binary.r);
            emit_op(cg, binop_opcode(n->as.binary.op));
            return;
        }
        case FX_UNARY:
            codegen_expr(cg, program, n->as.unary.operand);
            switch (n->as.unary.op) {
                case FX_OP_NEG: emit_op(cg, OP_NEG); break;
                case FX_OP_BNOT: emit_op(cg, OP_NOT); break;
                case FX_OP_LNOT: emit_imm_op(cg, OP_PUSH, 0); emit_op(cg, OP_EQ); break;
                case FX_OP_PLUS: break;
                default: break;
            }
            return;
        case FX_CALL: {
            const FxBuiltinSpec* builtin = find_builtin(n->as.call.name);
            if (builtin) {
                codegen_builtin_call(cg, program, builtin, n);
                return;
            }
            const FxExtern* ext = find_extern(cg, n->as.call.name);
            if (ext) {
                if (ext->is_void) {
                    cg_error(cg, n->line, "'%s' is declared 'extern void' and produces no value; "
                             "call it as a statement, not as an expression", ext->name);
                }
                for (int i = 0; i < n->as.call.nargs; i++) codegen_expr(cg, program, n->as.call.args[i]);
                emit_imm_op(cg, OP_CALL, ext->address);
                return;
            }
            int idx = find_func_index(cg, n->as.call.name);
            if (idx < 0) cg_error(cg, n->line, "call to undefined function '%s'", n->as.call.name);
            for (int i = 0; i < n->as.call.nargs; i++) codegen_expr(cg, program, n->as.call.args[i]);
            if (cg->func_addrs[idx] >= 0) {
                emit_imm_op(cg, OP_CALL, cg->func_addrs[idx]);
            } else {
                size_t off = emit_imm_op(cg, OP_CALL, 0);
                add_fixup(cg, off, n->as.call.name, n->line);
            }
            return;
        }
        case FX_STR_LIT:
            cg_error(cg, n->line,
                "string literals can only be used as array/local initializers "
                "('int s[] = \"...\";') or as arguments to builtins that accept them "
                "(e.g. vfs_open, draw_str) -- Fluxio has no general string-value type yet");
            return;
        default:
            cg_error(cg, n->line, "internal error: unexpected node in expression context");
    }
}

/* ---- Cloister bindings: byte-level memory access (compiler-internal only) ----
 *
 * Never exposed to Fluxio source -- user code stays restricted to
 * bounds-checked `[]` indexing (the JSF-inspired "restricted pointer
 * arithmetic" discipline). These exist purely so the compiler itself can
 * pack/unpack the byte-oriented SCI/VFS/draw-command wire format, which
 * needs individual-byte fields at offsets the VM's word-only LOAD/STORE
 * can't address directly.
 *
 * Memory words are big-endian (src/vm.c write_mem32/read_mem32): memory[addr]
 * is the MSB, memory[addr+3] the LSB. So byte offset k within an aligned
 * word sits at bit position (3-k)*8 -- that's the shift these compute. */

/* Stack in: [..., value, addr] (addr on top). Read-modify-writes the low
 * byte of value into memory[addr]. Consumes both, leaves nothing. */
static void emit_store_byte(Codegen* cg) {
    emit_imm_op(cg, OP_STORE, cg->scratch_addr); /* pop addr; stack: [value] */

    emit_imm_op(cg, OP_LOAD, cg->scratch_addr);
    emit_imm_op(cg, OP_PUSH, 3);
    emit_op(cg, OP_AND);           /* [value, addr&3] */
    emit_imm_op(cg, OP_PUSH, 3);
    emit_op(cg, OP_SWAP);
    emit_op(cg, OP_SUB);            /* [value, 3-(addr&3)] */
    emit_imm_op(cg, OP_PUSH, 8);
    emit_op(cg, OP_MUL);             /* [value, shift] */
    emit_imm_op(cg, OP_STORE, cg->scratch_shift); /* pop shift; stack: [value] */

    emit_imm_op(cg, OP_LOAD, cg->scratch_addr);
    emit_imm_op(cg, OP_PUSH, 3);
    emit_op(cg, OP_NOT);
    emit_op(cg, OP_AND);             /* [value, aligned] */
    emit_op(cg, OP_LOADI);            /* [value, old_word] */

    emit_imm_op(cg, OP_PUSH, 0xFF);
    emit_imm_op(cg, OP_LOAD, cg->scratch_shift);
    emit_op(cg, OP_SHL);              /* [value, old_word, 0xFF<<shift] */
    emit_op(cg, OP_NOT);
    emit_op(cg, OP_AND);               /* [value, cleared_word] */
    emit_imm_op(cg, OP_STORE, cg->scratch_word); /* pop cleared_word; stack: [value] */

    emit_imm_op(cg, OP_PUSH, 0xFF);
    emit_op(cg, OP_AND);                /* [value & 0xFF] */
    emit_imm_op(cg, OP_LOAD, cg->scratch_shift);
    emit_op(cg, OP_SHL);                 /* [(value&0xFF)<<shift] */
    emit_imm_op(cg, OP_LOAD, cg->scratch_word);
    emit_op(cg, OP_OR);                   /* [new_word] */

    emit_imm_op(cg, OP_LOAD, cg->scratch_addr);
    emit_imm_op(cg, OP_PUSH, 3);
    emit_op(cg, OP_NOT);
    emit_op(cg, OP_AND);                   /* [new_word, aligned] */
    emit_op(cg, OP_STOREI);                 /* pops addr then value; stack: [] */
}

/* Stack in: [..., addr]. Pushes the byte at addr (0..255). */
static void emit_load_byte(Codegen* cg) {
    emit_imm_op(cg, OP_STORE, cg->scratch_addr); /* pop addr; stack: [] */

    emit_imm_op(cg, OP_LOAD, cg->scratch_addr);
    emit_imm_op(cg, OP_PUSH, 3);
    emit_op(cg, OP_AND);
    emit_imm_op(cg, OP_PUSH, 3);
    emit_op(cg, OP_SWAP);
    emit_op(cg, OP_SUB);
    emit_imm_op(cg, OP_PUSH, 8);
    emit_op(cg, OP_MUL);
    emit_imm_op(cg, OP_STORE, cg->scratch_shift); /* stack: [] */

    emit_imm_op(cg, OP_LOAD, cg->scratch_addr);
    emit_imm_op(cg, OP_PUSH, 3);
    emit_op(cg, OP_NOT);
    emit_op(cg, OP_AND);
    emit_op(cg, OP_LOADI);            /* [word] */
    emit_imm_op(cg, OP_LOAD, cg->scratch_shift);
    emit_op(cg, OP_SHR);               /* [word >>> shift] */
    emit_imm_op(cg, OP_PUSH, 0xFF);
    emit_op(cg, OP_AND);                /* [byte] */
}

/* Packs one word-aligned 32-bit field of a /dev/draw command from a runtime
 * expression. The wire format gives every field its own word (see draw_write
 * in src/vfs.c), so this is a single aligned store rather than the
 * byte-at-a-time decomposition emit_pack_field has to do for byte-packed
 * layouts -- byte access is not a VM opcode, so each byte costs a
 * load/mask/shift/store sequence. */
static void emit_pack_word(Codegen* cg, FxProgram* program, FxNode* value_expr,
                           int32_t buf_addr, int32_t word_index) {
    codegen_expr(cg, program, value_expr);
    emit_imm_op(cg, OP_STORE, buf_addr + word_index * 4);
}

/* Same, for a compile-time-constant field (a command tag, a length). */
static void emit_pack_const_word(Codegen* cg, int32_t buf_addr, int32_t word_index, int32_t value) {
    emit_imm_op(cg, OP_PUSH, value);
    emit_imm_op(cg, OP_STORE, buf_addr + word_index * 4);
}

/* Packs a compile-time string literal as whole words, zero padding the last
 * one. Used only for DrawString's text payload, whose length travels in its
 * own header word -- unlike emit_pack_string_literal below, which packs
 * NUL-terminated C strings for path/title arguments and must keep its
 * terminator. Writing 4 bytes per store instead of 1 keeps the padded text
 * region aligned and costs a quarter of the runtime stores.
 *
 * The first character of each group goes in the most significant byte: VM
 * words are big-endian (write_mem32 in src/vm.c), so that is what puts
 * character i at byte offset i for the host to read back. */
static void emit_pack_string_literal_words(Codegen* cg, FxNode* str_node,
                                           int32_t buf_addr, int32_t byte_offset) {
    int32_t len = str_node->as.str_lit.len;
    for (int32_t w = 0; w * 4 < len; w++) {
        uint32_t packed = 0;
        for (int b = 0; b < 4; b++) {
            int32_t idx = w * 4 + b;
            if (idx < len) {
                packed |= (uint32_t)(unsigned char)str_node->as.str_lit.value[idx] << ((3 - b) * 8);
            }
        }
        emit_imm_op(cg, OP_PUSH, (int32_t)packed);
        emit_imm_op(cg, OP_STORE, buf_addr + byte_offset + w * 4);
    }
}

/* Packs a single compile-time-constant byte (a command-type tag, or one
 * byte of a compile-time-known string literal). */
static void emit_pack_const_byte(Codegen* cg, int32_t buf_addr, int32_t field_offset, int32_t value) {
    emit_imm_op(cg, OP_PUSH, value & 0xFF);
    emit_imm_op(cg, OP_PUSH, buf_addr + field_offset);
    emit_store_byte(cg);
}

/* Packs a string literal's bytes + NUL terminator, entirely at compile
 * time (the content is known at codegen time, so every byte is a constant
 * store -- no runtime string-to-bytes conversion is needed). */
static void emit_pack_string_literal(Codegen* cg, FxNode* str_node, int32_t buf_addr, int32_t field_offset) {
    for (int32_t i = 0; i < str_node->as.str_lit.len; i++) {
        emit_pack_const_byte(cg, buf_addr, field_offset + i, (unsigned char) str_node->as.str_lit.value[i]);
    }
    emit_pack_const_byte(cg, buf_addr, field_offset + str_node->as.str_lit.len, 0);
}

/* ---- Cloister bindings: SCI/VFS/draw builtin codegen ----
 *
 * SCI call protocol (src/system.c:11-14, confirmed against src/vm.c): STORE
 * cmd/arg1/(arg3) in any order, STORE arg2 LAST (that write fires the
 * syscall), then LOADI SCI_PORT for the result. Draw commands additionally
 * go through SCI_VFS_WRITE(fd, buf_ptr, len) onto a "/dev/draw" fd, with the
 * command bytes pre-packed into cg->sci_buf_addr. */
static void codegen_builtin_call(Codegen* cg, FxProgram* program, const FxBuiltinSpec* builtin, FxNode* call_node) {
    const char* name = builtin->name;
    FxNode** args = call_node->as.call.args;

    if (strcmp(name, "emit") == 0) {
        codegen_expr(cg, program, args[0]);
        emit_imm_op(cg, OP_PUSH, 1);
        emit_op(cg, OP_OUT);
        emit_imm_op(cg, OP_PUSH, 0);
        return;
    }
    if (strcmp(name, "print") == 0) {
        codegen_expr(cg, program, args[0]);
        emit_imm_op(cg, OP_PUSH, 0);
        emit_op(cg, OP_OUT);
        emit_imm_op(cg, OP_PUSH, 0);
        return;
    }

    if (strcmp(name, "vfs_open") == 0) {
        emit_pack_string_literal(cg, args[0], cg->sci_buf_addr, 0);
        emit_imm_op(cg, OP_PUSH, cg->sci_buf_addr);
        emit_imm_op(cg, OP_STORE, FX_SCI_ARG1_ADDR);
        emit_imm_op(cg, OP_PUSH, FX_SCI_CMD_VFS_OPEN);
        emit_imm_op(cg, OP_STORE, FX_SCI_CMD_ADDR);
        emit_imm_op(cg, OP_PUSH, 0);
        emit_imm_op(cg, OP_STORE, FX_SCI_ARG2_ADDR); /* flags=0, triggers */
        emit_imm_op(cg, OP_LOAD, FX_SCI_PORT);
        return;
    }
    if (strcmp(name, "vfs_close") == 0) {
        codegen_expr(cg, program, args[0]);
        emit_imm_op(cg, OP_STORE, FX_SCI_ARG1_ADDR);
        emit_imm_op(cg, OP_PUSH, FX_SCI_CMD_VFS_CLOSE);
        emit_imm_op(cg, OP_STORE, FX_SCI_CMD_ADDR);
        emit_imm_op(cg, OP_PUSH, 0);
        emit_imm_op(cg, OP_STORE, FX_SCI_ARG2_ADDR);
        emit_imm_op(cg, OP_LOAD, FX_SCI_PORT);
        return;
    }
    if (strcmp(name, "vfs_open_buf") == 0) {
        /* SCI_VFS_OPEN reads a NUL-terminated C string from arg1 (system.c's
         * read loop) -- it has no length parameter of its own, so a runtime
         * buffer+length path only works if we NUL-terminate it ourselves at
         * buf[len] first. Caller must reserve len+1 bytes. */
        codegen_expr(cg, program, args[0]); /* buf */
        emit_imm_op(cg, OP_STORE, cg->scratch_field); /* stash: needed twice below */
        emit_imm_op(cg, OP_PUSH, 0);
        emit_imm_op(cg, OP_LOAD, cg->scratch_field);
        codegen_expr(cg, program, args[1]); /* len */
        emit_op(cg, OP_ADD);
        emit_store_byte(cg); /* memory[buf+len] = 0 */
        emit_imm_op(cg, OP_LOAD, cg->scratch_field);
        emit_imm_op(cg, OP_STORE, FX_SCI_ARG1_ADDR);
        emit_imm_op(cg, OP_PUSH, FX_SCI_CMD_VFS_OPEN);
        emit_imm_op(cg, OP_STORE, FX_SCI_CMD_ADDR);
        codegen_expr(cg, program, args[2]); /* flags */
        emit_imm_op(cg, OP_STORE, FX_SCI_ARG2_ADDR);
        emit_imm_op(cg, OP_LOAD, FX_SCI_PORT);
        return;
    }
    if (strcmp(name, "vfs_read") == 0) {
        codegen_expr(cg, program, args[0]); /* fd */
        emit_imm_op(cg, OP_STORE, FX_SCI_ARG1_ADDR);
        codegen_expr(cg, program, args[2]); /* maxlen */
        emit_imm_op(cg, OP_STORE, FX_SCI_ARG3_ADDR);
        emit_imm_op(cg, OP_PUSH, FX_SCI_CMD_VFS_READ);
        emit_imm_op(cg, OP_STORE, FX_SCI_CMD_ADDR);
        codegen_expr(cg, program, args[1]); /* buf */
        emit_imm_op(cg, OP_STORE, FX_SCI_ARG2_ADDR);
        emit_imm_op(cg, OP_LOAD, FX_SCI_PORT);
        return;
    }
    if (strcmp(name, "vfs_write") == 0) {
        codegen_expr(cg, program, args[0]); /* fd */
        emit_imm_op(cg, OP_STORE, FX_SCI_ARG1_ADDR);
        codegen_expr(cg, program, args[2]); /* len */
        emit_imm_op(cg, OP_STORE, FX_SCI_ARG3_ADDR);
        emit_imm_op(cg, OP_PUSH, FX_SCI_CMD_VFS_WRITE);
        emit_imm_op(cg, OP_STORE, FX_SCI_CMD_ADDR);
        codegen_expr(cg, program, args[1]); /* buf */
        emit_imm_op(cg, OP_STORE, FX_SCI_ARG2_ADDR);
        emit_imm_op(cg, OP_LOAD, FX_SCI_PORT);
        return;
    }
    if (strcmp(name, "vfs_seek") == 0) {
        codegen_expr(cg, program, args[0]); /* fd */
        emit_imm_op(cg, OP_STORE, FX_SCI_ARG1_ADDR);
        emit_imm_op(cg, OP_PUSH, FX_SCI_CMD_VFS_SEEK);
        emit_imm_op(cg, OP_STORE, FX_SCI_CMD_ADDR);
        codegen_expr(cg, program, args[1]); /* pos */
        emit_imm_op(cg, OP_STORE, FX_SCI_ARG2_ADDR);
        emit_imm_op(cg, OP_LOAD, FX_SCI_PORT);
        return;
    }
    if (strcmp(name, "vfs_stat") == 0) {
        codegen_expr(cg, program, args[0]); /* fd */
        emit_imm_op(cg, OP_STORE, FX_SCI_ARG1_ADDR);
        emit_imm_op(cg, OP_PUSH, FX_SCI_CMD_VFS_STAT);
        emit_imm_op(cg, OP_STORE, FX_SCI_CMD_ADDR);
        emit_imm_op(cg, OP_PUSH, 0);
        emit_imm_op(cg, OP_STORE, FX_SCI_ARG2_ADDR);
        emit_imm_op(cg, OP_LOAD, FX_SCI_PORT);
        return;
    }
    if (strcmp(name, "vfs_write_chunk") == 0) {
        /* Packs the 5-word param block SCI_VFS_WRITE_CHUNK expects, same as
         * VFS::write-chunk (lib/vfs.lux) does for Lux -- system.c's handler
         * currently only consumes fd/bufPtr/length (offset/orig_len are
         * reserved for a fuller chunked-write implementation), but the
         * whole block is packed regardless so this builtin's call signature
         * won't need to change if/when that lands. */
        codegen_expr(cg, program, args[0]); emit_imm_op(cg, OP_STORE, cg->sci_buf_addr + 0);  /* fd */
        codegen_expr(cg, program, args[1]); emit_imm_op(cg, OP_STORE, cg->sci_buf_addr + 4);  /* buf */
        codegen_expr(cg, program, args[2]); emit_imm_op(cg, OP_STORE, cg->sci_buf_addr + 8);  /* len */
        codegen_expr(cg, program, args[3]); emit_imm_op(cg, OP_STORE, cg->sci_buf_addr + 12); /* offset */
        codegen_expr(cg, program, args[4]); emit_imm_op(cg, OP_STORE, cg->sci_buf_addr + 16); /* orig_len */
        emit_imm_op(cg, OP_PUSH, cg->sci_buf_addr);
        emit_imm_op(cg, OP_STORE, FX_SCI_ARG1_ADDR);
        emit_imm_op(cg, OP_PUSH, FX_SCI_CMD_VFS_WRITE_CHUNK);
        emit_imm_op(cg, OP_STORE, FX_SCI_CMD_ADDR);
        emit_imm_op(cg, OP_PUSH, 0);
        emit_imm_op(cg, OP_STORE, FX_SCI_ARG2_ADDR);
        emit_imm_op(cg, OP_LOAD, FX_SCI_PORT);
        return;
    }
    if (strcmp(name, "yield") == 0) {
        emit_imm_op(cg, OP_PUSH, FX_SCI_CMD_YIELD);
        emit_imm_op(cg, OP_STORE, FX_SCI_CMD_ADDR);
        emit_imm_op(cg, OP_PUSH, 0);
        emit_imm_op(cg, OP_STORE, FX_SCI_ARG1_ADDR);
        emit_imm_op(cg, OP_PUSH, 0);
        emit_imm_op(cg, OP_STORE, FX_SCI_ARG2_ADDR);
        emit_imm_op(cg, OP_LOAD, FX_SCI_PORT);
        return;
    }
    if (strcmp(name, "set_window_title") == 0) {
        emit_pack_string_literal(cg, args[0], cg->sci_buf_addr, 0);
        emit_imm_op(cg, OP_PUSH, cg->sci_buf_addr);
        emit_imm_op(cg, OP_STORE, FX_SCI_ARG1_ADDR);
        emit_imm_op(cg, OP_PUSH, FX_SCI_CMD_SET_WINDOW_TITLE);
        emit_imm_op(cg, OP_STORE, FX_SCI_CMD_ADDR);
        emit_imm_op(cg, OP_PUSH, 0);
        emit_imm_op(cg, OP_STORE, FX_SCI_ARG2_ADDR);
        emit_imm_op(cg, OP_LOAD, FX_SCI_PORT);
        return;
    }
    if (strcmp(name, "canvas_size") == 0) {
        /* SCI_VFS_READ(fd, sci_buf, 4) -> 4 bytes: w:u16 LE, h:u16 LE.
         * Returns (w<<16)|h; caller unpacks with existing >>/& operators. */
        codegen_expr(cg, program, args[0]);
        emit_imm_op(cg, OP_STORE, FX_SCI_ARG1_ADDR);
        emit_imm_op(cg, OP_PUSH, 4);
        emit_imm_op(cg, OP_STORE, FX_SCI_ARG3_ADDR);
        emit_imm_op(cg, OP_PUSH, FX_SCI_CMD_VFS_READ);
        emit_imm_op(cg, OP_STORE, FX_SCI_CMD_ADDR);
        emit_imm_op(cg, OP_PUSH, cg->sci_buf_addr);
        emit_imm_op(cg, OP_STORE, FX_SCI_ARG2_ADDR);
        emit_imm_op(cg, OP_LOAD, FX_SCI_PORT);
        emit_op(cg, OP_POP); /* discard bytes-read count */

        emit_imm_op(cg, OP_PUSH, cg->sci_buf_addr + 0); emit_load_byte(cg);
        emit_imm_op(cg, OP_PUSH, cg->sci_buf_addr + 1); emit_load_byte(cg);
        emit_imm_op(cg, OP_PUSH, 8); emit_op(cg, OP_SHL);
        emit_op(cg, OP_OR); /* [w] */
        emit_imm_op(cg, OP_STORE, cg->scratch_field);

        emit_imm_op(cg, OP_PUSH, cg->sci_buf_addr + 2); emit_load_byte(cg);
        emit_imm_op(cg, OP_PUSH, cg->sci_buf_addr + 3); emit_load_byte(cg);
        emit_imm_op(cg, OP_PUSH, 8); emit_op(cg, OP_SHL);
        emit_op(cg, OP_OR); /* [h] */

        emit_imm_op(cg, OP_LOAD, cg->scratch_field); /* [h, w] */
        emit_imm_op(cg, OP_PUSH, 16); emit_op(cg, OP_SHL); /* [h, w<<16] */
        emit_op(cg, OP_OR); /* [(w<<16)|h] */
        return;
    }
    if (strcmp(name, "begin_frame") == 0 || strcmp(name, "end_frame") == 0) {
        int32_t cmd_byte = strcmp(name, "begin_frame") == 0 ? FX_DRAW_CMD_BEGIN_FRAME : FX_DRAW_CMD_END_FRAME;
        emit_pack_const_word(cg, cg->sci_buf_addr, 0, cmd_byte);
        codegen_expr(cg, program, args[0]);
        emit_imm_op(cg, OP_STORE, FX_SCI_ARG1_ADDR);
        emit_imm_op(cg, OP_PUSH, 4);
        emit_imm_op(cg, OP_STORE, FX_SCI_ARG3_ADDR);
        emit_imm_op(cg, OP_PUSH, FX_SCI_CMD_VFS_WRITE);
        emit_imm_op(cg, OP_STORE, FX_SCI_CMD_ADDR);
        emit_imm_op(cg, OP_PUSH, cg->sci_buf_addr);
        emit_imm_op(cg, OP_STORE, FX_SCI_ARG2_ADDR);
        emit_imm_op(cg, OP_LOAD, FX_SCI_PORT);
        return;
    }
    if (strcmp(name, "fill_rect") == 0) {
        /* cmd, x, y, w, h, color -- one word each = 24 bytes */
        emit_pack_const_word(cg, cg->sci_buf_addr, 0, FX_DRAW_CMD_FILL_RECT);
        emit_pack_word(cg, program, args[1], cg->sci_buf_addr, 1);
        emit_pack_word(cg, program, args[2], cg->sci_buf_addr, 2);
        emit_pack_word(cg, program, args[3], cg->sci_buf_addr, 3);
        emit_pack_word(cg, program, args[4], cg->sci_buf_addr, 4);
        emit_pack_word(cg, program, args[5], cg->sci_buf_addr, 5);
        codegen_expr(cg, program, args[0]);
        emit_imm_op(cg, OP_STORE, FX_SCI_ARG1_ADDR);
        emit_imm_op(cg, OP_PUSH, 24);
        emit_imm_op(cg, OP_STORE, FX_SCI_ARG3_ADDR);
        emit_imm_op(cg, OP_PUSH, FX_SCI_CMD_VFS_WRITE);
        emit_imm_op(cg, OP_STORE, FX_SCI_CMD_ADDR);
        emit_imm_op(cg, OP_PUSH, cg->sci_buf_addr);
        emit_imm_op(cg, OP_STORE, FX_SCI_ARG2_ADDR);
        emit_imm_op(cg, OP_LOAD, FX_SCI_PORT);
        return;
    }
    if (strcmp(name, "draw_str") == 0) {
        /* cmd, x, y, color, scale, len -- one word each -- then text[len]
         * padded up to a word boundary. */
        FxNode* text_node = args[5];
        int32_t text_len = text_node->as.str_lit.len;
        int32_t total_len = 24 + ((text_len + 3) & ~3);
        if (total_len > FX_SCI_BUF_SIZE) {
            cg_error(cg, call_node->line,
                "draw_str text too long (%d bytes): exceeds the %d-byte draw scratch buffer",
                text_len, FX_SCI_BUF_SIZE);
        }
        emit_pack_const_word(cg, cg->sci_buf_addr, 0, FX_DRAW_CMD_DRAW_STRING);
        emit_pack_word(cg, program, args[1], cg->sci_buf_addr, 1);
        emit_pack_word(cg, program, args[2], cg->sci_buf_addr, 2);
        emit_pack_word(cg, program, args[3], cg->sci_buf_addr, 3);
        emit_pack_word(cg, program, args[4], cg->sci_buf_addr, 4);
        emit_pack_const_word(cg, cg->sci_buf_addr, 5, text_len);
        emit_pack_string_literal_words(cg, text_node, cg->sci_buf_addr, 24);
        codegen_expr(cg, program, args[0]);
        emit_imm_op(cg, OP_STORE, FX_SCI_ARG1_ADDR);
        emit_imm_op(cg, OP_PUSH, total_len);
        emit_imm_op(cg, OP_STORE, FX_SCI_ARG3_ADDR);
        emit_imm_op(cg, OP_PUSH, FX_SCI_CMD_VFS_WRITE);
        emit_imm_op(cg, OP_STORE, FX_SCI_CMD_ADDR);
        emit_imm_op(cg, OP_PUSH, cg->sci_buf_addr);
        emit_imm_op(cg, OP_STORE, FX_SCI_ARG2_ADDR);
        emit_imm_op(cg, OP_LOAD, FX_SCI_PORT);
        return;
    }
    if (strcmp(name, "draw_bytes") == 0) {
        /* Same wire format as draw_str (cmd, x, y, color, scale, len -- one
         * word each -- then text[len] padded to a word), but the text bytes
         * come from a runtime buffer+length instead of a compile-time string
         * literal, so this needs a real emitted copy loop. The text copy stays
         * byte-granular: the source pointer is a runtime value with no
         * alignment guarantee. */
        int32_t text_off = 24;
        int32_t max_text = FX_SCI_BUF_SIZE - text_off;

        emit_pack_const_word(cg, cg->sci_buf_addr, 0, FX_DRAW_CMD_DRAW_STRING);
        emit_pack_word(cg, program, args[1], cg->sci_buf_addr, 1); /* x */
        emit_pack_word(cg, program, args[2], cg->sci_buf_addr, 2); /* y */
        emit_pack_word(cg, program, args[3], cg->sci_buf_addr, 3); /* color */
        emit_pack_word(cg, program, args[4], cg->sci_buf_addr, 4); /* scale */

        /* n = clamp(len, 0, max_text): an oversized len would overrun the
         * fixed sci_buf_addr scratch region into adjacent reserved scratch
         * (mouse/kbd event buffers etc); a negative len, left unclamped,
         * would underflow into a huge copy in the loop below. */
        codegen_expr(cg, program, args[6]); /* len */
        emit_imm_op(cg, OP_PUSH, 0);
        emit_op(cg, OP_MAX);
        emit_imm_op(cg, OP_PUSH, max_text);
        emit_op(cg, OP_MIN);
        emit_imm_op(cg, OP_STORE, cg->scratch_field); /* n */

        /* len field of the wire format: word 5, a plain aligned store. */
        emit_imm_op(cg, OP_LOAD, cg->scratch_field);
        emit_imm_op(cg, OP_STORE, cg->sci_buf_addr + 20);

        /* SCI call args, set up before the copy loop below consumes
         * scratch_field as its countdown counter -- store order among
         * these three doesn't matter, only that ARG2 (below) is stored
         * last, since that write is what fires the syscall. */
        codegen_expr(cg, program, args[0]); /* fd */
        emit_imm_op(cg, OP_STORE, FX_SCI_ARG1_ADDR);
        /* total_len = text_off + round_up(n, 4). The text is padded so the
         * next command's tag stays word-aligned; draw_write skips the same
         * padding, so its contents are never read. */
        emit_imm_op(cg, OP_PUSH, text_off);
        emit_imm_op(cg, OP_LOAD, cg->scratch_field);
        emit_imm_op(cg, OP_PUSH, 3);
        emit_op(cg, OP_ADD);
        emit_imm_op(cg, OP_PUSH, 4);
        emit_op(cg, OP_DIV);
        emit_imm_op(cg, OP_PUSH, 4);
        emit_op(cg, OP_MUL);
        emit_op(cg, OP_ADD);
        emit_imm_op(cg, OP_STORE, FX_SCI_ARG3_ADDR);
        emit_imm_op(cg, OP_PUSH, FX_SCI_CMD_VFS_WRITE);
        emit_imm_op(cg, OP_STORE, FX_SCI_CMD_ADDR);

        /* Copy n bytes from the runtime buffer into sci_buf_addr+text_off,
         * one byte per iteration; scratch_field doubles as both the
         * remaining-count and the loop condition (0 = done). */
        codegen_expr(cg, program, args[5]); /* buf */
        emit_imm_op(cg, OP_STORE, cg->scratch_copy_src);
        emit_imm_op(cg, OP_PUSH, cg->sci_buf_addr + text_off);
        emit_imm_op(cg, OP_STORE, cg->scratch_copy_dst);

        int32_t loop_top = cur_addr(cg);
        emit_imm_op(cg, OP_LOAD, cg->scratch_field);
        size_t jz_done = emit_imm_op(cg, OP_JZ, 0);

        emit_imm_op(cg, OP_LOAD, cg->scratch_copy_src);
        emit_load_byte(cg);
        emit_imm_op(cg, OP_LOAD, cg->scratch_copy_dst);
        emit_store_byte(cg);

        emit_imm_op(cg, OP_LOAD, cg->scratch_copy_src);
        emit_imm_op(cg, OP_PUSH, 1);
        emit_op(cg, OP_ADD);
        emit_imm_op(cg, OP_STORE, cg->scratch_copy_src);
        emit_imm_op(cg, OP_LOAD, cg->scratch_copy_dst);
        emit_imm_op(cg, OP_PUSH, 1);
        emit_op(cg, OP_ADD);
        emit_imm_op(cg, OP_STORE, cg->scratch_copy_dst);
        emit_imm_op(cg, OP_LOAD, cg->scratch_field);
        emit_imm_op(cg, OP_PUSH, 1);
        emit_op(cg, OP_SUB);
        emit_imm_op(cg, OP_STORE, cg->scratch_field);

        emit_imm_op(cg, OP_JMP, loop_top);
        patch_i32(cg, jz_done, cur_addr(cg));

        emit_imm_op(cg, OP_PUSH, cg->sci_buf_addr);
        emit_imm_op(cg, OP_STORE, FX_SCI_ARG2_ADDR);
        emit_imm_op(cg, OP_LOAD, FX_SCI_PORT);
        return;
    }
    if (strcmp(name, "poll_mouse") == 0 || strcmp(name, "poll_kbd") == 0) {
        int32_t buf = strcmp(name, "poll_mouse") == 0 ? cg->mouse_buf_addr : cg->kbd_buf_addr;
        codegen_expr(cg, program, args[0]);
        emit_imm_op(cg, OP_STORE, FX_SCI_ARG1_ADDR);
        emit_imm_op(cg, OP_PUSH, 8);
        emit_imm_op(cg, OP_STORE, FX_SCI_ARG3_ADDR);
        emit_imm_op(cg, OP_PUSH, FX_SCI_CMD_VFS_READ);
        emit_imm_op(cg, OP_STORE, FX_SCI_CMD_ADDR);
        emit_imm_op(cg, OP_PUSH, buf);
        emit_imm_op(cg, OP_STORE, FX_SCI_ARG2_ADDR);
        emit_imm_op(cg, OP_LOAD, FX_SCI_PORT); /* [bytes_read] */
        emit_imm_op(cg, OP_PUSH, 8);
        emit_op(cg, OP_EQ);
        return;
    }
    if (strcmp(name, "mouse_type") == 0) {
        emit_imm_op(cg, OP_PUSH, cg->mouse_buf_addr + 0);
        emit_load_byte(cg);
        return;
    }
    if (strcmp(name, "mouse_button") == 0) {
        emit_imm_op(cg, OP_PUSH, cg->mouse_buf_addr + 1);
        emit_load_byte(cg);
        return;
    }
    if (strcmp(name, "mouse_x") == 0) {
        emit_imm_op(cg, OP_PUSH, cg->mouse_buf_addr + 2); emit_load_byte(cg);
        emit_imm_op(cg, OP_PUSH, cg->mouse_buf_addr + 3); emit_load_byte(cg);
        emit_imm_op(cg, OP_PUSH, 8); emit_op(cg, OP_SHL);
        emit_op(cg, OP_OR);
        return;
    }
    if (strcmp(name, "mouse_y") == 0) {
        emit_imm_op(cg, OP_PUSH, cg->mouse_buf_addr + 4); emit_load_byte(cg);
        emit_imm_op(cg, OP_PUSH, cg->mouse_buf_addr + 5); emit_load_byte(cg);
        emit_imm_op(cg, OP_PUSH, 8); emit_op(cg, OP_SHL);
        emit_op(cg, OP_OR);
        return;
    }
    if (strcmp(name, "kbd_type") == 0) {
        emit_imm_op(cg, OP_PUSH, cg->kbd_buf_addr + 0);
        emit_load_byte(cg);
        return;
    }
    if (strcmp(name, "kbd_key") == 0) {
        emit_imm_op(cg, OP_PUSH, cg->kbd_buf_addr + 2); emit_load_byte(cg);
        emit_imm_op(cg, OP_PUSH, cg->kbd_buf_addr + 3); emit_load_byte(cg);
        emit_imm_op(cg, OP_PUSH, 8); emit_op(cg, OP_SHL);
        emit_op(cg, OP_OR);
        return;
    }

    cg_error(cg, call_node->line, "internal error: unhandled builtin '%s'", name);
}

/* ---- statement codegen ---- */

static void codegen_stmt(Codegen* cg, FxProgram* program, FxNode* n, bool is_main) {
    switch (n->kind) {
        case FX_EMPTY:
            return;
        case FX_LOCAL_DECL: {
            if (n->as.local_decl.struct_type_name) {
                const FxStructDef* sd = fx_find_struct(program, n->as.local_decl.struct_type_name);
                int32_t base_offset = cg->frame_k - 1 - cg->next_local_slot;
                cg->next_local_slot += sd->nfields;
                push_scope_ex(cg, n->as.local_decl.name, base_offset, sd->nfields, sd, false);
                return; /* zero-initialized via the K prologue's placeholder pushes */
            }
            int32_t array_len = n->as.local_decl.array_len;
            if (array_len > 0) {
                int32_t base_offset = cg->frame_k - 1 - cg->next_local_slot;
                cg->next_local_slot += array_len;
                push_scope(cg, n->as.local_decl.name, base_offset, array_len);
                if (n->as.local_decl.has_string_init) {
                    /* Unrolled at compile time: the string's content is a
                     * compile-time constant, so each char becomes a direct
                     * LOCALSET. Slots beyond string_len (the NUL terminator
                     * and any declared padding) are already zero -- the
                     * function prologue's placeholder PUSH 0s cover them. */
                    for (int32_t i = 0; i < n->as.local_decl.string_len; i++) {
                        unsigned char c = (unsigned char) n->as.local_decl.string_value[i];
                        emit_imm_op(cg, OP_PUSH, c);
                        emit_imm_op(cg, OP_PUSH, base_offset - i);
                        emit_op(cg, OP_LOCALSET);
                    }
                }
                return;
            }
            int32_t offset = cg->frame_k - 1 - cg->next_local_slot;
            cg->next_local_slot++;
            push_scope(cg, n->as.local_decl.name, offset, 0);
            if (n->as.local_decl.init) {
                codegen_expr(cg, program, n->as.local_decl.init);
                emit_imm_op(cg, OP_PUSH, offset);
                emit_op(cg, OP_LOCALSET);
            }
            return;
        }
        case FX_EXPR_STMT: {
            FxNode* e = n->as.expr_stmt.expr;
            /* A `void` extern (docs/quill_fluxio.md Phase B6/B7) leaves
             * nothing on the stack, unlike every other call form -- codegen
             * it directly here and skip the POP below, instead of routing
             * through codegen_expr (which always assumes one value). */
            if (e->kind == FX_CALL) {
                const FxExtern* ext = find_extern(cg, e->as.call.name);
                if (ext && ext->is_void) {
                    if (ext->nparams != e->as.call.nargs) {
                        cg_error(cg, e->line, "extern '%s' expects %d argument(s) but %d given",
                                 ext->name, ext->nparams, e->as.call.nargs);
                    }
                    for (int i = 0; i < e->as.call.nargs; i++) codegen_expr(cg, program, e->as.call.args[i]);
                    emit_imm_op(cg, OP_CALL, ext->address);
                    return;
                }
            }
            codegen_expr(cg, program, e);
            emit_op(cg, OP_POP);
            return;
        }
        case FX_IF: {
            codegen_expr(cg, program, n->as.if_s.cond);
            size_t jz = emit_imm_op(cg, OP_JZ, 0);
            codegen_stmt(cg, program, n->as.if_s.then_s, is_main);
            if (n->as.if_s.else_s) {
                size_t jmp_end = emit_imm_op(cg, OP_JMP, 0);
                patch_i32(cg, jz, cur_addr(cg));
                codegen_stmt(cg, program, n->as.if_s.else_s, is_main);
                patch_i32(cg, jmp_end, cur_addr(cg));
            } else {
                patch_i32(cg, jz, cur_addr(cg));
            }
            return;
        }
        case FX_WHILE: {
            int32_t top = cur_addr(cg);
            codegen_expr(cg, program, n->as.while_s.cond);
            size_t jz = emit_imm_op(cg, OP_JZ, 0);
            codegen_stmt(cg, program, n->as.while_s.body, is_main);
            emit_imm_op(cg, OP_JMP, top);
            patch_i32(cg, jz, cur_addr(cg));
            return;
        }
        case FX_FOR: {
            int saved_scope = cg->scope_len;
            if (n->as.for_s.init) codegen_stmt(cg, program, n->as.for_s.init, is_main);
            int32_t top = cur_addr(cg);
            size_t jz = 0;
            bool has_cond = n->as.for_s.cond != NULL;
            if (has_cond) {
                codegen_expr(cg, program, n->as.for_s.cond);
                jz = emit_imm_op(cg, OP_JZ, 0);
            }
            codegen_stmt(cg, program, n->as.for_s.body, is_main);
            if (n->as.for_s.post) {
                codegen_expr(cg, program, n->as.for_s.post);
                emit_op(cg, OP_POP);
            }
            emit_imm_op(cg, OP_JMP, top);
            if (has_cond) patch_i32(cg, jz, cur_addr(cg));
            cg->scope_len = saved_scope;
            return;
        }
        case FX_RETURN:
            if (n->as.ret.expr) codegen_expr(cg, program, n->as.ret.expr);
            else emit_imm_op(cg, OP_PUSH, 0);
            if (cg->cur_is_recursive) {
                emit_imm_op(cg, OP_LOAD, cg->cur_recursion_slot);
                emit_op(cg, OP_DEC);
                emit_imm_op(cg, OP_STORE, cg->cur_recursion_slot);
            }
            if (is_main) {
                emit_op(cg, OP_HALT);
            } else {
                int32_t l = cg->frame_l;
                if (l > 0) {
                    emit_imm_op(cg, OP_PUSH, l);
                    emit_op(cg, OP_UNFRAME);
                }
                emit_op(cg, OP_RET);
            }
            return;
        case FX_BLOCK: {
            int saved_scope = cg->scope_len;
            for (int i = 0; i < n->as.block.nstmts; i++) {
                codegen_stmt(cg, program, n->as.block.stmts[i], is_main);
            }
            cg->scope_len = saved_scope;
            return;
        }
        default:
            cg_error(cg, n->line, "internal error: unexpected node in statement context");
    }
}

/* ---- function codegen ---- */

static void codegen_func(Codegen* cg, FxProgram* program, int idx, bool is_main) {
    FxFunc* f = &cg->funcs[idx];
    int32_t k = count_locals(f->body);
    int32_t l = f->nparams + k;

    cg->func_addrs[idx] = cur_addr(cg);
    cg->scope_len = 0;
    cg->next_local_slot = 0;
    cg->cur_is_recursive = f->is_recursive;
    cg->cur_recursion_slot = cg->recursion_slot[idx];

    if (f->is_recursive) {
        emit_imm_op(cg, OP_LOAD, cg->cur_recursion_slot);
        emit_op(cg, OP_INC);
        emit_op(cg, OP_DUP);
        emit_imm_op(cg, OP_STORE, cg->cur_recursion_slot);
        emit_imm_op(cg, OP_PUSH, f->max_depth);
        emit_op(cg, OP_GT);
        size_t jz_ok = emit_imm_op(cg, OP_JZ, 0);
        emit_imm_op(cg, OP_PUSH, -1);
        emit_op(cg, OP_HALT);
        patch_i32(cg, jz_ok, cur_addr(cg));
    }

    for (int i = 0; i < k; i++) emit_imm_op(cg, OP_PUSH, 0);

    /* codegen_stmt reads K (for local-decl offsets) from cg->frame_k and L
     * (for FRAME/UNFRAME sizing) from cg->frame_l; both are scoped to "the
     * function currently being emitted" for the duration of this call. */
    cg->frame_k = k;
    cg->frame_l = l;

    if (l > 0) {
        emit_imm_op(cg, OP_PUSH, l);
        emit_op(cg, OP_FRAME);
        for (int i = 0; i < f->nparams; i++) {
            int32_t offset = l - 1 - i;
            if (f->params[i].struct_type_name) {
                /* Struct params hold a decayed base address at runtime, but
                 * unlike array params their field offsets ARE known at
                 * compile time (from the declared parameter type) -- this
                 * is FX_BINDING_PARAM_STRUCT, not a plain unchecked scalar. */
                const FxStructDef* sd = fx_find_struct(program, f->params[i].struct_type_name);
                push_scope_ex(cg, f->params[i].name, offset, 0, sd, true);
            } else if (f->params[i].is_byte) {
                push_scope_byte_array_param(cg, f->params[i].name, offset);
            } else {
                /* Array params are always registered as scalars: they hold a
                 * decayed base address at runtime, with no compile-time-known
                 * length, so indexing on them is unchecked (FX_BINDING_LOCAL_SCALAR). */
                push_scope(cg, f->params[i].name, offset, 0);
            }
        }
    }

    codegen_stmt(cg, program, f->body, is_main);

    /* Implicit fall-off-end epilogue. */
    emit_imm_op(cg, OP_PUSH, 0);
    if (f->is_recursive) {
        emit_imm_op(cg, OP_LOAD, cg->cur_recursion_slot);
        emit_op(cg, OP_DEC);
        emit_imm_op(cg, OP_STORE, cg->cur_recursion_slot);
    }
    if (is_main) {
        emit_op(cg, OP_HALT);
    } else {
        if (l > 0) {
            emit_imm_op(cg, OP_PUSH, l);
            emit_op(cg, OP_UNFRAME);
        }
        emit_op(cg, OP_RET);
    }
}

/* ---- driver ---- */

uint8_t* fx_codegen(FxProgram* program, int32_t base_addr, size_t* out_len) {
    Codegen cg;
    memset(&cg, 0, sizeof(cg));
    cg.base_addr = base_addr;
    cg.funcs = program->funcs;
    cg.nfuncs = program->nfuncs;
    cg.externs = program->externs;
    cg.nexterns = program->nexterns;

    if (setjmp(cg.error_jmp) != 0) {
        free(cg.code);
        free(cg.global_addrs);
        free(cg.func_addrs);
        free(cg.recursion_slot);
        for (int i = 0; i < cg.nfixups; i++) free(cg.fixups[i].name);
        free(cg.fixups);
        free(cg.scope);
        return NULL;
    }

    int main_idx = find_func_index(&cg, "main");
    if (main_idx < 0) cg_error(&cg, 0, "program has no 'main' function");
    if (cg.funcs[main_idx].nparams != 0) cg_error(&cg, cg.funcs[main_idx].line, "'main' must take no parameters");
    if (cg.funcs[main_idx].is_recursive) cg_error(&cg, cg.funcs[main_idx].line, "'main' cannot be declared recursive");

    for (int i = 0; i < cg.nfuncs; i++) {
        for (int j = i + 1; j < cg.nfuncs; j++) {
            if (strcmp(cg.funcs[i].name, cg.funcs[j].name) == 0) {
                cg_error(&cg, cg.funcs[j].line, "function '%s' is already defined", cg.funcs[j].name);
            }
        }
        if (find_builtin(cg.funcs[i].name)) {
            cg_error(&cg, cg.funcs[i].line, "'%s' is a reserved builtin name", cg.funcs[i].name);
        }
    }
    for (int i = 0; i < program->nglobals; i++) {
        for (int j = i + 1; j < program->nglobals; j++) {
            if (strcmp(program->globals[i].name, program->globals[j].name) == 0) {
                cg_error(&cg, program->globals[j].line, "global '%s' is already defined", program->globals[j].name);
            }
        }
        if (find_builtin(program->globals[i].name)) {
            cg_error(&cg, program->globals[i].line, "'%s' is a reserved builtin name", program->globals[i].name);
        }
    }
    for (int i = 0; i < cg.nfuncs; i++) {
        FxFunc* f = &cg.funcs[i];
        for (int a = 0; a < f->nparams; a++) {
            for (int b = a + 1; b < f->nparams; b++) {
                if (strcmp(f->params[a].name, f->params[b].name) == 0) {
                    cg_error(&cg, f->params[b].line, "duplicate parameter '%s' in function '%s'",
                              f->params[b].name, f->name);
                }
            }
        }
    }
    for (int i = 0; i < cg.nexterns; i++) {
        for (int j = i + 1; j < cg.nexterns; j++) {
            if (strcmp(cg.externs[i].name, cg.externs[j].name) == 0) {
                cg_error(&cg, cg.externs[j].line, "extern '%s' is already defined", cg.externs[j].name);
            }
        }
        if (find_builtin(cg.externs[i].name)) {
            cg_error(&cg, cg.externs[i].line, "'%s' is a reserved builtin name", cg.externs[i].name);
        }
        if (find_func_index(&cg, cg.externs[i].name) >= 0) {
            cg_error(&cg, cg.externs[i].line, "'%s' is already defined as a function", cg.externs[i].name);
        }
        for (int j = 0; j < program->nglobals; j++) {
            if (strcmp(program->globals[j].name, cg.externs[i].name) == 0) {
                cg_error(&cg, cg.externs[i].line, "'%s' is already defined as a global", cg.externs[i].name);
            }
        }
    }

    /* Allocate globals (each consuming 4*array_len bytes, 4 for a scalar),
     * then one counter slot per recursive function, contiguously in low RAM
     * below device space -- EXCEPT large arrays, which go to the dedicated
     * bulk-globals band (MM_FX_BULK_GLOBALS_BASE, include/memory_map.h)
     * instead: the small-scalar band above is only ~60KB total budget,
     * nowhere near enough for something like a 1MB file buffer. See
     * docs/memory-map.md. Anything at or under FX_BULK_GLOBAL_THRESHOLD
     * bytes stays in the small band (most globals: loop counters, small
     * fixed-size arrays); anything larger bump-allocates from the bulk
     * band instead, tracked by a separate pointer so the two regions
     * can't collide with each other. */
    cg.nglobals = program->nglobals;
    cg.global_addrs = malloc(sizeof(int32_t) * (cg.nglobals > 0 ? cg.nglobals : 1));
    int32_t next_addr = FX_GLOBALS_BASE;
    int32_t next_bulk_addr = MM_FX_BULK_GLOBALS_BASE;
    for (int i = 0; i < cg.nglobals; i++) {
        int32_t len = program->globals[i].array_len > 0 ? program->globals[i].array_len : 1;
        /* byte arrays (Phase A1, docs/quill_fluxio.md) pack 1 byte/element
         * in real memory instead of a full word -- structs and scalars are
         * never is_byte, so this only ever fires for a `byte name[N];`
         * global. */
        int32_t size_bytes = program->globals[i].is_byte ? len : 4 * len;
        if (size_bytes > FX_BULK_GLOBAL_THRESHOLD) {
            cg.global_addrs[i] = next_bulk_addr;
            next_bulk_addr += size_bytes;
            if (next_bulk_addr > MM_FX_BULK_GLOBALS_END) {
                cg_error(&cg, program->globals[i].line, "bulk global '%s' overflows the bulk-globals "
                          "band [0x%x, 0x%x) -- see docs/memory-map.md",
                          program->globals[i].name, MM_FX_BULK_GLOBALS_BASE, MM_FX_BULK_GLOBALS_END);
            }
        } else {
            cg.global_addrs[i] = next_addr;
            next_addr += size_bytes;
        }
    }
    cg.recursion_slot = malloc(sizeof(int32_t) * (cg.nfuncs > 0 ? cg.nfuncs : 1));
    for (int i = 0; i < cg.nfuncs; i++) {
        if (cg.funcs[i].is_recursive) {
            cg.recursion_slot[i] = next_addr;
            next_addr += 4;
        } else {
            cg.recursion_slot[i] = -1;
        }
    }
    /* Scratch memory for the SCI/VFS/draw builtins (store_byte/load_byte
     * intermediates, persistent mouse/kbd event buffers, and a transient
     * buffer for packing draw commands and string arguments). Always
     * reserved, whether or not the program actually uses any of them. */
    cg.scratch_addr = next_addr;  next_addr += 4;
    cg.scratch_shift = next_addr; next_addr += 4;
    cg.scratch_word = next_addr;  next_addr += 4;
    cg.scratch_field = next_addr; next_addr += 4;
    cg.scratch_copy_src = next_addr; next_addr += 4;
    cg.scratch_copy_dst = next_addr; next_addr += 4;
    cg.mouse_buf_addr = next_addr; next_addr += 8;
    cg.kbd_buf_addr = next_addr;   next_addr += 8;
    cg.sci_buf_addr = next_addr;   next_addr += FX_SCI_BUF_SIZE;

    if (next_addr > FX_DEVICE_BOUNDARY) {
        cg_error(&cg, 0, "too many globals/recursive functions: globals region "
                  "[0x%x, 0x%x) overflows into device space at 0x%x",
                  FX_GLOBALS_BASE, next_addr, FX_DEVICE_BOUNDARY);
    }

    cg.func_addrs = malloc(sizeof(int32_t) * (cg.nfuncs > 0 ? cg.nfuncs : 1));
    for (int i = 0; i < cg.nfuncs; i++) cg.func_addrs[i] = -1;

    validate_calls_and_recursion(&cg);

    size_t entry_jmp_off = emit_imm_op(&cg, OP_JMP, 0);

    for (int i = 0; i < cg.nfuncs; i++) {
        if (i == main_idx) continue;
        codegen_func(&cg, program, i, false);
    }

    int32_t main_entry = cur_addr(&cg);
    for (int i = 0; i < program->nglobals; i++) {
        if (program->globals[i].has_init) {
            emit_imm_op(&cg, OP_PUSH, program->globals[i].init_value);
            emit_imm_op(&cg, OP_STORE, cg.global_addrs[i]);
        } else if (program->globals[i].has_string_init) {
            /* Unrolled at compile time, same as the local-array case: each
             * char of the compile-time-constant string becomes a direct
             * STORE. Bytes beyond string_len (NUL terminator + any declared
             * padding) are already zero -- VM memory starts zero-initialized. */
            for (int32_t k = 0; k < program->globals[i].string_len; k++) {
                unsigned char c = (unsigned char) program->globals[i].string_value[k];
                emit_imm_op(&cg, OP_PUSH, c);
                if (program->globals[i].is_byte) {
                    emit_imm_op(&cg, OP_PUSH, cg.global_addrs[i] + k);
                    emit_store_byte(&cg);
                } else {
                    emit_imm_op(&cg, OP_STORE, cg.global_addrs[i] + 4 * k);
                }
            }
        }
    }
    codegen_func(&cg, program, main_idx, true);

    patch_i32(&cg, entry_jmp_off, main_entry);

    for (int i = 0; i < cg.nfixups; i++) {
        int idx = find_func_index(&cg, cg.fixups[i].name);
        /* idx is guaranteed valid: validate_calls_and_recursion already
         * rejected calls to undefined functions before any code was emitted. */
        patch_i32(&cg, cg.fixups[i].imm_offset, cg.func_addrs[idx]);
        free(cg.fixups[i].name);
    }
    free(cg.fixups);
    free(cg.global_addrs);
    free(cg.func_addrs);
    free(cg.recursion_slot);
    free(cg.scope);

    *out_len = cg.len;
    return cg.code;
}
