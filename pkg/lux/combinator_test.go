package lux

import (
	"testing"
	"github.com/rmay/nuxvm/pkg/vm"
)

func TestCombinatorIf(t *testing.T) {
	tests := []struct {
		name     string
		source   string
		expected []int32
	}{
		{"if true", `@test 1 [ 42 ] ? 99 ; test`, []int32{42, 99}},
		{"if false", `@test 0 [ 42 ] ? 99 ; test`, []int32{99}},
		{"if true tail", `@test 1 [ 42 ] ? ; test`, []int32{42}},
		{"if false tail", `@test 0 [ 42 ] ? ; test`, []int32{}},
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
				t.Errorf("Expected stack %v, got %v", tt.expected, stack)
				return
			}
			for i := range stack {
				if stack[i] != tt.expected[i] {
					t.Errorf("Expected stack %v, got %v", tt.expected, stack)
					break
				}
			}
		})
	}
}

func TestCombinatorIfElse(t *testing.T) {
	tests := []struct {
		name     string
		source   string
		expected []int32
	}{
		{"if/else true", `@test 1 [ 1 ] [ 2 ] ?: 99 ; test`, []int32{1, 99}},
		{"if/else false", `@test 0 [ 1 ] [ 2 ] ?: 99 ; test`, []int32{2, 99}},
		{"if/else true tail", `@test 1 [ 1 ] [ 2 ] ?: ; test`, []int32{1}},
		{"if/else false tail", `@test 0 [ 1 ] [ 2 ] ?: ; test`, []int32{2}},
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
				t.Errorf("Expected stack %v, got %v", tt.expected, stack)
				return
			}
			for i := range stack {
				if stack[i] != tt.expected[i] {
					t.Errorf("Expected stack %v, got %v", tt.expected, stack)
					break
				}
			}
		})
	}
}

func TestCombinatorNested(t *testing.T) {
	// Nested loop and if: 
    // Push 42 five times using nested ?: and |:
	source := `
@test 0 5 [ dup 0 > ] [
    1 [ 42 ] [ 0 ] ?:
    ROT + SWAP
    1 -
] |: drop ;
test
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
	expected := []int32{210} // 42 * 5
	if len(stack) != 1 || stack[0] != 210 {
		t.Errorf("Expected %v, got %v", expected, stack)
	}
}

func TestCombinatorDeepNesting(t *testing.T) {
    // REAL Nested loops
    source := `
@test 0 3 [ dup 0 > ] [
    1 -
    SWAP
    2 [ dup 0 > ] [ 1 - SWAP 5 + SWAP ] |: drop
    SWAP
] |: drop ;
test
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
	if len(stack) != 1 || stack[0] != 30 {
		t.Errorf("Expected [30], got %v", stack)
	}
}
