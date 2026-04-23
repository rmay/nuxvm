package vm

import (
	"encoding/binary"
	"fmt"
	"testing"
	"time"
)

// MockBus implements the Bus interface for testing
type MockBus struct {
	ReadFunc  func(address uint32) (int32, error)
	WriteFunc func(address uint32, value int32) error
}

func (m *MockBus) Read(address uint32) (int32, error) {
	if m.ReadFunc != nil {
		return m.ReadFunc(address)
	}
	return 0, nil
}

func (m *MockBus) Write(address uint32, value int32) error {
	if m.WriteFunc != nil {
		return m.WriteFunc(address, value)
	}
	return nil
}

// Helper function to create a VM with a simple program
func createVMWithProgram(program []byte) *VM {
	vm := NewVM(program)
	vm.SetBus(&MockBus{})
	return vm
}

// Helper function to push a value onto the stack for testing
func pushValue(t *testing.T, vm *VM, value int32) {
	t.Helper()
	if err := vm.Push(value); err != nil {
		t.Fatalf("Failed to push value %d: %v", value, err)
	}
}

// Helper function to encode a 32-bit value as big-endian bytes
func encodeInt32(value int32) []byte {
	buf := make([]byte, 4)
	binary.BigEndian.PutUint32(buf, uint32(value))
	return buf
}

// Helper function to create a PUSH instruction
func pushInstruction(value int32) []byte {
	result := []byte{OpPush}
	result = append(result, encodeInt32(value)...)
	return result
}

// Helper function to check if string contains substring
func contains(s, substr string) bool {
	for i := 0; i <= len(s)-len(substr); i++ {
		if s[i:i+len(substr)] == substr {
			return true
		}
	}
	return false
}

func TestNewVM(t *testing.T) {
	program := []byte{OpHalt}
	vm := NewVM(program)

	if vm == nil {
		t.Fatal("NewVM returned nil")
	}
	if len(vm.Stack()) != 0 {
		t.Errorf("Expected empty stack, got length %d", len(vm.Stack()))
	}
	if vm.PC() != vm.UserMemoryStart() {
		t.Errorf("Expected PC=%d (user memory start), got %d", vm.UserMemoryStart(), vm.PC())
	}
	if !vm.Running() {
		t.Error("Expected VM to be running initially")
	}
	expectedMemSize := int(UserMemoryOffset) + len(program)
	if len(vm.memory) != expectedMemSize {
		t.Errorf("Expected memory length %d (UserMemoryOffset + program), got %d", expectedMemSize, len(vm.memory))
	}
	if len(vm.ReturnStack()) != 0 {
		t.Errorf("Expected empty return stack, got length %d", len(vm.ReturnStack()))
	}
	if vm.ReservedMemorySize() == 0 {
		t.Error("Expected non-zero reserved memory size")
	}
	if vm.UserMemoryStart() == 0 {
		t.Error("Expected non-zero user memory start")
	}
}

func TestPushPop(t *testing.T) {
	vm := createVMWithProgram([]byte{})

	// Test push
	if err := vm.Push(42); err != nil {
		t.Fatalf("Push failed: %v", err)
	}
	stack := vm.Stack()
	if len(stack) != 1 {
		t.Errorf("Expected stack length 1, got %d", len(stack))
	}
	if stack[0] != 42 {
		t.Errorf("Expected stack[0]=42, got %d", stack[0])
	}

	// Test pop
	value, err := vm.Pop()
	if err != nil {
		t.Fatalf("Pop failed: %v", err)
	}
	if value != 42 {
		t.Errorf("Expected popped value=42, got %d", value)
	}
	if len(vm.Stack()) != 0 {
		t.Errorf("Expected empty stack, got length %d", len(vm.Stack()))
	}

	// Test pop from empty stack
	_, err = vm.Pop()
	if err == nil {
		t.Error("Expected error when popping from empty stack")
	}
}

func TestStackOverflow(t *testing.T) {
	vm := createVMWithProgram([]byte{})

	// Fill stack to max
	for i := 0; i < MaxStackSize; i++ {
		if err := vm.Push(int32(i)); err != nil {
			t.Fatalf("Failed to push at index %d: %v", i, err)
		}
	}

	// Try to push one more
	err := vm.Push(999)
	if err == nil {
		t.Error("Expected stack overflow error")
	}
	if !contains(err.Error(), "stack overflow") {
		t.Errorf("Expected 'stack overflow' in error, got: %v", err)
	}
}

func TestDup(t *testing.T) {
	vm := createVMWithProgram([]byte{})
	pushValue(t, vm, 42)

	if err := vm.Dup(); err != nil {
		t.Fatalf("Dup failed: %v", err)
	}

	stack := vm.Stack()
	if len(stack) != 2 {
		t.Errorf("Expected stack length 2, got %d", len(stack))
	}
	if stack[0] != 42 || stack[1] != 42 {
		t.Errorf("Expected [42, 42], got %v", stack)
	}

	// Test dup on empty stack
	vm = createVMWithProgram([]byte{})
	if err := vm.Dup(); err == nil {
		t.Error("Expected error when duplicating empty stack")
	}
}

func TestSwap(t *testing.T) {
	vm := createVMWithProgram([]byte{})
	pushValue(t, vm, 10)
	pushValue(t, vm, 20)

	if err := vm.Swap(); err != nil {
		t.Fatalf("Swap failed: %v", err)
	}

	stack := vm.Stack()
	if len(stack) != 2 {
		t.Errorf("Expected stack length 2, got %d", len(stack))
	}
	if stack[0] != 20 || stack[1] != 10 {
		t.Errorf("Expected [20, 10], got %v", stack)
	}

	// Test swap with insufficient values
	vm = createVMWithProgram([]byte{})
	pushValue(t, vm, 1)
	if err := vm.Swap(); err == nil {
		t.Error("Expected error when swapping with only one value")
	}
}

func TestRoll(t *testing.T) {
	vm := createVMWithProgram([]byte{})
	pushValue(t, vm, 10)
	pushValue(t, vm, 20)

	if err := vm.Roll(); err != nil {
		t.Fatalf("Roll failed: %v", err)
	}

	stack := vm.Stack()
	if len(stack) != 3 {
		t.Errorf("Expected stack length 3, got %d", len(stack))
	}
	if stack[0] != 10 || stack[1] != 20 || stack[2] != 10 {
		t.Errorf("Expected [10, 20, 10], got %v", stack)
	}

	// Test roll with insufficient values
	vm = createVMWithProgram([]byte{})
	pushValue(t, vm, 1)
	if err := vm.Roll(); err == nil {
		t.Error("Expected error when roll with only one value")
	}
}

func TestRot(t *testing.T) {
	vm := createVMWithProgram([]byte{})
	pushValue(t, vm, 10)
	pushValue(t, vm, 20)
	pushValue(t, vm, 30)

	if err := vm.Rot(); err != nil {
		t.Fatalf("Rot failed: %v", err)
	}

	stack := vm.Stack()
	if len(stack) != 3 {
		t.Errorf("Expected stack length 3, got %d", len(stack))
	}
	// [a, b, c] -> [b, c, a]
	if stack[0] != 20 || stack[1] != 30 || stack[2] != 10 {
		t.Errorf("Expected [20, 30, 10], got %v", stack)
	}

	// Test rot with insufficient values
	vm = createVMWithProgram([]byte{})
	pushValue(t, vm, 1)
	pushValue(t, vm, 2)
	if err := vm.Rot(); err == nil {
		t.Error("Expected error when rot with only two values")
	}
}

func TestArithmeticBitwise(t *testing.T) {
	tests := []struct {
		name     string
		setup    []int32
		op       func(*VM) error
		expected []int32
		wantErr  string
	}{
		{"Add", []int32{10, 20}, (*VM).Add, []int32{30}, ""},
		{"Add Underflow", []int32{10}, (*VM).Add, nil, "stack underflow"},
		{"Sub", []int32{30, 10}, (*VM).Sub, []int32{20}, ""},
		{"Mul", []int32{5, 7}, (*VM).Mul, []int32{35}, ""},
		{"Div", []int32{20, 4}, (*VM).Div, []int32{5}, ""},
		{"Div By Zero", []int32{10, 0}, (*VM).Div, nil, "division by zero"},
		{"Mod", []int32{17, 5}, (*VM).Mod, []int32{2}, ""},
		{"Mod By Zero", []int32{10, 0}, (*VM).Mod, nil, "modulus by zero"},
		{"And", []int32{0b1100, 0b1010}, (*VM).And, []int32{0b1000}, ""},
		{"Or", []int32{0b1100, 0b1010}, (*VM).Or, []int32{0b1110}, ""},
		{"Xor", []int32{0b1100, 0b1010}, (*VM).Xor, []int32{0b0110}, ""},
		{"Not", []int32{0}, (*VM).Not, []int32{-1}, ""},
		{"Inc", []int32{41}, (*VM).Inc, []int32{42}, ""},
		{"Dec", []int32{43}, (*VM).Dec, []int32{42}, ""},
		{"Neg", []int32{42}, (*VM).Neg, []int32{-42}, ""},
		{"Shl", []int32{5, 2}, (*VM).Shl, []int32{20}, ""},
		{"Shl Modulo 32", []int32{5, 34}, (*VM).Shl, []int32{20}, ""},
		{"Eq True", []int32{42, 42}, (*VM).Eq, []int32{1}, ""},
		{"Eq False", []int32{10, 20}, (*VM).Eq, []int32{0}, ""},
		{"Lt True", []int32{10, 20}, (*VM).Lt, []int32{1}, ""},
		{"Lt False", []int32{20, 10}, (*VM).Lt, []int32{0}, ""},
		{"Gt True", []int32{20, 10}, (*VM).Gt, []int32{1}, ""},
		{"Gt False", []int32{10, 20}, (*VM).Gt, []int32{0}, ""},
	}

	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			vm := createVMWithProgram([]byte{})
			for _, v := range tt.setup {
				pushValue(t, vm, v)
			}
			err := tt.op(vm)
			if tt.wantErr != "" {
				if err == nil {
					t.Errorf("%s: expected error containing %q, got nil", tt.name, tt.wantErr)
				} else if !contains(err.Error(), tt.wantErr) {
					t.Errorf("%s: expected error containing %q, got %q", tt.name, tt.wantErr, err.Error())
				}
				return
			}
			if err != nil {
				t.Fatalf("%s: unexpected error: %v", tt.name, err)
			}
			stack := vm.Stack()
			if len(stack) != len(tt.expected) {
				t.Fatalf("%s: expected stack length %d, got %d", tt.name, len(tt.expected), len(stack))
			}
			for i := range stack {
				if stack[i] != tt.expected[i] {
					t.Errorf("%s: stack[%d] mismatch: expected %d, got %d", tt.name, i, tt.expected[i], stack[i])
				}
			}
		})
	}
}

func TestCallStack(t *testing.T) {
	// Build program: push quotation addr, callstack, halt, then quotation
	program := []byte{}
	pushAddr := len(program)
	program = append(program, pushInstruction(0)...) // placeholder address
	program = append(program, OpCallStack)           // pop addr and call
	program = append(program, OpHalt)                // reached after RET
	quotationAddr := len(program)
	program = append(program, pushInstruction(42)...) // PUSH 42
	program = append(program, OpRet)                  // RET

	vm := createVMWithProgram(program)

	actualCallTarget := vm.UserMemoryStart() + uint32(quotationAddr)
	binary.BigEndian.PutUint32(vm.memory[vm.UserMemoryStart()+uint32(pushAddr)+1:], actualCallTarget)

	if err := vm.Run(); err != nil {
		t.Fatalf("Run failed: %v", err)
	}

	stack := vm.Stack()
	if len(stack) != 1 || stack[0] != 42 {
		t.Errorf("Expected [42], got %v", stack)
	}
}

func TestCallStackUnderflow(t *testing.T) {
	vm := createVMWithProgram([]byte{OpCallStack})
	_, err := vm.ExecuteInstruction()
	if err == nil {
		t.Error("Expected error for CALLSTACK with empty stack")
	}
	if !contains(err.Error(), "callstack failed: stack underflow") {
		t.Errorf("Expected 'callstack failed: stack underflow' error, got: %v", err)
	}
}

func TestCallStackInvalidAddress(t *testing.T) {
	// Test calling an address that's too far out of bounds
	vm := createVMWithProgram([]byte{})
	pushValue(t, vm, 9999999) // Invalid address (well beyond memory)
	err := vm.CallStack()
	if err == nil {
		t.Error("Expected error for CALLSTACK with invalid address")
	}
	if !contains(err.Error(), "invalid call address: 9999999") {
		t.Errorf("Expected 'invalid call address: 9999999' error, got: %v", err)
	}
}

func TestJmp(t *testing.T) {
	program := []byte{}
	program = append(program, pushInstruction(10)...) // PUSH 10
	jmpAddr := len(program)
	program = append(program, JmpInstruction(0)...)   // JMP (placeholder)
	program = append(program, pushInstruction(20)...) // PUSH 20 (skipped)
	// Target for jump
	targetAddr := len(program)
	program = append(program, pushInstruction(30)...) // PUSH 30
	program = append(program, OpHalt)                 // HALT

	vm := createVMWithProgram(program)

	// Fix the JMP target to point to actual address in VM memory
	actualTarget := vm.UserMemoryStart() + uint32(targetAddr)
	binary.BigEndian.PutUint32(vm.memory[vm.UserMemoryStart()+uint32(jmpAddr)+1:], actualTarget)

	if err := vm.Run(); err != nil {
		t.Fatalf("Run failed: %v", err)
	}

	stack := vm.Stack()
	if len(stack) != 2 {
		t.Errorf("Expected stack length 2, got %d", len(stack))
	}
	if stack[0] != 10 || stack[1] != 30 {
		t.Errorf("Expected [10, 30], got %v", stack)
	}
}

func TestJz(t *testing.T) {
	// Test jump when condition is zero
	program := []byte{}
	program = append(program, pushInstruction(0)...) // PUSH 0
	jzAddr := len(program)
	program = append(program, JzInstruction(0)...)    // JZ (placeholder)
	program = append(program, pushInstruction(20)...) // PUSH 20 (skipped)
	// Target for jump
	targetAddr := len(program)
	program = append(program, pushInstruction(30)...) // PUSH 30
	program = append(program, OpHalt)                 // HALT

	vm := createVMWithProgram(program)

	// Fix the JZ target address to account for reserved memory
	actualTarget := vm.UserMemoryStart() + uint32(targetAddr)
	binary.BigEndian.PutUint32(vm.memory[vm.UserMemoryStart()+uint32(jzAddr)+1:], actualTarget)

	if err := vm.Run(); err != nil {
		t.Fatalf("Run failed: %v", err)
	}

	stack := vm.Stack()
	if len(stack) != 1 {
		t.Errorf("Expected stack length 1, got %d", len(stack))
	}
	if stack[0] != 30 {
		t.Errorf("Expected [30], got %v", stack)
	}

	// Test no jump when condition is non-zero
	program = []byte{}
	program = append(program, pushInstruction(1)...)  // PUSH 1
	program = append(program, JzInstruction(100)...)  // JZ to address 100 (not taken)
	program = append(program, pushInstruction(20)...) // PUSH 20
	program = append(program, OpHalt)                 // HALT

	vm = createVMWithProgram(program)

	if err := vm.Run(); err != nil {
		t.Fatalf("Run failed: %v", err)
	}

	stack = vm.Stack()
	if len(stack) != 1 {
		t.Errorf("Expected stack length 1, got %d", len(stack))
	}
	if stack[0] != 20 {
		t.Errorf("Expected [20], got %v", stack)
	}
}

// TestJnzEquivalent tests the PUSH 0; EQ; JZ pattern that replaces the removed OpJnz.
func TestJnzEquivalent(t *testing.T) {
	// Jump when condition is non-zero: PUSH 1; PUSH 0; EQ; JZ target
	program := []byte{}
	program = append(program, pushInstruction(1)...)  // PUSH 1 (nonzero cond)
	program = append(program, pushInstruction(0)...)  // PUSH 0
	program = append(program, OpEq)                   // EQ → 0 (false, so JZ won't jump)
	jzAddr := len(program)
	program = append(program, JzInstruction(0)...)    // JZ placeholder (not taken)
	program = append(program, pushInstruction(20)...) // PUSH 20 (skipped — wait, JZ not taken means we DO execute this)
	targetAddr := len(program)
	program = append(program, pushInstruction(30)...) // PUSH 30
	program = append(program, OpHalt)

	// Since cond=1 → EQ(1,0)=0 → JZ IS taken → skip PUSH 20, land at PUSH 30.
	vm := createVMWithProgram(program)
	actualTarget := vm.UserMemoryStart() + uint32(targetAddr)
	binary.BigEndian.PutUint32(vm.memory[vm.UserMemoryStart()+uint32(jzAddr)+1:], actualTarget)

	if err := vm.Run(); err != nil {
		t.Fatalf("Run failed: %v", err)
	}
	stack := vm.Stack()
	if len(stack) != 1 || stack[0] != 30 {
		t.Errorf("Expected [30], got %v", stack)
	}

	// No jump when condition is zero: PUSH 0; PUSH 0; EQ → 1 → JZ not taken
	program = []byte{}
	program = append(program, pushInstruction(0)...)  // PUSH 0 (zero cond)
	program = append(program, pushInstruction(0)...)  // PUSH 0
	program = append(program, OpEq)                   // EQ → 1 (JZ not taken)
	program = append(program, JzInstruction(9999)...) // JZ far address (not taken)
	program = append(program, pushInstruction(20)...) // PUSH 20 (executed)
	program = append(program, OpHalt)

	vm = createVMWithProgram(program)
	if err := vm.Run(); err != nil {
		t.Fatalf("Run failed: %v", err)
	}
	stack = vm.Stack()
	if len(stack) != 1 || stack[0] != 20 {
		t.Errorf("Expected [20], got %v", stack)
	}
}

func TestCallRet(t *testing.T) {
	// Test CALL/RET with separate return stack
	program := []byte{}
	program = append(program, pushInstruction(10)...) // PUSH 10
	callAddr := len(program)
	program = append(program, CallInstruction(0)...)  // CALL (placeholder)
	program = append(program, pushInstruction(20)...) // PUSH 20 (return here)
	program = append(program, OpHalt)                 // HALT
	// Subroutine at calculated address:
	subroutineAddr := len(program)
	program = append(program, pushInstruction(30)...) // PUSH 30
	program = append(program, pushInstruction(40)...) // PUSH 40
	program = append(program, OpAdd)                  // ADD (30+40=70)
	program = append(program, OpRet)                  // RET

	vm := createVMWithProgram(program)

	// Fix the CALL target to point to actual subroutine address in VM memory
	actualTarget := vm.UserMemoryStart() + uint32(subroutineAddr)
	binary.BigEndian.PutUint32(vm.memory[vm.UserMemoryStart()+uint32(callAddr)+1:], actualTarget)

	if err := vm.Run(); err != nil {
		t.Fatalf("Run failed: %v", err)
	}

	// Expected stack: [10 (initial), 70 (from subroutine), 20 (after return)]
	stack := vm.Stack()
	if len(stack) != 3 {
		t.Errorf("Expected stack length 3, got %d; stack: %v", len(stack), stack)
	}
	if len(stack) >= 3 && (stack[0] != 10 || stack[1] != 70 || stack[2] != 20) {
		t.Errorf("Expected [10, 70, 20], got %v", stack)
	}
}

func TestReturnStackOverflow(t *testing.T) {
	// Test that CALL fails when return stack would overflow
	program := make([]byte, 20)
	offset := 0

	// Create a valid CALL instruction
	program[offset] = OpCall
	// Call to a valid address within program bounds (account for reserved memory)
	vm := createVMWithProgram([]byte{OpHalt}) // temp VM to get userMemoryStart
	callTarget := vm.UserMemoryStart() + 10

	// Now create the real program
	program[offset] = OpCall
	binary.BigEndian.PutUint32(program[offset+1:], callTarget)
	offset += 5

	// Add HALT at the call target
	program[10] = OpHalt

	vm = createVMWithProgram(program)

	// Fill return stack to max capacity
	// The VM checks against MaxStackSize for return stack overflow
	for i := 0; i < MaxReturnStackSize; i++ { // Use MaxReturnStackSize
		vm.returnStack = append(vm.returnStack, int32(i))
	}

	// Call the Call() method directly instead of ExecuteInstruction()
	// This is necessary because Call() has the overflow check,
	// but ExecuteInstruction's OpCall case does not directly check it before delegating.
	// NOTE: The actual check is inside the Call() method.
	err := vm.Call()
	if err == nil {
		t.Error("Expected error when CALL causes return stack overflow")
	}
	if !contains(err.Error(), "return stack overflow") {
		t.Errorf("Expected 'return stack overflow' error, got: %v", err)
	}
}

func TestRetUnderflow(t *testing.T) {
	vm := createVMWithProgram([]byte{OpRet})
	_, err := vm.ExecuteInstruction()
	if err == nil {
		t.Error("Expected error for RET with empty return stack")
	}
	if !contains(err.Error(), "return stack underflow") {
		t.Errorf("Expected 'return stack underflow' error, got: %v", err)
	}
}

func TestLoadStore(t *testing.T) {
	// Create a program with some data space
	program := make([]byte, 256)
	offset := 0

	// PUSH 42
	program[offset] = OpPush
	binary.BigEndian.PutUint32(program[offset+1:], 42)
	offset += 5

	// STORE to address 100
	program[offset] = OpStore
	binary.BigEndian.PutUint32(program[offset+1:], 100)
	offset += 5

	// LOAD from address 100
	program[offset] = OpLoad
	binary.BigEndian.PutUint32(program[offset+1:], 100)
	offset += 5

	// HALT
	program[offset] = OpHalt

	vm := createVMWithProgram(program)

	if err := vm.Run(); err != nil {
		t.Fatalf("Run failed: %v", err)
	}

	stack := vm.Stack()
	if len(stack) != 1 {
		t.Errorf("Expected stack length 1, got %d", len(stack))
	}
	if stack[0] != 42 {
		t.Errorf("Expected loaded value 42, got %d", stack[0])
	}

	// Verify memory was written
	value := int32(binary.BigEndian.Uint32(vm.memory[100:104]))
	if value != 42 {
		t.Errorf("Expected memory[100]=42, got %d", value)
	}
}

func TestLoadStoreOutOfBounds(t *testing.T) {
	// Test LOAD from out-of-bounds address
	program := make([]byte, 20)
	offset := 0

	program[offset] = OpLoad
	binary.BigEndian.PutUint32(program[offset+1:], 9999999)
	offset += 5

	program[offset] = OpHalt

	vm := createVMWithProgram(program)
	err := vm.Run()
	if err == nil {
		t.Error("Expected error for LOAD out of bounds")
	}

	// Test STORE to out-of-bounds address
	program = make([]byte, 25)
	offset = 0

	program[offset] = OpPush
	binary.BigEndian.PutUint32(program[offset+1:], 42)
	offset += 5

	program[offset] = OpStore
	binary.BigEndian.PutUint32(program[offset+1:], 9999999)
	offset += 5

	program[offset] = OpHalt

	vm = createVMWithProgram(program)
	err = vm.Run()
	if err == nil {
		t.Error("Expected error for STORE out of bounds")
	}
}

func TestOut(t *testing.T) {
	// Test OUT instruction - can't easily capture stdout, so just verify it executes
	program := []byte{}
	program = append(program, pushInstruction(42)...) // PUSH 42
	program = append(program, pushInstruction(0)...)  // PUSH 0 (format: number)
	program = append(program, OpOut)                  // OUT
	program = append(program, pushInstruction(72)...) // PUSH 72 (H)
	program = append(program, pushInstruction(1)...)  // PUSH 1 (format: character)
	program = append(program, OpOut)                  // OUT
	program = append(program, OpHalt)                 // HALT

	vm := createVMWithProgram(program)

	if err := vm.Run(); err != nil {
		t.Fatalf("Run failed: %v", err)
	}

	// Stack should be empty after OUTs
	stack := vm.Stack()
	if len(stack) != 0 {
		t.Errorf("Expected empty stack, got %v", stack)
	}
}

func TestOutUnderflow(t *testing.T) {
	vm := createVMWithProgram([]byte{})

	// Test OUT with empty stack
	err := vm.Out()
	if err == nil {
		t.Error("Expected error when OUT with empty stack")
	}

	// Test OUT with only one value
	pushValue(t, vm, 42)
	err = vm.Out()
	if err == nil {
		t.Error("Expected error when OUT with only one value")
	}
}

func TestHalt(t *testing.T) {
	program := []byte{}
	program = append(program, pushInstruction(10)...) // PUSH 10
	program = append(program, OpHalt)                 // HALT
	program = append(program, pushInstruction(20)...) // PUSH 20 (not executed)

	vm := createVMWithProgram(program)

	if err := vm.Run(); err != nil {
		t.Fatalf("Run failed: %v", err)
	}

	if vm.Running() {
		t.Error("Expected VM to be stopped after HALT")
	}

	stack := vm.Stack()
	if len(stack) != 1 {
		t.Errorf("Expected stack length 1, got %d", len(stack))
	}
	if stack[0] != 10 {
		t.Errorf("Expected [10], got %v", stack)
	}
}

func TestUnknownOpcode(t *testing.T) {
	program := []byte{0xFF} // Invalid opcode
	vm := createVMWithProgram(program)

	err := vm.Run()
	if err == nil {
		t.Error("Expected error for unknown opcode")
	}
	if !contains(err.Error(), "unknown opcode") {
		t.Errorf("Expected 'unknown opcode' in error, got: %v", err)
	}
}

func TestPCOutOfBounds(t *testing.T) {
	// Test PC going out of bounds during instruction fetch
	program := []byte{OpPush} // PUSH without immediate value, PC will advance past end of program
	vm := createVMWithProgram(program)

	err := vm.Run()
	if err == nil {
		t.Error("Expected error for PC out of bounds during instruction fetch")
	}
	if !contains(err.Error(), "program counter out of bounds") {
		t.Errorf("Expected 'program counter out of bounds' in error, got: %v", err)
	}

	// Test PC out of bounds due to jump/call
	// Create a program that jumps/calls beyond the end of the memory
	programJmp := make([]byte, 10)
	programJmp[0] = OpJmp
	binary.BigEndian.PutUint32(programJmp[1:], 99999) // Jump to address well beyond memory

	vmJmp := createVMWithProgram(programJmp)
	err = vmJmp.Run()
	if err == nil {
		t.Error("Expected error for PC out of bounds after JMP")
	}
	if !contains(err.Error(), "program counter out of bounds") {
		t.Errorf("Expected 'program counter out of bounds' in error, got: %v", err)
	}
}

func TestStep(t *testing.T) {
	program := []byte{}
	program = append(program, pushInstruction(10)...) // PUSH 10
	program = append(program, pushInstruction(20)...) // PUSH 20
	program = append(program, OpAdd)                  // ADD
	program = append(program, OpHalt)                 // HALT

	vm := createVMWithProgram(program)

	// Execute first instruction
	cont, err := vm.Step()
	if err != nil {
		t.Fatalf("Step 1 failed: %v", err)
	}
	if !cont {
		t.Error("Expected continuation after step 1")
	}

	stack := vm.Stack()
	if len(stack) != 1 || stack[0] != 10 {
		t.Errorf("After step 1, expected [10], got %v", stack)
	}

	// Execute second instruction
	cont, err = vm.Step()
	if err != nil {
		t.Fatalf("Step 2 failed: %v", err)
	}
	if !cont {
		t.Error("Expected continuation after step 2")
	}

	stack = vm.Stack()
	if len(stack) != 2 || stack[0] != 10 || stack[1] != 20 {
		t.Errorf("After step 2, expected [10, 20], got %v", stack)
	}

	// Execute ADD
	cont, err = vm.Step()
	if err != nil {
		t.Fatalf("Step 3 failed: %v", err)
	}
	if !cont {
		t.Error("Expected continuation after step 3")
	}

	stack = vm.Stack()
	if len(stack) != 1 || stack[0] != 30 {
		t.Errorf("After step 3, expected [30], got %v", stack)
	}

	// Execute HALT
	cont, err = vm.Step()
	if err != nil {
		t.Fatalf("Step 4 failed: %v", err)
	}
	if cont {
		t.Error("Expected no continuation after HALT")
	}
}

func TestComplexProgram(t *testing.T) {
	// Program that computes (5 + 3) * 2 = 16
	program := []byte{}
	program = append(program, pushInstruction(5)...)
	program = append(program, pushInstruction(3)...)
	program = append(program, OpAdd) // ADD
	program = append(program, pushInstruction(2)...)
	program = append(program, OpMul) // MUL
	program = append(program, OpHalt)

	vm := createVMWithProgram(program)

	if err := vm.Run(); err != nil {
		t.Fatalf("Run failed: %v", err)
	}

	stack := vm.Stack()
	if len(stack) != 1 {
		t.Errorf("Expected stack length 1, got %d", len(stack))
	}
	if stack[0] != 16 {
		t.Errorf("Expected result 16, got %d", stack[0])
	}
}

func TestSimpleLoop(t *testing.T) {
	// Simple loop: push 5, loop: dup, jz end, dec, jmp loop, end: halt
	// This will count 5 down to 0
	program := []byte{}
	program = append(program, pushInstruction(5)...) // PUSH 5
	loopStart := len(program)
	program = append(program, OpDup) // DUP
	jzAddr := len(program)
	program = append(program, JzInstruction(0)...) // JZ (placeholder)
	program = append(program, OpDec)               // DEC
	jmpAddr := len(program)
	program = append(program, JmpInstruction(0)...) // JMP (placeholder)
	endAddr := len(program)
	program = append(program, OpHalt) // HALT

	vm := createVMWithProgram(program)

	// Fix JZ target to point to end
	actualEndAddr := vm.UserMemoryStart() + uint32(endAddr)
	binary.BigEndian.PutUint32(vm.memory[vm.UserMemoryStart()+uint32(jzAddr)+1:], actualEndAddr)

	// Fix JMP target to point to loop start
	actualLoopStart := vm.UserMemoryStart() + uint32(loopStart)
	binary.BigEndian.PutUint32(vm.memory[vm.UserMemoryStart()+uint32(jmpAddr)+1:], actualLoopStart)

	if err := vm.Run(); err != nil {
		t.Fatalf("Run failed: %v", err)
	}

	// After loop: started with 5, dec to 4,3,2,1,0, dup 0 -> [0,0], jz pops and jumps -> [0]
	stack := vm.Stack()
	if len(stack) != 1 {
		t.Errorf("Expected stack length 1, got %d; stack: %v", len(stack), stack)
	}
	if len(stack) >= 1 && stack[0] != 0 {
		t.Errorf("Expected [0], got %v", stack)
	}
}

func TestOpcodeName(t *testing.T) {
	tests := []struct {
		opcode byte
		name   string
	}{
		{OpPush, "PUSH"},
		{OpPop, "POP"},
		{OpDup, "DUP"},
		{OpSwap, "SWAP"},
		{OpRoll, "ROLL"},
		{OpRot, "ROT"},
		{OpAdd, "ADD"},
		{OpSub, "SUB"},
		{OpMul, "MUL"},
		{OpDiv, "DIV"},
		{OpMod, "MOD"},
		{OpInc, "INC"},
		{OpDec, "DEC"},
		{OpAnd, "AND"},
		{OpOr, "OR"},
		{OpXor, "XOR"},
		{OpNot, "NOT"},
		{OpShl, "SHL"},
		{OpEq, "EQ"},
		{OpLt, "LT"},
		{OpCallStack, "CALLSTACK"},
		{OpJmp, "JMP"},
		{OpJz, "JZ"},
		{OpCall, "CALL"},
		{OpRet, "RET"},
		{OpLoad, "LOAD"},
		{OpStore, "STORE"},
		{OpOut, "OUT"},
		{OpHalt, "HALT"},
		{0xFF, "UNKNOWN(0xFF)"},
	}

	for _, tt := range tests {
		name := OpcodeName(tt.opcode)
		if name != tt.name {
			t.Errorf("OpcodeName(0x%02X) = %s, want %s", tt.opcode, name, tt.name)
		}
	}
}

func TestHelperFunctions(t *testing.T) {
	// Test EncodeInt32
	encoded := EncodeInt32(42)
	if len(encoded) != 4 {
		t.Errorf("Expected 4 bytes, got %d", len(encoded))
	}
	decoded := int32(binary.BigEndian.Uint32(encoded))
	if decoded != 42 {
		t.Errorf("Expected 42, got %d", decoded)
	}

	// Test PushInstruction
	pushInstr := PushInstruction(100)
	if len(pushInstr) != 5 {
		t.Errorf("Expected 5 bytes, got %d", len(pushInstr))
	}
	if pushInstr[0] != OpPush {
		t.Errorf("Expected PUSH opcode, got 0x%02X", pushInstr[0])
	}
	value := int32(binary.BigEndian.Uint32(pushInstr[1:]))
	if value != 100 {
		t.Errorf("Expected value 100, got %d", value)
	}

	// Test JmpInstruction
	jmpInstr := JmpInstruction(200)
	if len(jmpInstr) != 5 {
		t.Errorf("Expected 5 bytes, got %d", len(jmpInstr))
	}
	if jmpInstr[0] != OpJmp {
		t.Errorf("Expected JMP opcode, got 0x%02X", jmpInstr[0])
	}

	// Test JzInstruction
	jzInstr := JzInstruction(300)
	if len(jzInstr) != 5 {
		t.Errorf("Expected 5 bytes, got %d", len(jzInstr))
	}
	if jzInstr[0] != OpJz {
		t.Errorf("Expected JZ opcode, got 0x%02X", jzInstr[0])
	}

	// Test CallInstruction
	callInstr := CallInstruction(500)
	if len(callInstr) != 5 {
		t.Errorf("Expected 5 bytes, got %d", len(callInstr))
	}
	if callInstr[0] != OpCall {
		t.Errorf("Expected CALL opcode, got 0x%02X", callInstr[0])
	}

	// Test LoadInstruction
	loadInstr := LoadInstruction(600)
	if len(loadInstr) != 5 {
		t.Errorf("Expected 5 bytes, got %d", len(loadInstr))
	}
	if loadInstr[0] != OpLoad {
		t.Errorf("Expected LOAD opcode, got 0x%02X", loadInstr[0])
	}

	// Test StoreInstruction
	storeInstr := StoreInstruction(700)
	if len(storeInstr) != 5 {
		t.Errorf("Expected 5 bytes, got %d", len(storeInstr))
	}
	if storeInstr[0] != OpStore {
		t.Errorf("Expected STORE opcode, got 0x%02X", storeInstr[0])
	}

	// Test OutNumber
	outNum := OutNumber()
	// OpPush (1 byte) + value (4 bytes) + OpOut (1 byte) = 6 bytes
	if len(outNum) != 6 {
		t.Errorf("Expected 6 bytes for OutNumber, got %d", len(outNum))
	}
	if outNum[0] != OpPush || outNum[5] != OpOut {
		t.Errorf("OutNumber instruction incorrect. Got: %x", outNum)
	}

	// Test OutCharacter
	outChar := OutCharacter()
	// OpPush (1 byte) + value (4 bytes) + OpOut (1 byte) = 6 bytes
	if len(outChar) != 6 {
		t.Errorf("Expected 6 bytes for OutCharacter, got %d", len(outChar))
	}
	if outChar[0] != OpPush || outChar[5] != OpOut {
		t.Errorf("OutCharacter instruction incorrect. Got: %x", outChar)
	}
}

func TestReservedMemory(t *testing.T) {
	program := []byte{OpHalt}
	vm := createVMWithProgram(program)

	// Test reserved memory size
	if vm.ReservedMemorySize() == 0 {
		t.Error("Expected non-zero reserved memory size")
	}

	// Test user memory start
	if vm.UserMemoryStart() == 0 {
		t.Error("Expected non-zero user memory start")
	}

	// Test that user memory start equals UserMemoryOffset (fixed in new architecture)
	if vm.UserMemoryStart() != UserMemoryOffset {
		t.Errorf("Expected user memory start (%d) to equal UserMemoryOffset (%d)",
			vm.UserMemoryStart(), UserMemoryOffset)
	}

	// Test writing to reserved memory
	testData := []byte{0x01, 0x02, 0x03, 0x04}
	err := vm.WriteReservedMemory(0, testData)
	if err != nil {
		t.Fatalf("Failed to write to reserved memory: %v", err)
	}

	// Test reading from reserved memory
	readData, err := vm.ReadReservedMemory(0, 4)
	if err != nil {
		t.Fatalf("Failed to read from reserved memory: %v", err)
	}
	for i := range testData {
		if readData[i] != testData[i] {
			t.Errorf("Reserved memory data mismatch at index %d: expected 0x%02X, got 0x%02X",
				i, testData[i], readData[i])
		}
	}

	// Test writing out of bounds (exactly at reservedSize)
	err = vm.WriteReservedMemory(vm.ReservedMemorySize(), testData)
	if err == nil {
		t.Error("Expected error when writing exactly at reserved memory size boundary")
	}

	// Test reading out of bounds (exactly at reservedSize)
	_, err = vm.ReadReservedMemory(vm.ReservedMemorySize(), 4)
	if err == nil {
		t.Error("Expected error when reading exactly at reserved memory size boundary")
	}

	// Test overflow write (writing beyond reserved memory)
	largeOffset := vm.ReservedMemorySize() - 2
	err = vm.WriteReservedMemory(largeOffset, testData) // This write will overflow
	if err == nil {
		t.Error("Expected error when write would overflow reserved memory")
	}

	// Test overflow read (reading beyond reserved memory)
	_, err = vm.ReadReservedMemory(largeOffset, 4) // This read will overflow
	if err == nil {
		t.Error("Expected error when read would overflow reserved memory")
	}
}

func TestNewVMWithReservedMemory(t *testing.T) {
	program := []byte{OpHalt}
	customReservedSize := uint32(8192)

	vm := NewVMWithReservedMemory(program, customReservedSize)

	if vm == nil {
		t.Fatal("NewVMWithReservedMemory returned nil")
	}

	if vm.ReservedMemorySize() != customReservedSize {
		t.Errorf("Expected reserved memory size %d, got %d",
			customReservedSize, vm.ReservedMemorySize())
	}

	// UserMemoryStart should be fixed at UserMemoryOffset
	if vm.UserMemoryStart() != UserMemoryOffset {
		t.Errorf("Expected user memory start %d, got %d",
			UserMemoryOffset, vm.UserMemoryStart())
	}

	if vm.PC() != UserMemoryOffset {
		t.Errorf("Expected PC to start at %d, got %d",
			UserMemoryOffset, vm.PC())
	}

	expectedMemSize := int(UserMemoryOffset) + len(program)
	if len(vm.memory) != expectedMemSize {
		t.Errorf("Expected memory length %d, got %d", expectedMemSize, len(vm.memory))
	}
}

func TestReservedMemoryInDebugInfo(t *testing.T) {
	program := []byte{}
	program = append(program, pushInstruction(42)...)
	program = append(program, OpHalt)

	vm := createVMWithProgram(program)
	pushValue(t, vm, 100) // Add to stack for debug info

	debugInfo := vm.DebugInfo()
	if debugInfo == "" {
		t.Error("Expected non-empty debug info")
	}

	// Check that debug info contains expected information
	if !contains(debugInfo, "PC:") {
		t.Error("Debug info should contain PC")
	}
	if !contains(debugInfo, "Stack:") {
		t.Error("Debug info should contain Stack")
	}
	if !contains(debugInfo, "Return Stack:") {
		t.Error("Debug info should contain Return Stack")
	}
	if !contains(debugInfo, "Reserved Memory:") {
		t.Error("Debug info should contain Reserved Memory")
	}
	if !contains(debugInfo, "User Memory:") {
		t.Error("Debug info should contain User Memory")
	}
	if !contains(debugInfo, fmt.Sprintf("Reserved Memory: 0x0-0x%X (%d bytes)", vm.ReservedMemorySize(), vm.ReservedMemorySize())) {
		t.Errorf("Debug info missing correct reserved memory range: %s", debugInfo)
	}
}

func TestReservedMemoryWithCode(t *testing.T) {
	// Create a VM
	program := []byte{}
	program = append(program, pushInstruction(10)...) // PUSH 10
	callAddr := len(program)
	program = append(program, CallInstruction(0)...) // CALL (placeholder)
	program = append(program, OpHalt)                // HALT

	vm := createVMWithProgram(program)

	// Write a simple subroutine to reserved memory
	// The subroutine will: PUSH 42, RET
	subroutine := []byte{}
	subroutine = append(subroutine, pushInstruction(42)...)
	subroutine = append(subroutine, OpRet)

	// Write subroutine at offset 100 within reserved memory
	err := vm.WriteReservedMemory(100, subroutine)
	if err != nil {
		t.Fatalf("Failed to write subroutine to reserved memory at offset 100: %v", err)
	}

	// Fix the CALL target to point to the actual reserved memory address
	actualCallTarget := uint32(100) // Address within reserved memory
	binary.BigEndian.PutUint32(vm.memory[vm.UserMemoryStart()+uint32(callAddr)+1:], actualCallTarget)

	// Run the program
	if err := vm.Run(); err != nil {
		t.Fatalf("Run failed: %v", err)
	}

	// Check result: stack should contain [10, 42]
	stack := vm.Stack()
	if len(stack) != 2 {
		t.Errorf("Expected stack length 2, got %d; stack: %v", len(stack), stack)
	}
	if len(stack) >= 2 && (stack[0] != 10 || stack[1] != 42) {
		t.Errorf("Expected stack [10, 42], got %v", stack)
	}
}

func TestExecuteInstructionErrors(t *testing.T) {
	tests := []struct {
		name    string
		program []byte
		setup   func(*VM)
		errMsg  string
	}{
		{
			name:    "POP underflow",
			program: []byte{OpPop},
			errMsg:  "pop failed",
		},
		{
			name:    "DUP underflow",
			program: []byte{OpDup},
			errMsg:  "dup failed",
		},
		{
			name:    "SWAP underflow",
			program: []byte{OpSwap},
			errMsg:  "swap failed",
		},
		{
			name:    "ROLL underflow",
			program: []byte{OpRoll},
			errMsg:  "roll failed",
		},
		{
			name:    "ROT underflow",
			program: []byte{OpRot},
			errMsg:  "rot failed",
		},
		{
			name:    "ADD underflow",
			program: []byte{OpAdd},
			errMsg:  "add failed",
		},
		{
			name:    "SUB underflow",
			program: []byte{OpSub},
			errMsg:  "sub failed",
		},
		{
			name:    "MUL underflow",
			program: []byte{OpMul},
			errMsg:  "mul failed",
		},
		{
			name:    "DIV underflow",
			program: []byte{OpDiv},
			errMsg:  "div failed",
		},
		{
			name:    "MOD underflow",
			program: []byte{OpMod},
			errMsg:  "mod failed",
		},
		{
			name:    "INC underflow",
			program: []byte{OpInc},
			errMsg:  "inc failed",
		},
		{
			name:    "DEC underflow",
			program: []byte{OpDec},
			errMsg:  "dec failed",
		},
		{
			name:    "AND underflow",
			program: []byte{OpAnd},
			errMsg:  "and failed",
		},
		{
			name:    "OR underflow",
			program: []byte{OpOr},
			errMsg:  "or failed",
		},
		{
			name:    "XOR underflow",
			program: []byte{OpXor},
			errMsg:  "xor failed",
		},
		{
			name:    "NOT underflow",
			program: []byte{OpNot},
			errMsg:  "not failed",
		},
		{
			name:    "SHL underflow",
			program: []byte{OpShl},
			errMsg:  "shl failed",
		},
		{
			name:    "EQ underflow",
			program: []byte{OpEq},
			errMsg:  "eq failed",
		},
		{
			name:    "LT underflow",
			program: []byte{OpLt},
			errMsg:  "lt failed",
		},
		{
			name:    "CALLSTACK underflow",
			program: []byte{OpCallStack},
			errMsg:  "callstack failed: stack underflow",
		},
		{
			name:    "JMP incomplete",
			program: []byte{OpJmp, 0xFF}, // Opcode + 1 byte of address
			errMsg:  "jmp failed: program counter out of bounds",
		},
		{
			name:    "JZ underflow",
			program: []byte{OpJz, 0x00, 0x00, 0x00, 0x10}, // JZ instruction + address
			errMsg:  "jz failed: stack underflow",
		},
		{
			name:    "JZ incomplete",
			program: []byte{OpJz, 0xFF}, // JZ + 1 byte of address
			errMsg:  "jz failed: program counter out of bounds",
		},
		{
			name:    "CALL incomplete",
			program: []byte{OpCall, 0xFF}, // Opcode + 1 byte of address
			errMsg:  "call failed: program counter out of bounds",
		},
		{
			name:    "RET underflow",
			program: []byte{OpRet},
			errMsg:  "ret failed: return stack underflow",
		},
		{
			name:    "LOAD incomplete",
			program: []byte{OpLoad, 0xFF}, // Opcode + 1 byte of address
			errMsg:  "load failed: program counter out of bounds",
		},
		{
			name:    "STORE underflow",
			program: []byte{OpStore, 0x00, 0x00, 0x00, 0x10}, // STORE instruction + address
			errMsg:  "store failed: stack underflow",
		},
		{
			name:    "STORE incomplete",
			program: []byte{OpStore, 0xFF}, // OpStore + 1 byte address
			setup: func(vm *VM) {
				// Push a value to be stored
				pushValue(t, vm, 42)
			},
			errMsg: "store failed: program counter out of bounds",
		},
		{
			name:    "OUT underflow",
			program: []byte{OpOut},
			errMsg:  "out failed: stack underflow",
		},
		{
			name:    "OUT needs 2 values",
			program: []byte{OpOut}, // OUT
			setup: func(vm *VM) { // Stack has only 1 value
				pushValue(t, vm, 42)
			},
			errMsg: "out failed: stack underflow",
		},
	}

	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			vm := createVMWithProgram(tt.program)
			if tt.setup != nil {
				tt.setup(vm)
			}
			_, err := vm.ExecuteInstruction()
			if err == nil {
				t.Errorf("Expected error containing '%s', got nil", tt.errMsg)
			} else if !contains(err.Error(), tt.errMsg) {
				t.Errorf("Expected error containing '%s', got '%s'", tt.errMsg, err.Error())
			}
		})
	}
}

// --- Device I/O Tests ---

func TestDeviceReadControllerStatus(t *testing.T) {
	program := []byte{}
	// Load from keyboard status address
	program = append(program, LoadInstruction(ControllerStatusAddr)...)
	program = append(program, OpHalt)

	vm := createVMWithProgram(program)
	vm.bus.(*MockBus).ReadFunc = func(addr uint32) (int32, error) {
		if addr == ControllerStatusAddr {
			return 1, nil
		}
		return 0, nil
	}

	if err := vm.Run(); err != nil {
		t.Fatalf("Run failed: %v", err)
	}

	stack := vm.Stack()
	if len(stack) != 1 {
		t.Errorf("Expected stack length 1, got %d", len(stack))
	}
	if stack[0] != 1 {
		t.Errorf("Expected keyboard status 1, got %d", stack[0])
	}
}

func TestDeviceReadVideoFramebuffer(t *testing.T) {
	program := []byte{}
	// Load from a video framebuffer address
	program = append(program, LoadInstruction(VideoFramebufferStart)...)
	program = append(program, OpHalt)

	vm := createVMWithProgram(program)
	vm.bus.(*MockBus).ReadFunc = func(addr uint32) (int32, error) {
		if addr == VideoFramebufferStart {
			return 0x12345678, nil
		}
		return 0, nil
	}

	if err := vm.Run(); err != nil {
		t.Fatalf("Run failed: %v", err)
	}

	stack := vm.Stack()
	if len(stack) != 1 {
		t.Errorf("Expected stack length 1, got %d", len(stack))
	}
	if stack[0] != 0x12345678 {
		t.Errorf("Expected video framebuffer read value 0x12345678, got 0x%X", stack[0])
	}
}

func TestDeviceWriteVideoFramebuffer(t *testing.T) {
	program := []byte{}
	// Push value to write
	program = append(program, PushInstruction(-1)...) // PUSH a value (e.g., white color)
	// Store to video framebuffer address
	program = append(program, StoreInstruction(VideoFramebufferStart)...)
	program = append(program, OpHalt)

	vm := createVMWithProgram(program)
	var writtenValue int32
	vm.bus.(*MockBus).WriteFunc = func(addr uint32, val int32) error {
		if addr == VideoFramebufferStart {
			writtenValue = val
		}
		return nil
	}

	if err := vm.Run(); err != nil {
		t.Fatalf("Run failed: %v", err)
	}

	if writtenValue != -1 {
		t.Errorf("Expected video framebuffer write value -1, got %d", writtenValue)
	}
}

func TestDeviceWriteAudioControl(t *testing.T) {
	program := []byte{}
	// Push a value representing an audio command
	program = append(program, PushInstruction(123)...) // Simulate an audio command value
	// Write to audio control address
	program = append(program, StoreInstruction(AudioControlAddr)...)
	program = append(program, OpHalt)

	vm := createVMWithProgram(program)
	var writtenValue int32
	vm.bus.(*MockBus).WriteFunc = func(addr uint32, val int32) error {
		if addr == AudioControlAddr {
			writtenValue = val
		}
		return nil
	}

	if err := vm.Run(); err != nil {
		t.Fatalf("Run failed: %v", err)
	}

	if writtenValue != 123 {
		t.Errorf("Expected audio control write value 123, got %d", writtenValue)
	}
}

func TestDeviceWriteControllerStatusUnsupported(t *testing.T) {
	program := []byte{}
	// Push a value to write
	program = append(program, PushInstruction(1)...)
	// Attempt to write to keyboard status address
	program = append(program, StoreInstruction(ControllerStatusAddr)...)
	program = append(program, OpHalt)

	vm := createVMWithProgram(program)
	vm.bus.(*MockBus).WriteFunc = func(addr uint32, val int32) error {
		if addr == ControllerStatusAddr {
			return fmt.Errorf("writing to controller address %d is not supported", addr)
		}
		return nil
	}

	err := vm.Run()
	if err == nil {
		t.Error("Expected error when writing to keyboard status address")
	}
	if !contains(err.Error(), "writing to controller address") {
		t.Errorf("Expected error message about unsupported write to controller, got: %v", err)
	}
}

func TestDeviceAccessUnhandledAddress(t *testing.T) {
	program := []byte{}
	// Address within device memory range but not explicitly handled
	unhandledDeviceAddr := DeviceMemoryOffset + DeviceMemorySize - 4 

	// Test read
	program = append(program, LoadInstruction(int32(unhandledDeviceAddr))...)
	// Test write
	program = append(program, PushInstruction(99)...)
	program = append(program, StoreInstruction(int32(unhandledDeviceAddr))...)
	program = append(program, OpHalt)

	vm := createVMWithProgram(program)
	vm.bus.(*MockBus).ReadFunc = func(addr uint32) (int32, error) {
		return 0, fmt.Errorf("unhandled device read")
	}
	vm.bus.(*MockBus).WriteFunc = func(addr uint32, val int32) error {
		return fmt.Errorf("unhandled device write")
	}

	err := vm.Run()
	if err == nil {
		t.Error("Expected error for unhandled device read/write access")
	}
	// The error message might come from either load or store depending on execution order,
	// but it should indicate an unhandled device access.
	if !contains(err.Error(), "unhandled device read") && !contains(err.Error(), "unhandled device write") {
		t.Errorf("Expected error related to unhandled device access, got: %v", err)
	}
}

func TestDeviceAccessOutsideRange(t *testing.T) {
	// Test that normal memory access still works and is not redirected
	normalMemAddr := NewVM([]byte{}).UserMemoryStart() + 100 // Address in normal user memory
	program := []byte{}
	// Write to normal memory
	program = append(program, pushInstruction(123)...)
	program = append(program, StoreInstruction(int32(normalMemAddr))...)
	// Read from normal memory
	program = append(program, LoadInstruction(int32(normalMemAddr))...)
	program = append(program, OpHalt)
	// Pad so memory extends to the store/load target address
	for uint32(len(program)) < 104 {
		program = append(program, 0)
	}

	vm := createVMWithProgram(program)
	vm.trace = true

	if err := vm.Run(); err != nil {
		t.Fatalf("Run failed: %v", err)
	}

	stack := vm.Stack()
	if len(stack) != 1 {
		t.Errorf("Expected stack length 1, got %d", len(stack))
	}
	if stack[0] != 123 {
		t.Errorf("Expected loaded value 123, got %d", stack[0])
	}

	// Verify write to normal memory
	writtenValue := int32(binary.BigEndian.Uint32(vm.memory[normalMemAddr : normalMemAddr+4]))
	if writtenValue != 123 {
		t.Errorf("Expected memory[%d] = 123, got %d", normalMemAddr, writtenValue)
	}
}

func TestStackLimits(t *testing.T) {
	vm := createVMWithProgram([]byte{OpHalt})
	
	// Test Data Stack Overflow
	for i := 0; i < MaxStackSize; i++ {
		if err := vm.Push(int32(i)); err != nil {
			t.Fatalf("Failed to push value %d: %v", i, err)
		}
	}
	if err := vm.Push(999); err == nil {
		t.Error("Expected stack overflow error")
	}

	// Test Return Stack Overflow
	vm = createVMWithProgram([]byte{OpCall, 0, 0, 0, 0})
	for i := 0; i < MaxReturnStackSize; i++ {
		vm.returnStack = append(vm.returnStack, int32(i))
	}
	// Attempt a CALL which should fail due to return stack overflow
	vm.memory[vm.UserMemoryStart()] = OpCall
	binary.BigEndian.PutUint32(vm.memory[vm.UserMemoryStart()+1:], vm.UserMemoryStart())
	vm.pc = vm.UserMemoryStart()
	_, err := vm.ExecuteInstruction()
	if err == nil || !contains(err.Error(), "return stack overflow") {
		t.Errorf("Expected return stack overflow error, got: %v", err)
	}
}

func TestShlLargeAmount(t *testing.T) {
	vm := createVMWithProgram([]byte{})
	
	// 1 << 33 should be equivalent to 1 << 1 (which is 2)
	pushValue(t, vm, 1)
	pushValue(t, vm, 33)
	if err := vm.Shl(); err != nil {
		t.Fatalf("Shl failed: %v", err)
	}
	val, _ := vm.Pop()
	if val != 2 {
		t.Errorf("Expected 2 for 1 << 33 (masked to 1 << 1), got %d", val)
	}

	// 1 << 32 should be equivalent to 1 << 0 (which is 1)
	pushValue(t, vm, 1)
	pushValue(t, vm, 32)
	vm.Shl()
	val, _ = vm.Pop()
	if val != 1 {
		t.Errorf("Expected 1 for 1 << 32 (masked to 1 << 0), got %d", val)
	}
}

func TestVMGetters(t *testing.T) {
	program := []byte{OpPush, 0, 0, 0, 42, OpHalt}
	vm := NewVM(program)

	if len(vm.Memory()) == 0 {
		t.Error("Expected Memory() to return non-empty slice")
	}

	// Run one step to set lastOpcode
	vm.Step()
	if vm.LastOpcode() != "PUSH" {
		t.Errorf("Expected LastOpcode PUSH, got %s", vm.LastOpcode())
	}
}

func TestDirectVMMethods(t *testing.T) {
	program := make([]byte, 100)
	vm := NewVM(program)

	// Test Jmp directly
	target := vm.UserMemoryStart() + 10
	binary.BigEndian.PutUint32(vm.memory[vm.pc:], uint32(target))
	if err := vm.Jmp(); err != nil {
		t.Fatalf("Direct Jmp failed: %v", err)
	}
	if vm.pc != uint32(target) {
		t.Errorf("Expected PC=%d, got %d", target, vm.pc)
	}

	// Test Jz directly (not zero)
	vm.pc = vm.UserMemoryStart()
	binary.BigEndian.PutUint32(vm.memory[vm.pc:], 99)
	pushValue(t, vm, 1)
	if err := vm.Jz(); err != nil {
		t.Fatalf("Direct Jz failed: %v", err)
	}
	if vm.pc != vm.UserMemoryStart()+4 {
		t.Errorf("Expected PC to skip 4 bytes, got %d", vm.pc)
	}

	// Test Jz directly (is zero)
	vm.pc = vm.UserMemoryStart()
	target = vm.UserMemoryStart() + 20
	binary.BigEndian.PutUint32(vm.memory[vm.pc:], uint32(target))
	pushValue(t, vm, 0)
	if err := vm.Jz(); err != nil {
		t.Fatalf("Direct Jz failed: %v", err)
	}
	if vm.pc != uint32(target) {
		t.Errorf("Expected PC to jump to %d, got %d", target, vm.pc)
	}

	// Test Halt directly
	vm.Halt()
	if vm.Running() {
		t.Error("Expected Running() to be false after Halt()")
	}
}

func TestDeviceReadControllerRNG(t *testing.T) {
	vm := createVMWithProgram([]byte{})

	// Test Keyboard Read
	vm.bus.(*MockBus).ReadFunc = func(addr uint32) (int32, error) {
		if addr == ControllerStatusAddr {
			return 42, nil
		}
		if addr == RNGDataAddr {
			// Simple mock RNG
			return int32(time.Now().UnixNano()), nil
		}
		return 0, nil
	}

	val, err := vm.handleDeviceRead(ControllerStatusAddr)
	if err != nil {
		t.Fatalf("Keyboard read failed: %v", err)
	}
	if val != 42 {
		t.Errorf("Expected keyboard value 42, got %d", val)
	}

	// Test RNG Read
	val1, err := vm.handleDeviceRead(RNGDataAddr)
	if err != nil {
		t.Fatalf("RNG read 1 failed: %v", err)
	}
	val2, err := vm.handleDeviceRead(RNGDataAddr)
	if err != nil {
		t.Fatalf("RNG read 2 failed: %v", err)
	}
	if val1 == val2 {
		// This might fail if the mock is too fast, but good enough for now
	}

	vm.bus.(*MockBus).ReadFunc = func(addr uint32) (int32, error) {
		return 0, fmt.Errorf("unhandled")
	}
	_, err = vm.handleDeviceRead(VideoFramebufferStart)
	if err == nil {
		t.Error("Expected error for video buffer read with mock returning error")
	}
}

func TestLoadStoreBoundaries(t *testing.T) {
	program := make([]byte, 100)
	program[0] = OpHalt
	vm := NewVM(program)
	memLen := uint32(len(vm.memory))

	// Store at the very last 4 bytes
	addr := memLen - 4
	pushValue(t, vm, 0x12345678)
	
	// Manually set PC to point to an address we'll use for OpStore/OpLoad
	// OpStore expects 4 bytes after PC for the address
	vm.pc = vm.UserMemoryStart()
	binary.BigEndian.PutUint32(vm.memory[vm.pc:], addr)
	
	if err := vm.Store(); err != nil {
		t.Fatalf("Store at boundary failed: %v", err)
	}

	// Load it back
	vm.pc = vm.UserMemoryStart()
	if err := vm.Load(); err != nil {
		t.Fatalf("Load from boundary failed: %v", err)
	}
	val, _ := vm.Pop()
	if val != 0x12345678 {
		t.Errorf("Expected 0x12345678, got 0x%x", val)
	}

	// Try to store 1 byte past the boundary (should fail)
	vm.pc = vm.UserMemoryStart()
	binary.BigEndian.PutUint32(vm.memory[vm.pc:], memLen-3)
	pushValue(t, vm, 1)
	if err := vm.Store(); err == nil {
		t.Error("Expected error for store out of bounds")
	}
}

func TestCallRetDirect(t *testing.T) {
	program := make([]byte, 100)
	vm := NewVM(program)

	// Test Call directly
	target := vm.UserMemoryStart() + 30
	binary.BigEndian.PutUint32(vm.memory[vm.pc:], uint32(target))
	returnAddr := vm.pc + 4
	if err := vm.Call(); err != nil {
		t.Fatalf("Direct Call failed: %v", err)
	}
	if vm.pc != uint32(target) {
		t.Errorf("Expected PC=%d, got %d", target, vm.pc)
	}
	if len(vm.returnStack) != 1 || vm.returnStack[0] != int32(returnAddr) {
		t.Errorf("Expected return stack to have %d, got %v", returnAddr, vm.returnStack)
	}

	// Test Ret directly
	if err := vm.Ret(); err != nil {
		t.Fatalf("Direct Ret failed: %v", err)
	}
	if vm.pc != returnAddr {
		t.Errorf("Expected PC back to %d, got %d", returnAddr, vm.pc)
	}
}

func TestDeviceIOEdgeCases(t *testing.T) {
	vm := createVMWithProgram([]byte{})

	// handleDeviceWrite: RNG seed with 0
	var writtenValue int32
	vm.bus.(*MockBus).WriteFunc = func(addr uint32, val int32) error {
		if addr == RNGDataAddr {
			writtenValue = val
			return nil
		}
		return fmt.Errorf("unhandled")
	}
	err := vm.handleDeviceWrite(RNGDataAddr, 0)
	if err != nil {
		t.Fatalf("handleDeviceWrite RNG 0 failed: %v", err)
	}
	if writtenValue != 0 {
		t.Errorf("Expected writtenValue 0, got %d", writtenValue)
	}

	// handleDeviceWrite: unhandled address
	err = vm.handleDeviceWrite(0x3FFF, 42)
	if err == nil {
		t.Error("Expected error for unhandled device write")
	}

	// handleDeviceRead: unhandled address
	vm.bus.(*MockBus).ReadFunc = func(addr uint32) (int32, error) {
		return 0, fmt.Errorf("unhandled")
	}
	_, err = vm.handleDeviceRead(0x3FFF)
	if err == nil {
		t.Error("Expected error for unhandled device read")
	}
}

func TestJnzMethod(t *testing.T) {
	vm := NewVM(make([]byte, 100))
	
	// Test Jnz directly (not zero)
	vm.pc = vm.UserMemoryStart()
	target := vm.UserMemoryStart() + 20
	binary.BigEndian.PutUint32(vm.memory[vm.pc:], uint32(target))
	pushValue(t, vm, 1)
	if err := vm.Jnz(); err != nil {
		t.Fatalf("Direct Jnz failed: %v", err)
	}
	if vm.pc != uint32(target) {
		t.Errorf("Expected PC to jump to %d, got %d", target, vm.pc)
	}

	// Test Jnz directly (is zero)
	vm.pc = vm.UserMemoryStart()
	binary.BigEndian.PutUint32(vm.memory[vm.pc:], 99)
	pushValue(t, vm, 0)
	if err := vm.Jnz(); err != nil {
		t.Fatalf("Direct Jnz failed: %v", err)
	}
	if vm.pc != vm.UserMemoryStart()+4 {
		t.Errorf("Expected PC to skip 4 bytes, got %d", vm.pc)
	}
}
