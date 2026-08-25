#include "fluxio_ast.h"
#include <stdlib.h>
#include <string.h>

FxNode* fx_node_new(FxNodeKind kind, int line, int col) {
    FxNode* n = calloc(1, sizeof(FxNode));
    n->kind = kind;
    n->line = line;
    n->col = col;
    return n;
}

void fx_node_free(FxNode* node) {
    if (!node) return;
    switch (node->kind) {
        case FX_INT_LIT:
            break;
        case FX_STR_LIT:
            free(node->as.str_lit.value);
            break;
        case FX_VAR_REF:
            free(node->as.var.name);
            break;
        case FX_ASSIGN:
            free(node->as.assign.name);
            fx_node_free(node->as.assign.value);
            break;
        case FX_BINARY:
            fx_node_free(node->as.binary.l);
            fx_node_free(node->as.binary.r);
            break;
        case FX_UNARY:
            fx_node_free(node->as.unary.operand);
            break;
        case FX_CALL:
            free(node->as.call.name);
            for (int i = 0; i < node->as.call.nargs; i++) fx_node_free(node->as.call.args[i]);
            free(node->as.call.args);
            break;
        case FX_INDEX:
            free(node->as.index.name);
            fx_node_free(node->as.index.index);
            break;
        case FX_INDEX_ASSIGN:
            free(node->as.index_assign.name);
            fx_node_free(node->as.index_assign.index);
            fx_node_free(node->as.index_assign.value);
            break;
        case FX_FIELD:
            free(node->as.field.name);
            free(node->as.field.field);
            break;
        case FX_FIELD_ASSIGN:
            free(node->as.field_assign.name);
            free(node->as.field_assign.field);
            fx_node_free(node->as.field_assign.value);
            break;
        case FX_LOCAL_DECL:
            free(node->as.local_decl.name);
            fx_node_free(node->as.local_decl.init);
            free(node->as.local_decl.string_value);
            free(node->as.local_decl.struct_type_name);
            break;
        case FX_EXPR_STMT:
            fx_node_free(node->as.expr_stmt.expr);
            break;
        case FX_IF:
            fx_node_free(node->as.if_s.cond);
            fx_node_free(node->as.if_s.then_s);
            fx_node_free(node->as.if_s.else_s);
            break;
        case FX_WHILE:
            fx_node_free(node->as.while_s.cond);
            fx_node_free(node->as.while_s.body);
            break;
        case FX_FOR:
            fx_node_free(node->as.for_s.init);
            fx_node_free(node->as.for_s.cond);
            fx_node_free(node->as.for_s.post);
            fx_node_free(node->as.for_s.body);
            break;
        case FX_RETURN:
            fx_node_free(node->as.ret.expr);
            break;
        case FX_BLOCK:
            for (int i = 0; i < node->as.block.nstmts; i++) fx_node_free(node->as.block.stmts[i]);
            free(node->as.block.stmts);
            break;
        case FX_EMPTY:
            break;
    }
    free(node);
}

void fx_program_free(FxProgram* program) {
    if (!program) return;
    for (int i = 0; i < program->nglobals; i++) {
        free(program->globals[i].name);
        free(program->globals[i].string_value);
        free(program->globals[i].struct_type_name);
    }
    free(program->globals);
    for (int i = 0; i < program->nfuncs; i++) {
        FxFunc* f = &program->funcs[i];
        free(f->name);
        for (int p = 0; p < f->nparams; p++) {
            free(f->params[p].name);
            free(f->params[p].struct_type_name);
        }
        free(f->params);
        fx_node_free(f->body);
    }
    free(program->funcs);
    for (int i = 0; i < program->nstructs; i++) {
        FxStructDef* s = &program->structs[i];
        free(s->name);
        for (int fi = 0; fi < s->nfields; fi++) free(s->fields[fi].name);
        free(s->fields);
    }
    free(program->structs);
    free(program);
}

const FxStructDef* fx_find_struct(const FxProgram* program, const char* name) {
    for (int i = 0; i < program->nstructs; i++) {
        if (strcmp(program->structs[i].name, name) == 0) return &program->structs[i];
    }
    return NULL;
}
