#include "lexer.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

typedef struct {
    const char* input;
    int pos;
    int line;
    int column;
} LexerState;

static void token_list_append(TokenList* list, Token token) {
    if (list->count >= list->capacity) {
        list->capacity = list->capacity == 0 ? 16 : list->capacity * 2;
        list->tokens = (Token*)realloc(list->tokens, list->capacity * sizeof(Token));
    }
    list->tokens[list->count++] = token;
}

void token_list_free(TokenList* list) {
    if (list) {
        for (size_t i = 0; i < list->count; i++) {
            if (list->tokens[i].value) {
                free(list->tokens[i].value);
            }
        }
        if (list->tokens) free(list->tokens);
        free(list);
    }
}

static char peek(LexerState* l) {
    return l->input[l->pos];
}

static char advance(LexerState* l) {
    char ch = l->input[l->pos];
    if (ch == '\0') return '\0';
    l->pos++;
    if (ch == '\n') {
        l->line++;
        l->column = 1;
    } else {
        l->column++;
    }
    return ch;
}

static void skip_whitespace(LexerState* l) {
    while (isspace((unsigned char)peek(l))) {
        advance(l);
    }
}

static bool is_hex_digit(char ch) {
    return isdigit((unsigned char)ch) || (ch >= 'a' && ch <= 'f') || (ch >= 'A' && ch <= 'F');
}

static bool is_number_start(LexerState* l, char ch) {
    if (isdigit((unsigned char)ch)) return true;
    if (ch == '-' && isdigit((unsigned char)l->input[l->pos + 1])) return true;
    return false;
}

TokenList* tokenize(const char* input) {
    TokenList* list = (TokenList*)calloc(1, sizeof(TokenList));
    LexerState l = { input, 0, 1, 1 };
    
    while (1) {
        skip_whitespace(&l);
        char ch = peek(&l);
        
        if (ch == '\0') {
            Token t = { TOKEN_EOF, NULL, l.line, l.column };
            token_list_append(list, t);
            break;
        }
        
        int start_line = l.line;
        int start_col = l.column;
        
        if (ch == '(') {
            // Comment ( ... )
            advance(&l);
            int depth = 1;
            while (peek(&l) != '\0' && depth > 0) {
                char c = advance(&l);
                if (c == '(') depth++;
                else if (c == ')') depth--;
            }
            continue;
        }
        
        if (ch == '/' && l.input[l.pos+1] == '/') {
            // Line comment
            advance(&l); advance(&l);
            while (peek(&l) != '\0' && peek(&l) != '\n') {
                advance(&l);
            }
            continue;
        }
        
        if (ch == '"' || (ch == 'T' && l.input[l.pos+1] == '"')) {
            if (ch == 'T') advance(&l); // skip T
            advance(&l); // skip "
            
            char buffer[4096];
            int bidx = 0;
            
            while (peek(&l) != '\0') {
                char c = peek(&l);
                if (c == '"') {
                    advance(&l);
                    break;
                }
                if (c == '\\') {
                    advance(&l);
                    char next = advance(&l);
                    if (next == 'n') buffer[bidx++] = '\n';
                    else if (next == 't') buffer[bidx++] = '\t';
                    else if (next == '\\') buffer[bidx++] = '\\';
                    else if (next == '"') buffer[bidx++] = '"';
                    else buffer[bidx++] = next;
                } else {
                    buffer[bidx++] = advance(&l);
                }
            }
            buffer[bidx] = '\0';
            Token t = { TOKEN_STRING, strdup(buffer), start_line, start_col };
            token_list_append(list, t);
            continue;
        }
        
        if (ch == '@') {
            advance(&l);
            Token t = { TOKEN_AT_SIGN, strdup("@"), start_line, start_col };
            token_list_append(list, t);
            continue;
        }
        
        if (ch == '$') {
            advance(&l);
            Token t = { TOKEN_DOLLAR, strdup("$"), start_line, start_col };
            token_list_append(list, t);
            continue;
        }
        
        if (ch == ';') {
            advance(&l);
            Token t = { TOKEN_SEMICOLON, strdup(";"), start_line, start_col };
            token_list_append(list, t);
            continue;
        }
        
        if (ch == '[') {
            advance(&l);
            Token t = { TOKEN_LBRACKET, strdup("["), start_line, start_col };
            token_list_append(list, t);
            continue;
        }
        
        if (ch == ']') {
            advance(&l);
            Token t = { TOKEN_RBRACKET, strdup("]"), start_line, start_col };
            token_list_append(list, t);
            continue;
        }
        
        if (is_number_start(&l, ch)) {
            char buffer[64];
            int bidx = 0;
            if (ch == '-') {
                buffer[bidx++] = advance(&l);
            }
            if (peek(&l) == '0' && (l.input[l.pos+1] == 'x' || l.input[l.pos+1] == 'X')) {
                buffer[bidx++] = advance(&l);
                buffer[bidx++] = advance(&l);
                while (is_hex_digit(peek(&l))) {
                    buffer[bidx++] = advance(&l);
                }
            } else {
                while (isdigit((unsigned char)peek(&l))) {
                    buffer[bidx++] = advance(&l);
                }
            }
            buffer[bidx] = '\0';
            Token t = { TOKEN_NUMBER, strdup(buffer), start_line, start_col };
            token_list_append(list, t);
            continue;
        }
        
        // Word or combinator
        char buffer[256];
        int bidx = 0;
        
        if ((ch == '?' || ch == '!' || ch == '|' || ch == '#') && l.input[l.pos+1] == ':') {
            buffer[bidx++] = advance(&l);
            buffer[bidx++] = advance(&l);
            buffer[bidx] = '\0';
            Token t = { TOKEN_WORD, strdup(buffer), start_line, start_col };
            token_list_append(list, t);
            continue;
        }
        
        while (peek(&l) != '\0') {
            char c = peek(&l);
            if (isspace((unsigned char)c) || c == '(' || c == ')' || c == ';' || c == '"' || c == '[' || c == ']') {
                break;
            }
            if (c == ':' && l.input[l.pos+1] == ':') {
                buffer[bidx++] = advance(&l);
                buffer[bidx++] = advance(&l);
                continue;
            }
            if (isalnum((unsigned char)c) || c == '_' || c == '+' || c == '-' || c == '*' || c == '/' || c == '%' ||
                c == '&' || c == '|' || c == '^' || c == '!' || c == '?' || c == '>' || c == '<' || c == '.' || c == '=' || c == '@' || c == ':') {
                buffer[bidx++] = advance(&l);
            } else {
                break;
            }
        }
        buffer[bidx] = '\0';
        if (bidx > 0) {
            Token t = { TOKEN_WORD, strdup(buffer), start_line, start_col };
            token_list_append(list, t);
        } else {
            advance(&l); // skip unknown
        }
    }
    
    return list;
}

bool parse_number(const Token* token, int32_t* out_val) {
    if (token->type != TOKEN_NUMBER || !token->value) return false;
    
    if (token->value[0] == '0' && (token->value[1] == 'x' || token->value[1] == 'X')) {
        *out_val = (int32_t)strtoul(token->value + 2, NULL, 16);
    } else {
        *out_val = (int32_t)strtol(token->value, NULL, 10);
    }
    return true;
}
