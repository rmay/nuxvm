package lux

import (
	"encoding/binary"
	"fmt"
	"os"
	"strconv"
	"strings"

	"github.com/rmay/nuxvm/pkg/vm"
)

type Word struct {
	Name    string
	Address int32
	Module  string
}

type Quotation struct {
	TempAddr      int32          // Virtual address during compilation
	Address       int32          // Final VM address
	Code          []byte         // Compiled bytecode
	InternalJumps []InternalJump // Jumps within Code that need absolute-address patching
}

type InternalJump struct {
	PlaceholderAt int32 // Offset within Quotation.Code where the 4-byte address is
	TargetOffset  int32 // Offset within Quotation.Code where the jump should go
}

type PatchRequest struct {
	QuotIdx  int // -1 for main bytecode
	Offset   int32
	TempAddr int32
}

type UnresolvedReference struct {
	Word      string
	Offset    int32
	Line      int
	Column    int
	QuotIdx   int
	Module    string
	IsAddress bool
}

type Compiler struct {
	tokens         []Token
	pos            int
	bytecode       []byte
	dictionary     map[string]Word
	quotations     []Quotation
	quotationStack []int // indices of quotations currently being compiled
	activeQuotIdx  int   // -1 if compiling main code
	currentModule  string
	currentWord    string
	imports        map[string]string
	baseAddr       int32
	tempAlloc      int32
	nextStringHeap int32
	unresolved     []UnresolvedReference
	patchRequests  []PatchRequest
	includeDepth   int
	includedFiles  map[string]bool
	trace          bool

	stringPatches []StringPatch
}

type StringPatch struct {
	Offset   int32
	QuotIdx  int
	TempAddr int32
}

var builtins = map[string]byte{
	"DUP":      vm.OpDup,
	"DROP":     vm.OpPop,
	"SWAP":     vm.OpSwap,
	"ROT":      vm.OpRot,
	"OVER":     vm.OpOver,
	"PICK":     vm.OpPick,
	"ROLL":     vm.OpRoll,
	"LOAD":     vm.OpLoad,
	"STORE":    vm.OpStore,
	"LOADI":    vm.OpLoadI,
	"STOREI":   vm.OpStoreI,
	"EXIT":     vm.OpRet,
	"HALT":     vm.OpHalt,
	"YIELD":    vm.OpYield,
	"JNZ":      vm.OpJnz,
	"NEGATE":   vm.OpNeg,
	"ADD":      vm.OpAdd,
	"+":        vm.OpAdd,
	"SUB":      vm.OpSub,
	"-":        vm.OpSub,
	"MUL":      vm.OpMul,
	"*":        vm.OpMul,
	"DIV":      vm.OpDiv,
	"/":        vm.OpDiv,
	"MOD":      vm.OpMod,
	"INC":      vm.OpInc,
	"DEC":      vm.OpDec,
	"AND":      vm.OpAnd,
	"OR":       vm.OpOr,
	"XOR":      vm.OpXor,
	"NOT":      vm.OpNot,
	"SHL":      vm.OpShl,
	"LSHIFT":   vm.OpShl,
	"SHR":      vm.OpShr,
	"SAR":      vm.OpSar,
	"RSHIFT":   vm.OpShr,
	"EQ":       vm.OpEq,
	"=":        vm.OpEq,
	"LT":       vm.OpLt,
	"<":        vm.OpLt,
	"GT":       vm.OpGt,
	">":        vm.OpGt,
	"NEQ":      vm.OpNeq,
	"<>":       vm.OpNeq,
	"LTE":      vm.OpLte,
	"<=":       vm.OpLte,
	"GTE":      vm.OpGte,
	">=":       vm.OpGte,
	"ABS":      vm.OpAbs,
	"MIN":      vm.OpMin,
	"MAX":      vm.OpMax,
	"DIVMOD":   vm.OpDivmod,
	"CALLSTACK": vm.OpCallStack,
	"JMPSTACK":  vm.OpJmpStack,
	"PUSHR":     vm.OpPushR,
	"POPR":      vm.OpPopR,
	"PEEKR":     vm.OpPeekR,
	"PEEKR2":    vm.OpPeekR2,
	"FRAME!":   vm.OpFrame,
	"UNFRAME!": vm.OpUnframe,
	"LOCAL@":   vm.OpLocalGet,
	"LOCAL!":   vm.OpLocalSet,
}

var combinators = map[string]bool{
	"CALL": true, "?:": true, "?": true, "!:": true, "|:": true, "#:": true, "DIP": true, "KEEP": true,
}

func NewCompiler(tokens []Token, baseAddr int32, trace ...bool) *Compiler {
	tr := false
	if len(trace) > 0 {
		tr = trace[0]
	}
	return &Compiler{
		tokens: tokens, dictionary: make(map[string]Word), imports: make(map[string]string),
		includedFiles:  make(map[string]bool),
		quotationStack: make([]int, 0),
		activeQuotIdx:  -1, baseAddr: baseAddr, nextStringHeap: 0,
		tempAlloc: 0x8000,
		trace:     tr,
	}
}

func (c *Compiler) emit(bytes ...byte) {
	if c.activeQuotIdx >= 0 && c.activeQuotIdx < len(c.quotations) {
		c.quotations[c.activeQuotIdx].Code = append(c.quotations[c.activeQuotIdx].Code, bytes...)
	} else {
		c.bytecode = append(c.bytecode, bytes...)
	}
}

func (c *Compiler) currentAddress() int32 {
	if c.activeQuotIdx >= 0 && c.activeQuotIdx < len(c.quotations) {
		return int32(len(c.quotations[c.activeQuotIdx].Code))
	}
	return c.baseAddr + int32(len(c.bytecode))
}

func (c *Compiler) currentOffset() int32 {
	if c.activeQuotIdx >= 0 && c.activeQuotIdx < len(c.quotations) {
		return int32(len(c.quotations[c.activeQuotIdx].Code))
	}
	return int32(len(c.bytecode))
}

func (c *Compiler) advance() Token {
	if c.pos >= len(c.tokens) {
		return Token{Type: TokenEOF}
	}
	t := c.tokens[c.pos]
	c.pos++
	return t
}

func (c *Compiler) peek() Token {
	if c.pos >= len(c.tokens) {
		return Token{Type: TokenEOF}
	}
	return c.tokens[c.pos]
}

func Compile(source string, baseAddr int32, trace ...bool) ([]byte, error) {
	tr := false
	if len(trace) > 0 {
		tr = trace[0]
	}
	l := NewLexer(source, tr)
	tokens, err := l.Tokenize()
	if err != nil {
		return nil, err
	}
	return NewCompiler(tokens, baseAddr, tr).compile()
}

func (c *Compiler) compile() ([]byte, error) {
	c.emit(vm.OpJmp, 0, 0, 0, 0)
	initialJmpLabel := int32(1)

	// Pass 1: Definitions
	startPos := c.pos
	for c.pos < len(c.tokens) && c.peek().Type != TokenEOF {
		t := c.advance()
		if t.Type == TokenWord {
			u := strings.ToUpper(t.Value)
			if u == "MODULE" {
				if err := c.handleModuleDirective(); err != nil {
					return nil, err
				}
				continue
			}
			if u == "IMPORT" {
				if err := c.handleImportDirective(); err != nil {
					return nil, err
				}
				continue
			}
			if u == "INCLUDE" {
				if err := c.handleIncludeDirective(); err != nil {
					return nil, err
				}
				continue
			}
		} else if t.Type == TokenAtSign {
			if err := c.compileWordDefinition(); err != nil {
				return nil, err
			}
			continue
		}
	}

	mainStart := c.currentAddress()
	binary.BigEndian.PutUint32(c.bytecode[initialJmpLabel:initialJmpLabel+4], uint32(mainStart))

	// Pass 2: Main Code
	c.pos = startPos
	c.currentModule = ""
	c.imports = make(map[string]string)
	c.quotationStack = []int{}
	for c.pos < len(c.tokens) && c.peek().Type != TokenEOF {
		t := c.advance()
		if t.Type == TokenAtSign {
			c.advance() // name
			for c.pos < len(c.tokens) && c.peek().Type != TokenSemicolon {
				c.advance()
			}
			c.advance()
			continue
		}
		if t.Type == TokenWord {
			u := strings.ToUpper(t.Value)
			if u == "MODULE" {
				if err := c.handleModuleDirective(); err != nil {
					return nil, err
				}
				continue
			}
			if u == "IMPORT" {
				if err := c.handleImportDirective(); err != nil {
					return nil, err
				}
				continue
			}
			if u == "INCLUDE" {
				c.advance() // path
				continue
			}
		}
		if err := c.compileToken(t); err != nil {
			return nil, err
		}
	}

	skipQuotPos := c.currentOffset()
	c.emit(vm.OpJmp, 0, 0, 0, 0)
	addrMap := make(map[int32]int32)
	for i := range c.quotations {
		c.quotations[i].Address = c.currentAddress()
		if c.trace {
			fmt.Fprintf(os.Stderr, "Compiler: quotation %d assigned address %d (temp 0x%X)\n", i, c.quotations[i].Address, c.quotations[i].TempAddr)
		}
		addrMap[c.quotations[i].TempAddr] = c.quotations[i].Address
		c.emit(c.quotations[i].Code...)
	}

	for i := range c.quotations {
		qStart := int(c.quotations[i].Address - c.baseAddr)
		for _, ij := range c.quotations[i].InternalJumps {
			target := c.quotations[i].Address + ij.TargetOffset
			binary.BigEndian.PutUint32(c.bytecode[qStart+int(ij.PlaceholderAt):qStart+int(ij.PlaceholderAt)+4], uint32(target))
		}
	}

	for _, u := range c.unresolved {
		c.currentModule = u.Module
		w, ok := c.resolveWord(u.Word)
		if !ok {
			return nil, fmt.Errorf("unknown word '%s' at line %d", u.Word, u.Line)
		}
		var base int
		if u.QuotIdx == -1 {
			base = 0
		} else {
			base = int(c.quotations[u.QuotIdx].Address - c.baseAddr)
		}
		if u.IsAddress {
			c.bytecode[base+int(u.Offset)] = vm.OpPush
		} else {
			c.bytecode[base+int(u.Offset)] = vm.OpCall
		}
		binary.BigEndian.PutUint32(c.bytecode[base+int(u.Offset)+1:base+int(u.Offset)+5], uint32(w.Address))
	}

	for _, pr := range c.patchRequests {
		real, ok := addrMap[pr.TempAddr]
		if !ok {
			return nil, fmt.Errorf("failed to resolve temp address 0x%X", pr.TempAddr)
		}
		var base int
		if pr.QuotIdx == -1 {
			base = 0
		} else {
			base = int(c.quotations[pr.QuotIdx].Address - c.baseAddr)
		}
		binary.BigEndian.PutUint32(c.bytecode[base+int(pr.Offset):base+int(pr.Offset)+4], uint32(real))
	}

	haltAddr := c.currentAddress()
	c.emit(vm.OpHalt)
	binary.BigEndian.PutUint32(c.bytecode[int(skipQuotPos)+1:int(skipQuotPos)+5], uint32(haltAddr))

	stringHeapBase := c.baseAddr + int32(len(c.bytecode))
	for _, sp := range c.stringPatches {
		var base int
		if sp.QuotIdx == -1 {
			base = 0
		} else {
			base = int(c.quotations[sp.QuotIdx].Address - c.baseAddr)
		}
		binary.BigEndian.PutUint32(c.bytecode[base+int(sp.Offset):base+int(sp.Offset)+4], uint32(stringHeapBase+sp.TempAddr))
	}

	// Append padding for the string heap so NewVM allocates enough space
	padding := make([]byte, int(c.nextStringHeap))
	c.bytecode = append(c.bytecode, padding...)

	return c.bytecode, nil
}

func (c *Compiler) compileToken(t Token) error {
	if c.trace {
		fmt.Fprintf(os.Stderr, "Compiler: compileToken: type=%d, value=%s, line=%d, col=%d\n", t.Type, t.Value, t.Line, t.Column)
	}
	switch t.Type {
	case TokenNumber:
		n, _ := c.ParseNumber(t.Value)
		c.emit(vm.OpPush)
		c.emit(vm.EncodeInt32(n)...)
		return nil
	case TokenWord:
		u := strings.ToUpper(t.Value)
		if u == "." {
			c.emit(vm.OpPush)
			c.emit(vm.EncodeInt32(0)...)
			c.emit(vm.OpOut)
			return nil
		}
		if u == "EMIT" {
			c.emit(vm.OpPush)
			c.emit(vm.EncodeInt32(1)...)
			c.emit(vm.OpOut)
			return nil
		}
		if combinators[u] {
			if c.trace {
				fmt.Fprintf(os.Stderr, "Compiler: combinator=%s, quotStackLen=%d\n", u, len(c.quotationStack))
			}
			if (u == "?:" || u == "|:") && len(c.quotationStack) < 2 {
				return fmt.Errorf("%s requires two quotations", u)
			}
			if (u == "?" || u == "!:" || u == "#:" || u == "DIP" || u == "KEEP") && len(c.quotationStack) < 1 {
				return fmt.Errorf("%s requires one quotation", u)
			}
			return c.compileCombinator(u, t.Line)
		}
		if op, ok := builtins[u]; ok {
			c.emit(op)
			return nil
		}
		if w, ok := c.resolveWord(t.Value); ok {
			// TRO: if this is a recursive call at the very end of a word definition or quotation
			next := c.peek().Type
			if c.currentWord == w.Name && (next == TokenSemicolon || next == TokenRBracket) {
				c.emit(vm.OpJmp)
				c.emit(vm.EncodeInt32(w.Address)...)
				return nil
			}
			c.emit(vm.OpCall)
			c.emit(vm.EncodeInt32(w.Address)...)
			return nil
		}
		if n, err := c.ParseNumber(t.Value); err == nil {
			c.emit(vm.OpPush)
			c.emit(vm.EncodeInt32(n)...)
			return nil
		}
		off := c.currentOffset()
		c.unresolved = append(c.unresolved, UnresolvedReference{Word: t.Value, Offset: off, Line: t.Line, Column: t.Column, QuotIdx: c.activeQuotIdx, Module: c.currentModule})
		c.emit(vm.OpPush, 0, 0, 0, 0)
	case TokenDollar:
		// Handle $word -> push the bytecode address of word (function pointer / address-of)
		t = c.advance()
		if t.Type != TokenWord {
			return fmt.Errorf("expected word name after $ at line %d", t.Line)
		}
		if w, ok := c.resolveWord(t.Value); ok {
			c.emit(vm.OpPush)
			c.emit(vm.EncodeInt32(w.Address)...)
			return nil
		}
		// Unresolved word address: patch it later as a PUSH
		off := c.currentOffset()
		c.unresolved = append(c.unresolved, UnresolvedReference{Word: t.Value, Offset: off, Line: t.Line, Column: t.Column, QuotIdx: c.activeQuotIdx, Module: c.currentModule, IsAddress: true})
		c.emit(vm.OpPush, 0, 0, 0, 0)
		return nil
	case TokenLBracket:
		return c.compileQuotation()
	case TokenString:
		c.emitFileString(t.Value)
	case TokenRBracket:
		return fmt.Errorf("unexpected ]")
	}
	return nil
}

func (c *Compiler) compileWordDefinition() error {
	c.quotationStack = []int{}
	name := strings.ToUpper(c.advance().Value)
	isExported := strings.HasPrefix(name, ".")
	if isExported {
		name = name[1:]
	} else if c.currentModule != "" && !strings.Contains(name, "::") {
		name = c.currentModule + "::" + name
	}
	c.currentWord = name
	addr := c.currentAddress()
	c.dictionary[name] = Word{Name: name, Address: addr, Module: c.currentModule}
	for c.pos < len(c.tokens) && c.peek().Type != TokenSemicolon {
		if c.peek().Type == TokenEOF {
			return fmt.Errorf("unexpected end of file")
		}
		if err := c.compileToken(c.advance()); err != nil {
			return err
		}
	}
	t := c.advance()
	if t.Type != TokenSemicolon {
		return fmt.Errorf("expected ;")
	}
	c.emit(vm.OpRet)
	c.currentWord = ""
	return nil
}
func (c *Compiler) compileQuotation() error {
	idx := len(c.quotations)
	temp := int32(0x7FFF0000 + idx)
	c.quotations = append(c.quotations, Quotation{TempAddr: temp, Code: []byte{}})
	prev := c.activeQuotIdx
	c.activeQuotIdx = idx

	oldStack := c.quotationStack
	c.quotationStack = []int{}

	if c.trace {
		fmt.Fprintf(os.Stderr, "Compiler: compileQuotation: pushed quotIdx=%d, stackLen=%d\n", idx, len(c.quotationStack))
	}
	for c.pos < len(c.tokens) && c.peek().Type != TokenRBracket {
		t := c.advance()
		if c.trace {
			fmt.Fprintf(os.Stderr, "Compiler: compileQuotation: token type=%d, value=%s\n", t.Type, t.Value)
		}
		if err := c.compileToken(t); err != nil {
			return err
		}
	}
	t := c.advance()
	if t.Type == TokenEOF {
		return fmt.Errorf("unclosed quotation")
	}
	if t.Type != TokenRBracket {
		return fmt.Errorf("unexpected ]")
	}
	c.emit(vm.OpRet)
	c.activeQuotIdx = prev

	c.quotationStack = oldStack
	if c.trace {
		fmt.Fprintf(os.Stderr, "Compiler: compileQuotation: left stackLen=%d\n", len(c.quotationStack))
	}
	c.emit(vm.OpPush)
	off := c.currentOffset()
	c.emit(vm.EncodeInt32(temp)...)
	c.patchRequests = append(c.patchRequests, PatchRequest{QuotIdx: prev, Offset: off, TempAddr: temp})
	c.quotationStack = append(c.quotationStack, idx)
	return nil
}

func (c *Compiler) compileCombinator(name string, line int) error {
	getBuf := func() []byte {
		if c.activeQuotIdx >= 0 {
			return c.quotations[c.activeQuotIdx].Code
		}
		return c.bytecode
	}
	switch name {
	case "CALL":
		if len(c.quotationStack) > 0 {
			c.quotationStack = c.quotationStack[:len(c.quotationStack)-1]
		}
		c.emit(vm.OpCallStack)
	case "?:":
		c.quotationStack = c.quotationStack[:len(c.quotationStack)-2]
		next := c.peek().Type
		isTailCall := next == TokenSemicolon || next == TokenRBracket
		// Stack at runtime: [ ... cond addr_if addr_else ]
		// We want to move cond to top: [ ... addr_if addr_else cond ]
		// OpRot works on top 3: a=cond, b=addr_if, c=addr_else -> b, c, a
		c.emit(vm.OpRot, vm.OpJz) // -> [ ... addr_if addr_else cond ] -> Jz pops cond
		jzAt := c.currentOffset()
		c.emit(0, 0, 0, 0)

		// True branch: [ ... addr_if addr_else ]
		c.emit(vm.OpPop) // -> [ ... addr_if ]
		var jmpAt int32
		if isTailCall {
			c.emit(vm.OpJmpStack)
		} else {
			c.emit(vm.OpCallStack)
			c.emit(vm.OpJmp)
			jmpAt = int32(c.currentOffset())
			c.emit(0, 0, 0, 0)
		}

		// False branch: [ ... addr_if addr_else ] (cond was 0)
		elseAt := c.currentAddress()
		c.emit(vm.OpSwap, vm.OpPop) // -> [ ... addr_else ]
		if isTailCall {
			c.emit(vm.OpJmpStack)
		} else {
			c.emit(vm.OpCallStack)
		}
		endAt := c.currentAddress()

		if c.activeQuotIdx >= 0 {
			if isTailCall {
				c.quotations[c.activeQuotIdx].InternalJumps = append(c.quotations[c.activeQuotIdx].InternalJumps, InternalJump{int32(jzAt), int32(elseAt)})
			} else {
				c.quotations[c.activeQuotIdx].InternalJumps = append(c.quotations[c.activeQuotIdx].InternalJumps, InternalJump{int32(jzAt), int32(elseAt)}, InternalJump{jmpAt, int32(endAt)})
			}
		} else {
			binary.BigEndian.PutUint32(getBuf()[jzAt:jzAt+4], uint32(elseAt))
			if !isTailCall {
				binary.BigEndian.PutUint32(getBuf()[int(jmpAt):int(jmpAt)+4], uint32(endAt))
			}
		}
		return nil
	case "?":
		if len(c.quotationStack) < 1 {
			return fmt.Errorf("if requires one quotation")
		}
		c.quotationStack = c.quotationStack[:len(c.quotationStack)-1]
		next := c.peek().Type
		isTailCall := next == TokenSemicolon || next == TokenRBracket
		// Stack at runtime: [ cond addr ]
		c.emit(vm.OpSwap, vm.OpJz) // -> [ addr cond ] -> Jz pops cond
		jzAt := c.currentOffset()
		c.emit(0, 0, 0, 0)
		// True branch: [ addr ]
		if isTailCall {
			c.emit(vm.OpJmpStack)
		} else {
			c.emit(vm.OpCallStack)
			c.emit(vm.OpJmp)
			jmpAt := c.currentOffset()
			c.emit(0, 0, 0, 0)

			// False branch: [ addr ]
			elseAt := c.currentAddress()
			c.emit(vm.OpPop)
			endAt := c.currentAddress()
			if c.activeQuotIdx >= 0 {
				c.quotations[c.activeQuotIdx].InternalJumps = append(c.quotations[c.activeQuotIdx].InternalJumps, InternalJump{jzAt, elseAt}, InternalJump{jmpAt, endAt})
			} else {
				binary.BigEndian.PutUint32(getBuf()[jzAt:jzAt+4], uint32(elseAt))
				binary.BigEndian.PutUint32(getBuf()[jmpAt:jmpAt+4], uint32(endAt))
			}
			return nil
		}
		// If tail call, False branch (skip)
		elseAt := c.currentAddress()
		c.emit(vm.OpPop)
		if c.activeQuotIdx >= 0 {
			c.quotations[c.activeQuotIdx].InternalJumps = append(c.quotations[c.activeQuotIdx].InternalJumps, InternalJump{jzAt, elseAt})
		} else {
			binary.BigEndian.PutUint32(getBuf()[jzAt:jzAt+4], uint32(elseAt))
		}
		return nil
	case "!:":
		if len(c.quotationStack) < 1 {
			return fmt.Errorf("unless requires one quotation")
		}
		c.quotationStack = c.quotationStack[:len(c.quotationStack)-1]
		next := c.peek().Type
		isTailCall := next == TokenSemicolon || next == TokenRBracket
		c.emit(vm.OpSwap, vm.OpJnz)
		jnzAt := c.currentOffset()
		c.emit(0, 0, 0, 0)
		if isTailCall {
			c.emit(vm.OpJmpStack, vm.OpJmp)
		} else {
			c.emit(vm.OpCallStack, vm.OpJmp)
		}
		jmpAt := c.currentOffset()
		c.emit(0, 0, 0, 0)
		skipAt := c.currentAddress()
		c.emit(vm.OpPop)
		endAt := c.currentAddress()
		if c.activeQuotIdx >= 0 {
			c.quotations[c.activeQuotIdx].InternalJumps = append(c.quotations[c.activeQuotIdx].InternalJumps, InternalJump{jnzAt, skipAt}, InternalJump{jmpAt, endAt})
		} else {
			binary.BigEndian.PutUint32(getBuf()[jnzAt:jnzAt+4], uint32(skipAt))
			binary.BigEndian.PutUint32(getBuf()[jmpAt:jmpAt+4], uint32(endAt))
		}
	case "|:":
		if len(c.quotationStack) < 2 {
			return fmt.Errorf("while requires two quotations")
		}
		c.quotationStack = c.quotationStack[:len(c.quotationStack)-2]

		// At runtime: [ ... cond body ]
		c.emit(vm.OpPushR) // body to R
		c.emit(vm.OpPushR) // cond to R

		start := c.currentAddress()

		c.emit(vm.OpPeekR)     // cond
		c.emit(vm.OpCallStack) // calls cond. Result on stack.

		c.emit(vm.OpJz)
		jzAt := c.currentOffset()
		c.emit(0, 0, 0, 0)

		c.emit(vm.OpPeekR2)    // [ body cond ]
		c.emit(vm.OpPop)       // drop cond copy. Stack: [ body ]
		c.emit(vm.OpCallStack) // calls body

		c.emit(vm.OpJmp)
		jmpAt := c.currentOffset()
		c.emit(0, 0, 0, 0)

		exit := c.currentAddress()
		c.emit(vm.OpPopR) // cond
		c.emit(vm.OpPopR) // body
		c.emit(vm.OpPop)  // drop cond
		c.emit(vm.OpPop)  // drop body

		if c.activeQuotIdx >= 0 {
			c.quotations[c.activeQuotIdx].InternalJumps = append(c.quotations[c.activeQuotIdx].InternalJumps, InternalJump{jzAt, exit}, InternalJump{jmpAt, start})
		} else {
			binary.BigEndian.PutUint32(getBuf()[jzAt:jzAt+4], uint32(exit))
			binary.BigEndian.PutUint32(getBuf()[jmpAt:jmpAt+4], uint32(start))
		}
	case "#:":
		c.quotationStack = c.quotationStack[:len(c.quotationStack)-1]

		// At runtime: [ ... addr count ]
		c.emit(vm.OpPushR) // count to R
		c.emit(vm.OpPushR) // addr to R

		start := c.currentAddress()

		c.emit(vm.OpPopR) // addr
		c.emit(vm.OpPopR) // count

		// Terminate if count <= 0
		c.emit(vm.OpDup)
		c.emit(vm.OpPush)
		c.emit(vm.EncodeInt32(0)...)
		c.emit(vm.OpGt, vm.OpJz) // If !(count > 0), jump to exit
		jzAt := c.currentOffset()
		c.emit(0, 0, 0, 0)

		c.emit(vm.OpDec)       // count--
		c.emit(vm.OpPushR)     // count-1 back to R
		c.emit(vm.OpDup)       // addr copy
		c.emit(vm.OpPushR)     // addr back to R
		c.emit(vm.OpCallStack) // calls addr

		c.emit(vm.OpJmp)
		jmpAt := c.currentOffset()
		c.emit(0, 0, 0, 0)

		exit := c.currentAddress()
		c.emit(vm.OpPop) // pops extra count (0)
		c.emit(vm.OpPop) // pops extra addr

		if c.activeQuotIdx >= 0 {
			c.quotations[c.activeQuotIdx].InternalJumps = append(c.quotations[c.activeQuotIdx].InternalJumps, InternalJump{jzAt, exit}, InternalJump{jmpAt, start})
		} else {
			binary.BigEndian.PutUint32(getBuf()[jzAt:jzAt+4], uint32(exit))
			binary.BigEndian.PutUint32(getBuf()[jmpAt:jmpAt+4], uint32(start))
		}
	case "DIP":
		c.quotationStack = c.quotationStack[:len(c.quotationStack)-1]
		t, _ := c.allocTemp(4)
		c.emit(vm.OpSwap, vm.OpPush)
		c.emit(vm.EncodeInt32(t)...)
		c.emit(vm.OpStoreI)
		c.emit(vm.OpCallStack)
		c.emit(vm.OpPush)
		c.emit(vm.EncodeInt32(t)...)
		c.emit(vm.OpLoadI)
	case "KEEP":
		c.quotationStack = c.quotationStack[:len(c.quotationStack)-1]
		t, _ := c.allocTemp(4)
		c.emit(vm.OpSwap, vm.OpPush)
		c.emit(vm.EncodeInt32(t)...)
		c.emit(vm.OpStoreI)
		c.emit(vm.OpPush)
		c.emit(vm.EncodeInt32(t)...)
		c.emit(vm.OpLoadI)
		c.emit(vm.OpSwap, vm.OpCallStack, vm.OpPush)
		c.emit(vm.EncodeInt32(t)...)
		c.emit(vm.OpLoadI)
	}
	return nil
}

func (c *Compiler) resolveWord(name string) (Word, bool) {
	upper := strings.ToUpper(name)
	if w, ok := c.dictionary[upper]; ok {
		return w, true
	}
	if c.currentModule != "" && !strings.Contains(upper, "::") {
		if w, ok := c.dictionary[c.currentModule+"::"+upper]; ok {
			return w, true
		}
	}
	parts := strings.Split(upper, "::")
	if len(parts) == 2 {
		if mod, ok := c.imports[parts[0]]; ok {
			if w, ok := c.dictionary[mod+"::"+parts[1]]; ok {
				return w, true
			}
		}
	}
	return Word{}, false
}

func (c *Compiler) ParseNumber(s string) (int32, error) {
	if strings.HasPrefix(s, "0x") || strings.HasPrefix(s, "0X") {
		v, _ := strconv.ParseUint(s[2:], 16, 32)
		return int32(v), nil
	}
	v, err := strconv.ParseInt(s, 10, 32)
	return int32(v), err
}

func (c *Compiler) handleModuleDirective() error {
	t := c.advance()
	if t.Type != TokenWord {
		return fmt.Errorf("expected module name")
	}
	name := strings.ToUpper(t.Value)
	if name == "GLOBAL" {
		c.currentModule = ""
	} else {
		c.currentModule = name
	}
	return nil
}

func (c *Compiler) handleImportDirective() error {
	t := c.advance()
	if t.Type != TokenWord {
		return fmt.Errorf("expected module name")
	}
	mod := strings.ToUpper(t.Value)
	alias := mod
	if strings.ToUpper(c.peek().Value) == "AS" {
		c.advance()
		t = c.advance()
		if t.Type != TokenWord {
			return fmt.Errorf("expected shorthand name")
		}
		alias = strings.ToUpper(t.Value)
	}
	c.imports[alias] = mod
	return nil
}

func (c *Compiler) handleIncludeDirective() error {
	if c.includeDepth > 10 {
		return fmt.Errorf("include depth exceeded")
	}
	c.includeDepth++
	defer func() { c.includeDepth-- }()
	t := c.advance()
	if c.trace {
		fmt.Fprintf(os.Stderr, "Lexer: handleIncludeDirective: token type=%d, value=%s\n", t.Type, t.Value)
	}
	if t.Type != TokenWord && t.Type != TokenString {
		return fmt.Errorf("expected file path, got type %d", t.Type)
	}

	path := t.Value
	if c.includedFiles[path] {
		// Already included, remove the INCLUDE token and path from the stream
		c.tokens = append(c.tokens[:c.pos-2], c.tokens[c.pos:]...)
		c.pos -= 2
		return nil
	}
	c.includedFiles[path] = true

	data, err := os.ReadFile(path)
	if err != nil {
		return fmt.Errorf("include failed: %v", err)
	}
	l := NewLexer(string(data), c.trace)
	tokens, err := l.Tokenize()
	if err != nil {
		return err
	}
	if len(tokens) > 0 && tokens[len(tokens)-1].Type == TokenEOF {
		tokens = tokens[:len(tokens)-1]
	}
	restore := c.currentModule
	if restore == "" {
		restore = "GLOBAL"
	}
	tokens = append(tokens, Token{TokenWord, "MODULE", 0, 0}, Token{TokenWord, restore, 0, 0})

	newTokens := make([]Token, 0, len(c.tokens)+len(tokens))
	newTokens = append(newTokens, c.tokens[:c.pos-2]...)
	newTokens = append(newTokens, tokens...)
	newTokens = append(newTokens, c.tokens[c.pos:]...)
	c.tokens = newTokens
	c.pos -= 2
	return nil
}
func (c *Compiler) allocTemp(size int32) (int32, error) {
	addr := c.tempAlloc
	c.tempAlloc += size
	return addr, nil
}

func (c *Compiler) emitFileString(s string) {
	addr := c.nextStringHeap
	aligned := (int32(len(s)) + 4) & ^3
	c.nextStringHeap += aligned
	for i := int32(0); i < aligned/4; i++ {
		var chunk int32
		for j := 0; j < 4; j++ {
			p := i*4 + int32(j)
			b := byte(0)
			if p < int32(len(s)) {
				b = s[p]
			}
			chunk = (chunk << 8) | int32(b)
		}
		c.emit(vm.OpPush)
		c.emit(vm.EncodeInt32(chunk)...)
		c.emit(vm.OpPush)
		off := c.currentOffset()
		c.stringPatches = append(c.stringPatches, StringPatch{Offset: off, QuotIdx: c.activeQuotIdx, TempAddr: addr + i*4})
		c.emit(0, 0, 0, 0) // placeholder for address
		c.emit(vm.OpStoreI)
	}
	c.emit(vm.OpPush)
	off := c.currentOffset()
	c.stringPatches = append(c.stringPatches, StringPatch{Offset: off, QuotIdx: c.activeQuotIdx, TempAddr: addr})
	c.emit(0, 0, 0, 0) // placeholder for start address
}
