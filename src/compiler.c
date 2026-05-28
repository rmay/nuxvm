#include "compiler.h"
#include "opcodes.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void emit_byte(Compiler* c, uint8_t byte) {
    if (c->active_quot_idx >= 0) {
        Quotation* q = &c->quotations[c->active_quot_idx];
        if (q->code_len >= q->code_cap) {
            q->code_cap = q->code_cap == 0 ? 16 : q->code_cap * 2;
            q->code = realloc(q->code, q->code_cap);
        }
        q->code[q->code_len++] = byte;
    } else {
        if (c->bytecode_len >= c->bytecode_cap) {
            c->bytecode_cap = c->bytecode_cap == 0 ? 64 : c->bytecode_cap * 2;
            c->bytecode = realloc(c->bytecode, c->bytecode_cap);
        }
        c->bytecode[c->bytecode_len++] = byte;
    }
}

static void emit_int32(Compiler* c, int32_t val) {
    emit_byte(c, (val >> 24) & 0xFF);
    emit_byte(c, (val >> 16) & 0xFF);
    emit_byte(c, (val >> 8) & 0xFF);
    emit_byte(c, val & 0xFF);
}

static int32_t current_address(Compiler* c) {
    if (c->active_quot_idx >= 0) {
        return c->quotations[c->active_quot_idx].code_len;
    }
    return c->base_addr + c->bytecode_len;
}

static int32_t current_offset(Compiler* c) {
    if (c->active_quot_idx >= 0) {
        return c->quotations[c->active_quot_idx].code_len;
    }
    return c->bytecode_len;
}

static Token advance(Compiler* c) {
    if (c->pos >= (int)c->token_list->count) {
        Token t = { TOKEN_EOF, NULL, 0, 0 };
        return t;
    }
    return c->token_list->tokens[c->pos++];
}

static Token peek(Compiler* c) {
    if (c->pos >= (int)c->token_list->count) {
        Token t = { TOKEN_EOF, NULL, 0, 0 };
        return t;
    }
    return c->token_list->tokens[c->pos];
}

static bool is_builtin(const char* name, uint8_t* out_op) {
    struct { const char* name; uint8_t op; } builtins[] = {
        {"DUP", OP_DUP}, {"DROP", OP_POP}, {"SWAP", OP_SWAP},
        {"ROT", OP_ROT}, {"OVER", OP_OVER}, {"PICK", OP_PICK},
        {"ROLL", OP_ROLL}, {"LOAD", OP_LOAD}, {"STORE", OP_STORE},
        {"LOADI", OP_LOADI}, {"STOREI", OP_STOREI}, {"EXIT", OP_RET},
        {"HALT", OP_HALT}, {"YIELD", OP_YIELD}, {"JNZ", OP_JNZ},
        {"NEGATE", OP_NEG}, {"ADD", OP_ADD}, {"+", OP_ADD},
        {"SUB", OP_SUB}, {"-", OP_SUB}, {"MUL", OP_MUL}, {"*", OP_MUL},
        {"DIV", OP_DIV}, {"/", OP_DIV}, {"MOD", OP_MOD}, {"INC", OP_INC},
        {"DEC", OP_DEC}, {"AND", OP_AND}, {"OR", OP_OR}, {"XOR", OP_XOR},
        {"NOT", OP_NOT}, {"SHL", OP_SHL}, {"LSHIFT", OP_SHL},
        {"SHR", OP_SHR}, {"SAR", OP_SAR}, {"RSHIFT", OP_SHR},
        {"EQ", OP_EQ}, {"=", OP_EQ}, {"LT", OP_LT}, {"<", OP_LT},
        {"GT", OP_GT}, {">", OP_GT}, {"NEQ", OP_NEQ}, {"<>", OP_NEQ},
        {"LTE", OP_LTE}, {"<=", OP_LTE}, {"GTE", OP_GTE}, {">=", OP_GTE},
        {"ABS", OP_ABS}, {"MIN", OP_MIN}, {"MAX", OP_MAX},
        {"DIVMOD", OP_DIVMOD}, {"CALLSTACK", OP_CALLSTACK},
        {"JMPSTACK", OP_JMPSTACK}, {"PUSHR", OP_PUSHR}, {"POPR", OP_POPR},
        {"PEEKR", OP_PEEKR}, {"PEEKR2", OP_PEEKR2},
        {"FRAME!", OP_FRAME}, {"UNFRAME!", OP_UNFRAME},
        {"LOCAL@", OP_LOCALGET}, {"LOCAL!", OP_LOCALSET},
        {NULL, 0}
    };
    
    for (int i = 0; builtins[i].name != NULL; i++) {
        if (strcasecmp(name, builtins[i].name) == 0) {
            *out_op = builtins[i].op;
            return true;
        }
    }
    return false;
}

static bool resolve_word(Compiler* c, const char* name, WordDef* out_word) {
    for (size_t i = 0; i < c->dict_count; i++) {
        if (strcasecmp(c->dictionary[i].name, name) == 0) {
            *out_word = c->dictionary[i];
            return true;
        }
    }
    return false;
}

static void add_dict(Compiler* c, const char* name, int32_t address) {
    if (c->dict_count >= c->dict_cap) {
        c->dict_cap = c->dict_cap == 0 ? 16 : c->dict_cap * 2;
        c->dictionary = realloc(c->dictionary, c->dict_cap * sizeof(WordDef));
    }
    c->dictionary[c->dict_count].name = strdup(name);
    c->dictionary[c->dict_count].address = address;
    c->dictionary[c->dict_count].module = NULL;
    c->dict_count++;
}

static bool compile_token(Compiler* c, Token t) {
    if (t.type == TOKEN_NUMBER) {
        int32_t val;
        parse_number(&t, &val);
        emit_byte(c, OP_PUSH);
        emit_int32(c, val);
        return true;
    }
    
    if (t.type == TOKEN_WORD) {
        if (strcmp(t.value, ".") == 0) {
            emit_byte(c, OP_PUSH); emit_int32(c, 0); emit_byte(c, OP_OUT);
            return true;
        }
        if (strcasecmp(t.value, "EMIT") == 0) {
            emit_byte(c, OP_PUSH); emit_int32(c, 1); emit_byte(c, OP_OUT);
            return true;
        }
        
        uint8_t op;
        if (is_builtin(t.value, &op)) {
            emit_byte(c, op);
            return true;
        }
        
        WordDef w;
        if (resolve_word(c, t.value, &w)) {
            emit_byte(c, OP_CALL);
            emit_int32(c, w.address);
            return true;
        }
        
        int32_t val;
        if (parse_number(&t, &val)) {
            emit_byte(c, OP_PUSH);
            emit_int32(c, val);
            return true;
        }
        
        // Unresolved
        if (c->unresolved_count >= c->unresolved_cap) {
            c->unresolved_cap = c->unresolved_cap == 0 ? 16 : c->unresolved_cap * 2;
            c->unresolved = realloc(c->unresolved, c->unresolved_cap * sizeof(UnresolvedRef));
        }
        UnresolvedRef ref = { strdup(t.value), current_offset(c), t.line, t.column, c->active_quot_idx, NULL, false };
        c->unresolved[c->unresolved_count++] = ref;
        
        emit_byte(c, OP_PUSH);
        emit_int32(c, 0); // Placeholder
        return true;
    }
    return true; // Ignore other tokens for now
}

static bool compile_word_def(Compiler* c) {
    Token name_tok = advance(c);
    if (name_tok.type != TOKEN_WORD) return false;
    
    int32_t addr = current_address(c);
    add_dict(c, name_tok.value, addr);
    
    while (c->pos < (int)c->token_list->count && peek(c).type != TOKEN_SEMICOLON) {
        compile_token(c, advance(c));
    }
    
    advance(c); // Skip ;
    emit_byte(c, OP_RET);
    return true;
}

Compiler* compiler_create(TokenList* list, int32_t base_addr, bool trace) {
    Compiler* c = calloc(1, sizeof(Compiler));
    c->token_list = list;
    c->base_addr = base_addr;
    c->trace = trace;
    c->active_quot_idx = -1;
    return c;
}

void compiler_free(Compiler* c) {
    if (!c) return;
    if (c->bytecode) free(c->bytecode);
    for (size_t i = 0; i < c->dict_count; i++) {
        free(c->dictionary[i].name);
    }
    if (c->dictionary) free(c->dictionary);
    for (size_t i = 0; i < c->unresolved_count; i++) {
        free(c->unresolved[i].word);
    }
    if (c->unresolved) free(c->unresolved);
    free(c);
}

uint8_t* compiler_compile(Compiler* c, size_t* out_len) {
    emit_byte(c, OP_JMP);
    emit_int32(c, 0); // Placeholder
    
    int start_pos = c->pos;
    
    // Pass 1: Words
    while (c->pos < (int)c->token_list->count && peek(c).type != TOKEN_EOF) {
        Token t = advance(c);
        if (t.type == TOKEN_AT_SIGN) {
            compile_word_def(c);
        }
    }
    
    int32_t main_start = current_address(c);
    c->bytecode[1] = (main_start >> 24) & 0xFF;
    c->bytecode[2] = (main_start >> 16) & 0xFF;
    c->bytecode[3] = (main_start >> 8) & 0xFF;
    c->bytecode[4] = main_start & 0xFF;
    
    // Pass 2: Main
    c->pos = start_pos;
    while (c->pos < (int)c->token_list->count && peek(c).type != TOKEN_EOF) {
        Token t = advance(c);
        if (t.type == TOKEN_AT_SIGN) {
            advance(c); // name
            while (c->pos < (int)c->token_list->count && peek(c).type != TOKEN_SEMICOLON) advance(c);
            advance(c); // ;
            continue;
        }
        compile_token(c, t);
    }
    
    // Resolve unresolved
    for (size_t i = 0; i < c->unresolved_count; i++) {
        UnresolvedRef* u = &c->unresolved[i];
        WordDef w;
        if (!resolve_word(c, u->word, &w)) {
            fprintf(stderr, "Unknown word '%s'\n", u->word);
            return NULL;
        }
        
        c->bytecode[u->offset] = OP_CALL;
        c->bytecode[u->offset + 1] = (w.address >> 24) & 0xFF;
        c->bytecode[u->offset + 2] = (w.address >> 16) & 0xFF;
        c->bytecode[u->offset + 3] = (w.address >> 8) & 0xFF;
        c->bytecode[u->offset + 4] = w.address & 0xFF;
    }
    
    emit_byte(c, OP_HALT);
    
    *out_len = c->bytecode_len;
    uint8_t* result = malloc(c->bytecode_len);
    memcpy(result, c->bytecode, c->bytecode_len);
    return result;
}

uint8_t* compile_source(const char* source, int32_t base_addr, size_t* out_len, bool trace) {
    TokenList* list = tokenize(source);
    if (!list) return NULL;
    
    Compiler* c = compiler_create(list, base_addr, trace);
    uint8_t* bytecode = compiler_compile(c, out_len);
    
    compiler_free(c);
    token_list_free(list);
    return bytecode;
}
