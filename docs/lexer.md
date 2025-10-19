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

The lexer recognizes 6 types of tokens:

1. **TokenNumber** - Numbers: `42`, `-17`, `0xFF`
2. **TokenWord** - Identifiers: `+`, `DUP`, `square`
3. **TokenColon** - Start of definition: `:`
4. **TokenSemicolon** - End of definition: `;`
5. **TokenComment** - Comments: `( ... )` (filtered out)
6. **TokenEOF** - End of file

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
Source: ": square dup * ;"

Step 1: Skip whitespace
Step 2: See ':' → emit TokenColon
Step 3: Skip whitespace  
Step 4: Read "square" → emit TokenWord("square")
Step 5: Skip whitespace
Step 6: Read "dup" → emit TokenWord("dup")
Step 7: Skip whitespace
Step 8: Read "*" → emit TokenWord("*")
Step 9: Skip whitespace
Step 10: See ';' → emit TokenSemicolon
Step 11: End of input → emit TokenEOF
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