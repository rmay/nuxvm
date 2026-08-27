#ifndef FLUXIO_TOKEN_H
#define FLUXIO_TOKEN_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

typedef enum {
    FXTOK_INT_LIT = 0,
    FXTOK_IDENT,
    FXTOK_STRING_LIT,

    /* keywords */
    FXTOK_KW_INT,
    FXTOK_KW_IF,
    FXTOK_KW_ELSE,
    FXTOK_KW_WHILE,
    FXTOK_KW_FOR,
    FXTOK_KW_RETURN,
    FXTOK_KW_RECURSIVE,
    FXTOK_KW_INCLUDE,
    FXTOK_KW_STRUCT,
    FXTOK_KW_EXTERN,
    FXTOK_KW_VOID,
    FXTOK_KW_BYTE,

    /* punctuation */
    FXTOK_LPAREN,
    FXTOK_RPAREN,
    FXTOK_LBRACE,
    FXTOK_RBRACE,
    FXTOK_LBRACKET,
    FXTOK_RBRACKET,
    FXTOK_SEMI,
    FXTOK_COMMA,
    FXTOK_DOT,

    /* operators */
    FXTOK_ASSIGN,     /* = */
    FXTOK_PLUS,
    FXTOK_MINUS,
    FXTOK_STAR,
    FXTOK_SLASH,
    FXTOK_PERCENT,
    FXTOK_AMP,        /* & */
    FXTOK_PIPE,       /* | */
    FXTOK_CARET,      /* ^ */
    FXTOK_TILDE,      /* ~ */
    FXTOK_BANG,       /* ! */
    FXTOK_AMPAMP,     /* && */
    FXTOK_PIPEPIPE,   /* || */
    FXTOK_SHL,        /* << */
    FXTOK_SHR,        /* >> */
    FXTOK_EQ,         /* == */
    FXTOK_NEQ,        /* != */
    FXTOK_LT,
    FXTOK_LTE,
    FXTOK_GT,
    FXTOK_GTE,

    FXTOK_EOF
} FxTokenType;

typedef struct {
    FxTokenType type;
    char* value;       /* identifier text, literal text, or decoded string bytes; NULL for punctuation/operators */
    int32_t int_value;  /* valid when type == FXTOK_INT_LIT */
    int32_t str_len;    /* valid when type == FXTOK_STRING_LIT: decoded byte length, excludes NUL */
    int line;
    int column;

    /* Doc-comment tracking: set when a doc-style block comment ended on the
     * line immediately before this token with no other token in between. Only
     * meaningful/checked on tokens that begin a top-level declaration. */
    bool has_doc_comment;
    char* doc_comment;  /* raw doc comment text, or NULL */
} FxToken;

typedef struct {
    FxToken* tokens;
    size_t count;
    size_t capacity;
} FxTokenList;

/* Tokenize a null-terminated input string. Returns a dynamically allocated
 * FxTokenList on success, or NULL on lexical error (message printed to stderr). */
FxTokenList* fx_tokenize(const char* input);

void fx_token_list_free(FxTokenList* list);

const char* fx_token_type_name(FxTokenType type);

#endif /* FLUXIO_TOKEN_H */
