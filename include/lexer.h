#ifndef LEXER_H
#define LEXER_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

typedef enum {
    TOKEN_NUMBER = 0,
    TOKEN_WORD,
    TOKEN_AT_SIGN,
    TOKEN_SEMICOLON,
    TOKEN_COMMENT,
    TOKEN_STRING,
    TOKEN_LBRACKET,
    TOKEN_RBRACKET,
    TOKEN_DOLLAR,
    TOKEN_EOF
} TokenType;

typedef struct {
    TokenType type;
    char* value;
    int line;
    int column;
} Token;

typedef struct {
    Token* tokens;
    size_t count;
    size_t capacity;
} TokenList;

// Tokenize a null-terminated input string.
// Returns a dynamically allocated TokenList on success, or NULL on error.
TokenList* tokenize(const char* input);

// Free a token list and its token values.
void token_list_free(TokenList* list);

// Parse a number token into int32_t
bool parse_number(const Token* token, int32_t* out_val);

#endif // LEXER_H
