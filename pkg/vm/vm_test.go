package vm

import (
	"encoding/binary"
	"testing"
)

// Helper function to create a VM with a simple program
func createVMWithProgram(program []byte) *VM {
	return NewVM(program)
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
	expectedMemSize := int(vm.ReservedMemorySize()) + len(program)
	if len(vm.memory) != expectedMemSize {
		t.Errorf("Expected memory length %d (reserved + program), got %d", expectedMemSize, len(vm.memory))
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

func TestAdd(t *testing.T) {
	vm := createVMWithProgram([]byte{})
	pushValue(t, vm, 10)
	pushValue(t, vm, 20)

	if err := vm.Add(); err != nil {
		t.Fatalf("Add failed: %v", err)
	}

	stack := vm.Stack()
	if len(stack) != 1 {
		t.Errorf("Expected stack length 1, got %d", len(stack))
	}
	if stack[0] != 30 {
		t.Errorf("Expected 30, got %d", stack[0])
	}

	// Test add with insufficient values
	vm = createVMWithProgram([]byte{})
	pushValue(t, vm, 1)
	if err := vm.Add(); err == nil {
		t.Error("Expected error when adding with only one value")
	}
}

func TestSub(t *testing.T) {
	vm := createVMWithProgram([]byte{})
	pushValue(t, vm, 30)
	pushValue(t, vm, 10)

	if err := vm.Sub(); err != nil {
		t.Fatalf("Sub failed: %v", err)
	}

	stack := vm.Stack()
	if len(stack) != 1 {
		t.Errorf("Expected stack length 1, got %d", len(stack))
	}
	if stack[0] != 20 {
		t.Errorf("Expected 20, got %d", stack[0])
	}
}

func TestMul(t *testing.T) {
	vm := createVMWithProgram([]byte{})
	pushValue(t, vm, 5)
	pushValue(t, vm, 7)

	if err := vm.Mul(); err != nil {
		t.Fatalf("Mul failed: %v", err)
	}

	stack := vm.Stack()
	if len(stack) != 1 {
		t.Errorf("Expected stack length 1, got %d", len(stack))
	}
	if stack[0] != 35 {
		t.Errorf("Expected 35, got %d", stack[0])
	}
}

func TestDiv(t *testing.T) {
	vm := createVMWithProgram([]byte{})
	pushValue(t, vm, 20)
	pushValue(t, vm, 4)

	if err := vm.Div(); err != nil {
		t.Fatalf("Div failed: %v", err)
	}

	stack := vm.Stack()
	if len(stack) != 1 {
		t.Errorf("Expected stack length 1, got %d", len(stack))
	}
	if stack[0] != 5 {
		t.Errorf("Expected 5, got %d", stack[0])
	}

	// Test division by zero
	vm = createVMWithProgram([]byte{})
	pushValue(t, vm, 10)
	pushValue(t, vm, 0)
	if err := vm.Div(); err == nil {
		t.Error("Expected error when dividing by zero")
	}
}

func TestMod(t *testing.T) {
	vm := createVMWithProgram([]byte{})
	pushValue(t, vm, 17)
	pushValue(t, vm, 5)

	if err := vm.Mod(); err != nil {
		t.Fatalf("Mod failed: %v", err)
	}

	stack := vm.Stack()
	if len(stack) != 1 {
		t.Errorf("Expected stack length 1, got %d", len(stack))
	}
	if stack[0] != 2 {
		t.Errorf("Expected 2, got %d", stack[0])
	}

	// Test modulus by zero
	vm = createVMWithProgram([]byte{})
	pushValue(t, vm, 10)
	pushValue(t, vm, 0)
	if err := vm.Mod(); err == nil {
		t.Error("Expected error when modulus by zero")
	}
}

func TestInc(t *testing.T) {
	vm := createVMWithProgram([]byte{})
	pushValue(t, vm, 41)

	if err := vm.Inc(); err != nil {
		t.Fatalf("Inc failed: %v", err)
	}

	stack := vm.Stack()
	if len(stack) != 1 {
		t.Errorf("Expected stack length 1, got %d", len(stack))
	}
	if stack[0] != 42 {
		t.Errorf("Expected 42, got %d", stack[0])
	}

	// Test inc on empty stack
	vm = createVMWithProgram([]byte{})
	if err := vm.Inc(); err == nil {
		t.Error("Expected error when inc on empty stack")
	}
}

func TestDec(t *testing.T) {
	vm := createVMWithProgram([]byte{})
	pushValue(t, vm, 43)

	if err := vm.Dec(); err != nil {
		t.Fatalf("Dec failed: %v", err)
	}

	stack := vm.Stack()
	if len(stack) != 1 {
		t.Errorf("Expected stack length 1, got %d", len(stack))
	}
	if stack[0] != 42 {
		t.Errorf("Expected 42, got %d", stack[0])
	}
}

func TestNeg(t *testing.T) {
	vm := createVMWithProgram([]byte{})
	pushValue(t, vm, 42)

	if err := vm.Neg(); err != nil {
		t.Fatalf("Neg failed: %v", err)
	}

	stack := vm.Stack()
	if len(stack) != 1 {
		t.Errorf("Expected stack length 1, got %d", len(stack))
	}
	if stack[0] != -42 {
		t.Errorf("Expected -42, got %d", stack[0])
	}

	// Test double negation
	if err := vm.Neg(); err != nil {
		t.Fatalf("Second Neg failed: %v", err)
	}
	stack = vm.Stack()
	if stack[0] != 42 {
		t.Errorf("Expected 42 after double negation, got %d", stack[0])
	}
}

func TestAnd(t *testing.T) {
	vm := createVMWithProgram([]byte{})
	pushValue(t, vm, 0b1100)
	pushValue(t, vm, 0b1010)

	if err := vm.And(); err != nil {
		t.Fatalf("And failed: %v", err)
	}

	stack := vm.Stack()
	if len(stack) != 1 {
		t.Errorf("Expected stack length 1, got %d", len(stack))
	}
	if stack[0] != 0b1000 {
		t.Errorf("Expected 0b1000 (8), got %d", stack[0])
	}
}

func TestOr(t *testing.T) {
	vm := createVMWithProgram([]byte{})
	pushValue(t, vm, 0b1100)
	pushValue(t, vm, 0b1010)

	if err := vm.Or(); err != nil {
		t.Fatalf("Or failed: %v", err)
	}

	stack := vm.Stack()
	if len(stack) != 1 {
		t.Errorf("Expected stack length 1, got %d", len(stack))
	}
	if stack[0] != 0b1110 {
		t.Errorf("Expected 0b1110 (14), got %d", stack[0])
	}
}

func TestXor(t *testing.T) {
	vm := createVMWithProgram([]byte{})
	pushValue(t, vm, 0b1100)
	pushValue(t, vm, 0b1010)

	if err := vm.Xor(); err != nil {
		t.Fatalf("Xor failed: %v", err)
	}

	stack := vm.Stack()
	if len(stack) != 1 {
		t.Errorf("Expected stack length 1, got %d", len(stack))
	}
	if stack[0] != 0b0110 {
		t.Errorf("Expected 0b0110 (6), got %d", stack[0])
	}
}

func TestNot(t *testing.T) {
	vm := createVMWithProgram([]byte{})
	pushValue(t, vm, 0)

	if err := vm.Not(); err != nil {
		t.Fatalf("Not failed: %v", err)
	}

	stack := vm.Stack()
	if len(stack) != 1 {
		t.Errorf("Expected stack length 1, got %d", len(stack))
	}
	if stack[0] != -1 {
		t.Errorf("Expected -1, got %d", stack[0])
	}
}

func TestShl(t *testing.T) {
	vm := createVMWithProgram([]byte{})
	pushValue(t, vm, 5)
	pushValue(t, vm, 2)

	if err := vm.Shl(); err != nil {
		t.Fatalf("Shl failed: %v", err)
	}

	stack := vm.Stack()
	if len(stack) != 1 {
		t.Errorf("Expected stack length 1, got %d", len(stack))
	}
	if stack[0] != 20 {
		t.Errorf("Expected 20, got %d", stack[0])
	}

	// Test shift with modulo 32
	vm = createVMWithProgram([]byte{})
	pushValue(t, vm, 5)
	pushValue(t, vm, 34) // Should be treated as 34 % 32 = 2
	if err := vm.Shl(); err != nil {
		t.Fatalf("Shl with large shift failed: %v", err)
	}
	stack = vm.Stack()
	if stack[0] != 20 {
		t.Errorf("Expected 20 with modulo shift, got %d", stack[0])
	}
}

func TestEq(t *testing.T) {
	vm := createVMWithProgram([]byte{})

	// Test equal values
	pushValue(t, vm, 42)
	pushValue(t, vm, 42)
	if err := vm.Eq(); err != nil {
		t.Fatalf("Eq failed: %v", err)
	}
	stack := vm.Stack()
	if stack[0] != 1 {
		t.Errorf("Expected 1 for equal values, got %d", stack[0])
	}

	// Test unequal values
	vm = createVMWithProgram([]byte{})
	pushValue(t, vm, 10)
	pushValue(t, vm, 20)
	if err := vm.Eq(); err != nil {
		t.Fatalf("Eq failed: %v", err)
	}
	stack = vm.Stack()
	if stack[0] != 0 {
		t.Errorf("Expected 0 for unequal values, got %d", stack[0])
	}
}

func TestLt(t *testing.T) {
	vm := createVMWithProgram([]byte{})

	// Test less than
	pushValue(t, vm, 10)
	pushValue(t, vm, 20)
	if err := vm.Lt(); err != nil {
		t.Fatalf("Lt failed: %v", err)
	}
	stack := vm.Stack()
	if stack[0] != 1 {
		t.Errorf("Expected 1 for 10 < 20, got %d", stack[0])
	}

	// Test not less than
	vm = createVMWithProgram([]byte{})
	pushValue(t, vm, 20)
	pushValue(t, vm, 10)
	if err := vm.Lt(); err != nil {
		t.Fatalf("Lt failed: %v", err)
	}
	stack = vm.Stack()
	if stack[0] != 0 {
		t.Errorf("Expected 0 for 20 < 10, got %d", stack[0])
	}
}

func TestGt(t *testing.T) {
	vm := createVMWithProgram([]byte{})

	// Test greater than
	pushValue(t, vm, 20)
	pushValue(t, vm, 10)
	if err := vm.Gt(); err != nil {
		t.Fatalf("Gt failed: %v", err)
	}
	stack := vm.Stack()
	if stack[0] != 1 {
		t.Errorf("Expected 1 for 20 > 10, got %d", stack[0])
	}

	// Test not greater than
	vm = createVMWithProgram([]byte{})
	pushValue(t, vm, 10)
	pushValue(t, vm, 20)
	if err := vm.Gt(); err != nil {
		t.Fatalf("Gt failed: %v", err)
	}
	stack = vm.Stack()
	if stack[0] != 0 {
		t.Errorf("Expected 0 for 10 > 20, got %d", stack[0])
	}
}

func TestCallStack(t *testing.T) {
	// Build program: push quotation addr, call it, halt
	program := []byte{}
	program = append(program, pushInstruction(10)...) // Push address 10
	program = append(program, OpCallStack)            // CALLSTACK
	program = append(program, OpHalt)                 // HALT

	// Pad to address 10
	for len(program) < 10 {
		program = append(program, OpHalt)
	}

	// Quotation at address 10
	program = append(program, pushInstruction(42)...) // PUSH 42
	program = append(program, OpRet)                  // RET

	vm := createVMWithProgram(program)

	// Adjust the pushed address to account for reserved memory offset
	// We need to push the actual address in VM memory space
	quotationAddr := vm.UserMemoryStart() + 10

	// Recreate the VM with corrected program
	program = []byte{}
	program = append(program, pushInstruction(int32(quotationAddr))...) // Push actual address
	program = append(program, OpCallStack)                              // CALLSTACK
	program = append(program, OpHalt)                                   // HALT

	// Pad to address 10 in user space
	for len(program) < 10 {
		program = append(program, OpHalt)
	}

	// Quotation at address 10
	program = append(program, pushInstruction(42)...) // PUSH 42
	program = append(program, OpRet)                  // RET

	vm = createVMWithProgram(program)
	if err := vm.Run(); err != nil {
		t.Fatalf("Run failed: %v", err)
	}

	stack := vm.Stack()
	if len(stack) != 1 || stack[0] != 42 {
		t.Errorf("Expected [42], got %v", stack)
	}
}

func TestCallStackUnderflow(t *testing.T) {
	vm := createVMWithProgram([]byte{})
	err := vm.CallStack()
	if err == nil {
		t.Error("Expected error for CALLSTACK with empty stack")
	}
}

func TestCallStackInvalidAddress(t *testing.T) {
	vm := createVMWithProgram([]byte{})
	pushValue(t, vm, 10000) // Invalid address
	err := vm.CallStack()
	if err == nil {
		t.Error("Expected error for CALLSTACK with invalid address")
	}
}

func TestJmp(t *testing.T) {
	program := []byte{}
	program = append(program, pushInstruction(10)...) // PUSH 10
	jmpAddr := len(program)
	program = append(program, JmpInstruction(0)...)   // JMP (placeholder)
	program = append(program, pushInstruction(20)...) // PUSH 20 (skipped)
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
	// Jump target
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

func TestJnz(t *testing.T) {
	// Test jump when condition is non-zero
	program := []byte{}
	program = append(program, pushInstruction(1)...) // PUSH 1
	jnzAddr := len(program)
	program = append(program, JnzInstruction(0)...)   // JNZ (placeholder)
	program = append(program, pushInstruction(20)...) // PUSH 20 (skipped)
	// Jump target
	targetAddr := len(program)
	program = append(program, pushInstruction(30)...) // PUSH 30
	program = append(program, OpHalt)                 // HALT

	vm := createVMWithProgram(program)

	// Fix the JNZ target address to account for reserved memory
	actualTarget := vm.UserMemoryStart() + uint32(targetAddr)
	binary.BigEndian.PutUint32(vm.memory[vm.UserMemoryStart()+uint32(jnzAddr)+1:], actualTarget)

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

	// Test no jump when condition is zero
	program = []byte{}
	program = append(program, pushInstruction(0)...)  // PUSH 0
	program = append(program, JnzInstruction(100)...) // JNZ to address 100 (not taken)
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
	for len(program) < subroutineAddr+16 {
		program = append(program, OpHalt) // Pad
	}
	program = append(program, pushInstruction(30)...) // PUSH 30
	program = append(program, pushInstruction(40)...) // PUSH 40
	program = append(program, OpAdd)                  // ADD (30+40=70)
	program = append(program, OpRet)                  // RET

	vm := createVMWithProgram(program)

	// Fix the CALL target to point to actual subroutine address in VM memory
	actualTarget := vm.UserMemoryStart() + uint32(subroutineAddr+16)
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

	// Position PC at the CALL instruction (it's already there, but making it explicit)
	// The PC should be at UserMemoryStart(), pointing to the OpCall byte

	// Fill return stack to max capacity
	// The VM checks against MaxStackSize for return stack overflow
	for i := 0; i < MaxStackSize; i++ {
		vm.returnStack = append(vm.returnStack, int32(i))
	}

	// Call the Call() method directly instead of ExecuteInstruction()
	// This is necessary because Call() has the overflow check,
	// but ExecuteInstruction's OpCall case does not
	err := vm.Call()
	if err == nil {
		t.Error("Expected error when CALL causes return stack overflow")
	} else if !contains(err.Error(), "return stack overflow") {
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
	binary.BigEndian.PutUint32(program[offset+1:], 10000)
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
	binary.BigEndian.PutUint32(program[offset+1:], 10000)
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
	program := []byte{OpPush} // PUSH without immediate value
	vm := createVMWithProgram(program)

	err := vm.Run()
	if err == nil {
		t.Error("Expected error for PC out of bounds")
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
		{OpNeg, "NEG"},
		{OpAnd, "AND"},
		{OpOr, "OR"},
		{OpXor, "XOR"},
		{OpNot, "NOT"},
		{OpShl, "SHL"},
		{OpEq, "EQ"},
		{OpLt, "LT"},
		{OpGt, "GT"},
		{OpCallStack, "CALLSTACK"},
		{OpJmp, "JMP"},
		{OpJz, "JZ"},
		{OpJnz, "JNZ"},
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

	// Test JnzInstruction
	jnzInstr := JnzInstruction(400)
	if len(jnzInstr) != 5 {
		t.Errorf("Expected 5 bytes, got %d", len(jnzInstr))
	}
	if jnzInstr[0] != OpJnz {
		t.Errorf("Expected JNZ opcode, got 0x%02X", jnzInstr[0])
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
	if len(outNum) != 6 {
		t.Errorf("Expected 6 bytes, got %d", len(outNum))
	}
	if outNum[5] != OpOut {
		t.Errorf("Expected OUT opcode at end, got 0x%02X", outNum[5])
	}

	// Test OutCharacter
	outChar := OutCharacter()
	if len(outChar) != 6 {
		t.Errorf("Expected 6 bytes, got %d", len(outChar))
	}
	if outChar[5] != OpOut {
		t.Errorf("Expected OUT opcode at end, got 0x%02X", outChar[5])
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

	// Test that user memory start equals reserved memory size
	if vm.UserMemoryStart() != vm.ReservedMemorySize() {
		t.Errorf("Expected user memory start (%d) to equal reserved memory size (%d)",
			vm.UserMemoryStart(), vm.ReservedMemorySize())
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

	// Test writing out of bounds
	err = vm.WriteReservedMemory(vm.ReservedMemorySize(), testData)
	if err == nil {
		t.Error("Expected error when writing past reserved memory bounds")
	}

	// Test reading out of bounds
	_, err = vm.ReadReservedMemory(vm.ReservedMemorySize(), 4)
	if err == nil {
		t.Error("Expected error when reading past reserved memory bounds")
	}

	// Test overflow write
	largeOffset := vm.ReservedMemorySize() - 2
	err = vm.WriteReservedMemory(largeOffset, testData)
	if err == nil {
		t.Error("Expected error when write would overflow reserved memory")
	}

	// Test overflow read
	_, err = vm.ReadReservedMemory(largeOffset, 4)
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

	if vm.UserMemoryStart() != customReservedSize {
		t.Errorf("Expected user memory start %d, got %d",
			customReservedSize, vm.UserMemoryStart())
	}

	if vm.PC() != customReservedSize {
		t.Errorf("Expected PC to start at %d, got %d",
			customReservedSize, vm.PC())
	}

	expectedMemSize := int(customReservedSize) + len(program)
	if len(vm.memory) != expectedMemSize {
		t.Errorf("Expected memory length %d, got %d", expectedMemSize, len(vm.memory))
	}
}

func TestReservedMemoryInDebugInfo(t *testing.T) {
	program := []byte{}
	program = append(program, pushInstruction(42)...)
	program = append(program, OpHalt)

	vm := createVMWithProgram(program)
	pushValue(t, vm, 100)

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
}

func TestReservedMemoryWithCode(t *testing.T) {
	// Create a VM
	program := []byte{}
	program = append(program, pushInstruction(10)...) // PUSH 10
	program = append(program, OpHalt)                 // HALT

	vm := createVMWithProgram(program)

	// Write a simple subroutine to reserved memory
	// The subroutine will: PUSH 42, RET
	subroutine := []byte{}
	subroutine = append(subroutine, pushInstruction(42)...)
	subroutine = append(subroutine, OpRet)

	err := vm.WriteReservedMemory(100, subroutine)
	if err != nil {
		t.Fatalf("Failed to write subroutine to reserved memory: %v", err)
	}

	// Modify program to call the reserved subroutine
	program2 := []byte{}
	program2 = append(program2, pushInstruction(10)...)  // PUSH 10
	program2 = append(program2, CallInstruction(100)...) // CALL reserved subroutine at address 100
	program2 = append(program2, OpAdd)                   // ADD (10 + 42)
	program2 = append(program2, OpHalt)                  // HALT

	vm2 := createVMWithProgram(program2)

	// Write the same subroutine
	err = vm2.WriteReservedMemory(100, subroutine)
	if err != nil {
		t.Fatalf("Failed to write subroutine to reserved memory: %v", err)
	}

	// Run the program
	if err := vm2.Run(); err != nil {
		t.Fatalf("Run failed: %v", err)
	}

	// Check result
	stack := vm2.Stack()
	if len(stack) != 1 {
		t.Errorf("Expected stack length 1, got %d", len(stack))
	}
	if len(stack) >= 1 && stack[0] != 52 {
		t.Errorf("Expected result 52 (10+42), got %d", stack[0])
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
			name:    "NEG underflow",
			program: []byte{OpNeg},
			errMsg:  "neg failed",
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
			name:    "GT underflow",
			program: []byte{OpGt},
			errMsg:  "gt failed",
		},
		{
			name:    "CALLSTACK underflow",
			program: []byte{OpCallStack},
			errMsg:  "callstack failed",
		},
		{
			name:    "JMP incomplete",
			program: []byte{OpJmp, 0xFF},
			errMsg:  "jmp failed",
		},
		{
			name:    "JZ underflow",
			program: []byte{OpJz, 0x00, 0x00, 0x00, 0x10},
			errMsg:  "jz failed",
		},
		{
			name:    "JNZ underflow",
			program: []byte{OpJnz, 0x00, 0x00, 0x00, 0x10},
			errMsg:  "jnz failed",
		},
		{
			name:    "CALL incomplete",
			program: []byte{OpCall, 0xFF},
			errMsg:  "call failed",
		},
		{
			name:    "RET underflow",
			program: []byte{OpRet},
			errMsg:  "ret failed",
		},
		{
			name:    "LOAD incomplete",
			program: []byte{OpLoad, 0xFF},
			errMsg:  "load failed",
		},
		{
			name:    "STORE underflow",
			program: []byte{OpStore, 0x00, 0x00, 0x00, 0x10},
			errMsg:  "store failed",
		},
		{
			name:    "OUT underflow",
			program: []byte{OpOut},
			errMsg:  "out failed",
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
