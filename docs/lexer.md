# Understanding the Lexer (`src/lexer.c`)

## What is a Lexer?

The lexer (also called tokenizer or scanner) is the **first step** in compiling LUX code. It breaks raw text into meaningful chunks called "tokens".

**Example:**
```
Input:  "5 10 + ."
Output: [Number(5), Number(10), Word(+), Word(.), EOF]
```

## How to Use It

```c
#include <stdio.h>
#include "lexer.h"

int main(void) {
    TokenList* list = tokenize("5 10 + .");
    if (!list) {
        return 1;
    }

    for (size_t i = 0; i < list->count; i++) {
        printf("%d: %s\n", list->tokens[i].type, list->tokens[i].value);
    }

    token_list_free(list);
    return 0;
}
```

## Token Types

The lexer recognizes several types of tokens:

1. **TOKEN_NUMBER** - Numbers: `42`, `-17`, `0xFF`
2. **TOKEN_WORD** - Identifiers and Combinators: `+`, `DUP`, `?:`, `|:`, `#:`
3. **TOKEN_AT_SIGN** - Start of definition: `@`
4. **TOKEN_SEMICOLON** - End of definition: `;`
5. **TOKEN_COMMENT** - Comments: `( ... )` or `// ...` (filtered out)
6. **TOKEN_STRING** - Quoted strings: `"Hello"`
7. **TOKEN_LBRACKET / TOKEN_RBRACKET** - Quotations: `[` and `]`
8. **TOKEN_EOF** - End of file

## Key Functions

### Main API
```c
TokenList* list = tokenize(source);  // Tokenize a C string
token_list_free(list);               // Free tokens and the list
```

### Helper Function
```c
int32_t value;
parse_number(&token, &value);        // Convert a number token to int32
```

## How It Works

```
Source: "@square dup * ;"

Step 1: Skip whitespace
Step 2: See '@' → emit TokenAtSign
Step 3: Read "square" → emit TokenWord("square")
Step 4: Skip whitespace
Step 5: Read "dup" → emit TokenWord("dup")
Step 6: Skip whitespace
Step 7: Read "*" → emit TokenWord("*")
Step 8: Skip whitespace
Step 9: See ';' → emit TokenSemicolon
Step 10: End of input → emit TokenEOF
```

## Testing It

The lexer is covered by `src/test_compiler.c`. From the repo root:

```bash
make test
```

You should see all tests pass.

## What's Next?

After lexing, you need a **compiler** that:
1. Takes the tokens
2. Converts them to NUXVM bytecode
3. Handles word definitions

The lexer's job is done once it produces clean tokens!

## Common Patterns

### Reading a Number
```c
if (is_number_start(ch)) {
    return read_number(l);
}
```

### Reading a Word
```c
/* Read until whitespace or a special character */
while (!isspace(ch) && ch != '(' && ch != ')') {
    word[n++] = advance(l);
}
```

### Skipping Whitespace
```c
while (isspace(peek(l))) {
    advance(l);
}
```

## Error Handling

The lexer reports:
- Line and column numbers for each token
- Errors for unclosed comments
- Context for invalid numbers

This makes debugging easy later. You do want something easier, right?