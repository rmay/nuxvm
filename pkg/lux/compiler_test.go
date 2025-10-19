// pkg/lux/compiler_test.go
package lux

import (
	"testing"

	"github.com/rmay/nuxvm/pkg/vm"
)

// ==========================================
// BASIC COMPILATION TESTS
// ==========================================

func TestCompileEmptyProgram(t *testing.T) {
	source := ""
	bytecode, err := Compile(source)
	if err != nil {
		t.Fatalf("Compile error: %v", err)
	}
	// Minimum: JMP (5 bytes) + HALT (1 byte)
	if len(bytecode) < 6 {
		t.Errorf("Expected minimum bytecode length >= 6, got %d", len(bytecode))
	}
	// Run to ensure no runtime errors
	machine := vm.NewVM(bytecode)
	if err := machine.Run(); err != nil {
		t.Fatalf("Runtime error: %v", err)
	}
	if len(machine.Stack()) != 0 {
		t.Errorf("Expected empty stack, got %v", machine.Stack())
	}
}

func TestCompileOnlyComments(t *testing.T) {
	source := "( this is a comment ) // another comment"
	bytecode, err := Compile(source)
	if err != nil {
		t.Fatalf("Compile error: %v", err)
	}
	if len(bytecode) < 6 {
		t.Errorf("Expected minimum bytecode length, got %d", len(bytecode))
	}
}

func TestCompileWhitespace(t *testing.T) {
	source := "   \n\t  \n  "
	bytecode, err := Compile(source)
	if err != nil {
		t.Fatalf("Compile error: %v", err)
	}
	if len(bytecode) < 6 {
		t.Errorf("Expected minimum bytecode length, got %d", len(bytecode))
	}
}

// ==========================================
// NUMBER COMPILATION
// ==========================================

func TestCompilePositiveNumber(t *testing.T) {
	source := "42"
	bytecode, err := Compile(source)
	if err != nil {
		t.Fatalf("Compile error: %v", err)
	}

	machine := vm.NewVM(bytecode)
	if err := machine.Run(); err != nil {
		t.Fatalf("Runtime error: %v", err)
	}

	stack := machine.Stack()
	if len(stack) != 1 || stack[0] != 42 {
		t.Errorf("Expected [42], got %v", stack)
	}
}

func TestCompileNegativeNumber(t *testing.T) {
	source := "-42"
	bytecode, err := Compile(source)
	if err != nil {
		t.Fatalf("Compile error: %v", err)
	}

	machine := vm.NewVM(bytecode)
	if err := machine.Run(); err != nil {
		t.Fatalf("Runtime error: %v", err)
	}

	stack := machine.Stack()
	if len(stack) != 1 || stack[0] != -42 {
		t.Errorf("Expected [-42], got %v", stack)
	}
}

func TestCompileHexNumber(t *testing.T) {
	tests := []struct {
		source   string
		expected int32
	}{
		{"0xFF", 255},
		{"0x10", 16},
		{"0x0", 0},
		{"0x7FFFFFFF", 2147483647}, // Max int32
	}

	for _, tt := range tests {
		t.Run(tt.source, func(t *testing.T) {
			bytecode, err := Compile(tt.source)
			if err != nil {
				t.Fatalf("Compile error: %v", err)
			}

			machine := vm.NewVM(bytecode)
			if err := machine.Run(); err != nil {
				t.Fatalf("Runtime error: %v", err)
			}

			stack := machine.Stack()
			if len(stack) != 1 || stack[0] != tt.expected {
				t.Errorf("Expected [%d], got %v", tt.expected, stack)
			}
		})
	}
}

func TestCompileMultipleNumbers(t *testing.T) {
	source := "1 2 3 4 5"
	bytecode, err := Compile(source)
	if err != nil {
		t.Fatalf("Compile error: %v", err)
	}

	machine := vm.NewVM(bytecode)
	if err := machine.Run(); err != nil {
		t.Fatalf("Runtime error: %v", err)
	}

	stack := machine.Stack()
	expected := []int32{1, 2, 3, 4, 5}
	if len(stack) != len(expected) {
		t.Fatalf("Expected stack length %d, got %d", len(expected), len(stack))
	}
	for i, v := range expected {
		if stack[i] != v {
			t.Errorf("Position %d: expected %d, got %d", i, v, stack[i])
		}
	}
}

// ==========================================
// STRING COMPILATION
// ==========================================

func TestCompileEmptyString(t *testing.T) {
	source := `""`
	bytecode, err := Compile(source)
	if err != nil {
		t.Fatalf("Compile error: %v", err)
	}
	// Should compile without error
	if len(bytecode) < 6 {
		t.Errorf("Expected bytecode, got length %d", len(bytecode))
	}
}

func TestCompileSimpleString(t *testing.T) {
	source := `"Hi"`
	bytecode, err := Compile(source)
	if err != nil {
		t.Fatalf("Compile error: %v", err)
	}
	// Should have bytecode for 'H' and 'i'
	if len(bytecode) < 20 {
		t.Errorf("Expected bytecode for string, got length %d", len(bytecode))
	}
}

func TestCompileStringWithEscapes(t *testing.T) {
	source := `"Hello\nWorld\t!"`
	bytecode, err := Compile(source)
	if err != nil {
		t.Fatalf("Compile error: %v", err)
	}
	if len(bytecode) < 20 {
		t.Errorf("Expected bytecode for string, got length %d", len(bytecode))
	}
}

func TestCompileMultipleStrings(t *testing.T) {
	source := `"Hello" "World"`
	bytecode, err := Compile(source)
	if err != nil {
		t.Fatalf("Compile error: %v", err)
	}
	if len(bytecode) < 40 {
		t.Errorf("Expected bytecode for strings, got length %d", len(bytecode))
	}
}

// ==========================================
// STACK OPERATIONS
// ==========================================

func TestCompileAllStackOps(t *testing.T) {
	tests := []struct {
		name     string
		source   string
		expected []int32
	}{
		{"DUP", "5 DUP", []int32{5, 5}},
		{"DROP", "5 10 DROP", []int32{5}},
		{"SWAP", "5 10 SWAP", []int32{10, 5}},
		{"ROLL", "5 10 ROLL", []int32{5, 10, 5}},
		{"ROT", "1 2 3 ROT", []int32{2, 3, 1}},
	}

	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			bytecode, err := Compile(tt.source)
			if err != nil {
				t.Fatalf("Compile error: %v", err)
			}

			machine := vm.NewVM(bytecode)
			if err := machine.Run(); err != nil {
				t.Fatalf("Runtime error: %v", err)
			}

			stack := machine.Stack()
			if len(stack) != len(tt.expected) {
				t.Fatalf("Expected stack length %d, got %d", len(tt.expected), len(stack))
			}
			for i, v := range tt.expected {
				if stack[i] != v {
					t.Errorf("Position %d: expected %d, got %d", i, v, stack[i])
				}
			}
		})
	}
}

// ==========================================
// ARITHMETIC OPERATIONS
// ==========================================

func TestCompileAllArithmetic(t *testing.T) {
	tests := []struct {
		name     string
		source   string
		expected int32
	}{
		{"ADD", "5 10 +", 15},
		{"SUB", "10 3 -", 7},
		{"MUL", "5 7 *", 35},
		{"DIV", "20 4 /", 5},
		{"MOD", "17 5 MOD", 2},
		{"INC", "41 INC", 42},
		{"DEC", "43 DEC", 42},
		{"NEGATE", "42 NEGATE", -42},
	}

	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			bytecode, err := Compile(tt.source)
			if err != nil {
				t.Fatalf("Compile error: %v", err)
			}

			machine := vm.NewVM(bytecode)
			if err := machine.Run(); err != nil {
				t.Fatalf("Runtime error: %v", err)
			}

			stack := machine.Stack()
			if len(stack) != 1 || stack[0] != tt.expected {
				t.Errorf("Expected [%d], got %v", tt.expected, stack)
			}
		})
	}
}

// ==========================================
// BITWISE OPERATIONS
// ==========================================

func TestCompileAllBitwise(t *testing.T) {
	tests := []struct {
		name     string
		source   string
		expected int32
	}{
		{"AND", "12 10 AND", 8},     // 0b1100 & 0b1010 = 0b1000
		{"OR", "12 10 OR", 14},      // 0b1100 | 0b1010 = 0b1110
		{"XOR", "12 10 XOR", 6},     // 0b1100 ^ 0b1010 = 0b0110
		{"NOT", "0 NOT", -1},        // ~0 = -1 (two's complement)
		{"LSHIFT", "1 2 LSHIFT", 4}, // 1 << 2 = 4
	}

	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			bytecode, err := Compile(tt.source)
			if err != nil {
				t.Fatalf("Compile error: %v", err)
			}

			machine := vm.NewVM(bytecode)
			if err := machine.Run(); err != nil {
				t.Fatalf("Runtime error: %v", err)
			}

			stack := machine.Stack()
			if len(stack) != 1 || stack[0] != tt.expected {
				t.Errorf("Expected [%d], got %v", tt.expected, stack)
			}
		})
	}
}

// ==========================================
// CONTROL FLOW
// ==========================================
func TestCompileIfElse(t *testing.T) {
	tests := []struct {
		name     string
		source   string
		expected int32
	}{
		{"True branch", "1 [ 42 ] [ 99 ] ?:", 42},
		{"False branch", "0 [ 42 ] [ 99 ] ?:", 99},
	}

	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			t.Log("Hold on...")
			bytecode, err := Compile(tt.source)
			if err != nil {
				t.Fatalf("Compile error: %v", err)
			}

			// Log bytecode for debugging
			t.Logf("Bytecode (length %d): %v", len(bytecode), bytecode)

			machine := vm.NewVM(bytecode)
			t.Logf("Initial VM state:\n%s", machine.DebugInfo())

			if err := machine.Run(); err != nil {
				t.Fatalf("Runtime error: %v\nFinal VM state:\n%s", err, machine.DebugInfo())
			}

			stack := machine.Stack()
			t.Logf("Final VM state:\n%s", machine.DebugInfo())
			if len(stack) != 1 || stack[0] != tt.expected {
				t.Errorf("Expected [%d], got %v\nFinal VM state:\n%s", tt.expected, stack, machine.DebugInfo())
			}
		})
	}
}
func TestCompileIf(t *testing.T) {
	tests := []struct {
		name     string
		source   string
		expected []int32
	}{
		{"True branch", "1 [ 42 ] ?", []int32{42}},
		{"False branch", "0 [ 42 ] ?", []int32{}},
	}

	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			bytecode, err := Compile(tt.source)
			if err != nil {
				t.Fatalf("Compile error: %v", err)
			}

			machine := vm.NewVM(bytecode)
			if err := machine.Run(); err != nil {
				t.Fatalf("Runtime error: %v", err)
			}

			stack := machine.Stack()
			if len(stack) != len(tt.expected) {
				t.Fatalf("Expected stack length %d, got %d", len(tt.expected), len(stack))
			}
			for i, v := range tt.expected {
				if stack[i] != v {
					t.Errorf("Position %d: expected %d, got %d", i, v, stack[i])
				}
			}
		})
	}
}

func TestCompileUnless(t *testing.T) {
	tests := []struct {
		name     string
		source   string
		expected []int32
	}{
		{"False branch", "0 [ 42 ] !:", []int32{42}},
		{"True branch", "1 [ 42 ] !:", []int32{}},
	}

	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			bytecode, err := Compile(tt.source)
			if err != nil {
				t.Fatalf("Compile error: %v", err)
			}

			machine := vm.NewVM(bytecode)
			if err := machine.Run(); err != nil {
				t.Fatalf("Runtime error: %v", err)
			}

			stack := machine.Stack()
			if len(stack) != len(tt.expected) {
				t.Fatalf("Expected stack length %d, got %d", len(tt.expected), len(stack))
			}
			for i, v := range tt.expected {
				if stack[i] != v {
					t.Errorf("Position %d: expected %d, got %d", i, v, stack[i])
				}
			}
		})
	}
}

func TestCompileWhile(t *testing.T) {
	source := `
		5 [ 0 > ] [ 1 - dup ] |:
	`
	bytecode, err := Compile(source)
	if err != nil {
		t.Fatalf("Compile error: %v", err)
	}

	machine := vm.NewVM(bytecode)
	if err := machine.Run(); err != nil {
		t.Fatalf("Runtime error: %v", err)
	}

	stack := machine.Stack()
	if len(stack) != 6 || stack[0] != 4 || stack[1] != 3 || stack[2] != 2 || stack[3] != 1 ||
		stack[4] != 0 || stack[5] != 0 {
		t.Errorf("Expected [4 3 2 1 0 0], got %v", stack)
	}
}

func TestCompileTimes(t *testing.T) {
	source := `
		0
		[ 1 + ] 5 #:
	`
	bytecode, err := Compile(source)
	if err != nil {
		t.Fatalf("Compile error: %v", err)
	}

	machine := vm.NewVM(bytecode)
	if err := machine.Run(); err != nil {
		t.Fatalf("Runtime error: %v", err)
	}

	stack := machine.Stack()
	if len(stack) != 1 || stack[0] != 5 {
		t.Errorf("Expected [5], got %v", stack)
	}
}

func TestCompileDip(t *testing.T) {
	source := `
		5
		[ 1 + ] dip
	`
	bytecode, err := Compile(source)
	if err != nil {
		t.Fatalf("Compile error: %v", err)
	}

	machine := vm.NewVM(bytecode)
	if err := machine.Run(); err != nil {
		t.Fatalf("Runtime error: %v", err)
	}

	stack := machine.Stack()
	if len(stack) != 1 || stack[0] != 6 {
		t.Errorf("Expected [6], got %v", stack)
	}
}

func TestCompileNestedDip(t *testing.T) {
	source := `
		[ 5 [ 1 + ] dip ] dip
	`
	bytecode, err := Compile(source)
	if err != nil {
		t.Fatalf("Compile error: %v", err)
	}
	machine := vm.NewVM(bytecode)
	if err := machine.Run(); err != nil {
		t.Fatalf("Runtime error: %v", err)
	}
	stack := machine.Stack()
	if len(stack) != 1 || stack[0] != 6 {
		t.Errorf("Expected [6], got %v", stack)
	}
}

func TestCompileKeep(t *testing.T) {
	source := `
		5 [ 1 + ] keep
	`
	bytecode, err := Compile(source)
	if err != nil {
		t.Fatalf("Compile error: %v", err)
	}

	machine := vm.NewVM(bytecode)
	if err := machine.Run(); err != nil {
		t.Fatalf("Runtime error: %v", err)
	}

	stack := machine.Stack()
	if len(stack) != 2 || stack[0] != 5 || stack[1] != 6 {
		t.Errorf("Expected [5, 6], got %v", stack)
	}
}

// ==========================================
// MODULES AND IMPORTS
// ==========================================

func TestCompileModuleDefinition(t *testing.T) {
	source := `
		MODULE MATH
		@SQUARE dup * ;
		5 MATH::SQUARE
	`
	bytecode, err := Compile(source)
	if err != nil {
		t.Fatalf("Compile error: %v", err)
	}

	machine := vm.NewVM(bytecode)
	if err := machine.Run(); err != nil {
		t.Fatalf("Runtime error: %v", err)
	}

	stack := machine.Stack()
	if len(stack) != 1 || stack[0] != 25 {
		t.Errorf("Expected [25], got %v", stack)
	}
}

func TestCompileImport(t *testing.T) {
	source := `
		MODULE MATH
		@SQUARE dup * ;
		MODULE MAIN
		IMPORT MATH AS M
		5 M::SQUARE
	`
	bytecode, err := Compile(source)
	if err != nil {
		t.Fatalf("Compile error: %v", err)
	}

	machine := vm.NewVM(bytecode)
	if err := machine.Run(); err != nil {
		t.Fatalf("Runtime error: %v", err)
	}

	stack := machine.Stack()
	if len(stack) != 1 || stack[0] != 25 {
		t.Errorf("Expected [25], got %v", stack)
	}
}

func TestCompileImportWithAlias(t *testing.T) {
	source := `
		MODULE MATH
		@SQUARE dup * ;
		MODULE MAIN
		IMPORT MATH AS M
		5 M::SQUARE
	`
	bytecode, err := Compile(source)
	if err != nil {
		t.Fatalf("Compile error: %v", err)
	}

	machine := vm.NewVM(bytecode)
	if err := machine.Run(); err != nil {
		t.Fatalf("Runtime error: %v", err)
	}

	stack := machine.Stack()
	if len(stack) != 1 || stack[0] != 25 {
		t.Errorf("Expected [25], got %v", stack)
	}
}

func TestCompileMultipleWordsInModule(t *testing.T) {
	source := `
		MODULE MATH
		@inc1 1 + ;
		@inc2 2 + ;
		@inc3 3 + ;
		10 MATH::inc1 MATH::inc2 MATH::inc3
	`
	bytecode, err := Compile(source)
	if err != nil {
		t.Fatalf("Compile error: %v", err)
	}

	machine := vm.NewVM(bytecode)
	if err := machine.Run(); err != nil {
		t.Fatalf("Runtime error: %v", err)
	}

	stack := machine.Stack()
	if len(stack) != 1 || stack[0] != 16 { // 10+1+2+3
		t.Errorf("Expected [16], got %v", stack)
	}
}

// ==========================================
// EDGE CASES
// ==========================================

func TestCompileCaseSensitivity(t *testing.T) {
	// Words should be case-insensitive
	tests := []string{
		"5 dup +",
		"5 DUP +",
		"5 Dup +",
		"5 DuP +",
	}

	for _, source := range tests {
		t.Run(source, func(t *testing.T) {
			bytecode, err := Compile(source)
			if err != nil {
				t.Fatalf("Compile error: %v", err)
			}

			machine := vm.NewVM(bytecode)
			if err := machine.Run(); err != nil {
				t.Fatalf("Runtime error: %v", err)
			}

			stack := machine.Stack()
			if len(stack) != 1 || stack[0] != 10 {
				t.Errorf("Expected [10], got %v", stack)
			}
		})
	}
}

func TestCompileWordDefinitionCaseSensitivity(t *testing.T) {
	source := `
		@MyWord 42 ;
		myword
	`
	bytecode, err := Compile(source)
	if err != nil {
		t.Fatalf("Compile error: %v", err)
	}

	machine := vm.NewVM(bytecode)
	if err := machine.Run(); err != nil {
		t.Fatalf("Runtime error: %v", err)
	}

	stack := machine.Stack()
	if len(stack) != 1 || stack[0] != 42 {
		t.Errorf("Expected [42], got %v", stack)
	}
}

func TestCompileLargeNumbers(t *testing.T) {
	tests := []struct {
		source   string
		expected int32
	}{
		{"2147483647", 2147483647},   // Max int32
		{"-2147483648", -2147483648}, // Min int32
		{"0x7FFFFFFF", 2147483647},   // Max int32 in hex
	}

	for _, tt := range tests {
		t.Run(tt.source, func(t *testing.T) {
			bytecode, err := Compile(tt.source)
			if err != nil {
				t.Fatalf("Compile error: %v", err)
			}

			machine := vm.NewVM(bytecode)
			if err := machine.Run(); err != nil {
				t.Fatalf("Runtime error: %v", err)
			}

			stack := machine.Stack()
			if len(stack) != 1 || stack[0] != tt.expected {
				t.Errorf("Expected [%d], got %v", tt.expected, stack)
			}
		})
	}
}

func TestCompileManyWords(t *testing.T) {
	// Test with many word definitions
	source := "@w1 1 + ; @w2 2 + ; @w3 3 + ; @w4 4 + ; @w5 5 + ; 10 w1 w2 w3 w4 w5"
	bytecode, err := Compile(source)
	if err != nil {
		t.Fatalf("Compile error: %v", err)
	}

	machine := vm.NewVM(bytecode)
	if err := machine.Run(); err != nil {
		t.Fatalf("Runtime error: %v", err)
	}

	stack := machine.Stack()
	if len(stack) != 1 || stack[0] != 25 { // 10+1+2+3+4+5
		t.Errorf("Expected [25], got %v", stack)
	}
}

func TestCompileStackDeep(t *testing.T) {
	// Build up a deep stack
	source := "1 2 3 4 5 6 7 8 9 10"
	bytecode, err := Compile(source)
	if err != nil {
		t.Fatalf("Compile error: %v", err)
	}

	machine := vm.NewVM(bytecode)
	if err := machine.Run(); err != nil {
		t.Fatalf("Runtime error: %v", err)
	}

	stack := machine.Stack()
	if len(stack) != 10 {
		t.Fatalf("Expected stack length 10, got %d", len(stack))
	}
	for i := 0; i < 10; i++ {
		if stack[i] != int32(i+1) {
			t.Errorf("Position %d: expected %d, got %d", i, i+1, stack[i])
		}
	}
}

// ==========================================
// HELPER METHOD COVERAGE
// ==========================================

func TestHelperMethods(t *testing.T) {
	compiler := &Compiler{
		tokens: []Token{
			{Type: TokenNumber, Value: "42"},
			{Type: TokenWord, Value: "+"},
			{Type: TokenEOF, Value: ""},
		},
		pos:        0,
		bytecode:   []byte{},
		dictionary: make(map[string]Word),
		baseAddr:   vm.UserMemoryOffset,
	}

	// Test peek
	token := compiler.peek()
	if token.Type != TokenNumber {
		t.Errorf("Expected TokenNumber, got %v", token.Type)
	}

	// Test advance
	token = compiler.advance()
	if token.Type != TokenNumber {
		t.Errorf("Expected TokenNumber, got %v", token.Type)
	}
	if compiler.pos != 1 {
		t.Errorf("Expected pos=1, got %d", compiler.pos)
	}

	// Test emit
	compiler.emit(0x01, 0x02, 0x03)
	if len(compiler.bytecode) != 3 {
		t.Errorf("Expected bytecode length 3, got %d", len(compiler.bytecode))
	}

	// Test currentAddress
	addr := compiler.currentAddress()
	if addr != vm.UserMemoryOffset+3 {
		t.Errorf("Expected address %d, got %d", vm.UserMemoryOffset+3, addr)
	}
}

func TestResolveWordAllPaths(t *testing.T) {
	compiler := &Compiler{
		tokens:        []Token{},
		pos:           0,
		bytecode:      []byte{},
		dictionary:    make(map[string]Word),
		quotations:    []Quotation{},
		currentModule: "TEST",
		imports:       make(map[string]string),
		baseAddr:      vm.UserMemoryOffset,
	}

	// Add test words
	compiler.dictionary["EXACT"] = Word{Name: "EXACT", Address: 100}
	compiler.dictionary["TEST::LOCAL"] = Word{Name: "TEST::LOCAL", Address: 200}
	compiler.dictionary["OTHER::REMOTE"] = Word{Name: "OTHER::REMOTE", Address: 300}
	compiler.imports["O"] = "OTHER"

	// Test exact match
	word, found := compiler.resolveWord("EXACT")
	if !found || word.Address != 100 {
		t.Errorf("Failed to resolve exact match")
	}

	// Test current module prefix
	word, found = compiler.resolveWord("LOCAL")
	if !found || word.Address != 200 {
		t.Errorf("Failed to resolve with module prefix")
	}

	// Test import resolution
	word, found = compiler.resolveWord("O::REMOTE")
	if !found || word.Address != 300 {
		t.Errorf("Failed to resolve with import alias")
	}

	// Test not found
	_, found = compiler.resolveWord("NOTFOUND")
	if found {
		t.Error("Should not have found non-existent word")
	}
}

// ==========================================
// REGRESSION TESTS
// ==========================================

func TestRegressionEmptyDefinition(t *testing.T) {
	source := "@empty ;"
	bytecode, err := Compile(source)
	if err != nil {
		t.Fatalf("Compile error: %v", err)
	}
	// Empty definition should compile without error
	if len(bytecode) < 6 {
		t.Errorf("Expected bytecode, got length %d", len(bytecode))
	}
}

func TestRegressionMultipleModuleSwitches(t *testing.T) {
	source := `
		MODULE A
		@foo 1 + ;
		MODULE B
		@bar 2 + ;
		MODULE A
		@baz 3 + ;
		10 A::FOO A::BAZ B::BAR
	`
	bytecode, err := Compile(source)
	if err != nil {
		t.Fatalf("Compile error: %v", err)
	}

	machine := vm.NewVM(bytecode)
	if err := machine.Run(); err != nil {
		t.Fatalf("Runtime error: %v", err)
	}

	stack := machine.Stack()
	// 10+1 = 11, 11+3 = 14, 14+2 = 16
	if len(stack) != 1 || stack[0] != 16 {
		t.Errorf("Expected [16], got %v", stack)
	}
}

func TestRegressionQuotationInDefinition(t *testing.T) {
	source := `
		@makequot [ 42 ] ;
		makequot
	`
	bytecode, err := Compile(source)
	if err != nil {
		t.Fatalf("Compile error: %v", err)
	}
	// Should compile without error
	if len(bytecode) < 10 {
		t.Errorf("Expected bytecode, got length %d", len(bytecode))
	}
}
