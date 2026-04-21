# Understanding lexer.go

## What is a Lexer?

The lexer (also called tokenizer or scanner) is the **first step** in compiling LUX code. It breaks raw text into meaningful chunks called "tokens".

**Example:**
```
Input:  "5 10 + ."
Output: [Number(5), Number(10), Word(+), Word(.), EOF]
```

## How to Use It

```go
package main

import "your-project/pkg/lux"

func main() {
    source := "5 10 + ."
    
    lexer := lux.NewLexer(source)
    tokens, err := lexer.Tokenize()
    
    if err != nil {
        panic(err)
    }
    
    for _, token := range tokens {
        fmt.Printf("%v: %s\n", token.Type, token.Value)
    }
}
```

## Token Types

The lexer recognizes several types of tokens:

1. **TokenNumber** - Numbers: `42`, `-17`, `0xFF`
2. **TokenWord** - Identifiers and Combinators: `+`, `DUP`, `?:`, `|:`, `#:`
3. **TokenAtSign** - Start of definition: `@`
4. **TokenSemicolon** - End of definition: `;`
5. **TokenComment** - Comments: `( ... )` or `// ...` (filtered out)
6. **TokenString** - Quoted strings: `"Hello"`
7. **TokenLBracket / TokenRBracket** - Quotations: `[` and `]`
8. **TokenEOF** - End of file

## Key Functions

### Main API
```go
lexer := NewLexer(source)        // Create lexer
tokens, err := lexer.Tokenize()  // Get all tokens
```

### Helper Function
```go
value, err := ParseNumber(token)  // Convert number token to int32
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

Save as `lexer_test.go` in the same package and run:

```bash
go test -v
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
```go
if l.isNumberStart(ch) {
    return l.readNumber()
}
```

### Reading a Word
```go
// Read until whitespace or special char
for !unicode.IsSpace(ch) && ch != '(' && ch != ')' {
    word.WriteByte(l.advance())
}
```

### Skipping Whitespace
```go
for unicode.IsSpace(l.peek()) {
    l.advance()
}
```

## Error Handling

The lexer reports:
- Line and column numbers for each token
- Errors for unclosed comments
- Context for invalid numbers

This makes debugging easy later!