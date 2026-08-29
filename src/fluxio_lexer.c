#include "fluxio_token.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

static FxTokenList* list_create(void) {
    FxTokenList* list = malloc(sizeof(FxTokenList));
    list->capacity = 64;
    list->count = 0;
    list->tokens = malloc(sizeof(FxToken) * list->capacity);
    return list;
}

static void list_push(FxTokenList* list, FxToken tok) {
    if (list->count == list->capacity) {
        list->capacity *= 2;
        list->tokens = realloc(list->tokens, sizeof(FxToken) * list->capacity);
    }
    list->tokens[list->count++] = tok;
}

void fx_token_list_free(FxTokenList* list) {
    if (!list) return;
    for (size_t i = 0; i < list->count; i++) {
        free(list->tokens[i].value);
        free(list->tokens[i].doc_comment);
    }
    free(list->tokens);
    free(list);
}

const char* fx_token_type_name(FxTokenType type) {
    switch (type) {
        case FXTOK_INT_LIT: return "INT_LIT";
        case FXTOK_IDENT: return "IDENT";
        case FXTOK_STRING_LIT: return "STRING_LIT";
        case FXTOK_KW_INT: return "int";
        case FXTOK_KW_IF: return "if";
        case FXTOK_KW_ELSE: return "else";
        case FXTOK_KW_WHILE: return "while";
        case FXTOK_KW_FOR: return "for";
        case FXTOK_KW_RETURN: return "return";
        case FXTOK_KW_RECURSIVE: return "recursive";
        case FXTOK_KW_INCLUDE: return "include";
        case FXTOK_KW_STRUCT: return "struct";
        case FXTOK_KW_EXTERN: return "extern";
        case FXTOK_KW_VOID: return "void";
        case FXTOK_KW_BYTE: return "byte";
        case FXTOK_KW_VERSION: return "version";
        case FXTOK_LPAREN: return "(";
        case FXTOK_RPAREN: return ")";
        case FXTOK_LBRACE: return "{";
        case FXTOK_RBRACE: return "}";
        case FXTOK_LBRACKET: return "[";
        case FXTOK_RBRACKET: return "]";
        case FXTOK_SEMI: return ";";
        case FXTOK_COMMA: return ",";
        case FXTOK_DOT: return ".";
        case FXTOK_ASSIGN: return "=";
        case FXTOK_PLUS: return "+";
        case FXTOK_MINUS: return "-";
        case FXTOK_STAR: return "*";
        case FXTOK_SLASH: return "/";
        case FXTOK_PERCENT: return "%";
        case FXTOK_AMP: return "&";
        case FXTOK_PIPE: return "|";
        case FXTOK_CARET: return "^";
        case FXTOK_TILDE: return "~";
        case FXTOK_BANG: return "!";
        case FXTOK_AMPAMP: return "&&";
        case FXTOK_PIPEPIPE: return "||";
        case FXTOK_SHL: return "<<";
        case FXTOK_SHR: return ">>";
        case FXTOK_EQ: return "==";
        case FXTOK_NEQ: return "!=";
        case FXTOK_LT: return "<";
        case FXTOK_LTE: return "<=";
        case FXTOK_GT: return ">";
        case FXTOK_GTE: return ">=";
        case FXTOK_EOF: return "EOF";
    }
    return "?";
}

typedef struct {
    const char* src;
    size_t pos;
    size_t len;
    int line;
    int column;

    /* pending doc comment carried forward to the next real token */
    char* pending_doc;
} Lexer;

static char peek(Lexer* lx) { return lx->pos < lx->len ? lx->src[lx->pos] : '\0'; }
static char peek2(Lexer* lx) { return lx->pos + 1 < lx->len ? lx->src[lx->pos + 1] : '\0'; }

static char advance(Lexer* lx) {
    char c = lx->src[lx->pos++];
    if (c == '\n') { lx->line++; lx->column = 1; }
    else { lx->column++; }
    return c;
}

static FxToken make_tok(FxTokenType type, int line, int col) {
    FxToken t;
    t.type = type;
    t.value = NULL;
    t.int_value = 0;
    t.str_len = 0;
    t.line = line;
    t.column = col;
    t.has_doc_comment = false;
    t.doc_comment = NULL;
    return t;
}

/* Consumes whitespace and comments. Tracks whether a doc-style block
 * comment was the most recent non-whitespace thing seen, so it can be
 * attached to the next emitted token as a doc comment. Any other
 * intervening token (including a line comment or plain block comment)
 * clears it. */
static void skip_trivia(Lexer* lx) {
    for (;;) {
        char c = peek(lx);
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
            advance(lx);
            continue;
        }
        if (c == '/' && peek2(lx) == '/') {
            while (peek(lx) != '\0' && peek(lx) != '\n') advance(lx);
            free(lx->pending_doc);
            lx->pending_doc = NULL;
            continue;
        }
        if (c == '/' && peek2(lx) == '*') {
            bool is_doc = (lx->pos + 2 < lx->len && lx->src[lx->pos + 2] == '*');
            size_t start = lx->pos;
            advance(lx); advance(lx); /* consume opening slash-star */
            while (!(peek(lx) == '*' && peek2(lx) == '/')) {
                if (peek(lx) == '\0') {
                    fprintf(stderr, "fluxio: unterminated block comment at line %d\n", lx->line);
                    break;
                }
                advance(lx);
            }
            size_t end = lx->pos; /* excludes closing */
            if (peek(lx) == '*') { advance(lx); advance(lx); } /* consume */
            free(lx->pending_doc);
            if (is_doc) {
                size_t n = end - start;
                lx->pending_doc = malloc(n + 1);
                memcpy(lx->pending_doc, lx->src + start, n);
                lx->pending_doc[n] = '\0';
            } else {
                lx->pending_doc = NULL;
            }
            continue;
        }
        break;
    }
}

static bool is_ident_start(char c) { return isalpha((unsigned char)c) || c == '_'; }
static bool is_ident_char(char c) { return isalnum((unsigned char)c) || c == '_'; }

static FxTokenType keyword_type(const char* s) {
    if (strcmp(s, "int") == 0) return FXTOK_KW_INT;
    if (strcmp(s, "if") == 0) return FXTOK_KW_IF;
    if (strcmp(s, "else") == 0) return FXTOK_KW_ELSE;
    if (strcmp(s, "while") == 0) return FXTOK_KW_WHILE;
    if (strcmp(s, "for") == 0) return FXTOK_KW_FOR;
    if (strcmp(s, "return") == 0) return FXTOK_KW_RETURN;
    if (strcmp(s, "recursive") == 0) return FXTOK_KW_RECURSIVE;
    if (strcmp(s, "include") == 0) return FXTOK_KW_INCLUDE;
    if (strcmp(s, "struct") == 0) return FXTOK_KW_STRUCT;
    if (strcmp(s, "extern") == 0) return FXTOK_KW_EXTERN;
    if (strcmp(s, "void") == 0) return FXTOK_KW_VOID;
    if (strcmp(s, "byte") == 0) return FXTOK_KW_BYTE;
    if (strcmp(s, "version") == 0) return FXTOK_KW_VERSION;
    return FXTOK_IDENT;
}

FxTokenList* fx_tokenize(const char* input) {
    Lexer lx = { .src = input, .pos = 0, .len = strlen(input), .line = 1, .column = 1, .pending_doc = NULL };
    FxTokenList* list = list_create();
    bool had_error = false;

    for (;;) {
        skip_trivia(&lx);
        int line = lx.line, col = lx.column;
        char c = peek(&lx);
        if (c == '\0') {
            FxToken t = make_tok(FXTOK_EOF, line, col);
            list_push(list, t);
            break;
        }

        FxToken t;
        bool emitted = true;

        if (isdigit((unsigned char)c)) {
            size_t start = lx.pos;
            if (c == '0' && (peek2(&lx) == 'x' || peek2(&lx) == 'X')) {
                advance(&lx); advance(&lx);
                while (isxdigit((unsigned char)peek(&lx))) advance(&lx);
            } else {
                while (isdigit((unsigned char)peek(&lx))) advance(&lx);
            }
            size_t n = lx.pos - start;
            char* text = malloc(n + 1);
            memcpy(text, input + start, n);
            text[n] = '\0';
            int32_t val;
            if (n > 1 && text[1] == 'x') {
                val = (int32_t) strtoul(text + 2, NULL, 16);
            } else {
                val = (int32_t) strtol(text, NULL, 10);
            }
            t = make_tok(FXTOK_INT_LIT, line, col);
            t.value = text;
            t.int_value = val;
        } else if (is_ident_start(c)) {
            size_t start = lx.pos;
            while (is_ident_char(peek(&lx))) advance(&lx);
            size_t n = lx.pos - start;
            char* text = malloc(n + 1);
            memcpy(text, input + start, n);
            text[n] = '\0';
            FxTokenType kt = keyword_type(text);
            t = make_tok(kt, line, col);
            if (kt == FXTOK_IDENT) t.value = text;
            else free(text);
        } else if (c == '"') {
            advance(&lx); /* opening quote */
            size_t cap = 32, len = 0;
            char* buf = malloc(cap);
            bool terminated = false;
            while (peek(&lx) != '\0') {
                char ch = peek(&lx);
                if (ch == '"') { advance(&lx); terminated = true; break; }
                if (ch == '\n') break; /* unterminated: no multi-line string literals */
                if (ch == '\\') {
                    advance(&lx);
                    char esc = peek(&lx);
                    char decoded;
                    switch (esc) {
                        case 'n': decoded = '\n'; break;
                        case 't': decoded = '\t'; break;
                        case 'r': decoded = '\r'; break;
                        case '0': decoded = '\0'; break;
                        case '\\': decoded = '\\'; break;
                        case '"': decoded = '"'; break;
                        default:
                            fprintf(stderr, "fluxio: unknown escape sequence '\\%c' at line %d\n", esc, lx.line);
                            free(buf);
                            free(lx.pending_doc);
                            fx_token_list_free(list);
                            return NULL;
                    }
                    advance(&lx);
                    if (len == cap) { cap *= 2; buf = realloc(buf, cap); }
                    buf[len++] = decoded;
                } else {
                    advance(&lx);
                    if (len == cap) { cap *= 2; buf = realloc(buf, cap); }
                    buf[len++] = ch;
                }
            }
            if (!terminated) {
                fprintf(stderr, "fluxio: unterminated string literal at line %d\n", line);
                free(buf);
                free(lx.pending_doc);
                fx_token_list_free(list);
                return NULL;
            }
            buf = realloc(buf, len + 1);
            buf[len] = '\0';
            t = make_tok(FXTOK_STRING_LIT, line, col);
            t.value = buf;
            t.str_len = (int32_t) len;
        } else {
            switch (c) {
                case '(': advance(&lx); t = make_tok(FXTOK_LPAREN, line, col); break;
                case ')': advance(&lx); t = make_tok(FXTOK_RPAREN, line, col); break;
                case '{': advance(&lx); t = make_tok(FXTOK_LBRACE, line, col); break;
                case '}': advance(&lx); t = make_tok(FXTOK_RBRACE, line, col); break;
                case '[': advance(&lx); t = make_tok(FXTOK_LBRACKET, line, col); break;
                case ']': advance(&lx); t = make_tok(FXTOK_RBRACKET, line, col); break;
                case ';': advance(&lx); t = make_tok(FXTOK_SEMI, line, col); break;
                case ',': advance(&lx); t = make_tok(FXTOK_COMMA, line, col); break;
                case '.': advance(&lx); t = make_tok(FXTOK_DOT, line, col); break;
                case '+': advance(&lx); t = make_tok(FXTOK_PLUS, line, col); break;
                case '-': advance(&lx); t = make_tok(FXTOK_MINUS, line, col); break;
                case '*': advance(&lx); t = make_tok(FXTOK_STAR, line, col); break;
                case '/': advance(&lx); t = make_tok(FXTOK_SLASH, line, col); break;
                case '%': advance(&lx); t = make_tok(FXTOK_PERCENT, line, col); break;
                case '~': advance(&lx); t = make_tok(FXTOK_TILDE, line, col); break;
                case '^': advance(&lx); t = make_tok(FXTOK_CARET, line, col); break;
                case '=':
                    advance(&lx);
                    if (peek(&lx) == '=') { advance(&lx); t = make_tok(FXTOK_EQ, line, col); }
                    else t = make_tok(FXTOK_ASSIGN, line, col);
                    break;
                case '!':
                    advance(&lx);
                    if (peek(&lx) == '=') { advance(&lx); t = make_tok(FXTOK_NEQ, line, col); }
                    else t = make_tok(FXTOK_BANG, line, col);
                    break;
                case '&':
                    advance(&lx);
                    if (peek(&lx) == '&') { advance(&lx); t = make_tok(FXTOK_AMPAMP, line, col); }
                    else t = make_tok(FXTOK_AMP, line, col);
                    break;
                case '|':
                    advance(&lx);
                    if (peek(&lx) == '|') { advance(&lx); t = make_tok(FXTOK_PIPEPIPE, line, col); }
                    else t = make_tok(FXTOK_PIPE, line, col);
                    break;
                case '<':
                    advance(&lx);
                    if (peek(&lx) == '=') { advance(&lx); t = make_tok(FXTOK_LTE, line, col); }
                    else if (peek(&lx) == '<') { advance(&lx); t = make_tok(FXTOK_SHL, line, col); }
                    else t = make_tok(FXTOK_LT, line, col);
                    break;
                case '>':
                    advance(&lx);
                    if (peek(&lx) == '=') { advance(&lx); t = make_tok(FXTOK_GTE, line, col); }
                    else if (peek(&lx) == '>') { advance(&lx); t = make_tok(FXTOK_SHR, line, col); }
                    else t = make_tok(FXTOK_GT, line, col);
                    break;
                default:
                    fprintf(stderr, "fluxio: unexpected character '%c' at line %d, column %d\n", c, line, col);
                    advance(&lx);
                    had_error = true;
                    emitted = false;
                    break;
            }
        }

        if (emitted) {
            if (lx.pending_doc) {
                t.has_doc_comment = true;
                t.doc_comment = lx.pending_doc;
                lx.pending_doc = NULL;
            }
            list_push(list, t);
        }
    }

    free(lx.pending_doc);

    if (had_error) {
        fx_token_list_free(list);
        return NULL;
    }
    return list;
}
