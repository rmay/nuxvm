// pkg/lux/lexer.go - Enhanced with quotation support
package lux

import (
	"fmt"
	"os"
	"strconv"
	"strings"
	"unicode"
)

// TokenType represents different kinds of tokens
type TokenType int

const (
	TokenNumber    TokenType = iota // 42, -17, 0xFF
	TokenWord                       // +, DUP, square, MATH::SQUARE
	TokenAtSign                     // @
	TokenSemicolon                  // ;
	TokenComment                    // ( ... )
	TokenString                     // "chars"
	TokenLBracket                   // [ - start quotation
	TokenRBracket                   // ] - end quotation
	TokenEOF                        // End of file
)

// Token represents a single lexical element
type Token struct {
	Type   TokenType
	Value  string // The actual text
	Line   int    // For error messages
	Column int
}

// Lexer breaks source code into tokens
type Lexer struct {
	input  string
	pos    int // Current position in input
	line   int
	column int
	trace  bool // Trace compilation steps, defaults to false
}

// NewLexer creates a new lexer
func NewLexer(input string, trace ...bool) *Lexer {
	traceEnabled := false
	if len(trace) > 0 {
		traceEnabled = trace[0]
	}

	return &Lexer{
		input:  input,
		pos:    0,
		line:   1,
		column: 1,
		trace:  traceEnabled,
	}
}

// Tokenize returns all tokens from the source
func (l *Lexer) Tokenize() ([]Token, error) {
	var tokens []Token

	for {
		token, err := l.NextToken()
		if err != nil {
			return nil, err
		}

		// Skip comments, but keep everything else
		if token.Type != TokenComment {
			tokens = append(tokens, token)
		}

		if token.Type == TokenEOF {
			break
		}
	}

	return tokens, nil
}

// NextToken reads and returns the next token
func (l *Lexer) NextToken() (Token, error) {
	l.skipWhitespace()
	if l.trace {
		fmt.Fprintf(os.Stderr, "Lexer: NextToken: pos=%d, line=%d, column=%d\n", l.pos, l.line, l.column)
	}

	if l.pos >= len(l.input) {
		if l.trace {
			fmt.Fprintf(os.Stderr, "Lexer: NextToken: Reached EOF\n")
		}
		return Token{Type: TokenEOF, Line: l.line, Column: l.column}, nil
	}

	ch := l.peek()
	if l.trace {
		fmt.Fprintf(os.Stderr, "Lexer: NextToken: Processing char='%c'\n", ch)
	}

	switch {
	case ch == '(':
		if l.trace {
			fmt.Fprintf(os.Stderr, "Lexer: NextToken: Reading comment\n")
		}
		return l.readComment()
	case ch == '/' && l.pos+1 < len(l.input) && l.input[l.pos+1] == '/':
		if l.trace {
			fmt.Fprintf(os.Stderr, "Lexer: NextToken: Reading line comment\n")
		}
		return l.readLineComment()
	case ch == '"':
		if l.trace {
			fmt.Fprintf(os.Stderr, "Lexer: NextToken: Reading string\n")
		}
		return l.readString()
	case ch == '@':
		if l.trace {
			fmt.Fprintf(os.Stderr, "Lexer: NextToken: Reading @\n")
		}
		return l.readSingleChar(TokenAtSign), nil
	case ch == ';':
		if l.trace {
			fmt.Fprintf(os.Stderr, "Lexer: NextToken: Reading ;\n")
		}
		return l.readSingleChar(TokenSemicolon), nil
	case ch == '[':
		if l.trace {
			fmt.Fprintf(os.Stderr, "Lexer: NextToken: Reading [\n")
		}
		return l.readSingleChar(TokenLBracket), nil
	case ch == ']':
		if l.trace {
			fmt.Fprintf(os.Stderr, "Lexer: NextToken: Reading ]\n")
		}
		return l.readSingleChar(TokenRBracket), nil
	case l.isNumberStart(ch):
		if l.trace {
			fmt.Fprintf(os.Stderr, "Lexer: NextToken: Reading number\n")
		}
		return l.readNumber(), nil
	case ch == '?' && l.pos+1 < len(l.input) && l.input[l.pos+1] == ':':
		if l.trace {
			fmt.Fprintf(os.Stderr, "Lexer: NextToken: Reading ?: combinator\n")
		}
		token := Token{Type: TokenWord, Value: "?:", Line: l.line, Column: l.column}
		l.pos += 2
		l.column += 2
		return token, nil
	case ch == '!' && l.pos+1 < len(l.input) && l.input[l.pos+1] == ':':
		if l.trace {
			fmt.Fprintf(os.Stderr, "Lexer: NextToken: Reading !: combinator\n")
		}
		token := Token{Type: TokenWord, Value: "!:", Line: l.line, Column: l.column}
		l.pos += 2
		l.column += 2
		return token, nil
	case ch == '|' && l.pos+1 < len(l.input) && l.input[l.pos+1] == ':':
		if l.trace {
			fmt.Fprintf(os.Stderr, "Lexer: NextToken: Reading |: combinator\n")
		}
		token := Token{Type: TokenWord, Value: "|:", Line: l.line, Column: l.column}
		l.pos += 2
		l.column += 2
		return token, nil
	case ch == '#' && l.pos+1 < len(l.input) && l.input[l.pos+1] == ':':
		if l.trace {
			fmt.Fprintf(os.Stderr, "Lexer: NextToken: Reading #: combinator\n")
		}
		token := Token{Type: TokenWord, Value: "#:", Line: l.line, Column: l.column}
		l.pos += 2
		l.column += 2
		return token, nil
	default:
		if l.trace {
			fmt.Fprintf(os.Stderr, "Lexer: NextToken: Reading word\n")
		}
		return l.readWord()
	}
}

// peek returns current character without advancing
func (l *Lexer) peek() byte {
	if l.pos >= len(l.input) {
		return 0
	}
	return l.input[l.pos]
}

// advance moves to next character and returns it
func (l *Lexer) advance() byte {
	if l.pos >= len(l.input) {
		return 0
	}
	ch := l.input[l.pos]
	l.pos++
	if ch == '\n' {
		l.line++
		l.column = 1
	} else {
		l.column++
	}
	return ch
}

// skipWhitespace skips spaces, tabs, newlines
func (l *Lexer) skipWhitespace() {
	for l.pos < len(l.input) && unicode.IsSpace(rune(l.peek())) {
		l.advance()
	}
}

// readString reads a string literal
func (l *Lexer) readString() (Token, error) {
	startLine := l.line
	startCol := l.column
	l.advance() // skip opening "

	var str strings.Builder

	for l.pos < len(l.input) {
		ch := l.peek()
		if ch == '"' {
			l.advance() // skip closing "
			return Token{
				Type:   TokenString,
				Value:  str.String(),
				Line:   startLine,
				Column: startCol,
			}, nil
		}
		if ch == '\\' {
			l.advance()
			if l.pos >= len(l.input) {
				return Token{}, fmt.Errorf("unexpected end of string at line %d", startLine)
			}
			next := l.advance()
			switch next {
			case 'n':
				str.WriteByte('\n')
			case 't':
				str.WriteByte('\t')
			case '\\':
				str.WriteByte('\\')
			case '"':
				str.WriteByte('"')
			default:
				str.WriteByte(next)
			}
		} else {
			str.WriteByte(l.advance())
		}
	}

	return Token{}, fmt.Errorf("unclosed string at line %d, column %d", startLine, startCol)
}

// readSingleChar reads a single character token
func (l *Lexer) readSingleChar(tokenType TokenType) Token {
	token := Token{
		Type:   tokenType,
		Value:  string(l.peek()),
		Line:   l.line,
		Column: l.column,
	}
	l.advance()
	return token
}

// readComment reads ( ... ) comments
func (l *Lexer) readComment() (Token, error) {
	startLine := l.line
	startCol := l.column
	l.advance() // skip '('

	var comment strings.Builder
	depth := 1 // Support nested comments

	for l.pos < len(l.input) && depth > 0 {
		ch := l.peek()
		if ch == '(' {
			depth++
		} else if ch == ')' {
			depth--
			if depth == 0 {
				l.advance() // skip closing ')'
				break
			}
		}
		comment.WriteByte(l.advance())
	}

	if depth > 0 {
		return Token{}, fmt.Errorf("unclosed comment at line %d, column %d", startLine, startCol)
	}

	return Token{
		Type:   TokenComment,
		Value:  comment.String(),
		Line:   startLine,
		Column: startCol,
	}, nil
}

// readLineComment reads comments starting with //
func (l *Lexer) readLineComment() (Token, error) {
	startLine := l.line
	startCol := l.column

	l.advance() // Skip first /
	l.advance() // Skip second /

	var comment strings.Builder

	for l.pos < len(l.input) && l.peek() != '\n' {
		comment.WriteByte(l.advance())
	}

	return Token{
		Type:   TokenComment,
		Value:  comment.String(),
		Line:   startLine,
		Column: startCol,
	}, nil
}

// readNumber reads numeric literals (decimal or hex)
func (l *Lexer) readNumber() Token {
	startLine := l.line
	startCol := l.column
	var num strings.Builder

	// Handle negative sign
	if l.peek() == '-' {
		num.WriteByte(l.advance())
	}

	// Check for hexadecimal (0x or 0X)
	if l.peek() == '0' && l.pos+1 < len(l.input) {
		next := l.input[l.pos+1]
		if next == 'x' || next == 'X' {
			num.WriteByte(l.advance()) // 0
			num.WriteByte(l.advance()) // x
			for l.pos < len(l.input) && isHexDigit(l.peek()) {
				num.WriteByte(l.advance())
			}
			return Token{
				Type:   TokenNumber,
				Value:  num.String(),
				Line:   startLine,
				Column: startCol,
			}
		}
	}

	// Read decimal digits
	for l.pos < len(l.input) && unicode.IsDigit(rune(l.peek())) {
		num.WriteByte(l.advance())
	}

	return Token{
		Type:   TokenNumber,
		Value:  num.String(),
		Line:   startLine,
		Column: startCol,
	}
}

// readWord reads a word (identifier)
func (l *Lexer) readWord() (Token, error) {
	startLine := l.line
	startCol := l.column
	var word strings.Builder

	for l.pos < len(l.input) {
		ch := l.peek()

		// Stop at whitespace, brackets, or special characters
		if unicode.IsSpace(rune(ch)) || ch == '(' || ch == ')' ||
			ch == ';' || ch == '@' || ch == '"' || ch == '[' || ch == ']' {
			break
		}

		// Allow single colon in words (e.g., for ?:, |:, !:)
		if ch == ':' && l.pos > startCol {
			word.WriteByte(l.advance())
			continue
		}

		// Special handling for :: in module names
		if ch == ':' && l.pos+1 < len(l.input) && l.input[l.pos+1] == ':' {
			word.WriteByte(l.advance()) // First :
			word.WriteByte(l.advance()) // Second :
			continue
		}

		// Allow letters, digits, underscores, and certain symbols
		if unicode.IsLetter(rune(ch)) || unicode.IsDigit(rune(ch)) || ch == '_' ||
			ch == '+' || ch == '-' || ch == '*' || ch == '/' || ch == '%' ||
			ch == '&' || ch == '|' || ch == '^' || ch == '!' || ch == '?' || ch == '>' ||
			ch == '<' || ch == '.' || ch == '=' {
			word.WriteByte(l.advance())
		} else {
			break
		}
	}

	value := word.String()
	if value == "" {
		return Token{}, fmt.Errorf("empty word at line %d, column %d", startLine, startCol)
	}
	if l.trace {
		fmt.Fprintf(os.Stderr, "Lexer: readWord: Produced token={Type:%v, Value:%s, Line:%d, Column:%d}\n", TokenWord, value, startLine, startCol)
	}
	return Token{
		Type:   TokenWord,
		Value:  value,
		Line:   startLine,
		Column: startCol,
	}, nil
}

// isNumberStart checks if character can start a number
func (l *Lexer) isNumberStart(ch byte) bool {
	if unicode.IsDigit(rune(ch)) {
		return true
	}
	if ch == '-' && l.pos+1 < len(l.input) && unicode.IsDigit(rune(l.input[l.pos+1])) {
		return true
	}
	return false
}

// isHexDigit checks if character is valid in hexadecimal
func isHexDigit(ch byte) bool {
	return unicode.IsDigit(rune(ch)) || (ch >= 'a' && ch <= 'f') || (ch >= 'A' && ch <= 'F')
}

// ParseNumber converts a number token to int32
func ParseNumber(token Token) (int32, error) {
	if token.Type != TokenNumber {
		return 0, fmt.Errorf("expected number token")
	}

	// Handle hexadecimal
	if strings.HasPrefix(token.Value, "0x") || strings.HasPrefix(token.Value, "0X") {
		val, err := strconv.ParseInt(token.Value[2:], 16, 32)
		if err != nil {
			return 0, fmt.Errorf("invalid hex number '%s' at line %d: %v",
				token.Value, token.Line, err)
		}
		return int32(val), nil
	}

	// Handle decimal
	val, err := strconv.ParseInt(token.Value, 10, 32)
	if err != nil {
		return 0, fmt.Errorf("invalid number '%s' at line %d: %v",
			token.Value, token.Line, err)
	}
	return int32(val), nil
}
