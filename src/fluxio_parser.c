#include "fluxio_parser.h"
#include "fluxio_token.h"
#include "fluxio_ast.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <setjmp.h>
#include <ctype.h>

typedef struct {
    FxTokenList* list;
    size_t pos;
    jmp_buf error_jmp;

    /* Struct definitions seen so far, in declaration order. Structs must be
     * declared before use (no forward references) -- this array is built
     * up incrementally as the top-level loop parses `struct` blocks, and
     * consulted by every declaration site (global/local/param) to decide
     * whether a leading identifier names a type. */
    FxStructDef* structs;
    size_t nstructs, scap;
} Parser;

static FxToken* cur(Parser* p) { return &p->list->tokens[p->pos]; }

static bool check(Parser* p, FxTokenType type) { return cur(p)->type == type; }

static FxToken* advance(Parser* p) {
    FxToken* t = cur(p);
    if (t->type != FXTOK_EOF) p->pos++;
    return t;
}

static void fail(Parser* p, const char* fmt, ...) {
    FxToken* t = cur(p);
    fprintf(stderr, "fluxio: error at line %d, column %d: ", t->line, t->column);
    va_list args;
    va_start(args, fmt);
    vfprintf(stderr, fmt, args);
    va_end(args);
    fprintf(stderr, "\n");
    longjmp(p->error_jmp, 1);
}

static FxToken* expect(Parser* p, FxTokenType type, const char* what) {
    if (!check(p, type)) fail(p, "expected %s but found '%s'", what, fx_token_type_name(cur(p)->type));
    return advance(p);
}

static bool match(Parser* p, FxTokenType type) {
    if (check(p, type)) { advance(p); return true; }
    return false;
}

/* Strict naming convention: identifiers must be lower_snake_case
 * (^[a-z][a-z0-9_]*$). Enforced as a hard compile error per Fluxio's
 * JSF-AV-inspired auditability requirement. */
static void check_snake_case(Parser* p, const char* name, int line) {
    bool ok = islower((unsigned char)name[0]) != 0;
    for (const char* c = name; ok && *c; c++) {
        ok = islower((unsigned char)*c) || isdigit((unsigned char)*c) || *c == '_';
    }
    if (!ok) {
        fprintf(stderr,
            "fluxio: error at line %d: identifier '%s' violates naming convention "
            "(must be lower_snake_case)\n", line, name);
        longjmp(p->error_jmp, 1);
    }
}

/* Struct type names use a distinct convention (UpperCamelCase) from value
 * identifiers (lower_snake_case): partly for auditability (visually
 * distinguish types from values at a glance), and as a side effect the two
 * disjoint character classes on the first letter mean a struct name and a
 * function/variable name can never collide -- they occupy what's
 * effectively a separate namespace without needing one to be implemented. */
static void check_type_name(Parser* p, const char* name, int line) {
    bool ok = isupper((unsigned char) name[0]) != 0;
    for (const char* c = name; ok && *c; c++) {
        ok = isalnum((unsigned char) *c) != 0;
    }
    if (!ok) {
        fprintf(stderr,
            "fluxio: error at line %d: struct type name '%s' violates naming convention "
            "(must be UpperCamelCase, e.g. 'Point')\n", line, name);
        longjmp(p->error_jmp, 1);
    }
}

static bool is_known_struct(Parser* p, const char* name) {
    for (size_t i = 0; i < p->nstructs; i++) {
        if (strcmp(p->structs[i].name, name) == 0) return true;
    }
    return false;
}

static const FxStructDef* find_parsed_struct(Parser* p, const char* name) {
    for (size_t i = 0; i < p->nstructs; i++) {
        if (strcmp(p->structs[i].name, name) == 0) return &p->structs[i];
    }
    return NULL;
}

/* Forward decls */
static FxNode* parse_expr(Parser* p);
static FxNode* parse_statement(Parser* p);
static FxNode* parse_block(Parser* p);

/* Consumes a type token at the current position: 'int' or 'byte' (returns
 * NULL, *out_is_byte set accordingly), or an identifier matching an
 * already-declared struct name (returns that name, borrowed from
 * p->structs -- valid for the parser's lifetime, callers must strdup it to
 * keep it past that; *out_is_byte set false). out_is_byte may be NULL for
 * callers that don't accept 'byte' at all (e.g. function return types). */
static const char* parse_decl_type(Parser* p, bool* out_is_byte) {
    if (out_is_byte) *out_is_byte = false;
    if (match(p, FXTOK_KW_INT)) return NULL;
    if (out_is_byte && match(p, FXTOK_KW_BYTE)) { *out_is_byte = true; return NULL; }
    if (check(p, FXTOK_IDENT) && is_known_struct(p, cur(p)->value)) {
        FxToken* t = advance(p);
        return t->value;
    }
    fail(p, "expected a type ('int'%s, or a declared struct name) but found '%s'",
         out_is_byte ? " or 'byte'" : "", fx_token_type_name(cur(p)->type));
    return NULL; /* unreachable */
}

/* ---- expressions (precedence climbing, one function per level) ---- */

static FxNode* parse_primary(Parser* p) {
    FxToken* t = cur(p);

    if (check(p, FXTOK_INT_LIT)) {
        advance(p);
        FxNode* n = fx_node_new(FX_INT_LIT, t->line, t->column);
        n->as.int_lit = t->int_value;
        return n;
    }

    if (check(p, FXTOK_STRING_LIT)) {
        advance(p);
        FxNode* n = fx_node_new(FX_STR_LIT, t->line, t->column);
        n->as.str_lit.value = malloc((size_t) t->str_len + 1);
        memcpy(n->as.str_lit.value, t->value, (size_t) t->str_len + 1);
        n->as.str_lit.len = t->str_len;
        return n;
    }

    if (check(p, FXTOK_IDENT)) {
        advance(p);
        char* name = strdup(t->value);
        if (check(p, FXTOK_LPAREN)) {
            advance(p);
            FxNode** args = NULL;
            int nargs = 0, cap = 0;
            if (!check(p, FXTOK_RPAREN)) {
                for (;;) {
                    FxNode* a = parse_expr(p);
                    if (nargs == cap) {
                        cap = cap ? cap * 2 : 4;
                        args = realloc(args, sizeof(FxNode*) * cap);
                    }
                    args[nargs++] = a;
                    if (!match(p, FXTOK_COMMA)) break;
                }
            }
            expect(p, FXTOK_RPAREN, "')'");
            FxNode* n = fx_node_new(FX_CALL, t->line, t->column);
            n->as.call.name = name;
            n->as.call.args = args;
            n->as.call.nargs = nargs;
            return n;
        }
        if (check(p, FXTOK_LBRACKET)) {
            advance(p);
            FxNode* index = parse_expr(p);
            expect(p, FXTOK_RBRACKET, "']'");
            FxNode* n = fx_node_new(FX_INDEX, t->line, t->column);
            n->as.index.name = name;
            n->as.index.index = index;
            return n;
        }
        if (check(p, FXTOK_DOT)) {
            advance(p);
            FxToken* fid = expect(p, FXTOK_IDENT, "field name");
            FxNode* n = fx_node_new(FX_FIELD, t->line, t->column);
            n->as.field.name = name;
            n->as.field.field = strdup(fid->value);
            return n;
        }
        FxNode* n = fx_node_new(FX_VAR_REF, t->line, t->column);
        n->as.var.name = name;
        return n;
    }

    if (match(p, FXTOK_LPAREN)) {
        FxNode* n = parse_expr(p);
        expect(p, FXTOK_RPAREN, "')'");
        return n;
    }

    fail(p, "expected expression but found '%s'", fx_token_type_name(t->type));
    return NULL; /* unreachable */
}

static FxNode* parse_unary(Parser* p) {
    FxToken* t = cur(p);
    FxOp op;
    bool is_unary = true;
    switch (t->type) {
        case FXTOK_MINUS: op = FX_OP_NEG; break;
        case FXTOK_BANG:  op = FX_OP_LNOT; break;
        case FXTOK_TILDE: op = FX_OP_BNOT; break;
        case FXTOK_PLUS:  op = FX_OP_PLUS; break;
        default: is_unary = false; op = FX_OP_NEG; break;
    }
    if (is_unary) {
        advance(p);
        FxNode* operand = parse_unary(p);
        FxNode* n = fx_node_new(FX_UNARY, t->line, t->column);
        n->as.unary.op = op;
        n->as.unary.operand = operand;
        return n;
    }
    return parse_primary(p);
}

typedef FxNode* (*ParseLevelFn)(Parser*);

static FxNode* parse_binary_level(Parser* p, ParseLevelFn next,
                                   const FxTokenType* types, const FxOp* ops, int ntypes) {
    FxNode* left = next(p);
    for (;;) {
        FxToken* t = cur(p);
        int matched = -1;
        for (int i = 0; i < ntypes; i++) {
            if (t->type == types[i]) { matched = i; break; }
        }
        if (matched < 0) break;
        advance(p);
        FxNode* right = next(p);
        FxNode* n = fx_node_new(FX_BINARY, t->line, t->column);
        n->as.binary.op = ops[matched];
        n->as.binary.l = left;
        n->as.binary.r = right;
        left = n;
    }
    return left;
}

static FxNode* parse_mul(Parser* p) {
    static const FxTokenType ty[] = { FXTOK_STAR, FXTOK_SLASH, FXTOK_PERCENT };
    static const FxOp op[] = { FX_OP_MUL, FX_OP_DIV, FX_OP_MOD };
    return parse_binary_level(p, parse_unary, ty, op, 3);
}
static FxNode* parse_add(Parser* p) {
    static const FxTokenType ty[] = { FXTOK_PLUS, FXTOK_MINUS };
    static const FxOp op[] = { FX_OP_ADD, FX_OP_SUB };
    return parse_binary_level(p, parse_mul, ty, op, 2);
}
static FxNode* parse_shift(Parser* p) {
    static const FxTokenType ty[] = { FXTOK_SHL, FXTOK_SHR };
    static const FxOp op[] = { FX_OP_SHL, FX_OP_SAR };
    return parse_binary_level(p, parse_add, ty, op, 2);
}
static FxNode* parse_rel(Parser* p) {
    static const FxTokenType ty[] = { FXTOK_LT, FXTOK_LTE, FXTOK_GT, FXTOK_GTE };
    static const FxOp op[] = { FX_OP_LT, FX_OP_LTE, FX_OP_GT, FX_OP_GTE };
    return parse_binary_level(p, parse_shift, ty, op, 4);
}
static FxNode* parse_eq(Parser* p) {
    static const FxTokenType ty[] = { FXTOK_EQ, FXTOK_NEQ };
    static const FxOp op[] = { FX_OP_EQ, FX_OP_NEQ };
    return parse_binary_level(p, parse_rel, ty, op, 2);
}
static FxNode* parse_bitand(Parser* p) {
    static const FxTokenType ty[] = { FXTOK_AMP };
    static const FxOp op[] = { FX_OP_AND };
    return parse_binary_level(p, parse_eq, ty, op, 1);
}
static FxNode* parse_bitxor(Parser* p) {
    static const FxTokenType ty[] = { FXTOK_CARET };
    static const FxOp op[] = { FX_OP_XOR };
    return parse_binary_level(p, parse_bitand, ty, op, 1);
}
static FxNode* parse_bitor(Parser* p) {
    static const FxTokenType ty[] = { FXTOK_PIPE };
    static const FxOp op[] = { FX_OP_OR };
    return parse_binary_level(p, parse_bitxor, ty, op, 1);
}
static FxNode* parse_logand(Parser* p) {
    static const FxTokenType ty[] = { FXTOK_AMPAMP };
    static const FxOp op[] = { FX_OP_LAND };
    return parse_binary_level(p, parse_bitor, ty, op, 1);
}
static FxNode* parse_logor(Parser* p) {
    static const FxTokenType ty[] = { FXTOK_PIPEPIPE };
    static const FxOp op[] = { FX_OP_LOR };
    return parse_binary_level(p, parse_logand, ty, op, 1);
}

static FxNode* parse_assignment(Parser* p) {
    FxToken* t = cur(p);
    FxNode* left = parse_logor(p);
    if (check(p, FXTOK_ASSIGN)) {
        if (left->kind == FX_INDEX) {
            advance(p);
            char* name = strdup(left->as.index.name);
            FxNode* index = left->as.index.index;
            left->as.index.index = NULL; /* detach so fx_node_free doesn't take it with left */
            fx_node_free(left);
            FxNode* value = parse_assignment(p);
            FxNode* n = fx_node_new(FX_INDEX_ASSIGN, t->line, t->column);
            n->as.index_assign.name = name;
            n->as.index_assign.index = index;
            n->as.index_assign.value = value;
            return n;
        }
        if (left->kind == FX_FIELD) {
            advance(p);
            char* name = strdup(left->as.field.name);
            char* field = strdup(left->as.field.field);
            fx_node_free(left);
            FxNode* value = parse_assignment(p);
            FxNode* n = fx_node_new(FX_FIELD_ASSIGN, t->line, t->column);
            n->as.field_assign.name = name;
            n->as.field_assign.field = field;
            n->as.field_assign.value = value;
            return n;
        }
        if (left->kind != FX_VAR_REF) {
            fail(p, "invalid assignment target");
        }
        advance(p);
        char* name = strdup(left->as.var.name);
        fx_node_free(left);
        FxNode* value = parse_assignment(p);
        FxNode* n = fx_node_new(FX_ASSIGN, t->line, t->column);
        n->as.assign.name = name;
        n->as.assign.value = value;
        return n;
    }
    return left;
}

static FxNode* parse_expr(Parser* p) { return parse_assignment(p); }

/* Constant-folds a compile-time-constant expression for global initializers.
 * v1 only needs literals and unary minus (matching what a sane const_expr
 * looks like without a general constant evaluator). */
static bool fold_const(FxNode* n, int32_t* out) {
    if (n->kind == FX_INT_LIT) { *out = n->as.int_lit; return true; }
    if (n->kind == FX_UNARY) {
        int32_t v;
        if (!fold_const(n->as.unary.operand, &v)) return false;
        switch (n->as.unary.op) {
            case FX_OP_NEG: *out = -v; return true;
            case FX_OP_BNOT: *out = ~v; return true;
            case FX_OP_LNOT: *out = !v; return true;
            case FX_OP_PLUS: *out = v; return true;
            default: return false;
        }
    }
    if (n->kind == FX_BINARY) {
        int32_t a, b;
        if (!fold_const(n->as.binary.l, &a) || !fold_const(n->as.binary.r, &b)) return false;
        switch (n->as.binary.op) {
            case FX_OP_ADD: *out = a + b; return true;
            case FX_OP_SUB: *out = a - b; return true;
            case FX_OP_MUL: *out = a * b; return true;
            case FX_OP_DIV: if (b == 0) return false; *out = a / b; return true;
            case FX_OP_MOD: if (b == 0) return false; *out = a % b; return true;
            case FX_OP_AND: *out = a & b; return true;
            case FX_OP_OR:  *out = a | b; return true;
            case FX_OP_XOR: *out = a ^ b; return true;
            case FX_OP_SHL: *out = a << (b % 32); return true;
            case FX_OP_SAR: *out = a >> (b % 32); return true;
            default: return false;
        }
    }
    return false;
}

/* ---- statements ---- */

/* Parses an optional "[N]" or "[]" array-size suffix following a declared
 * identifier. Returns true if a bracket was present. On true, *out_len is
 * the folded constant size ("[N]"), or -1 for "[]" (auto-sized -- only
 * legal paired with a string-literal initializer). */
static bool parse_array_suffix(Parser* p, int32_t* out_len) {
    if (!match(p, FXTOK_LBRACKET)) return false;
    if (match(p, FXTOK_RBRACKET)) {
        *out_len = -1;
        return true;
    }
    FxNode* size_expr = parse_expr(p);
    int32_t len;
    bool ok = fold_const(size_expr, &len);
    fx_node_free(size_expr);
    if (!ok || len <= 0) {
        fail(p, "array size must be a positive compile-time constant");
    }
    expect(p, FXTOK_RBRACKET, "']'");
    *out_len = len;
    return true;
}

static FxNode* parse_local_decl(Parser* p) {
    FxToken* kw = cur(p);
    bool is_byte = false;
    const char* struct_type = parse_decl_type(p, &is_byte);
    FxToken* id = expect(p, FXTOK_IDENT, "identifier");
    check_snake_case(p, id->value, id->line);

    FxNode* n = fx_node_new(FX_LOCAL_DECL, kw->line, kw->column);
    n->as.local_decl.name = strdup(id->value);
    n->as.local_decl.init = NULL;
    n->as.local_decl.has_string_init = false;
    n->as.local_decl.string_value = NULL;
    n->as.local_decl.string_len = 0;
    n->as.local_decl.struct_type_name = NULL;
    n->as.local_decl.is_byte = is_byte;

    if (struct_type) {
        const FxStructDef* sd = find_parsed_struct(p, struct_type);
        n->as.local_decl.struct_type_name = strdup(struct_type);
        n->as.local_decl.array_len = sd->nfields;
        expect(p, FXTOK_SEMI, "';'");
        return n;
    }

    if (is_byte && !check(p, FXTOK_LBRACKET)) {
        fail(p, "'byte' may only be used as an array, e.g. 'byte %s[N];'", id->value);
    }

    int32_t array_len = 0;
    bool is_array = parse_array_suffix(p, &array_len);
    n->as.local_decl.array_len = (is_array && array_len > 0) ? array_len : 0;

    if (match(p, FXTOK_ASSIGN)) {
        if (check(p, FXTOK_STRING_LIT)) {
            if (!is_array) {
                fail(p, "string literal initializer requires an array declaration, e.g. 'int %s[] = \"...\";'", id->value);
            }
            FxToken* str = advance(p);
            int32_t needed = str->str_len + 1;
            if (array_len > 0 && needed > array_len) {
                fail(p, "string literal (%d bytes incl. terminator) does not fit declared array size [%d]", needed, array_len);
            }
            n->as.local_decl.array_len = array_len > 0 ? array_len : needed;
            n->as.local_decl.has_string_init = true;
            n->as.local_decl.string_value = malloc((size_t) str->str_len + 1);
            memcpy(n->as.local_decl.string_value, str->value, (size_t) str->str_len + 1);
            n->as.local_decl.string_len = str->str_len;
        } else {
            if (is_array) {
                fail(p, "array '%s' can only be initialized from a string literal in this version of Fluxio", id->value);
            }
            n->as.local_decl.init = parse_expr(p);
        }
    } else if (is_array && array_len <= 0) {
        fail(p, "array '%s' declared with '[]' must have a string literal initializer to determine its size", id->value);
    }

    expect(p, FXTOK_SEMI, "';'");
    return n;
}

static FxNode* parse_statement(Parser* p) {
    FxToken* t = cur(p);

    if (check(p, FXTOK_KW_INT) || check(p, FXTOK_KW_BYTE) ||
        (check(p, FXTOK_IDENT) && is_known_struct(p, cur(p)->value))) {
        return parse_local_decl(p);
    }

    if (check(p, FXTOK_LBRACE)) return parse_block(p);

    if (match(p, FXTOK_SEMI)) {
        return fx_node_new(FX_EMPTY, t->line, t->column);
    }

    if (match(p, FXTOK_KW_IF)) {
        expect(p, FXTOK_LPAREN, "'('");
        FxNode* cond = parse_expr(p);
        expect(p, FXTOK_RPAREN, "')'");
        FxNode* then_s = parse_statement(p);
        FxNode* else_s = NULL;
        if (match(p, FXTOK_KW_ELSE)) else_s = parse_statement(p);
        FxNode* n = fx_node_new(FX_IF, t->line, t->column);
        n->as.if_s.cond = cond;
        n->as.if_s.then_s = then_s;
        n->as.if_s.else_s = else_s;
        return n;
    }

    if (match(p, FXTOK_KW_WHILE)) {
        expect(p, FXTOK_LPAREN, "'('");
        FxNode* cond = parse_expr(p);
        expect(p, FXTOK_RPAREN, "')'");
        FxNode* body = parse_statement(p);
        FxNode* n = fx_node_new(FX_WHILE, t->line, t->column);
        n->as.while_s.cond = cond;
        n->as.while_s.body = body;
        return n;
    }

    if (match(p, FXTOK_KW_FOR)) {
        expect(p, FXTOK_LPAREN, "'('");
        FxNode* init = NULL;
        if (!check(p, FXTOK_SEMI)) {
            if (check(p, FXTOK_KW_INT)) {
                init = parse_local_decl(p); /* consumes trailing ';' */
            } else {
                FxNode* e = parse_expr(p);
                expect(p, FXTOK_SEMI, "';'");
                FxNode* stmt = fx_node_new(FX_EXPR_STMT, e->line, e->col);
                stmt->as.expr_stmt.expr = e;
                init = stmt;
            }
        } else {
            expect(p, FXTOK_SEMI, "';'");
        }
        FxNode* cond = NULL;
        if (!check(p, FXTOK_SEMI)) cond = parse_expr(p);
        expect(p, FXTOK_SEMI, "';'");
        FxNode* post = NULL;
        if (!check(p, FXTOK_RPAREN)) post = parse_expr(p);
        expect(p, FXTOK_RPAREN, "')'");
        FxNode* body = parse_statement(p);
        FxNode* n = fx_node_new(FX_FOR, t->line, t->column);
        n->as.for_s.init = init;
        n->as.for_s.cond = cond;
        n->as.for_s.post = post;
        n->as.for_s.body = body;
        return n;
    }

    if (match(p, FXTOK_KW_RETURN)) {
        FxNode* expr = NULL;
        if (!check(p, FXTOK_SEMI)) expr = parse_expr(p);
        expect(p, FXTOK_SEMI, "';'");
        FxNode* n = fx_node_new(FX_RETURN, t->line, t->column);
        n->as.ret.expr = expr;
        return n;
    }

    FxNode* e = parse_expr(p);
    expect(p, FXTOK_SEMI, "';'");
    FxNode* n = fx_node_new(FX_EXPR_STMT, t->line, t->column);
    n->as.expr_stmt.expr = e;
    return n;
}

static FxNode* parse_block(Parser* p) {
    FxToken* t = expect(p, FXTOK_LBRACE, "'{'");
    FxNode** stmts = NULL;
    int n = 0, cap = 0;
    while (!check(p, FXTOK_RBRACE)) {
        FxNode* s = parse_statement(p);
        if (n == cap) { cap = cap ? cap * 2 : 8; stmts = realloc(stmts, sizeof(FxNode*) * cap); }
        stmts[n++] = s;
    }
    expect(p, FXTOK_RBRACE, "'}'");
    FxNode* block = fx_node_new(FX_BLOCK, t->line, t->column);
    block->as.block.stmts = stmts;
    block->as.block.nstmts = n;
    return block;
}

/* ---- top level ---- */

static void parse_func_decl(Parser* p, FxToken* leading, char* doc_comment, FxFunc* out) {
    bool is_recursive = false;
    int32_t max_depth = 0;
    if (match(p, FXTOK_KW_RECURSIVE)) {
        is_recursive = true;
        expect(p, FXTOK_LPAREN, "'('");
        FxToken* n = expect(p, FXTOK_INT_LIT, "integer literal");
        if (n->int_value <= 0) fail(p, "recursive(N) requires N > 0");
        max_depth = n->int_value;
        expect(p, FXTOK_RPAREN, "')'");
    }
    expect(p, FXTOK_KW_INT, "'int'");
    FxToken* id = expect(p, FXTOK_IDENT, "identifier");
    check_snake_case(p, id->value, id->line);

    expect(p, FXTOK_LPAREN, "'('");
    FxParam* params = NULL;
    int nparams = 0, cap = 0;
    if (!check(p, FXTOK_RPAREN)) {
        for (;;) {
            bool param_is_byte = false;
            const char* struct_type = parse_decl_type(p, &param_is_byte);
            FxToken* pid = expect(p, FXTOK_IDENT, "identifier");
            check_snake_case(p, pid->value, pid->line);
            bool is_array_param = false;
            char* param_struct_name = NULL;
            if (struct_type) {
                param_struct_name = strdup(struct_type);
            } else if (match(p, FXTOK_LBRACKET)) {
                expect(p, FXTOK_RBRACKET, "']' (array parameters take no size, e.g. 'int arr[]')");
                is_array_param = true;
            } else if (param_is_byte) {
                fail(p, "'byte' may only be used as an array parameter, e.g. 'byte %s[]'", pid->value);
            }
            if (nparams == cap) { cap = cap ? cap * 2 : 4; params = realloc(params, sizeof(FxParam) * cap); }
            params[nparams].name = strdup(pid->value);
            params[nparams].is_array = is_array_param;
            params[nparams].is_byte = param_is_byte;
            params[nparams].struct_type_name = param_struct_name;
            params[nparams].line = pid->line;
            nparams++;
            if (!match(p, FXTOK_COMMA)) break;
        }
    }
    expect(p, FXTOK_RPAREN, "')'");

    FxNode* body = parse_block(p);

    out->name = strdup(id->value);
    out->params = params;
    out->nparams = nparams;
    out->body = body;
    out->is_recursive = is_recursive;
    out->max_depth = max_depth;
    out->line = leading->line;
    out->has_doc_comment = (doc_comment != NULL);
}

/* Struct definitions: `struct Name { int field; ... }` -- no trailing
 * semicolon (consistent with function definitions, which also end at '}'),
 * doc comment required (consistent with functions' auditability rule),
 * fields are int-only (no nested structs, no array fields in this
 * version), at least one field required. */
static void parse_struct_decl(Parser* p, FxStructDef* out) {
    FxToken* kw = expect(p, FXTOK_KW_STRUCT, "'struct'");
    FxToken* id = expect(p, FXTOK_IDENT, "identifier");
    check_type_name(p, id->value, id->line);
    if (is_known_struct(p, id->value)) {
        fail(p, "struct '%s' is already defined", id->value);
    }

    expect(p, FXTOK_LBRACE, "'{'");
    FxStructField* fields = NULL;
    int nfields = 0, cap = 0;
    while (!check(p, FXTOK_RBRACE)) {
        expect(p, FXTOK_KW_INT, "'int'");
        FxToken* fid = expect(p, FXTOK_IDENT, "field name");
        check_snake_case(p, fid->value, fid->line);
        for (int i = 0; i < nfields; i++) {
            if (strcmp(fields[i].name, fid->value) == 0) {
                fail(p, "duplicate field '%s' in struct '%s'", fid->value, id->value);
            }
        }
        expect(p, FXTOK_SEMI, "';'");
        if (nfields == cap) { cap = cap ? cap * 2 : 4; fields = realloc(fields, sizeof(FxStructField) * cap); }
        fields[nfields].name = strdup(fid->value);
        fields[nfields].line = fid->line;
        nfields++;
    }
    expect(p, FXTOK_RBRACE, "'}'");
    if (nfields == 0) {
        fail(p, "struct '%s' must declare at least one field", id->value);
    }

    out->name = strdup(id->value);
    out->fields = fields;
    out->nfields = nfields;
    out->line = kw->line;
}

/* `extern int name(int a, int b, ...) = 0xADDR;` (Phase B5,
 * docs/quill_fluxio.md). No doc-comment requirement (unlike functions/
 * structs) -- an extern binds to code that lives outside this file
 * entirely, so there's nothing here for a doc comment to usefully
 * describe beyond what the linked library's own source already has. */
static void parse_extern_decl(Parser* p, FxToken* leading, FxExtern* out) {
    expect(p, FXTOK_KW_EXTERN, "'extern'");
    bool is_void = check(p, FXTOK_KW_VOID);
    if (is_void) { advance(p); } else { expect(p, FXTOK_KW_INT, "'int' or 'void'"); }
    FxToken* id = expect(p, FXTOK_IDENT, "identifier");
    check_snake_case(p, id->value, id->line);

    expect(p, FXTOK_LPAREN, "'('");
    int nparams = 0;
    if (!check(p, FXTOK_RPAREN)) {
        for (;;) {
            expect(p, FXTOK_KW_INT, "'int' (extern declarations only support plain int parameters)");
            FxToken* pid = expect(p, FXTOK_IDENT, "identifier");
            check_snake_case(p, pid->value, pid->line);
            nparams++;
            if (!match(p, FXTOK_COMMA)) break;
        }
    }
    expect(p, FXTOK_RPAREN, "')'");
    expect(p, FXTOK_ASSIGN, "'=' 0xADDR (extern declarations must bind to a fixed address, e.g. 'extern int f() = 0x70000C;')");
    FxToken* addr = expect(p, FXTOK_INT_LIT, "an address (integer literal)");
    expect(p, FXTOK_SEMI, "';'");

    out->name = strdup(id->value);
    out->nparams = nparams;
    out->address = addr->int_value;
    out->line = leading->line;
    out->is_void = is_void;
}

FxProgram* fx_parse(FxTokenList* tokens) {
    Parser p;
    p.list = tokens;
    p.pos = 0;
    p.structs = NULL;
    p.nstructs = 0;
    p.scap = 0;

    FxProgram* program = calloc(1, sizeof(FxProgram));

    FxGlobal* globals = NULL;
    int nglobals = 0, gcap = 0;
    FxFunc* funcs = NULL;
    int nfuncs = 0, fcap = 0;
    FxExtern* externs = NULL;
    int nexterns = 0, ecap = 0;

    if (setjmp(p.error_jmp) != 0) {
        free(globals);
        free(funcs);
        free(externs);
        for (size_t i = 0; i < p.nstructs; i++) {
            free(p.structs[i].name);
            for (int fi = 0; fi < p.structs[i].nfields; fi++) free(p.structs[i].fields[fi].name);
            free(p.structs[i].fields);
        }
        free(p.structs);
        free(program);
        return NULL;
    }

    while (!check(&p, FXTOK_EOF)) {
        FxToken* t = cur(&p);
        char* doc = t->has_doc_comment ? t->doc_comment : NULL;

        if (check(&p, FXTOK_KW_INCLUDE)) {
            fail(&p, "'include' directives are resolved before parsing (via fx_load_with_includes) "
                      "and should never reach the parser directly -- this token list wasn't preprocessed");
        }

        if (check(&p, FXTOK_KW_VERSION)) {
            advance(&p);
            FxToken* ver = expect(&p, FXTOK_INT_LIT, "an integer literal");
            if (ver->int_value < 0) {
                fail(&p, "version must be a nonnegative integer (Kelvin versioning, AGENTS.md)");
            }
            expect(&p, FXTOK_SEMI, "';'");
            program->version_seen = true;
            program->version_value = ver->int_value;
            continue;
        }

        if (check(&p, FXTOK_KW_EXTERN)) {
            if (nexterns == ecap) { ecap = ecap ? ecap * 2 : 8; externs = realloc(externs, sizeof(FxExtern) * ecap); }
            parse_extern_decl(&p, t, &externs[nexterns]);
            nexterns++;
            continue;
        }

        if (check(&p, FXTOK_KW_STRUCT)) {
            if (!doc) {
                fail(&p, "struct declaration must be preceded by a /** ... */ doc comment");
            }
            if (p.nstructs == p.scap) { p.scap = p.scap ? p.scap * 2 : 8; p.structs = realloc(p.structs, sizeof(FxStructDef) * p.scap); }
            parse_struct_decl(&p, &p.structs[p.nstructs]);
            p.nstructs++;
            continue;
        }

        /* Look ahead past an optional `recursive(N)` to find `int NAME (` vs
         * `int NAME [=|;]` to distinguish a function from a global. */
        size_t look = p.pos;
        if (p.list->tokens[look].type == FXTOK_KW_RECURSIVE) {
            /* skip 'recursive' '(' NUMBER ')' */
            look++;
            if (p.list->tokens[look].type == FXTOK_LPAREN) {
                look++;
                if (p.list->tokens[look].type == FXTOK_INT_LIT) look++;
                if (p.list->tokens[look].type == FXTOK_RPAREN) look++;
            }
        }
        bool is_func = false;
        if (p.list->tokens[look].type == FXTOK_KW_INT) {
            size_t after_name = look + 2; /* 'int' NAME */
            if (after_name < p.list->count && p.list->tokens[after_name].type == FXTOK_LPAREN) {
                is_func = true;
            }
        }

        if (is_func) {
            if (!doc) {
                fail(&p, "function declaration must be preceded by a /** ... */ doc comment");
            }
            if (nfuncs == fcap) { fcap = fcap ? fcap * 2 : 8; funcs = realloc(funcs, sizeof(FxFunc) * fcap); }
            parse_func_decl(&p, t, doc, &funcs[nfuncs]);
            nfuncs++;
        } else if (check(&p, FXTOK_KW_INT) || check(&p, FXTOK_KW_BYTE) ||
                   (check(&p, FXTOK_IDENT) && is_known_struct(&p, t->value))) {
            bool is_byte = false;
            const char* struct_type = parse_decl_type(&p, &is_byte);
            FxToken* id = expect(&p, FXTOK_IDENT, "identifier");
            check_snake_case(&p, id->value, id->line);

            if (struct_type) {
                const FxStructDef* sd = find_parsed_struct(&p, struct_type);
                expect(&p, FXTOK_SEMI, "';'"); /* no initializer syntax for struct globals in this version */
                if (nglobals == gcap) { gcap = gcap ? gcap * 2 : 8; globals = realloc(globals, sizeof(FxGlobal) * gcap); }
                memset(&globals[nglobals], 0, sizeof(FxGlobal));
                globals[nglobals].name = strdup(id->value);
                globals[nglobals].array_len = sd->nfields;
                globals[nglobals].struct_type_name = strdup(struct_type);
                globals[nglobals].line = id->line;
                nglobals++;
                continue;
            }

            if (is_byte && !check(&p, FXTOK_LBRACKET)) {
                fail(&p, "'byte' may only be used as an array, e.g. 'byte %s[N];'", id->value);
            }

            int32_t array_len = 0;
            bool is_array = parse_array_suffix(&p, &array_len);

            bool has_init = false;
            int32_t init_value = 0;
            bool has_string_init = false;
            char* string_value = NULL;
            int32_t string_len = 0;
            int32_t final_array_len = (is_array && array_len > 0) ? array_len : 0;

            if (match(&p, FXTOK_ASSIGN)) {
                if (check(&p, FXTOK_STRING_LIT)) {
                    if (!is_array) {
                        fail(&p, "string literal initializer requires an array declaration, e.g. 'int %s[] = \"...\";'", id->value);
                    }
                    FxToken* str = advance(&p);
                    int32_t needed = str->str_len + 1;
                    if (array_len > 0 && needed > array_len) {
                        fail(&p, "string literal (%d bytes incl. terminator) does not fit declared array size [%d]", needed, array_len);
                    }
                    final_array_len = array_len > 0 ? array_len : needed;
                    has_string_init = true;
                    string_value = malloc((size_t) str->str_len + 1);
                    memcpy(string_value, str->value, (size_t) str->str_len + 1);
                    string_len = str->str_len;
                } else {
                    if (is_array) {
                        fail(&p, "array '%s' can only be initialized from a string literal in this version of Fluxio", id->value);
                    }
                    FxNode* e = parse_expr(&p);
                    if (!fold_const(e, &init_value)) {
                        fail(&p, "global initializer must be a compile-time constant expression");
                    }
                    fx_node_free(e);
                    has_init = true;
                }
            } else if (is_array && array_len <= 0) {
                fail(&p, "array '%s' declared with '[]' must have a string literal initializer to determine its size", id->value);
            }

            expect(&p, FXTOK_SEMI, "';'");
            if (nglobals == gcap) { gcap = gcap ? gcap * 2 : 8; globals = realloc(globals, sizeof(FxGlobal) * gcap); }
            globals[nglobals].name = strdup(id->value);
            globals[nglobals].array_len = final_array_len;
            globals[nglobals].has_init = has_init;
            globals[nglobals].init_value = init_value;
            globals[nglobals].has_string_init = has_string_init;
            globals[nglobals].string_value = string_value;
            globals[nglobals].string_len = string_len;
            globals[nglobals].struct_type_name = NULL;
            globals[nglobals].line = id->line;
            globals[nglobals].is_byte = is_byte;
            nglobals++;
        } else {
            fail(&p, "expected a global, struct, or function declaration but found '%s'", fx_token_type_name(t->type));
        }
    }

    program->structs = p.structs;
    program->nstructs = (int) p.nstructs;
    program->globals = globals;
    program->nglobals = nglobals;
    program->funcs = funcs;
    program->nfuncs = nfuncs;
    program->externs = externs;
    program->nexterns = nexterns;
    return program;
}
