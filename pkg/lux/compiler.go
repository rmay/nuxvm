package lux

import (
	"encoding/binary"
	"fmt"
	"os"
	"strings"

	"github.com/rmay/nuxvm/pkg/vm"
)

// Built-in words map to opcodes
var builtins = map[string]byte{
	// Stack operations
	"DUP":  vm.OpDup,
	"DROP": vm.OpPop,
	"SWAP": vm.OpSwap,
	"ROLL": vm.OpRoll,
	"ROT":  vm.OpRot,
	// Arithmetic
	"+":      vm.OpAdd,
	"-":      vm.OpSub,
	"*":      vm.OpMul,
	"/":      vm.OpDiv,
	"MOD":    vm.OpMod,
	"INC":    vm.OpInc,
	"DEC":    vm.OpDec,
	"NEGATE": vm.OpNeg,
	// Bitwise
	"AND":    vm.OpAnd,
	"OR":     vm.OpOr,
	"XOR":    vm.OpXor,
	"NOT":    vm.OpNot,
	"LSHIFT": vm.OpShl,
	// Comparison
	"=": vm.OpEq,
	"<": vm.OpLt,
	">": vm.OpGt,
	// Control flow
	"EXIT": vm.OpRet,
}

// Control flow combinators
var combinators = map[string]bool{
	"?:":   true,
	"?":    true,
	"!:":   true,
	"|:":   true,
	"#:":   true,
	"CALL": true,
	"DIP":  true,
	"KEEP": true,
}

// Word represents a user-defined word
type Word struct {
	Name    string
	Address int32
	Module  string
}

// Quotation represents a compiled code block
type Quotation struct {
	Address  int32  // Where the quotation code starts
	EndAddr  int32  // Where it ends
	Code     []byte // Compiled bytecode
	TempAddr int32  // Temporary address for patching
}

// Compiler compiles LUX source to bytecode
type Compiler struct {
	tokens        []Token
	pos           int
	bytecode      []byte
	dictionary    map[string]Word
	quotations    []Quotation
	currentModule string
	imports       map[string]string
	baseAddr      int32 // Added for address calculations
	tempAlloc     int32 // Added for temporary memory allocation in reserved area
	trace         bool  // Trace compilation steps, defaults to false
}

// Compile converts LUX source to NUXVM bytecode
func Compile(source string, trace ...bool) ([]byte, error) {
	traceEnabled := false
	if len(trace) > 0 {
		traceEnabled = trace[0]
	}

	lexer := NewLexer(source, traceEnabled)
	tokens, err := lexer.Tokenize()
	if err != nil {
		return nil, err
	}

	compiler := &Compiler{
		tokens:        tokens,
		pos:           0,
		bytecode:      []byte{},
		dictionary:    make(map[string]Word),
		quotations:    []Quotation{},
		currentModule: "",
		imports:       make(map[string]string),
		baseAddr:      4096,
		tempAlloc:     0,
		trace:         traceEnabled,
	}
	return compiler.compile()
}

// compile is the main compilation loop
func (c *Compiler) compile() ([]byte, error) {
	if c.trace {
		fmt.Fprintf(os.Stderr, "compile: Starting, tokens=%v\n", c.tokens)
	}
	jmpAddr := int32(len(c.bytecode))
	if c.trace {
		fmt.Fprintf(os.Stderr, "compile: Emitting initial JMP at offset=%d\n", jmpAddr)
	}
	c.emit(vm.OpJmp)
	c.emit(0, 0, 0, 0)

	startPos := c.pos
	maxIterations := len(c.tokens) * 2
	iterations := 0

	// First pass: Handle directives and word definitions
	for c.pos < len(c.tokens) && c.peek().Type != TokenEOF {
		iterations++
		if iterations > maxIterations {
			return nil, fmt.Errorf("infinite loop detected in first pass at pos=%d, token=%v", c.pos, c.peek())
		}
		token := c.peek()
		if c.trace {
			fmt.Fprintf(os.Stderr, "compile: First pass, pos=%d, token=%v\n", c.pos, token)
		}
		if token.Type == TokenWord && strings.ToUpper(token.Value) == "MODULE" {
			if err := c.handleModuleDirective(); err != nil {
				return nil, err
			}
		} else if token.Type == TokenWord && strings.ToUpper(token.Value) == "IMPORT" {
			if err := c.handleImportDirective(); err != nil {
				return nil, err
			}
		} else if token.Type == TokenAtSign {
			if err := c.compileWordDefinition(); err != nil {
				return nil, err
			}
		} else {
			c.advance()
		}
	}

	mainStart := c.currentAddress()
	if c.trace {
		fmt.Fprintf(os.Stderr, "compile: Main code starts at addr=%d\n", mainStart)
	}
	mainStartBytes := vm.EncodeInt32(mainStart)
	if c.trace {
		fmt.Fprintf(os.Stderr, "compile: Patching JMP at %d with addr=%d\n", jmpAddr+1, mainStart)
	}
	copy(c.bytecode[jmpAddr+1:jmpAddr+5], mainStartBytes)

	c.pos = startPos
	if c.trace {
		fmt.Fprintf(os.Stderr, "compile: Starting second pass, pos=%d\n", c.pos)
	}
	// Second pass: Compile main code and quotations
	for c.pos < len(c.tokens) && c.peek().Type != TokenEOF {
		token := c.peek()
		if c.trace {
			fmt.Fprintf(os.Stderr, "compile: Second pass, pos=%d, token=%v\n", c.pos, token)
		}
		if token.Type == TokenWord {
			upperVal := strings.ToUpper(token.Value)
			if upperVal == "MODULE" {
				c.advance()
				c.advance()
				if c.trace {
					fmt.Fprintf(os.Stderr, "compile: Skipped MODULE directive\n")
				}
				continue
			} else if upperVal == "IMPORT" {
				c.advance()
				c.advance()
				if c.peek().Type == TokenWord && strings.ToUpper(c.peek().Value) == "AS" {
					c.advance()
					c.advance()
				}
				if c.trace {
					fmt.Fprintf(os.Stderr, "compile: Skipped IMPORT directive\n")
				}
				continue
			}
		}
		if token.Type == TokenAtSign {
			if c.trace {
				fmt.Fprintf(os.Stderr, "compile: Skipping word definition\n")
			}
			c.skipWordDefinition()
		} else if token.Type == TokenLBracket {
			// Initialize quotation and emit PUSH
			if err := c.compileToken(token); err != nil {
				return nil, err
			}
			c.advance() // Skip [
			// Compile quotation code
			if err := c.compileQuotation(); err != nil {
				return nil, err
			}
		} else if token.Type != TokenEOF {
			if c.trace {
				fmt.Fprintf(os.Stderr, "compile: Compiling token %v\n", token)
			}
			if err := c.compileToken(token); err != nil {
				return nil, err
			}
			c.advance()
		} else {
			break
		}
	}

	// After main code completes, emit JMP to skip quotation storage area
	skipQuotationsLabel := len(c.bytecode)
	c.emit(vm.OpJmp)
	c.emit(0, 0, 0, 0) // Placeholder, will be patched to point to HALT

	// Store the position where main code ends (before quotations)
	mainEndPos := len(c.bytecode)

	// Build a map of temp addresses to real addresses as we place quotations
	addrMap := make(map[int32]int32)

	// Append quotations at the end and record their real addresses
	for i := range c.quotations {
		c.quotations[i].Address = c.currentAddress()
		addrMap[c.quotations[i].TempAddr] = c.quotations[i].Address
		if c.trace {
			fmt.Fprintf(os.Stderr, "compile: Placing quotation %d at addr=%d (was temp %d)\n",
				i, c.quotations[i].Address, c.quotations[i].TempAddr)
		}
		c.bytecode = append(c.bytecode, c.quotations[i].Code...)
		c.quotations[i].EndAddr = c.currentAddress()
	}

	// Now patch all PUSH instructions that reference quotation addresses
	// First patch addresses in the main code section
	for j := 0; j < mainEndPos; j++ {
		if c.bytecode[j] == vm.OpPush && j+4 < mainEndPos {
			addr := int32(binary.BigEndian.Uint32(c.bytecode[j+1 : j+5]))
			if realAddr, ok := addrMap[addr]; ok {
				binary.BigEndian.PutUint32(c.bytecode[j+1:j+5], uint32(realAddr))
				if c.trace {
					fmt.Fprintf(os.Stderr, "compile: Patched PUSH at %d with addr=%d (was %d)\n",
						j+1, realAddr, addr)
				}
			}
		}
	}

	// CRITICAL: Also patch addresses within the quotation bytecode itself
	// This handles nested quotations that reference other quotations
	currentPos := mainEndPos
	for i := range c.quotations {
		quotCode := c.bytecode[currentPos : currentPos+len(c.quotations[i].Code)]
		for j := 0; j < len(quotCode); j++ {
			if quotCode[j] == vm.OpPush && j+4 < len(quotCode) {
				addr := int32(binary.BigEndian.Uint32(quotCode[j+1 : j+5]))
				if realAddr, ok := addrMap[addr]; ok {
					binary.BigEndian.PutUint32(quotCode[j+1:j+5], uint32(realAddr))
					if c.trace {
						fmt.Fprintf(os.Stderr, "compile: Patched nested PUSH in quotation %d at bytecode pos %d with addr=%d (was %d)\n",
							i, currentPos+j+1, realAddr, addr)
					}
				}
			}
		}
		currentPos += len(c.quotations[i].Code)
	}

	// Emit HALT and patch the skip quotations JMP
	haltAddr := c.currentAddress()
	if c.trace {
		fmt.Fprintf(os.Stderr, "compile: Emitting HALT at addr=%d, bytecode length=%d\n", haltAddr, len(c.bytecode))
	}
	c.emit(vm.OpHalt)

	// Patch the JMP that skips quotations to jump to HALT
	haltAddrBytes := vm.EncodeInt32(haltAddr)
	copy(c.bytecode[skipQuotationsLabel+1:skipQuotationsLabel+5], haltAddrBytes)
	if c.trace {
		fmt.Fprintf(os.Stderr, "compile: Patched skip-quotations JMP at %d to jump to HALT at %d\n",
			skipQuotationsLabel+1, haltAddr)
		fmt.Fprintf(os.Stderr, "compile: Final bytecode=%v\n", c.bytecode)
	}
	return c.bytecode, nil
}

// handleModuleDirective processes MODULE directives
func (c *Compiler) handleModuleDirective() error {
	c.advance() // Skip MODULE
	nameToken := c.peek()
	if nameToken.Type != TokenWord {
		return fmt.Errorf("expected module name after MODULE at line %d", nameToken.Line)
	}
	c.currentModule = strings.ToUpper(nameToken.Value)
	c.advance()
	return nil
}

// handleImportDirective processes IMPORT directives
func (c *Compiler) handleImportDirective() error {
	c.advance() // Skip IMPORT
	nameToken := c.peek()
	if nameToken.Type != TokenWord {
		return fmt.Errorf("expected module name after IMPORT at line %d", nameToken.Line)
	}
	moduleName := strings.ToUpper(nameToken.Value)
	c.advance()
	if c.peek().Type == TokenWord && strings.ToUpper(c.peek().Value) == "AS" {
		c.advance() // Skip AS
		shorthandToken := c.peek()
		if shorthandToken.Type != TokenWord {
			return fmt.Errorf("expected shorthand name after AS at line %d", shorthandToken.Line)
		}
		shorthand := strings.ToUpper(shorthandToken.Value)
		c.imports[shorthand] = moduleName
		c.advance()
	}
	return nil
}

// resolveWord resolves a word reference
func (c *Compiler) resolveWord(wordName string) (Word, bool) {
	upperName := strings.ToUpper(wordName)
	if word, ok := c.dictionary[upperName]; ok {
		return word, true
	}
	if !strings.Contains(upperName, "::") && c.currentModule != "" {
		qualified := c.currentModule + "::" + upperName
		if word, ok := c.dictionary[qualified]; ok {
			return word, true
		}
	}
	if strings.Contains(upperName, "::") {
		parts := strings.SplitN(upperName, "::", 2)
		prefix, wordPart := parts[0], parts[1]
		if fullModule, ok := c.imports[prefix]; ok {
			qualified := fullModule + "::" + wordPart
			if word, ok := c.dictionary[qualified]; ok {
				return word, true
			}
		}
	}
	return Word{}, false
}

// compileToken compiles a single token
func (c *Compiler) compileToken(token Token) error {
	if c.trace {
		fmt.Fprintf(os.Stderr, "compileToken: Processing token=%v\n", token)
	}
	switch token.Type {
	case TokenNumber:
		value, err := ParseNumber(token)
		if err != nil {
			return err
		}
		if c.trace {
			fmt.Fprintf(os.Stderr, "compileToken: Emitting PUSH %d\n", value)
		}
		c.emit(vm.OpPush)
		c.emit(vm.EncodeInt32(value)...)
	case TokenString:
		for _, ch := range token.Value {
			c.emit(vm.OpPush)
			c.emit(vm.EncodeInt32(int32(ch))...)
			c.emit(vm.OpPush)
			c.emit(vm.EncodeInt32(1)...)
			c.emit(vm.OpOut)
		}
	case TokenWord:
		wordName := strings.ToUpper(token.Value)
		if c.trace {
			fmt.Fprintf(os.Stderr, "compileToken: Word '%s' (upper='%s')\n", token.Value, wordName)
		}
		if wordName == "." {
			c.emit(vm.OpPush)
			c.emit(vm.EncodeInt32(0)...)
			c.emit(vm.OpOut)
			return nil
		}
		if wordName == "EMIT" {
			c.emit(vm.OpPush)
			c.emit(vm.EncodeInt32(1)...)
			c.emit(vm.OpOut)
			return nil
		}
		if word, ok := c.resolveWord(wordName); ok {
			if c.trace {
				fmt.Fprintf(os.Stderr, "compileToken: Emitting CALL to word '%s' at addr=%d\n", word.Name, word.Address)
			}
			c.emit(vm.OpCall)
			c.emit(vm.EncodeInt32(word.Address)...)
			return nil
		}
		if combinators[wordName] {
			if c.trace {
				fmt.Fprintf(os.Stderr, "compileToken: Dispatching to combinator '%s'\n", wordName)
			}
			return c.compileCombinator(wordName, token.Line)
		}
		if opcode, ok := builtins[wordName]; ok {
			if c.trace {
				fmt.Fprintf(os.Stderr, "compileToken: Emitting builtin opcode=%s\n", vm.OpcodeName(opcode))
			}
			c.emit(opcode)
			return nil
		}
		return fmt.Errorf("unknown word '%s' at line %d", token.Value, token.Line)
	case TokenLBracket:
		tempAddr := c.currentAddress() + 5
		if c.trace {
			fmt.Fprintf(os.Stderr, "compileToken: Emitting PUSH for quotation at temp addr=%d\n", tempAddr)
		}
		c.quotations = append(c.quotations, Quotation{TempAddr: tempAddr, Code: []byte{}})
		c.emit(vm.OpPush)
		c.emit(vm.EncodeInt32(tempAddr)...)
	case TokenRBracket:
		return fmt.Errorf("unexpected ] at line %d", token.Line)
	default:
		if c.trace {
			fmt.Fprintf(os.Stderr, "compileToken: Unexpected token type=%v\n", token.Type)
		}
		return fmt.Errorf("unexpected token type %v at line %d", token.Type, token.Line)
	}
	return nil
}

// compileWordDefinition compiles a word definition
func (c *Compiler) compileWordDefinition() error {
	c.advance() // Skip @
	nameToken := c.advance()
	if nameToken.Type != TokenWord {
		return fmt.Errorf("expected word name after '@', got %v at line %d", nameToken.Type, nameToken.Line)
	}
	baseName := strings.ToUpper(nameToken.Value)
	var wordName string
	if c.currentModule != "" && !strings.Contains(baseName, "::") {
		wordName = c.currentModule + "::" + baseName
	} else {
		wordName = baseName
	}
	wordAddress := c.currentAddress()

	// Compile the word body
	for {
		token := c.peek()
		if token.Type == TokenEOF {
			return fmt.Errorf("unexpected end of file in word definition '%s'", wordName)
		}
		if token.Type == TokenSemicolon {
			c.advance()
			break
		}
		if token.Type == TokenAtSign {
			return fmt.Errorf("nested word definitions not allowed at line %d", token.Line)
		}

		// Special handling for quotations in word definitions
		switch token.Type {
		case TokenLBracket:
			// Create a quotation entry
			tempAddr := c.currentAddress() + 5 // Address after the PUSH instruction
			c.quotations = append(c.quotations, Quotation{TempAddr: tempAddr, Code: []byte{}})

			// Emit PUSH with temporary address
			c.emit(vm.OpPush)
			c.emit(vm.EncodeInt32(tempAddr)...)

			// Skip the [
			c.advance()

			// Compile the quotation
			if err := c.compileQuotationInDefinition(); err != nil {
				return err
			}
			// The ] has been consumed by compileQuotationInDefinition
		case TokenRBracket:
			return fmt.Errorf("unexpected ] in word definition at line %d", token.Line)
		default:
			if err := c.compileToken(token); err != nil {
				return err
			}
			c.advance()
		}
	}

	c.emit(vm.OpRet)
	c.dictionary[wordName] = Word{
		Name:    wordName,
		Address: wordAddress,
		Module:  c.currentModule,
	}
	return nil
}

// compileQuotationInDefinition is a special version for compiling quotations inside word definitions
func (c *Compiler) compileQuotationInDefinition() error {
	quotIndex := len(c.quotations) - 1
	if quotIndex < 0 {
		return fmt.Errorf("no quotation started for [ at line %d", c.peek().Line)
	}
	quot := &c.quotations[quotIndex]

	depth := 1
	for c.pos < len(c.tokens) && depth > 0 && c.peek().Type != TokenEOF {
		token := c.peek()

		if token.Type == TokenLBracket {
			// Handle nested quotation
			depth++
			// Calculate a temporary address for the nested quotation
			tempAddr := int32(0x1000 + len(c.quotations)*0x100)

			// Emit PUSH instruction in the parent quotation
			quot.Code = append(quot.Code, vm.OpPush)
			quot.Code = append(quot.Code, vm.EncodeInt32(tempAddr)...)

			// Create new quotation entry
			c.quotations = append(c.quotations, Quotation{TempAddr: tempAddr, Code: []byte{}})

			// Advance past the [
			c.advance()

			// Recursively compile the nested quotation
			if err := c.compileQuotationInDefinition(); err != nil {
				return err
			}

			// Refresh our quotation pointer
			quot = &c.quotations[quotIndex]

		} else if token.Type == TokenRBracket {
			depth--
			if depth == 0 {
				// This is our closing bracket
				break
			}
			// Shouldn't get here with proper nesting
			return fmt.Errorf("unexpected ] in quotation at line %d", token.Line)

		} else if token.Type == TokenSemicolon {
			// Semicolon inside quotation is an error
			return fmt.Errorf("unexpected ; inside quotation at line %d", token.Line)

		} else {
			// Compile regular tokens into quotation bytecode
			switch token.Type {
			case TokenNumber:
				num, err := ParseNumber(token)
				if err != nil {
					return err
				}
				quot.Code = append(quot.Code, vm.OpPush)
				quot.Code = append(quot.Code, vm.EncodeInt32(num)...)
				c.advance()

			case TokenWord:
				upperVal := strings.ToUpper(token.Value)

				if upperVal == "." {
					quot.Code = append(quot.Code, vm.OpPush)
					quot.Code = append(quot.Code, vm.EncodeInt32(0)...)
					quot.Code = append(quot.Code, vm.OpOut)
					c.advance()
				} else if upperVal == "EMIT" {
					quot.Code = append(quot.Code, vm.OpPush)
					quot.Code = append(quot.Code, vm.EncodeInt32(1)...)
					quot.Code = append(quot.Code, vm.OpOut)
					c.advance()
				} else if opcode, ok := builtins[upperVal]; ok {
					quot.Code = append(quot.Code, opcode)
					c.advance()
				} else if combinators[upperVal] {
					c.advance()
					if err := c.compileQuotationCombinator(upperVal, quot); err != nil {
						return err
					}
				} else if word, ok := c.resolveWord(upperVal); ok {
					quot.Code = append(quot.Code, vm.OpCall)
					quot.Code = append(quot.Code, vm.EncodeInt32(word.Address)...)
					c.advance()
				} else {
					return fmt.Errorf("unknown word '%s' in quotation at line %d", token.Value, token.Line)
				}

			case TokenString:
				for _, ch := range token.Value {
					quot.Code = append(quot.Code, vm.OpPush)
					quot.Code = append(quot.Code, vm.EncodeInt32(int32(ch))...)
					quot.Code = append(quot.Code, vm.OpPush)
					quot.Code = append(quot.Code, vm.EncodeInt32(1)...)
					quot.Code = append(quot.Code, vm.OpOut)
				}
				c.advance()

			default:
				return fmt.Errorf("invalid token %v in quotation at line %d", token.Type, token.Line)
			}
		}
	}

	if c.peek().Type != TokenRBracket {
		return fmt.Errorf("unclosed quotation at line %d", c.tokens[c.pos-1].Line)
	}

	// Append RET to end the quotation
	quot.Code = append(quot.Code, vm.OpRet)

	// Skip the closing ]
	c.advance()

	return nil
}

// skipWordDefinition skips a word definition
func (c *Compiler) skipWordDefinition() {
	c.advance() // Skip @
	c.advance() // Skip name
	depth := 0
	for c.peek().Type != TokenEOF {
		token := c.peek()
		if token.Type == TokenLBracket {
			depth++
		} else if token.Type == TokenRBracket {
			depth--
		} else if token.Type == TokenSemicolon && depth == 0 {
			c.advance()
			break
		}
		c.advance()
	}
}

// compileQuotation compiles a [ ... ] block
func (c *Compiler) compileQuotation() error {
	quotIndex := len(c.quotations) - 1
	if quotIndex < 0 {
		return fmt.Errorf("no quotation started for [ at line %d", c.peek().Line)
	}
	quot := &c.quotations[quotIndex]
	if c.trace {
		fmt.Fprintf(os.Stderr, "compileQuotation: Compiling quotation %d at temp addr=%d\n", quotIndex, quot.TempAddr)
	}
	depth := 1
	for c.pos < len(c.tokens) && depth > 0 && c.peek().Type != TokenEOF {
		token := c.peek()
		if c.trace {
			fmt.Fprintf(os.Stderr, "compile: Compiling quotation token %v, depth=%d\n", token, depth)
		}

		if token.Type == TokenLBracket {
			// Found a nested quotation - we need to:
			// 1. Emit PUSH instruction in parent quotation
			// 2. Create and compile the nested quotation

			// Calculate a temporary address for the nested quotation
			// This will be patched later when we know the real address
			tempAddr := int32(0x1000 + len(c.quotations)*0x100) // Temporary unique address

			// Emit PUSH instruction in the parent quotation with temp address
			quot.Code = append(quot.Code, vm.OpPush)
			quot.Code = append(quot.Code, vm.EncodeInt32(tempAddr)...)

			// Create new quotation entry
			c.quotations = append(c.quotations, Quotation{TempAddr: tempAddr, Code: []byte{}})

			// Advance past the [
			c.advance()

			// Recursively compile the nested quotation
			if err := c.compileQuotation(); err != nil {
				return err
			}

			// After recursive call, refresh our quotation pointer as the slice may have been reallocated
			quot = &c.quotations[quotIndex]
			// Note: The recursive call consumed everything including the closing ]

		} else if token.Type == TokenRBracket {
			depth--
			if depth == 0 {
				// This is our closing bracket
				break
			} else {
				// This shouldn't happen if nesting is handled correctly
				return fmt.Errorf("unexpected ] in quotation at line %d", token.Line)
			}
		} else {
			// Compile regular tokens into the quotation's bytecode
			switch token.Type {
			case TokenNumber:
				num, err := ParseNumber(token)
				if err != nil {
					return err
				}
				quot.Code = append(quot.Code, vm.OpPush)
				quot.Code = append(quot.Code, vm.EncodeInt32(num)...)
				c.advance()

			case TokenWord:
				upperVal := strings.ToUpper(token.Value)

				// Check for special output words
				if upperVal == "." {
					quot.Code = append(quot.Code, vm.OpPush)
					quot.Code = append(quot.Code, vm.EncodeInt32(0)...)
					quot.Code = append(quot.Code, vm.OpOut)
					c.advance()
				} else if upperVal == "EMIT" {
					quot.Code = append(quot.Code, vm.OpPush)
					quot.Code = append(quot.Code, vm.EncodeInt32(1)...)
					quot.Code = append(quot.Code, vm.OpOut)
					c.advance()
				} else if opcode, ok := builtins[upperVal]; ok {
					// Builtin opcode
					quot.Code = append(quot.Code, opcode)
					c.advance()
				} else if combinators[upperVal] {
					// Handle combinators in quotations
					c.advance()
					if err := c.compileQuotationCombinator(upperVal, quot); err != nil {
						return err
					}
				} else if word, ok := c.resolveWord(upperVal); ok {
					// User-defined word
					quot.Code = append(quot.Code, vm.OpCall)
					quot.Code = append(quot.Code, vm.EncodeInt32(word.Address)...)
					c.advance()
				} else {
					return fmt.Errorf("unknown word '%s' in quotation at line %d", token.Value, token.Line)
				}

			case TokenString:
				// Handle string literals in quotations
				for _, ch := range token.Value {
					quot.Code = append(quot.Code, vm.OpPush)
					quot.Code = append(quot.Code, vm.EncodeInt32(int32(ch))...)
					quot.Code = append(quot.Code, vm.OpPush)
					quot.Code = append(quot.Code, vm.EncodeInt32(1)...)
					quot.Code = append(quot.Code, vm.OpOut)
				}
				c.advance()

			default:
				return fmt.Errorf("invalid token %v in quotation at line %d", token.Type, token.Line)
			}
		}
	}

	// Check for the closing bracket
	if c.peek().Type != TokenRBracket {
		return fmt.Errorf("unclosed quotation at line %d", c.tokens[c.pos-1].Line)
	}

	// Append RET to mark the end of the quotation
	quot.Code = append(quot.Code, vm.OpRet)

	// Skip the closing ]
	c.advance()
	if c.trace {
		fmt.Fprintf(os.Stderr, "compile: Quotation %d compiled, code=%v\n", quotIndex, quot.Code)
	}
	return nil
}

// compileQuotationCombinator compiles a combinator within a quotation
func (c *Compiler) compileQuotationCombinator(name string, quot *Quotation) error {
	switch strings.ToUpper(name) {
	case "DIP":
		// DIP in a quotation just emits CALLSTACK
		// At runtime: stack has [... x quotation-addr]
		// DIP will pop quotation-addr and call it, leaving x on stack
		quot.Code = append(quot.Code, vm.OpCallStack)

	case "KEEP":
		// KEEP: x [ quot ] keep -> x (quot x) x
		quot.Code = append(quot.Code, vm.OpSwap)      // quot x
		quot.Code = append(quot.Code, vm.OpDup)       // quot x x
		quot.Code = append(quot.Code, vm.OpRot)       // x x quot
		quot.Code = append(quot.Code, vm.OpCallStack) // x result

	case "CALL":
		// CALL just executes the quotation on top of stack
		quot.Code = append(quot.Code, vm.OpCallStack)

	default:
		// For now, other combinators aren't supported in quotations
		return fmt.Errorf("combinator '%s' not yet supported in quotations", name)
	}
	return nil
}

func patchQuotationAddresses(bytecode []byte, quotations []Quotation, mainEndPos int) {
	// Build a map of temp addresses to real addresses
	addrMap := make(map[int32]int32)
	for i := range quotations {
		addrMap[quotations[i].TempAddr] = quotations[i].Address
	}

	// Patch addresses in main code
	for j := 0; j < mainEndPos; j++ {
		if bytecode[j] == vm.OpPush && j+4 < mainEndPos {
			addr := int32(binary.BigEndian.Uint32(bytecode[j+1 : j+5]))
			if realAddr, ok := addrMap[addr]; ok {
				binary.BigEndian.PutUint32(bytecode[j+1:j+5], uint32(realAddr))
			}
		}
	}

	// Also patch addresses within quotations themselves (for nested quotations)
	for i := range quotations {
		code := quotations[i].Code
		for j := 0; j < len(code); j++ {
			if code[j] == vm.OpPush && j+4 < len(code) {
				addr := int32(binary.BigEndian.Uint32(code[j+1 : j+5]))
				if realAddr, ok := addrMap[addr]; ok {
					binary.BigEndian.PutUint32(code[j+1:j+5], uint32(realAddr))
				}
			}
		}
	}
}

// compileCombinator compiles control flow combinators
func (c *Compiler) compileCombinator(name string, line int) error {
	if c.trace {
		fmt.Fprintf(os.Stderr, "compileCombinator: Starting, bytecode length=%d, baseAddr=%d\n", len(c.bytecode), c.baseAddr)
		fmt.Fprintf(os.Stderr, "compileCombinator: name=%s, line=%d\n", name, line)
	}
	switch strings.ToUpper(name) {
	case "CALL":
		c.emit(vm.OpCallStack)
		return nil
	case "?:":
		return c.compileIfElse()
	case "?":
		return c.compileIf()
	case "!:":
		return c.compileUnless()
	case "|:":
		return c.compileWhile()
	case "#:":
		return c.compileTimes()
	case "DIP":
		return c.compileDip()
	case "KEEP":
		return c.compileKeep()
	default:
		return fmt.Errorf("unknown combinator '%s' at line %d", name, line)
	}
}

// compileIfElse compiles: condition [ true ] [ false ] ?:
func (c *Compiler) compileIfElse() error {
	if c.trace {
		fmt.Fprintf(os.Stderr, "compileIfElse: Starting, bytecode length=%d, baseAddr=%d\n", len(c.bytecode), c.baseAddr)
	}
	if len(c.quotations) < 2 {
		return fmt.Errorf("if-else requires two quotations at line %d", c.peek().Line)
	}
	c.emit(vm.OpSwap)
	if c.trace {
		fmt.Fprintf(os.Stderr, "Emitted SWAP, bytecode=%v\n", c.bytecode)
	}
	c.emit(vm.OpRot)
	if c.trace {
		fmt.Fprintf(os.Stderr, "Emitted ROT, bytecode=%v\n", c.bytecode)
	}
	elseLabel := len(c.bytecode)
	c.emit(vm.OpJz)
	c.emit(0, 0, 0, 0)
	if c.trace {
		fmt.Fprintf(os.Stderr, "Emitted JZ, elseLabel=%d (relative), bytecode length=%d\n", elseLabel, len(c.bytecode))
	}
	c.emit(vm.OpSwap)
	if c.trace {
		fmt.Fprintf(os.Stderr, "Emitted SWAP (true branch), bytecode=%v\n", c.bytecode)
	}
	c.emit(vm.OpPop)
	if c.trace {
		fmt.Fprintf(os.Stderr, "Emitted POP (true branch), bytecode=%v\n", c.bytecode)
	}
	c.emit(vm.OpCallStack)
	if c.trace {
		fmt.Fprintf(os.Stderr, "Emitted CALLSTACK (true branch), bytecode=%v\n", c.bytecode)
	}
	endLabel := len(c.bytecode)
	c.emit(vm.OpJmp)
	c.emit(0, 0, 0, 0)
	if c.trace {
		fmt.Fprintf(os.Stderr, "Emitted JMP, endLabel=%d (relative), bytecode length=%d\n", endLabel, len(c.bytecode))
	}
	elseBranch := c.currentAddress()
	if c.trace {
		fmt.Fprintf(os.Stderr, "Else branch starts at absolute addr=%d\n", elseBranch)
	}
	c.emit(vm.OpPop)
	if c.trace {
		fmt.Fprintf(os.Stderr, "Emitted POP (else branch), bytecode=%v\n", c.bytecode)
	}
	c.emit(vm.OpCallStack)
	if c.trace {
		fmt.Fprintf(os.Stderr, "Emitted CALLSTACK (else branch), bytecode=%v\n", c.bytecode)
	}
	// FIX: Calculate end address AFTER emitting else branch code
	end := c.currentAddress()
	if c.trace {
		fmt.Fprintf(os.Stderr, "End at absolute addr=%d\n", end)
	}
	// Patch JZ to jump to else branch
	elseLabelBytes := vm.EncodeInt32(elseBranch)
	copy(c.bytecode[elseLabel+1:elseLabel+5], elseLabelBytes)
	if c.trace {
		fmt.Fprintf(os.Stderr, "Patching JZ at %d with addr=%d\n", elseLabel+1, elseBranch)
		fmt.Fprintf(os.Stderr, "After JZ patch, bytecode=%v\n", c.bytecode)
	}
	// Patch JMP to jump to end (after else branch)
	endLabelBytes := vm.EncodeInt32(end)
	copy(c.bytecode[endLabel+1:endLabel+5], endLabelBytes)
	if c.trace {
		fmt.Fprintf(os.Stderr, "Patching JMP at %d with addr=%d\n", endLabel+1, end)
		fmt.Fprintf(os.Stderr, "After JMP patch, bytecode=%v\n", c.bytecode)
	}
	return nil
}

// compileIf compiles: condition [ true ] ?
func (c *Compiler) compileIf() error {
	c.emit(vm.OpSwap)
	c.emit(vm.OpJz)
	skipLabel := c.currentOffset() // Use offset, not address
	c.emit(0, 0, 0, 0)
	c.emit(vm.OpCallStack)
	c.emit(vm.OpJmp)
	endLabel := c.currentOffset() // Use offset, not address
	c.emit(0, 0, 0, 0)
	skip := c.currentAddress() // Keep this as address for the jump target
	c.emit(vm.OpPop)
	end := c.currentAddress() // Keep this as address for the jump target
	skipBytes := vm.EncodeInt32(skip)
	copy(c.bytecode[skipLabel:skipLabel+4], skipBytes) // Now skipLabel is a valid offset
	endBytes := vm.EncodeInt32(end)
	copy(c.bytecode[endLabel:endLabel+4], endBytes) // Now endLabel is a valid offset
	return nil
}

// compileUnless compiles: condition [ false ] !:
func (c *Compiler) compileUnless() error {
	c.emit(vm.OpSwap)
	c.emit(vm.OpJnz)
	skipLabel := c.currentOffset()
	c.emit(0, 0, 0, 0)
	c.emit(vm.OpCallStack)
	c.emit(vm.OpJmp)
	endLabel := c.currentOffset()
	c.emit(0, 0, 0, 0)
	skip := c.currentAddress()
	c.emit(vm.OpPop)
	end := c.currentAddress()
	skipBytes := vm.EncodeInt32(skip)
	copy(c.bytecode[skipLabel:skipLabel+4], skipBytes)
	endBytes := vm.EncodeInt32(end)
	copy(c.bytecode[endLabel:endLabel+4], endBytes)
	return nil
}

// compileUntil compiles: [ condition ] [ body ] until
func (c *Compiler) compileUntil() error {
	// Use reserved memory to store condition and body quotation addresses
	tempCondAddr, err := c.allocTemp(4)
	if err != nil {
		return err
	}
	tempBodyAddr, err := c.allocTemp(4)
	if err != nil {
		return err
	}

	// Stack: [... n condition-addr body-addr]
	// Save body-addr to memory
	c.emit(vm.OpStore)
	c.emit(vm.EncodeInt32(tempBodyAddr)...)

	// Save condition-addr to memory
	c.emit(vm.OpStore)
	c.emit(vm.EncodeInt32(tempCondAddr)...)

	// Stack: [... n]
	loopStart := c.currentAddress()

	// Load and execute body
	c.emit(vm.OpLoad)
	c.emit(vm.EncodeInt32(tempBodyAddr)...)
	c.emit(vm.OpCallStack) // Execute body: [... n-1]

	// Load and execute condition
	c.emit(vm.OpLoad)
	c.emit(vm.EncodeInt32(tempCondAddr)...)
	c.emit(vm.OpCallStack) // Execute condition: [... n-1 1/0]

	// Swap to put condition on top
	c.emit(vm.OpSwap) // [... 1/0 n-1]

	// Check condition: exit if true (non-zero)
	c.emit(vm.OpJnz)
	exitLabel := c.currentOffset()
	c.emit(0, 0, 0, 0) // Placeholder for jump target

	// Loop back
	c.emit(vm.OpJmp)
	c.emit(vm.EncodeInt32(loopStart)...)

	// Exit point
	exit := c.currentAddress()

	// Patch JNZ to jump to exit
	exitBytes := vm.EncodeInt32(exit)
	copy(c.bytecode[exitLabel:exitLabel+4], exitBytes)

	return nil
}

// compileWhile compiles: [ condition ] [ body ] |:
func (c *Compiler) compileWhile() error {
	tempCondAddr, err := c.allocTemp(4)
	if err != nil {
		return err
	}
	tempBodyAddr, err := c.allocTemp(4)
	if err != nil {
		return err
	}

	c.emit(vm.OpStore)
	c.emit(vm.EncodeInt32(tempBodyAddr)...)
	c.emit(vm.OpStore)
	c.emit(vm.EncodeInt32(tempCondAddr)...)

	loopStart := c.currentAddress()

	c.emit(vm.OpDup)

	c.emit(vm.OpLoad)
	c.emit(vm.EncodeInt32(tempCondAddr)...)
	c.emit(vm.OpCallStack)
	// Stack: [... original-value result]

	// NO SWAP - result is already on top for JZ

	c.emit(vm.OpJz)
	exitLabel := c.currentOffset()
	c.emit(0, 0, 0, 0)
	// JZ pops result, leaves original-value

	c.emit(vm.OpLoad)
	c.emit(vm.EncodeInt32(tempBodyAddr)...)
	c.emit(vm.OpCallStack)

	c.emit(vm.OpJmp)
	c.emit(vm.EncodeInt32(loopStart)...)

	exit := c.currentAddress()
	exitBytes := vm.EncodeInt32(exit)
	copy(c.bytecode[exitLabel:exitLabel+4], exitBytes)

	return nil
}

// compileTimes compiles: [ body ] n #:
// Stack before #:: [... data... quot-addr count]
// Stack after: [... data'... ] (quotation executed count times on data)
//
// The quotation should operate on the data BELOW the loop control variables.
// We need to temporarily remove quot-addr and count, execute the quotation,
// then restore them for the next iteration.
func (c *Compiler) compileTimes() error {
	// Use reserved memory to save loop variables
	// This is similar to the dip combinator
	tempQuotAddr, err := c.allocTemp(4)
	if err != nil {
		return err
	}
	tempCountAddr, err := c.allocTemp(4)
	if err != nil {
		return err
	}

	loopStart := c.currentAddress()

	// Stack: [... data... quot-addr count]
	c.emit(vm.OpDup) // [... data... quot-addr count count]
	c.emit(vm.OpJz)  // [... data... quot-addr count], jump if 0
	exitLabel := c.currentOffset()
	c.emit(0, 0, 0, 0)

	// Save count-1 to memory
	c.emit(vm.OpDec)   // [... data... quot-addr count-1]
	c.emit(vm.OpStore) // [... data... quot-addr]
	c.emit(vm.EncodeInt32(tempCountAddr)...)

	// Save quot-addr to memory
	c.emit(vm.OpStore) // [... data...]
	c.emit(vm.EncodeInt32(tempQuotAddr)...)

	// Execute quotation on the data
	c.emit(vm.OpLoad) // [... data... quot-addr]
	c.emit(vm.EncodeInt32(tempQuotAddr)...)
	c.emit(vm.OpCallStack) // [... data'...], quotation executes

	// Restore loop variables
	c.emit(vm.OpLoad) // [... data'... quot-addr]
	c.emit(vm.EncodeInt32(tempQuotAddr)...)
	c.emit(vm.OpLoad) // [... data'... quot-addr count-1]
	c.emit(vm.EncodeInt32(tempCountAddr)...)

	c.emit(vm.OpJmp)
	c.emit(vm.EncodeInt32(loopStart)...)

	// Exit: clean up
	exit := c.currentAddress()
	c.emit(vm.OpPop) // Pop count (0)
	c.emit(vm.OpPop) // Pop quot-addr

	copy(c.bytecode[exitLabel:exitLabel+4], vm.EncodeInt32(exit))
	return nil
}

// compileDip compiles: [body] dip
func (c *Compiler) compileDip() error {
	// Stack: [... x body-addr]
	// Execute body directly
	c.emit(vm.OpCallStack) // Execute body: [... x']

	return nil
}

// compileKeep compiles: x [ quot ] keep
func (c *Compiler) compileKeep() error {
	// Initial stack assumption: ... x quot (quot is the address of the quotation to execute)
	// Step 1: Emit SWAP to rearrange the stack so quot is below x
	c.emit(vm.OpSwap)
	// Stack after: ... quot x

	// Step 2: Emit DUP to duplicate x (this creates a copy for the quotation to consume)
	c.emit(vm.OpDup)
	// Stack after: ... quot x x

	// Step 3: Emit ROT to rotate the top three items, positioning quot on top for execution
	c.emit(vm.OpRot)
	// Stack after: ... x x quot

	// Step 4: Emit CALLSTACK to pop quot (as the address), push the return address to the return stack, and jump to execute the quotation
	// The quotation executes on the top x (consumes it and produces result), leaving the original x preserved below
	c.emit(vm.OpCallStack)

	return nil
}

// Helper methods
func (c *Compiler) peek() Token {
	if c.pos >= len(c.tokens) {
		return Token{Type: TokenEOF}
	}
	return c.tokens[c.pos]
}

func (c *Compiler) advance() Token {
	token := c.peek()
	if c.pos < len(c.tokens) {
		c.pos++
	}
	return token
}

func (c *Compiler) emit(bytes ...byte) {
	c.bytecode = append(c.bytecode, bytes...)
}

// currentOffset returns the current position in the bytecode slice
func (c *Compiler) currentOffset() int32 {
	return int32(len(c.bytecode))
}

// currentAddress returns the absolute VM address
func (c *Compiler) currentAddress() int32 {
	return int32(c.baseAddr + int32(len(c.bytecode)))
}

// allocTemp allocates space in reserved memory for temporary variables
func (c *Compiler) allocTemp(size int32) (int32, error) {
	addr := c.tempAlloc
	c.tempAlloc += size
	if c.tempAlloc > vm.ReservedMemorySize {
		return 0, fmt.Errorf("reserved memory overflow: exceeded %d bytes", vm.ReservedMemorySize)
	}
	return addr, nil
}
