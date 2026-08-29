#include "compiler.h"
#include "opcodes.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

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

/* Case-insensitive substring test, portable (avoids relying on strcasestr,
 * which isn't in the C standard library on every platform). */
static bool contains_ci(const char* haystack, const char* needle) {
    size_t hlen = strlen(haystack), nlen = strlen(needle);
    if (nlen == 0 || nlen > hlen) return false;
    for (size_t i = 0; i + nlen <= hlen; i++) {
        size_t j = 0;
        for (; j < nlen; j++) {
            if (tolower((unsigned char)haystack[i+j]) != tolower((unsigned char)needle[j])) break;
        }
        if (j == nlen) return true;
    }
    return false;
}

/* Records a `@NAME 0xHEX ;` constant for the best-effort duplicate-address
 * check (docs/memory-map.md) -- see the call site in compile_word_def()
 * and the warning scan at the end of compiler_compile(). */
static void record_addr_const(Compiler* c, const char* name, int32_t value, int line) {
    if (c->addr_const_count >= c->addr_const_cap) {
        c->addr_const_cap = c->addr_const_cap == 0 ? 32 : c->addr_const_cap * 2;
        c->addr_consts = realloc(c->addr_consts, c->addr_const_cap * sizeof(*c->addr_consts));
    }
    c->addr_consts[c->addr_const_count].name = strdup(name);
    c->addr_consts[c->addr_const_count].value = value;
    c->addr_consts[c->addr_const_count].line = line;
    c->addr_const_count++;
}

/* Best-effort: warn (not a hard error) when two or more differently-named
 * `@NAME 0xHEX ;` constants share the exact same value -- exactly the bug
 * class fixed in docs/memory-map.md (lib/log.lux vs apps/Quill.lux, etc).
 * Deliberately excludes names containing "CLR"/"COLOR": this codebase's
 * dominant source of *intentional* duplicate hex constants is every
 * app/library defining its own CLR_BG/CLR_TEXT/etc with the same RGB
 * values, which would otherwise drown out real collisions. Sub-range
 * overlap (one buffer's span containing another's base address, not just
 * an exact duplicate) isn't caught here -- see docs/memory-map.md's
 * "Collision checking" section for why that's out of scope for now. */
static void warn_duplicate_addr_consts(Compiler* c) {
    for (size_t i = 0; i < c->addr_const_count; i++) {
        if (c->addr_consts[i].name == NULL) continue; // already reported as part of an earlier group
        bool first = true;
        for (size_t j = i + 1; j < c->addr_const_count; j++) {
            if (c->addr_consts[j].name == NULL) continue;
            if (c->addr_consts[i].value != c->addr_consts[j].value) continue;
            if (first) {
                fprintf(stderr, "Warning: address 0x%X is used by multiple constants (possible collision, see docs/memory-map.md):\n",
                        (unsigned int)c->addr_consts[i].value);
                fprintf(stderr, "  %s (line %d)\n", c->addr_consts[i].name, c->addr_consts[i].line);
                first = false;
            }
            fprintf(stderr, "  %s (line %d)\n", c->addr_consts[j].name, c->addr_consts[j].line);
            free(c->addr_consts[j].name);
            c->addr_consts[j].name = NULL;
        }
    }
}

static bool resolve_word(Compiler* c, const char* name, WordDef* out_word) {
    for (size_t i = 0; i < c->dict_count; i++) {
        if (strcasecmp(c->dictionary[i].name, name) == 0) {
            *out_word = c->dictionary[i];
            return true;
        }
    }
    char buf[256];
    if (c->current_module && c->current_module[0] != '\0') {
        snprintf(buf, sizeof(buf), "%s::%s", c->current_module, name);
        for (size_t i = 0; i < c->dict_count; i++) {
            if (strcasecmp(c->dictionary[i].name, buf) == 0) {
                *out_word = c->dictionary[i];
                return true;
            }
        }
    }
    char* colon = strstr(name, "::");
    if (colon) {
        size_t plen = colon - name;
        char prefix[128];
        if (plen < 127) {
            strncpy(prefix, name, plen);
            prefix[plen] = '\0';
            for (size_t i = 0; i < c->import_count; i++) {
                if (strcasecmp(c->imports[i].alias, prefix) == 0) {
                    snprintf(buf, sizeof(buf), "%s::%s", c->imports[i].module, colon + 2);
                    for (size_t j = 0; j < c->dict_count; j++) {
                        if (strcasecmp(c->dictionary[j].name, buf) == 0) {
                            *out_word = c->dictionary[j];
                            return true;
                        }
                    }
                }
            }
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

static void qualify_name(Compiler* c, const char* name, char* out, size_t outsz) {
    if (c->current_module && c->current_module[0] != '\0' && !strstr(name, "::")) {
        snprintf(out, outsz, "%s::%s", c->current_module, name);
    } else {
        snprintf(out, outsz, "%s", name);
    }
}

static bool lookup_local(Compiler* c, const char* name, int32_t* out_offset) {
    int extra = 0;
    for (int d = c->local_depth - 1; d >= 0; d--) {
        int n = c->local_frames[d].count;
        for (int i = 0; i < n; i++) {
            if (strcasecmp(c->local_frames[d].names[i], name) == 0) {
                /* last declared name is local 0 (same as FRAME! pop order) */
                *out_offset = extra + (n - 1 - i);
                return true;
            }
        }
        extra += n + 1; /* saved fp sits between frames */
    }
    return false;
}

static bool open_named_frame(Compiler* c, char names[COMPILER_MAX_LOCAL_NAMES][COMPILER_MAX_LOCAL_NAME], int count) {
    if (c->local_depth >= COMPILER_MAX_LOCAL_FRAMES) {
        fprintf(stderr, "Too many nested local frames\n");
        return false;
    }
    memcpy(c->local_frames[c->local_depth].names, names,
           sizeof(c->local_frames[c->local_depth].names));
    c->local_frames[c->local_depth].count = count;
    emit_byte(c, OP_PUSH);
    emit_int32(c, count);
    emit_byte(c, OP_FRAME);
    c->local_depth++;
    return true;
}

static bool compile_local_frame_start(Compiler* c) {
    int count = 0;
    char names[COMPILER_MAX_LOCAL_NAMES][COMPILER_MAX_LOCAL_NAME];
    bool skipping_doc = false;
    int start_line = peek(c).line;

    while (c->pos < (int)c->token_list->count) {
        Token t = peek(c);
        if (t.type == TOKEN_WORD && t.value && strcmp(t.value, "}") == 0) {
            advance(c);
            break;
        }
        if (t.type == TOKEN_WORD && t.value && strcmp(t.value, "--") == 0) {
            skipping_doc = true;
            advance(c);
            continue;
        }
        if (t.type == TOKEN_SEMICOLON || t.type == TOKEN_EOF || t.type == TOKEN_RBRACKET) {
            fprintf(stderr, "Unclosed { local frame at line %d\n", start_line);
            return false;
        }
        if (!skipping_doc) {
            if (t.type != TOKEN_WORD || !t.value) {
                fprintf(stderr, "Expected local name inside { } at line %d\n", t.line);
                return false;
            }
            if (count >= COMPILER_MAX_LOCAL_NAMES) {
                fprintf(stderr, "Too many locals in frame at line %d\n", t.line);
                return false;
            }
            strncpy(names[count], t.value, COMPILER_MAX_LOCAL_NAME - 1);
            names[count][COMPILER_MAX_LOCAL_NAME - 1] = '\0';
            count++;
        }
        advance(c);
    }

    if (count == 0) {
        fprintf(stderr, "Empty local frame { } at line %d\n", start_line);
        return false;
    }

    return open_named_frame(c, names, count);
}

/* GIRD name — bind TOS as a one-slot named frame. UNGIRD pops it. */
static bool compile_gird(Compiler* c, int line) {
    Token name = peek(c);
    if (name.type != TOKEN_WORD || !name.value ||
        strcmp(name.value, "}") == 0 || strcmp(name.value, "{") == 0 ||
        strcmp(name.value, "--") == 0 ||
        strcasecmp(name.value, "GIRD") == 0 ||
        strcasecmp(name.value, "UNGIRD") == 0) {
        fprintf(stderr, "Expected local name after GIRD at line %d\n", line);
        return false;
    }
    advance(c);
    char names[COMPILER_MAX_LOCAL_NAMES][COMPILER_MAX_LOCAL_NAME];
    memset(names, 0, sizeof(names));
    strncpy(names[0], name.value, COMPILER_MAX_LOCAL_NAME - 1);
    names[0][COMPILER_MAX_LOCAL_NAME - 1] = '\0';
    return open_named_frame(c, names, 1);
}

static bool compile_local_frame_end(Compiler* c, int line, const char* tok) {
    if (c->local_depth <= 0) {
        fprintf(stderr, "Unexpected %s at line %d\n", tok, line);
        return false;
    }
    c->local_depth--;
    emit_byte(c, OP_PUSH);
    emit_int32(c, c->local_frames[c->local_depth].count);
    emit_byte(c, OP_UNFRAME);
    return true;
}

static void emit_unframe_all(Compiler* c) {
    while (c->local_depth > 0) {
        compile_local_frame_end(c, 0, "UNGIRD");
    }
}

/* JMPSTACK/JMP would skip UNFRAME for any still-open { } frame. */
static bool at_tail_position(Compiler* c) {
    if (c->local_depth > 0) return false;
    Token next = peek(c);
    return next.type == TOKEN_SEMICOLON || next.type == TOKEN_RBRACKET;
}

static bool is_fields_terminator(Token t) {
    if (t.type == TOKEN_AT_SIGN || t.type == TOKEN_EOF ||
        t.type == TOKEN_SEMICOLON || t.type == TOKEN_LBRACKET) {
        return true;
    }
    if (t.type == TOKEN_WORD && t.value) {
        if (strcmp(t.value, "MODULE") == 0 || strcmp(t.value, "IMPORT") == 0 ||
            strcmp(t.value, "INCLUDE") == 0 || strcasecmp(t.value, "FIELDS") == 0) {
            return true;
        }
    }
    return false;
}

static void skip_fields(Compiler* c) {
    if (c->pos < (int)c->token_list->count) advance(c); /* prefix */
    while (c->pos < (int)c->token_list->count && !is_fields_terminator(peek(c))) {
        advance(c);
    }
    if (peek(c).type == TOKEN_SEMICOLON) advance(c);
}

static bool compile_fields(Compiler* c) {
    Token prefix = advance(c);
    if (prefix.type != TOKEN_WORD || !prefix.value) {
        fprintf(stderr, "FIELDS expects a prefix name\n");
        return false;
    }

    char fields[32][64];
    int n = 0;
    while (c->pos < (int)c->token_list->count && !is_fields_terminator(peek(c))) {
        Token f = advance(c);
        if (f.type != TOKEN_WORD || !f.value) {
            fprintf(stderr, "FIELDS expected field name\n");
            return false;
        }
        if (n >= 32) {
            fprintf(stderr, "Too many FIELDS on %s\n", prefix.value);
            return false;
        }
        strncpy(fields[n], f.value, 63);
        fields[n][63] = '\0';
        n++;
    }
    if (n == 0) {
        fprintf(stderr, "FIELDS %s has no fields\n", prefix.value);
        return false;
    }
    if (peek(c).type == TOKEN_SEMICOLON) {
        advance(c);
    }

    char raw[256];
    char qname[256];

    snprintf(raw, sizeof(raw), "%s.SIZE", prefix.value);
    qualify_name(c, raw, qname, sizeof(qname));
    add_dict(c, qname, current_address(c));
    emit_byte(c, OP_PUSH);
    emit_int32(c, n * 4);
    emit_byte(c, OP_RET);

    for (int i = 0; i < n; i++) {
        int32_t off = i * 4;

        snprintf(raw, sizeof(raw), "%s.%s", prefix.value, fields[i]);
        qualify_name(c, raw, qname, sizeof(qname));
        add_dict(c, qname, current_address(c));
        emit_byte(c, OP_PUSH);
        emit_int32(c, off);
        emit_byte(c, OP_RET);

        snprintf(raw, sizeof(raw), "%s.%s@", prefix.value, fields[i]);
        qualify_name(c, raw, qname, sizeof(qname));
        add_dict(c, qname, current_address(c));
        emit_byte(c, OP_PUSH);
        emit_int32(c, off);
        emit_byte(c, OP_ADD);
        emit_byte(c, OP_LOADI);
        emit_byte(c, OP_RET);

        snprintf(raw, sizeof(raw), "%s.%s!", prefix.value, fields[i]);
        qualify_name(c, raw, qname, sizeof(qname));
        add_dict(c, qname, current_address(c));
        emit_byte(c, OP_PUSH);
        emit_int32(c, off);
        emit_byte(c, OP_ADD);
        emit_byte(c, OP_STOREI);
        emit_byte(c, OP_RET);
    }
    return true;
}


static void push_quot_stack(Compiler* c, int idx) {
    if (c->quot_stack_count >= c->quot_stack_cap) {
        c->quot_stack_cap = c->quot_stack_cap == 0 ? 16 : c->quot_stack_cap * 2;
        c->quot_stack = realloc(c->quot_stack, c->quot_stack_cap * sizeof(int));
    }
    c->quot_stack[c->quot_stack_count++] = idx;
}

static void push_quot_stack_frame(Compiler* c) {
    if (c->quot_saved_count >= c->quot_saved_cap) {
        c->quot_saved_cap = c->quot_saved_cap == 0 ? 16 : c->quot_saved_cap * 2;
        c->quot_saved_frames = realloc(c->quot_saved_frames, c->quot_saved_cap * sizeof(QuotStackFrame));
    }
    QuotStackFrame* frame = &c->quot_saved_frames[c->quot_saved_count++];
    frame->stack = c->quot_stack;
    frame->count = c->quot_stack_count;
    frame->cap = c->quot_stack_cap;
    frame->active_quot_idx = c->active_quot_idx;
    c->quot_stack = NULL;
    c->quot_stack_count = 0;
    c->quot_stack_cap = 0;
}

static void pop_quot_stack_frame(Compiler* c) {
    if (c->quot_saved_count == 0) return;
    c->quot_saved_count--;
    QuotStackFrame* frame = &c->quot_saved_frames[c->quot_saved_count];
    c->quot_stack = frame->stack;
    c->quot_stack_count = frame->count;
    c->quot_stack_cap = frame->cap;
    c->active_quot_idx = frame->active_quot_idx;
}

static void compile_quotation_start(Compiler* c) {
    if (c->quot_count >= c->quot_cap) {
        c->quot_cap = c->quot_cap == 0 ? 16 : c->quot_cap * 2;
        c->quotations = realloc(c->quotations, c->quot_cap * sizeof(Quotation));
    }
    int idx = c->quot_count++;
    Quotation* q = &c->quotations[idx];
    memset(q, 0, sizeof(Quotation));
    
    q->temp_addr = c->temp_alloc;
    c->temp_alloc += 4;
    q->saved_local_depth = c->local_depth;
    
    push_quot_stack_frame(c);
    c->active_quot_idx = idx;
}

static void compile_quotation_end(Compiler* c) {
    int idx = c->active_quot_idx;
    if (idx >= 0 && idx < (int)c->quot_count) {
        while (c->local_depth > c->quotations[idx].saved_local_depth) {
            compile_local_frame_end(c, 0, "UNGIRD");
        }
    }
    emit_byte(c, OP_RET);
    pop_quot_stack_frame(c);
    
    push_quot_stack(c, idx);
    
    emit_byte(c, OP_PUSH);
    int32_t off = current_offset(c);
    emit_int32(c, 0); // Placeholder
    
    if (c->patches_count >= c->patches_cap) {
        c->patches_cap = c->patches_cap == 0 ? 16 : c->patches_cap * 2;
        c->patches = realloc(c->patches, c->patches_cap * sizeof(PatchRequest));
    }
    c->patches[c->patches_count++] = (PatchRequest){ idx, off, 0, c->active_quot_idx };
}

static void add_jump(Compiler* c, int32_t at, int32_t to) {
    if (c->active_quot_idx >= 0) {
        Quotation* q = &c->quotations[c->active_quot_idx];
        if (q->jumps_count >= q->jumps_cap) {
            q->jumps_cap = q->jumps_cap == 0 ? 16 : q->jumps_cap * 2;
            q->jumps = realloc(q->jumps, q->jumps_cap * sizeof(InternalJump));
        }
        q->jumps[q->jumps_count++] = (InternalJump){ at, to };
    } else {
        c->bytecode[at] = (to >> 24) & 0xFF;
        c->bytecode[at+1] = (to >> 16) & 0xFF;
        c->bytecode[at+2] = (to >> 8) & 0xFF;
        c->bytecode[at+3] = to & 0xFF;
    }
}

static bool compile_combinator(Compiler* c, const char* name) {
    if (strcasecmp(name, "CALL") == 0) {
        if (c->quot_stack_count > 0) c->quot_stack_count--;
        emit_byte(c, OP_CALLSTACK);
        return true;
    }
    
    if (strcmp(name, "?:") == 0) {
        if (c->quot_stack_count < 2) return false;
        c->quot_stack_count -= 2;
        
        bool is_tail = at_tail_position(c);
        
        emit_byte(c, OP_ROT);
        emit_byte(c, OP_JZ);
        int32_t jz_at = current_offset(c);
        emit_int32(c, 0);
        
        emit_byte(c, OP_POP);
        int32_t jmp_at = 0;
        if (is_tail) {
            emit_byte(c, OP_JMPSTACK);
        } else {
            emit_byte(c, OP_CALLSTACK);
            emit_byte(c, OP_JMP);
            jmp_at = current_offset(c);
            emit_int32(c, 0);
        }
        
        int32_t else_at = current_address(c);
        emit_byte(c, OP_SWAP);
        emit_byte(c, OP_POP);
        if (is_tail) {
            emit_byte(c, OP_JMPSTACK);
        } else {
            emit_byte(c, OP_CALLSTACK);
        }
        int32_t end_at = current_address(c);
        
        add_jump(c, jz_at, else_at);
        if (!is_tail) add_jump(c, jmp_at, end_at);
        return true;
    }

    if (strcmp(name, "?") == 0) {
        if (c->quot_stack_count < 1) return false;
        c->quot_stack_count -= 1;
        bool is_tail = at_tail_position(c);
        
        emit_byte(c, OP_SWAP);
        emit_byte(c, OP_JZ);
        int32_t jz_at = current_offset(c);
        emit_int32(c, 0);
        
        if (is_tail) {
            emit_byte(c, OP_JMPSTACK);
            add_jump(c, jz_at, current_address(c));
            emit_byte(c, OP_POP);
            return true;
        } else {
            emit_byte(c, OP_CALLSTACK);
            emit_byte(c, OP_JMP);
            int32_t jmp_at = current_offset(c);
            emit_int32(c, 0);
            add_jump(c, jz_at, current_address(c));
            emit_byte(c, OP_POP);
            add_jump(c, jmp_at, current_address(c));
            return true;
        }
    }

    if (strcmp(name, "!:") == 0) {
        if (c->quot_stack_count < 1) return false;
        c->quot_stack_count -= 1;
        bool is_tail = at_tail_position(c);
        
        emit_byte(c, OP_SWAP);
        emit_byte(c, OP_JNZ);
        int32_t jnz_at = current_offset(c);
        emit_int32(c, 0);
        
        if (is_tail) {
            emit_byte(c, OP_JMPSTACK);
        } else {
            emit_byte(c, OP_CALLSTACK);
            emit_byte(c, OP_JMP);
            int32_t jmp_at = current_offset(c);
            emit_int32(c, 0);
            add_jump(c, jnz_at, current_address(c));
            emit_byte(c, OP_POP);
            add_jump(c, jmp_at, current_address(c));
            return true;
        }
        
        int32_t end_at = current_address(c);
        add_jump(c, jnz_at, end_at);
        return true;
    }

    if (strcmp(name, "|:") == 0) {
        if (c->quot_stack_count < 2) return false;
        c->quot_stack_count -= 2;
        emit_byte(c, OP_PUSHR);
        emit_byte(c, OP_PUSHR);
        int32_t start_at = current_address(c);
        emit_byte(c, OP_PEEKR);
        emit_byte(c, OP_CALLSTACK);
        emit_byte(c, OP_JZ);
        int32_t jz_at = current_offset(c);
        emit_int32(c, 0);
        emit_byte(c, OP_PEEKR2);
        emit_byte(c, OP_CALLSTACK);
        emit_byte(c, OP_JMP);
        int32_t jmp_at = current_offset(c);
        emit_int32(c, 0);
        int32_t exit_at = current_address(c);
        emit_byte(c, OP_POPR);
        emit_byte(c, OP_POPR);
        emit_byte(c, OP_POP);
        emit_byte(c, OP_POP);
        
        add_jump(c, jz_at, exit_at);
        add_jump(c, jmp_at, start_at);
        return true;
    }

    if (strcmp(name, "#:") == 0) {
        if (c->quot_stack_count < 1) return false;
        c->quot_stack_count -= 1;
        emit_byte(c, OP_SWAP);
        emit_byte(c, OP_PUSHR);
        emit_byte(c, OP_PUSHR);
        int32_t start_at = current_address(c);
        emit_byte(c, OP_PEEKR);
        emit_byte(c, OP_JZ);
        int32_t jz_at = current_offset(c);
        emit_int32(c, 0);
        emit_byte(c, OP_PEEKR2);
        emit_byte(c, OP_CALLSTACK);
        emit_byte(c, OP_POPR);
        emit_byte(c, OP_DEC);
        emit_byte(c, OP_PUSHR);
        emit_byte(c, OP_JMP);
        int32_t jmp_at = current_offset(c);
        emit_int32(c, 0);
        int32_t exit_at = current_address(c);
        emit_byte(c, OP_POPR);
        emit_byte(c, OP_POPR);
        emit_byte(c, OP_POP);
        emit_byte(c, OP_POP);
        
        add_jump(c, jz_at, exit_at);
        add_jump(c, jmp_at, start_at);
        return true;
    }

    if (strcmp(name, "DIP") == 0) {
        if (c->quot_stack_count < 1) return false;
        c->quot_stack_count -= 1;
        emit_byte(c, OP_SWAP);
        emit_byte(c, OP_PUSHR);
        emit_byte(c, OP_CALLSTACK);
        emit_byte(c, OP_POPR);
        return true;
    }
    
    if (strcmp(name, "KEEP") == 0) {
        if (c->quot_stack_count < 1) return false;
        c->quot_stack_count -= 1;
        emit_byte(c, OP_SWAP);
        emit_byte(c, OP_DUP);
        emit_byte(c, OP_PUSHR);
        emit_byte(c, OP_SWAP);
        emit_byte(c, OP_CALLSTACK);
        emit_byte(c, OP_POPR);
        return true;
    }

    return false;
}

static bool is_combinator_token(const char* name) {
    return strcasecmp(name, "CALL") == 0 ||
           strcmp(name, "?:") == 0 ||
           strcmp(name, "?") == 0 ||
           strcmp(name, "!:") == 0 ||
           strcmp(name, "|:") == 0 ||
           strcmp(name, "#:") == 0 ||
           strcasecmp(name, "DIP") == 0 ||
           strcasecmp(name, "KEEP") == 0;
}

static bool report_combinator_error(const char* name, Token t) {
    if (strcmp(name, "?:") == 0 || strcmp(name, "|:") == 0) {
        fprintf(stderr, "%s requires two quotations at line %d\n", name, t.line);
        return true;
    }
    if (strcmp(name, "?") == 0 || strcmp(name, "!:") == 0 || strcmp(name, "#:") == 0 ||
        strcasecmp(name, "DIP") == 0 || strcasecmp(name, "KEEP") == 0) {
        fprintf(stderr, "%s requires one quotation at line %d\n", name, t.line);
        return true;
    }
    return false;
}

static bool compile_token(Compiler* c, Token t);


static bool compile_token(Compiler* c, Token t) {
    if (t.type == TOKEN_LBRACKET) {
        compile_quotation_start(c);
        return true;
    }
    if (t.type == TOKEN_RBRACKET) {
        compile_quotation_end(c);
        return true;
    }
    if (t.type == TOKEN_STRING) {
        // T-string
        emit_byte(c, OP_PUSH);
        int32_t off = current_offset(c);
        emit_int32(c, 0); // placeholder
        if (c->string_patches_count >= c->string_patches_cap) {
            c->string_patches_cap = c->string_patches_cap == 0 ? 16 : c->string_patches_cap * 2;
            c->string_patches = realloc(c->string_patches, c->string_patches_cap * sizeof(StringPatch));
        }
        c->string_patches[c->string_patches_count++] = (StringPatch){ off, c->active_quot_idx, strdup(t.value) };
        return true;
    }
    if (t.type == TOKEN_NUMBER) {
        int32_t val;
        parse_number(&t, &val);
        emit_byte(c, OP_PUSH);
        emit_int32(c, val);
        return true;
    }
    
    if (t.type == TOKEN_WORD) {
        if (strcmp(t.value, "{") == 0) {
            return compile_local_frame_start(c);
        }
        if (strcmp(t.value, "}") == 0) {
            fprintf(stderr, "Unexpected } at line %d (use UNGIRD to close a frame)\n", t.line);
            return false;
        }
        if (strcasecmp(t.value, "GIRD") == 0) {
            return compile_gird(c, t.line);
        }
        if (strcasecmp(t.value, "UNGIRD") == 0) {
            return compile_local_frame_end(c, t.line, "UNGIRD");
        }

        int32_t local_off;
        if (lookup_local(c, t.value, &local_off)) {
            emit_byte(c, OP_PUSH);
            emit_int32(c, local_off);
            emit_byte(c, OP_LOCALGET);
            return true;
        }
        size_t nlen = strlen(t.value);
        if (nlen > 1 && t.value[nlen - 1] == '!') {
            char lname[COMPILER_MAX_LOCAL_NAME];
            if (nlen - 1 < sizeof(lname)) {
                memcpy(lname, t.value, nlen - 1);
                lname[nlen - 1] = '\0';
                if (lookup_local(c, lname, &local_off)) {
                    emit_byte(c, OP_PUSH);
                    emit_int32(c, local_off);
                    emit_byte(c, OP_LOCALSET);
                    return true;
                }
            }
        }

        if (strcmp(t.value, ".") == 0) {
            emit_byte(c, OP_PUSH); emit_int32(c, 0); emit_byte(c, OP_OUT);
            return true;
        }
        if (strcasecmp(t.value, "EMIT") == 0) {
            emit_byte(c, OP_PUSH); emit_int32(c, 1); emit_byte(c, OP_OUT);
            return true;
        }
        if (strcmp(t.value, "MODULE") == 0) {
            Token name = advance(c);
            if (name.type != TOKEN_WORD || !name.value) {
                fprintf(stderr, "Error: expected name after MODULE\n");
                return false;
            }
            if (c->current_module) free(c->current_module);
            c->current_module = strdup(name.value);
            return true;
        }
        if (strcmp(t.value, "IMPORT") == 0) {
            Token mod = advance(c);
            Token as_tok = advance(c);
            char* alias_val = NULL;
            if (as_tok.type == TOKEN_WORD && strcmp(as_tok.value, "AS") == 0) {
                Token alias = advance(c);
                alias_val = strdup(alias.value);
            } else {
                c->pos--; // backtrack AS
            }
            if (c->import_count >= c->import_cap) {
                c->import_cap = c->import_cap == 0 ? 16 : c->import_cap * 2;
                c->imports = realloc(c->imports, c->import_cap * sizeof(*c->imports));
            }
            c->imports[c->import_count].module = strdup(mod.value);
            c->imports[c->import_count].alias = alias_val;
            c->import_count++;
            return true;
        }
        if (strcmp(t.value, "VERSION") == 0) {
            Token ver = advance(c);
            int32_t val;
            if (ver.type != TOKEN_NUMBER || !parse_number(&ver, &val)) {
                fprintf(stderr, "Error: expected integer after VERSION (line %d)\n", t.line);
                return false;
            }
            c->version_seen = true;
            c->version_value = val;
            return true;
        }

        if (compile_combinator(c, t.value)) {
            return true;
        }
        if (is_combinator_token(t.value)) {
            report_combinator_error(t.value, t);
            return false;
        }
        
        uint8_t op;
        if (is_builtin(t.value, &op)) {
            emit_byte(c, op);
            return true;
        }
        
        bool is_tail = at_tail_position(c);
        WordDef w;
        if (resolve_word(c, t.value, &w)) {
            if (is_tail) emit_byte(c, OP_JMP);
            else emit_byte(c, OP_CALL);
            emit_int32(c, w.address);
            return true;
        }
        
        int32_t val;
        if (parse_number(&t, &val)) {
            emit_byte(c, OP_PUSH);
            emit_int32(c, val);
            return true;
        }
        
        if (c->unresolved_count >= c->unresolved_cap) {
            c->unresolved_cap = c->unresolved_cap == 0 ? 16 : c->unresolved_cap * 2;
            c->unresolved = realloc(c->unresolved, c->unresolved_cap * sizeof(UnresolvedRef));
        }
        UnresolvedRef ref = { strdup(t.value), current_offset(c), t.line, t.column, c->active_quot_idx, NULL, false, is_tail };
        c->unresolved[c->unresolved_count++] = ref;
        
        emit_byte(c, OP_PUSH);
        emit_int32(c, 0); // Placeholder
        return true;
    }
    if (t.type == TOKEN_DOLLAR) {
        Token target = advance(c);
        if (c->unresolved_count >= c->unresolved_cap) {
            c->unresolved_cap = c->unresolved_cap == 0 ? 16 : c->unresolved_cap * 2;
            c->unresolved = realloc(c->unresolved, c->unresolved_cap * sizeof(UnresolvedRef));
        }
        UnresolvedRef ref = { strdup(target.value), current_offset(c), t.line, t.column, c->active_quot_idx, NULL, true, false };
        c->unresolved[c->unresolved_count++] = ref;
        
        emit_byte(c, OP_PUSH);
        emit_int32(c, 0); // Placeholder
        return true;
    }
    return true;
}

static bool compile_word_def(Compiler* c) {
    Token name_tok = advance(c);
    if (name_tok.type != TOKEN_WORD) return false;

    c->quot_stack_count = 0;
    c->local_depth = 0;
    
    char* final_name = name_tok.value;
    char buf[256];
    if (name_tok.value[0] == '.') {
        final_name = name_tok.value + 1;
    } else if (c->current_module && c->current_module[0] != '\0' && !strstr(name_tok.value, "::")) {
        snprintf(buf, sizeof(buf), "%s::%s", c->current_module, name_tok.value);
        final_name = buf;
    }
    
    int32_t addr = current_address(c);
    add_dict(c, final_name, addr);

    /* Duplicate-address check (docs/memory-map.md): the `@NAME 0xHEX ;`
     * constant idiom is exactly one number token followed by `;`. Record
     * it before compiling the body so the same lookahead logic doesn't
     * have to be duplicated post-hoc from bytecode. */
    if (c->pos + 1 < (int)c->token_list->count) {
        Token body0 = c->token_list->tokens[c->pos];
        Token body1 = c->token_list->tokens[c->pos + 1];
        if (body0.type == TOKEN_NUMBER && body1.type == TOKEN_SEMICOLON && body0.value &&
            (strncmp(body0.value, "0x", 2) == 0 || strncmp(body0.value, "0X", 2) == 0) &&
            !contains_ci(final_name, "CLR") && !contains_ci(final_name, "COLOR")) {
            int32_t val;
            if (parse_number(&body0, &val)) {
                record_addr_const(c, final_name, val, name_tok.line);
            }
        }
    }

    while (c->pos < (int)c->token_list->count && peek(c).type != TOKEN_SEMICOLON) {
        if (!compile_token(c, advance(c))) return false;
    }
    
    advance(c); // Skip ;
    emit_unframe_all(c);
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
    for (size_t i = 0; i < c->addr_const_count; i++) {
        if (c->addr_consts[i].name) free(c->addr_consts[i].name);
    }
    if (c->addr_consts) free(c->addr_consts);
    for (size_t i = 0; i < c->unresolved_count; i++) {
        free(c->unresolved[i].word);
    }
    if (c->unresolved) free(c->unresolved);
    if (c->quot_stack) free(c->quot_stack);
    for (size_t i = 0; i < c->quot_saved_count; i++) {
        if (c->quot_saved_frames[i].stack) free(c->quot_saved_frames[i].stack);
    }
    if (c->quot_saved_frames) free(c->quot_saved_frames);
    free(c);
}

static bool preprocess_includes(Compiler* c) {
    char* current_module = NULL;
    c->pos = 0;
    while (c->pos < (int)c->token_list->count && peek(c).type != TOKEN_EOF) {
        Token t = advance(c);
        if (t.type == TOKEN_WORD && strcmp(t.value, "MODULE") == 0) {
            Token name = peek(c);
            if (name.type != TOKEN_WORD || !name.value) {
                fprintf(stderr, "Error: expected name after MODULE\n");
                if (current_module) free(current_module);
                return false;
            }
            if (current_module) free(current_module);
            current_module = strdup(name.value);
            continue;
        }
        if (t.type == TOKEN_WORD && strcmp(t.value, "INCLUDE") == 0) {
            Token file = advance(c);
            if (file.type != TOKEN_STRING && file.type != TOKEN_WORD) {
                fprintf(stderr, "Error: expected string after INCLUDE\n");
                return false;
            }
            
            bool already_included = false;
            for (size_t i = 0; i < c->included_count; i++) {
                if (strcmp(c->included_files[i], file.value) == 0) {
                    already_included = true;
                    break;
                }
            }
            
            if (already_included) {
                size_t move_count = c->token_list->count - c->pos;
                memmove(&c->token_list->tokens[c->pos - 2], 
                        &c->token_list->tokens[c->pos], 
                        move_count * sizeof(Token));
                c->token_list->count -= 2;
                c->pos -= 2;
                continue;
            }
            
            if (c->included_count >= c->included_cap) {
                c->included_cap = c->included_cap == 0 ? 16 : c->included_cap * 2;
                c->included_files = realloc(c->included_files, c->included_cap * sizeof(char*));
            }
            c->included_files[c->included_count++] = strdup(file.value);
            
            FILE* f = fopen(file.value, "rb");
            if (!f) return false;
            fseek(f, 0, SEEK_END);
            long fsize = ftell(f);
            fseek(f, 0, SEEK_SET);
            char* source = malloc(fsize + 1);
            fread(source, 1, fsize, f);
            source[fsize] = '\0';
            fclose(f);
            
            TokenList* inc_list = tokenize(source);
            free(source);
            
            size_t inc_count = inc_list->count;
            if (inc_count > 0 && inc_list->tokens[inc_count-1].type == TOKEN_EOF) {
                inc_count--;
            }
            
            int net_change = (int)inc_count; // + MODULE restore
            
            size_t new_total = c->token_list->count + net_change;
            if (new_total > c->token_list->capacity) {
                while (c->token_list->capacity < new_total) {
                    c->token_list->capacity = c->token_list->capacity == 0 ? 16 : c->token_list->capacity * 2;
                }
                c->token_list->tokens = realloc(c->token_list->tokens, c->token_list->capacity * sizeof(Token));
            }
            
            size_t move_count = c->token_list->count - c->pos;
            memmove(&c->token_list->tokens[c->pos - 2 + inc_count + 2], 
                    &c->token_list->tokens[c->pos], 
                    move_count * sizeof(Token));
            
            for (size_t i = 0; i < inc_count; i++) {
                c->token_list->tokens[c->pos - 2 + i] = inc_list->tokens[i];
                inc_list->tokens[i].value = NULL; 
            }
            
            c->token_list->tokens[c->pos - 2 + inc_count].type = TOKEN_WORD;
            c->token_list->tokens[c->pos - 2 + inc_count].value = strdup("MODULE");
            c->token_list->tokens[c->pos - 2 + inc_count].line = 0;
            c->token_list->tokens[c->pos - 2 + inc_count].column = 0;
            
            c->token_list->tokens[c->pos - 2 + inc_count + 1].type = TOKEN_WORD;
            c->token_list->tokens[c->pos - 2 + inc_count + 1].value = current_module ? strdup(current_module) : strdup("");
            c->token_list->tokens[c->pos - 2 + inc_count + 1].line = 0;
            c->token_list->tokens[c->pos - 2 + inc_count + 1].column = 0;
            
            c->token_list->count += net_change;
            token_list_free(inc_list);
            
            c->pos -= 2;
        }
    }
    if (current_module) free(current_module);
    return true;
}

uint8_t* compiler_compile(Compiler* c, size_t* out_len) {
    if (!preprocess_includes(c)) return NULL;
    c->pos = 0;

    emit_byte(c, OP_JMP);
    emit_int32(c, 0); // Placeholder
    
    int start_pos = c->pos;
    
    // Pass 1: Words
    while (c->pos < (int)c->token_list->count && peek(c).type != TOKEN_EOF) {
        Token t = advance(c);
        if (t.type == TOKEN_WORD && strcmp(t.value, "MODULE") == 0) {
            Token name = advance(c);
            if (name.type != TOKEN_WORD || !name.value) {
                fprintf(stderr, "Error: expected name after MODULE\n");
                return NULL;
            }
            if (c->current_module) free(c->current_module);
            c->current_module = strdup(name.value);
        } else if (t.type == TOKEN_AT_SIGN) {
            if (!compile_word_def(c)) return NULL;
        } else if (t.type == TOKEN_WORD && t.value && strcasecmp(t.value, "FIELDS") == 0) {
            if (!compile_fields(c)) return NULL;
        }
    }
    
    // Reset current_module for Pass 2
    if (c->current_module) free(c->current_module);
    c->current_module = NULL;
    c->quot_stack_count = 0;
    c->local_depth = 0;
    
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
        if (t.type == TOKEN_WORD && t.value && strcasecmp(t.value, "FIELDS") == 0) {
            skip_fields(c);
            continue;
        }
        if (!compile_token(c, t)) return NULL;
    }
    
    if (c->active_quot_idx >= 0 || c->quot_stack_count > 0) {
        fprintf(stderr, "Unclosed quotation\n");
        return NULL;
    }
    if (c->local_depth > 0) {
        fprintf(stderr, "Unclosed local frame\n");
        return NULL;
    }

    emit_byte(c, OP_HALT);
    // Compute quotation addresses
    int32_t curr_addr = current_address(c);
    for (size_t i = 0; i < c->quot_count; i++) {
        c->quotations[i].address = curr_addr;
        curr_addr += c->quotations[i].code_len;
    }
    
    // Resolve string patches
    int32_t string_heap = curr_addr;
    for (size_t i = 0; i < c->string_patches_count; i++) {
        StringPatch* p = &c->string_patches[i];
        int32_t str_addr = string_heap;
        size_t slen = strlen(p->str);
        string_heap += slen + 1;
        
        if (p->quot_idx >= 0) {
            c->quotations[p->quot_idx].code[p->offset] = (str_addr >> 24) & 0xFF;
            c->quotations[p->quot_idx].code[p->offset+1] = (str_addr >> 16) & 0xFF;
            c->quotations[p->quot_idx].code[p->offset+2] = (str_addr >> 8) & 0xFF;
            c->quotations[p->quot_idx].code[p->offset+3] = str_addr & 0xFF;
        } else {
            c->bytecode[p->offset] = (str_addr >> 24) & 0xFF;
            c->bytecode[p->offset+1] = (str_addr >> 16) & 0xFF;
            c->bytecode[p->offset+2] = (str_addr >> 8) & 0xFF;
            c->bytecode[p->offset+3] = str_addr & 0xFF;
        }
    }
    
    // Patch unresolved inside quotations BEFORE flattening
    for (size_t i = 0; i < c->unresolved_count; i++) {
        UnresolvedRef* u = &c->unresolved[i];
        WordDef w;
        if (!resolve_word(c, u->word, &w)) {
            fprintf(stderr, "Unknown word '%s'\n", u->word);
            return NULL;
        }
        if (u->quot_idx >= 0) {
            if (!u->is_address) c->quotations[u->quot_idx].code[u->offset] = u->is_tail_call ? OP_JMP : OP_CALL;
            c->quotations[u->quot_idx].code[u->offset+1] = (w.address >> 24) & 0xFF;
            c->quotations[u->quot_idx].code[u->offset+2] = (w.address >> 16) & 0xFF;
            c->quotations[u->quot_idx].code[u->offset+3] = (w.address >> 8) & 0xFF;
            c->quotations[u->quot_idx].code[u->offset+4] = w.address & 0xFF;
        } else {
            if (!u->is_address) c->bytecode[u->offset] = u->is_tail_call ? OP_JMP : OP_CALL;
            c->bytecode[u->offset+1] = (w.address >> 24) & 0xFF;
            c->bytecode[u->offset+2] = (w.address >> 16) & 0xFF;
            c->bytecode[u->offset+3] = (w.address >> 8) & 0xFF;
            c->bytecode[u->offset+4] = w.address & 0xFF;
        }
    }
    c->unresolved_count = 0; // Prevent second loop
    
    // Patch quotation pushes
    for (size_t i = 0; i < c->patches_count; i++) {
        PatchRequest* p = &c->patches[i];
        int32_t addr = c->quotations[p->quot_idx].address;
        // The offset where OP_PUSH was emitted is p->offset
        // But wait! We need to know WHICH quotation emitted it.
        // It's the parent quotation! Wait, we don't track parent in PatchRequest.
        // For now let's assume it was emitted in main bytecode
        if (p->parent_quot_idx >= 0) {
            c->quotations[p->parent_quot_idx].code[p->offset] = (addr >> 24) & 0xFF;
            c->quotations[p->parent_quot_idx].code[p->offset+1] = (addr >> 16) & 0xFF;
            c->quotations[p->parent_quot_idx].code[p->offset+2] = (addr >> 8) & 0xFF;
            c->quotations[p->parent_quot_idx].code[p->offset+3] = addr & 0xFF;
        } else {
            c->bytecode[p->offset] = (addr >> 24) & 0xFF;
            c->bytecode[p->offset+1] = (addr >> 16) & 0xFF;
            c->bytecode[p->offset+2] = (addr >> 8) & 0xFF;
            c->bytecode[p->offset+3] = addr & 0xFF;
        }
    }
    
    // Append quotations
    for (size_t i = 0; i < c->quot_count; i++) {
        Quotation* q = &c->quotations[i];
        
        // Patch internal jumps
        for (size_t j = 0; j < q->jumps_count; j++) {
            InternalJump* ij = &q->jumps[j];
            int32_t target = q->address + ij->target_offset;
            q->code[ij->placeholder_at] = (target >> 24) & 0xFF;
            q->code[ij->placeholder_at+1] = (target >> 16) & 0xFF;
            q->code[ij->placeholder_at+2] = (target >> 8) & 0xFF;
            q->code[ij->placeholder_at+3] = target & 0xFF;
        }
        
        // Append code
        for (size_t j = 0; j < q->code_len; j++) {
            emit_byte(c, q->code[j]);
        }
    }
    
    // Append string literals
    for (size_t i = 0; i < c->string_patches_count; i++) {
        StringPatch* p = &c->string_patches[i];
        size_t slen = strlen(p->str);
        for (size_t j = 0; j < slen; j++) emit_byte(c, p->str[j]);
        emit_byte(c, 0); // null term
    }
    
    warn_duplicate_addr_consts(c);

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
