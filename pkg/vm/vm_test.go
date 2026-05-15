package vm

import (
	"encoding/binary"
	"testing"
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
	vm := NewVMWithMemorySize(program, uint32(UserMemoryOffset)+uint32(len(program)))
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
	if expectedMemSize < 0x800000 {
		expectedMemSize = 0x800000
	}
	if len(vm.memory) != expectedMemSize {
		t.Errorf("Expected memory length %d, got %d", expectedMemSize, len(vm.memory))
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
		t.Errorf("Expected stack value 42, got %d", stack[0])
	}

	// Test pop
	val, err := vm.Pop()
	if err != nil {
		t.Fatalf("Pop failed: %v", err)
	}
	if val != 42 {
		t.Errorf("Expected popped value 42, got %d", val)
	}
	if len(vm.Stack()) != 0 {
		t.Errorf("Expected empty stack, got length %d", len(vm.Stack()))
	}
}

func TestOpPush(t *testing.T) {
	program := make([]byte, 5)
	program[0] = OpPush
	binary.BigEndian.PutUint32(program[1:], 12345)

	vm := createVMWithProgram(program)

	if _, err := vm.Step(); err != nil {
		t.Fatalf("Step failed: %v", err)
	}

	stack := vm.Stack()
	if len(stack) != 1 {
		t.Errorf("Expected stack length 1, got %d", len(stack))
	}
	if stack[0] != 12345 {
		t.Errorf("Expected stack value 12345, got %d", stack[0])
	}
}

func TestOpPop(t *testing.T) {
	program := []byte{OpPop, OpHalt}
	vm := createVMWithProgram(program)

	pushValue(t, vm, 42)

	if _, err := vm.Step(); err != nil {
		t.Fatalf("Step failed: %v", err)
	}

	if len(vm.Stack()) != 0 {
		t.Errorf("Expected empty stack, got length %d", len(vm.Stack()))
	}
}

func TestOpDup(t *testing.T) {
	program := []byte{OpDup, OpHalt}
	vm := createVMWithProgram(program)

	pushValue(t, vm, 42)

	if _, err := vm.Step(); err != nil {
		t.Fatalf("Step failed: %v", err)
	}

	stack := vm.Stack()
	if len(stack) != 2 {
		t.Errorf("Expected stack length 2, got %d", len(stack))
	}
	if stack[0] != 42 || stack[1] != 42 {
		t.Errorf("Expected stack values [42, 42], got %v", stack)
	}
}

func TestOpSwap(t *testing.T) {
	program := []byte{OpSwap, OpHalt}
	vm := createVMWithProgram(program)

	pushValue(t, vm, 1)
	pushValue(t, vm, 2)

	if _, err := vm.Step(); err != nil {
		t.Fatalf("Step failed: %v", err)
	}

	stack := vm.Stack()
	if len(stack) != 2 {
		t.Errorf("Expected stack length 2, got %d", len(stack))
	}
	if stack[0] != 2 || stack[1] != 1 {
		t.Errorf("Expected stack values [2, 1], got %v", stack)
	}
}

func TestOpOver(t *testing.T) {
	program := []byte{OpOver, OpHalt}
	vm := createVMWithProgram(program)

	pushValue(t, vm, 1)
	pushValue(t, vm, 2)

	if _, err := vm.Step(); err != nil {
		t.Fatalf("Step failed: %v", err)
	}

	stack := vm.Stack()
	if len(stack) != 3 {
		t.Errorf("Expected stack length 3, got %d", len(stack))
	}
	if stack[0] != 1 || stack[1] != 2 || stack[2] != 1 {
		t.Errorf("Expected stack values [1, 2, 1], got %v", stack)
	}
}

func TestOpRot(t *testing.T) {
	program := []byte{OpRot, OpHalt}
	vm := createVMWithProgram(program)

	pushValue(t, vm, 1)
	pushValue(t, vm, 2)
	pushValue(t, vm, 3)

	if _, err := vm.Step(); err != nil {
		t.Fatalf("Step failed: %v", err)
	}

	stack := vm.Stack()
	if len(stack) != 3 {
		t.Errorf("Expected stack length 3, got %d", len(stack))
	}
	if stack[0] != 2 || stack[1] != 3 || stack[2] != 1 {
		t.Errorf("Expected stack values [2, 3, 1], got %v", stack)
	}
}

func TestArithmetic(t *testing.T) {
	tests := []struct {
		name     string
		op       byte
		a, b     int32
		expected int32
	}{
		{"Add", OpAdd, 10, 20, 30},
		{"Sub", OpSub, 30, 10, 20},
		{"Mul", OpMul, 5, 6, 30},
		{"Div", OpDiv, 30, 6, 5},
		{"Mod", OpMod, 35, 6, 5},
		{"And", OpAnd, 0xFF, 0x0F, 0x0F},
		{"Or", OpOr, 0xF0, 0x0F, 0xFF},
		{"Xor", OpXor, 0xFF, 0xAA, 0x55},
		{"Shl", OpShl, 1, 4, 16},
		{"Shr", OpShr, 16, 4, 1},
		{"Sar", OpSar, -16, 4, -1},
	}

	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			program := []byte{tt.op, OpHalt}
			vm := createVMWithProgram(program)

			pushValue(t, vm, tt.a)
			pushValue(t, vm, tt.b)

			if _, err := vm.Step(); err != nil {
				t.Fatalf("Step failed: %v", err)
			}

			stack := vm.Stack()
			if len(stack) != 1 {
				t.Errorf("Expected stack length 1, got %d", len(stack))
			}
			if stack[0] != tt.expected {
				t.Errorf("Expected stack value %d, got %d", tt.expected, stack[0])
			}
		})
	}
}

func TestComparison(t *testing.T) {
	tests := []struct {
		name     string
		op       byte
		a, b     int32
		expected bool
	}{
		{"EqTrue", OpEq, 10, 10, true},
		{"EqFalse", OpEq, 10, 20, false},
		{"LtTrue", OpLt, 10, 20, true},
		{"LtFalse", OpLt, 20, 10, false},
		{"GtTrue", OpGt, 20, 10, true},
		{"GtFalse", OpGt, 10, 20, false},
		{"LteTrue", OpLte, 10, 10, true},
		{"LteFalse", OpLte, 20, 10, false},
		{"GteTrue", OpGte, 10, 10, true},
		{"GteFalse", OpGte, 10, 20, false},
		{"NeqTrue", OpNeq, 10, 20, true},
		{"NeqFalse", OpNeq, 10, 10, false},
	}

	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			program := []byte{tt.op, OpHalt}
			vm := createVMWithProgram(program)

			pushValue(t, vm, tt.a)
			pushValue(t, vm, tt.b)

			if _, err := vm.Step(); err != nil {
				t.Fatalf("Step failed: %v", err)
			}

			stack := vm.Stack()
			if len(stack) != 1 {
				t.Errorf("Expected stack length 1, got %d", len(stack))
			}
			var expected int32 = 0
			if tt.expected {
				expected = 1
			}
			if stack[0] != expected {
				t.Errorf("Expected stack value %d, got %d", expected, stack[0])
			}
		})
	}
}

func TestUnary(t *testing.T) {
	tests := []struct {
		name     string
		op       byte
		a        int32
		expected int32
	}{
		{"Inc", OpInc, 10, 11},
		{"Dec", OpDec, 10, 9},
		{"Not", OpNot, 0, -1},
		{"Neg", OpNeg, 10, -10},
		{"AbsPositive", OpAbs, 10, 10},
		{"AbsNegative", OpAbs, -10, 10},
	}

	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			program := []byte{tt.op, OpHalt}
			vm := createVMWithProgram(program)

			pushValue(t, vm, tt.a)

			if _, err := vm.Step(); err != nil {
				t.Fatalf("Step failed: %v", err)
			}

			stack := vm.Stack()
			if len(stack) != 1 {
				t.Errorf("Expected stack length 1, got %d", len(stack))
			}
			if stack[0] != tt.expected {
				t.Errorf("Expected stack value %d, got %d", tt.expected, stack[0])
			}
		})
	}
}

func TestJmp(t *testing.T) {
	program := make([]byte, 10)
	target := UserMemoryOffset + 5
	program[0] = OpJmp
	binary.BigEndian.PutUint32(program[1:], uint32(target))
	program[5] = OpHalt

	vm := createVMWithProgram(program)

	if _, err := vm.Step(); err != nil {
		t.Fatalf("Step failed: %v", err)
	}

	if vm.PC() != uint32(target) {
		t.Errorf("Expected PC=%d, got %d", target, vm.PC())
	}
}

func TestJz(t *testing.T) {
	program := make([]byte, 15)
	target := UserMemoryOffset + 8 // distinct from fall-through PC (UserMemoryOffset+5)
	program[0] = OpJz
	binary.BigEndian.PutUint32(program[1:], uint32(target))
	program[5] = OpHalt
	program[8] = OpHalt

	// Test Jz (condition is zero) — branch taken
	vm := createVMWithProgram(program)
	pushValue(t, vm, 0)

	if _, err := vm.Step(); err != nil {
		t.Fatalf("Step failed: %v", err)
	}

	if vm.PC() != uint32(target) {
		t.Errorf("Expected PC=%d, got %d", target, vm.PC())
	}

	// Test Jz (condition is non-zero) — branch not taken
	vm = createVMWithProgram(program)
	pushValue(t, vm, 1)

	if _, err := vm.Step(); err != nil {
		t.Fatalf("Step failed: %v", err)
	}

	if vm.PC() == uint32(target) {
		t.Errorf("Expected PC != %d", target)
	}
}

func TestJnz(t *testing.T) {
	program := make([]byte, 15)
	target := UserMemoryOffset + 8 // distinct from fall-through PC (UserMemoryOffset+5)
	program[0] = OpJnz
	binary.BigEndian.PutUint32(program[1:], uint32(target))
	program[5] = OpHalt
	program[8] = OpHalt

	// Test Jnz (condition is non-zero) — branch taken
	vm := createVMWithProgram(program)
	pushValue(t, vm, 1)

	if _, err := vm.Step(); err != nil {
		t.Fatalf("Step failed: %v", err)
	}

	if vm.PC() != uint32(target) {
		t.Errorf("Expected PC=%d, got %d", target, vm.PC())
	}

	// Test Jnz (condition is zero) — branch not taken
	vm = createVMWithProgram(program)
	pushValue(t, vm, 0)

	if _, err := vm.Step(); err != nil {
		t.Fatalf("Step failed: %v", err)
	}

	if vm.PC() == uint32(target) {
		t.Errorf("Expected PC != %d", target)
	}
}

func TestCallRet(t *testing.T) {
	program := make([]byte, 20)
	funcAddr := UserMemoryOffset + 10
	program[0] = OpCall
	binary.BigEndian.PutUint32(program[1:], uint32(funcAddr))
	program[5] = OpHalt

	program[10] = OpRet

	vm := createVMWithProgram(program)

	// Step 1: Call
	if _, err := vm.Step(); err != nil {
		t.Fatalf("Step 1 failed: %v", err)
	}
	if vm.PC() != uint32(funcAddr) {
		t.Errorf("Expected PC=%d after call, got %d", funcAddr, vm.PC())
	}
	if len(vm.ReturnStack()) != 1 {
		t.Errorf("Expected return stack length 1, got %d", len(vm.ReturnStack()))
	}

	// Step 2: Ret
	if _, err := vm.Step(); err != nil {
		t.Fatalf("Step 2 failed: %v", err)
	}
	if vm.PC() != UserMemoryOffset+5 {
		t.Errorf("Expected PC=%d after ret, got %d", UserMemoryOffset+5, vm.PC())
	}
	if len(vm.ReturnStack()) != 0 {
		t.Errorf("Expected empty return stack, got length %d", len(vm.ReturnStack()))
	}
}

func TestLoadStore(t *testing.T) {
	program := make([]byte, 120) // large enough for addr=UserMemoryOffset+100
	addr := uint32(UserMemoryOffset + 100)

	// Store 42 at addr
	program[0] = OpStore
	binary.BigEndian.PutUint32(program[1:], addr)

	// Load from addr
	program[5] = OpLoad
	binary.BigEndian.PutUint32(program[6:], addr)

	program[11] = OpHalt

	vm := createVMWithProgram(program)
	pushValue(t, vm, 42)

	// Step 1: Store
	if _, err := vm.Step(); err != nil {
		t.Fatalf("Step 1 failed: %v", err)
	}
	if binary.BigEndian.Uint32(vm.memory[addr:addr+4]) != 42 {
		t.Errorf("Expected value 42 in memory, got %d", binary.BigEndian.Uint32(vm.memory[addr:addr+4]))
	}

	// Step 2: Load
	if _, err := vm.Step(); err != nil {
		t.Fatalf("Step 2 failed: %v", err)
	}
	stack := vm.Stack()
	if len(stack) != 1 {
		t.Errorf("Expected stack length 1, got %d", len(stack))
	}
	if stack[0] != 42 {
		t.Errorf("Expected stack value 42, got %d", stack[0])
	}
}

func TestLoadIStoreI(t *testing.T) {
	program := make([]byte, 120) // large enough for addr=UserMemoryOffset+100
	program[0] = OpStoreI
	program[1] = OpLoadI
	program[2] = OpHalt
	vm := createVMWithProgram(program)
	addr := int32(UserMemoryOffset + 100)
	val := int32(12345)

	pushValue(t, vm, val)
	pushValue(t, vm, addr)

	// Step 1: StoreI
	if _, err := vm.Step(); err != nil {
		t.Fatalf("Step 1 failed: %v", err)
	}
	if binary.BigEndian.Uint32(vm.memory[addr:addr+4]) != uint32(val) {
		t.Errorf("Expected value %d in memory, got %d", val, binary.BigEndian.Uint32(vm.memory[addr:addr+4]))
	}

	// Step 2: LoadI
	pushValue(t, vm, addr)
	if _, err := vm.Step(); err != nil {
		t.Fatalf("Step 2 failed: %v", err)
	}
	stack := vm.Stack()
	if len(stack) != 1 {
		t.Errorf("Expected stack length 1, got %d", len(stack))
	}
	if stack[0] != val {
		t.Errorf("Expected stack value %d, got %d", val, stack[0])
	}
}

func TestHalt(t *testing.T) {
	program := []byte{OpHalt}
	vm := createVMWithProgram(program)

	if _, err := vm.Step(); err != nil {
		t.Fatalf("Step failed: %v", err)
	}

	if vm.Running() {
		t.Error("Expected VM to stop running after Halt")
	}
}

func TestLoopStack(t *testing.T) {
	program := []byte{OpPushR, OpPushR, OpPeekR, OpPeekR2, OpPopR, OpHalt}
	vm := createVMWithProgram(program)

	pushValue(t, vm, 100)
	pushValue(t, vm, 200)

	// OpPushR (200)
	if _, err := vm.Step(); err != nil {
		t.Fatalf("PushR failed: %v", err)
	}

	// Push 300 for PeekR2 test later
	pushValue(t, vm, 300)
	// OpPushR (300)
	if _, err := vm.Step(); err != nil {
		t.Fatalf("PushR failed: %v", err)
	}

	// OpPeekR (should push 300 to data stack)
	if _, err := vm.Step(); err != nil {
		t.Fatalf("PeekR failed: %v", err)
	}

	// OpPeekR2 (should push 200, 300 to data stack)
	if _, err := vm.Step(); err != nil {
		t.Fatalf("PeekR2 failed: %v", err)
	}

	// OpPopR (should push 300 to data stack)
	if _, err := vm.Step(); err != nil {
		t.Fatalf("PopR failed: %v", err)
	}

	stack := vm.Stack()
	// Stack: [100, 300, 200, 300, 300]
	if len(stack) != 5 {
		t.Errorf("Expected stack length 5, got %d", len(stack))
	}
	if stack[1] != 300 || stack[2] != 200 || stack[3] != 300 || stack[4] != 300 {
		t.Errorf("Unexpected stack values: %v", stack)
	}
}

func TestFrame(t *testing.T) {
	// Frame pops n items: first pop becomes local[0], second becomes local[1], etc.
	// LocalSet pops offset (top), then val.
	// Unframe pops n before restoring the frame pointer.
	program := []byte{OpFrame, OpLocalGet, OpLocalSet, OpLocalGet, OpUnframe, OpHalt}
	vm := createVMWithProgram(program)

	// Push 10 (→ local[1]), 20 (→ local[0]), n=2
	pushValue(t, vm, 10)
	pushValue(t, vm, 20)
	pushValue(t, vm, 2)

	// OpFrame: pops n=2, local[0]=20, local[1]=10
	if _, err := vm.Step(); err != nil {
		t.Fatalf("Frame failed: %v", err)
	}
	if len(vm.stack) != 0 {
		t.Errorf("Expected empty stack after Frame, got length %d", len(vm.stack))
	}

	// OpLocalGet(1) -> 10
	pushValue(t, vm, 1)
	if _, err := vm.Step(); err != nil {
		t.Fatalf("LocalGet failed: %v", err)
	}

	// OpLocalSet(0, 99): LocalSet pops offset first (top), then val
	pushValue(t, vm, 99) // val
	pushValue(t, vm, 0)  // offset (top)
	if _, err := vm.Step(); err != nil {
		t.Fatalf("LocalSet failed: %v", err)
	}

	// OpLocalGet(0) -> 99 (updated)
	pushValue(t, vm, 0)
	if _, err := vm.Step(); err != nil {
		t.Fatalf("LocalGet after LocalSet failed: %v", err)
	}

	// OpUnframe: push n=2 before calling
	pushValue(t, vm, 2)
	if _, err := vm.Step(); err != nil {
		t.Fatalf("Unframe failed: %v", err)
	}

	stack := vm.Stack()
	// Stack: [10, 99]
	if len(stack) != 2 {
		t.Errorf("Expected stack length 2 after Unframe, got %d", len(stack))
	}
	if stack[0] != 10 || stack[1] != 99 {
		t.Errorf("Unexpected stack values: %v", stack)
	}
}

func TestDivmod(t *testing.T) {
	program := []byte{OpDivmod, OpHalt}
	vm := createVMWithProgram(program)

	pushValue(t, vm, 17)
	pushValue(t, vm, 5)

	if _, err := vm.Step(); err != nil {
		t.Fatalf("Step failed: %v", err)
	}

	stack := vm.Stack()
	if len(stack) != 2 {
		t.Errorf("Expected stack length 2, got %d", len(stack))
	}
	if stack[0] != 3 || stack[1] != 2 {
		t.Errorf("Expected quotient 3, remainder 2; got %d, %d", stack[0], stack[1])
	}
}

func TestMinMax(t *testing.T) {
	// Max
	program := []byte{OpMax, OpHalt}
	vm := createVMWithProgram(program)
	pushValue(t, vm, 10)
	pushValue(t, vm, 20)
	if _, err := vm.Step(); err != nil {
		t.Fatalf("Step Max failed: %v", err)
	}
	if vm.stack[0] != 20 {
		t.Errorf("Expected Max 20, got %d", vm.stack[0])
	}

	// Min
	program = []byte{OpMin, OpHalt}
	vm = createVMWithProgram(program)
	pushValue(t, vm, 10)
	pushValue(t, vm, 20)
	if _, err := vm.Step(); err != nil {
		t.Fatalf("Step Min failed: %v", err)
	}
	if vm.stack[0] != 10 {
		t.Errorf("Expected Min 10, got %d", vm.stack[0])
	}
}

func TestPick(t *testing.T) {
	program := []byte{OpPick, OpHalt}
	vm := createVMWithProgram(program)

	pushValue(t, vm, 10)
	pushValue(t, vm, 20)
	pushValue(t, vm, 30)
	pushValue(t, vm, 1) // Pick index 1 (value 20)

	if _, err := vm.Step(); err != nil {
		t.Fatalf("Step failed: %v", err)
	}

	stack := vm.Stack()
	if len(stack) != 4 {
		t.Errorf("Expected stack length 4, got %d", len(stack))
	}
	// Stack should be [10, 20, 30, 20]
	if stack[3] != 20 {
		t.Errorf("Expected top value 20, got %d", stack[3])
	}
}

func TestRoll(t *testing.T) {
	program := []byte{OpRoll, OpHalt}
	vm := createVMWithProgram(program)

	pushValue(t, vm, 10)
	pushValue(t, vm, 20)
	pushValue(t, vm, 30)
	pushValue(t, vm, 2) // Roll index 2 (value 10) to top

	if _, err := vm.Step(); err != nil {
		t.Fatalf("Step failed: %v", err)
	}

	stack := vm.Stack()
	if len(stack) != 3 {
		t.Errorf("Expected stack length 3, got %d", len(stack))
	}
	// Stack was [10, 20, 30], rolling 10 to top -> [20, 30, 10]
	if stack[0] != 20 || stack[1] != 30 || stack[2] != 10 {
		t.Errorf("Expected stack [20, 30, 10], got %v", stack)
	}
}
