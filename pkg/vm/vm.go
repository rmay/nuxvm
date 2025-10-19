// Package vm implements a simple stack-based virtual machine with 32 opcodes.
package vm

import (
	"encoding/binary"
	"fmt"
	"os"
)

// MaxStackSize defines the maximum number of elements in the stack.
const MaxStackSize = 8192
const MaxReturnStackSize = 1024

// Memory layout constants
const (
	ReservedMemorySize   = 4096               // Size of reserved memory for internal use (DIP, quotations, etc.)
	ReservedMemoryOffset = 0                  // Reserved memory starts at address 0
	UserMemoryOffset     = ReservedMemorySize // User program starts after reserved memory
)

// VM represents the stack-based virtual machine.
type VM struct {
	stack              []int32 // Stack for 32-bit integers
	returnStack        []int32 // Return stack for return addresses
	memory             []byte  // Program and data memory
	pc                 uint32  // Program counter (32-bit address)
	running            bool    // VM execution state
	reservedMemorySize uint32  // Size of reserved memory region
	userMemoryStart    uint32  // Start of user-accessible memory
	trace              bool
}

// NewVM initializes a new VM with the given program.
// The program is loaded after the reserved memory region.
func NewVM(program []byte, trace ...bool) *VM {
	// Allocate memory: reserved region + program
	totalMemory := make([]byte, ReservedMemorySize+len(program))

	// Copy program to user memory area
	copy(totalMemory[UserMemoryOffset:], program)

	traceEnabled := false
	if len(trace) > 0 {
		traceEnabled = trace[0]
	}

	return &VM{
		stack:              make([]int32, 0, MaxStackSize),
		returnStack:        make([]int32, 0, MaxStackSize),
		memory:             totalMemory,
		pc:                 UserMemoryOffset, // Start execution at user memory
		running:            true,
		reservedMemorySize: ReservedMemorySize,
		userMemoryStart:    UserMemoryOffset,
		trace:              traceEnabled,
	}
}

// NewVMWithReservedMemory creates a VM with custom reserved memory size
func NewVMWithReservedMemory(program []byte, reservedSize uint32, trace ...bool) *VM {
	// Allocate memory: reserved region + program
	totalMemory := make([]byte, reservedSize+uint32(len(program)))

	// Copy program to user memory area
	copy(totalMemory[reservedSize:], program)

	traceEnabled := false
	if len(trace) > 0 {
		traceEnabled = trace[0]
	}

	return &VM{
		stack:              make([]int32, 0, MaxStackSize),
		returnStack:        make([]int32, 0, MaxStackSize),
		memory:             totalMemory,
		pc:                 reservedSize, // Start execution at user memory
		running:            true,
		reservedMemorySize: reservedSize,
		userMemoryStart:    reservedSize,
		trace:              traceEnabled,
	}
}

// WriteReservedMemory writes data to reserved memory region (for setting up DIP, etc.)
func (vm *VM) WriteReservedMemory(offset uint32, data []byte) error {
	if offset >= vm.reservedMemorySize {
		return fmt.Errorf("reserved memory offset %d out of bounds (max %d)", offset, vm.reservedMemorySize)
	}
	if offset+uint32(len(data)) > vm.reservedMemorySize {
		return fmt.Errorf("reserved memory write would overflow (offset %d + size %d > %d)",
			offset, len(data), vm.reservedMemorySize)
	}
	copy(vm.memory[offset:], data)
	return nil
}

// ReadReservedMemory reads data from reserved memory region
func (vm *VM) ReadReservedMemory(offset uint32, size uint32) ([]byte, error) {
	if offset >= vm.reservedMemorySize {
		return nil, fmt.Errorf("reserved memory offset %d out of bounds (max %d)", offset, vm.reservedMemorySize)
	}
	if offset+size > vm.reservedMemorySize {
		return nil, fmt.Errorf("reserved memory read would overflow (offset %d + size %d > %d)",
			offset, size, vm.reservedMemorySize)
	}
	result := make([]byte, size)
	copy(result, vm.memory[offset:offset+size])
	return result, nil
}

// ReservedMemorySize returns the size of the reserved memory region
func (vm *VM) ReservedMemorySize() uint32 {
	return vm.reservedMemorySize
}

// UserMemoryStart returns the address where user memory begins
func (vm *VM) UserMemoryStart() uint32 {
	return vm.userMemoryStart
}

// Stack returns a copy of the current stack (for debugging/testing)
func (vm *VM) Stack() []int32 {
	return append([]int32{}, vm.stack...)
}

func (vm *VM) ReturnStack() []int32 {
	return append([]int32{}, vm.returnStack...)
}

// PC returns the current program counter
func (vm *VM) PC() uint32 {
	return vm.pc
}

// Running returns whether the VM is currently running
func (vm *VM) Running() bool {
	return vm.running
}

// Push adds a value to the top of the stack.
func (vm *VM) Push(value int32) error {
	if len(vm.stack) >= MaxStackSize {
		return fmt.Errorf("stack overflow: max size %d reached", MaxStackSize)
	}
	vm.stack = append(vm.stack, value)
	return nil
}

// Pop removes and returns the top value from the stack.
func (vm *VM) Pop() (int32, error) {
	if len(vm.stack) == 0 {
		return 0, fmt.Errorf("stack underflow")
	}
	value := vm.stack[len(vm.stack)-1]
	vm.stack = vm.stack[:len(vm.stack)-1]
	return value, nil
}

// Dup duplicates the top value on the stack.
func (vm *VM) Dup() error {
	if len(vm.stack) < 1 {
		return fmt.Errorf("stack underflow: need 1 value for DUP")
	}
	value := vm.stack[len(vm.stack)-1]
	return vm.Push(value)
}

// Swap swaps the top two values on the stack.
func (vm *VM) Swap() error {
	if len(vm.stack) < 2 {
		return fmt.Errorf("stack underflow: need 2 values for SWAP")
	}
	n := len(vm.stack)
	vm.stack[n-1], vm.stack[n-2] = vm.stack[n-2], vm.stack[n-1]
	return nil
}

// Roll copies the second-from-top value to the top.
func (vm *VM) Roll() error {
	if len(vm.stack) < 2 {
		return fmt.Errorf("stack underflow: need 2 values for ROLL")
	}
	return vm.Push(vm.stack[len(vm.stack)-2])
}

// Rot rotates the top three values.
func (vm *VM) Rot() error {
	if len(vm.stack) < 3 {
		return fmt.Errorf("stack underflow: need 3 values for ROT")
	}
	n := len(vm.stack)
	vm.stack[n-3], vm.stack[n-2], vm.stack[n-1] = vm.stack[n-2], vm.stack[n-1], vm.stack[n-3]
	return nil
}

// Add pops two values, adds them, and pushes the result.
func (vm *VM) Add() error {
	if len(vm.stack) < 2 {
		return fmt.Errorf("stack underflow: need 2 values for ADD")
	}
	b, err := vm.Pop()
	if err != nil {
		return err
	}
	a, err := vm.Pop()
	if err != nil {
		return err
	}
	return vm.Push(a + b)
}

// Sub pops two values, subtracts them, and pushes the result.
func (vm *VM) Sub() error {
	if len(vm.stack) < 2 {
		return fmt.Errorf("stack underflow: need 2 values for SUB")
	}
	b, err := vm.Pop()
	if err != nil {
		return err
	}
	a, err := vm.Pop()
	if err != nil {
		return err
	}
	return vm.Push(a - b)
}

// Mul pops two values, multiplies them, and pushes the result.
func (vm *VM) Mul() error {
	if len(vm.stack) < 2 {
		return fmt.Errorf("stack underflow: need 2 values for MUL")
	}
	b, err := vm.Pop()
	if err != nil {
		return err
	}
	a, err := vm.Pop()
	if err != nil {
		return err
	}
	return vm.Push(a * b)
}

// Div pops two values, divides them, and pushes the quotient.
func (vm *VM) Div() error {
	if len(vm.stack) < 2 {
		return fmt.Errorf("stack underflow: need 2 values for DIV")
	}
	b, err := vm.Pop()
	if err != nil {
		return err
	}
	if b == 0 {
		return fmt.Errorf("division by zero")
	}
	a, err := vm.Pop()
	if err != nil {
		return err
	}
	return vm.Push(a / b)
}

// Mod pops two values, computes modulus, and pushes the result.
func (vm *VM) Mod() error {
	if len(vm.stack) < 2 {
		return fmt.Errorf("stack underflow: need 2 values for MOD")
	}
	b, err := vm.Pop()
	if err != nil {
		return err
	}
	if b == 0 {
		return fmt.Errorf("modulus by zero")
	}
	a, err := vm.Pop()
	if err != nil {
		return err
	}
	return vm.Push(a % b)
}

// Inc increments the top value by 1.
func (vm *VM) Inc() error {
	if len(vm.stack) < 1 {
		return fmt.Errorf("stack underflow: need 1 value for INC")
	}
	value, err := vm.Pop()
	if err != nil {
		return err
	}
	return vm.Push(value + 1)
}

// Dec decrements the top value by 1.
func (vm *VM) Dec() error {
	if len(vm.stack) < 1 {
		return fmt.Errorf("stack underflow: need 1 value for DEC")
	}
	value, err := vm.Pop()
	if err != nil {
		return err
	}
	return vm.Push(value - 1)
}

// Neg negates the top value.
func (vm *VM) Neg() error {
	if len(vm.stack) < 1 {
		return fmt.Errorf("stack underflow: need 1 value for NEG")
	}
	value, err := vm.Pop()
	if err != nil {
		return err
	}
	return vm.Push(-value)
}

// And performs bitwise AND on the top two values.
func (vm *VM) And() error {
	if len(vm.stack) < 2 {
		return fmt.Errorf("stack underflow: need 2 values for AND")
	}
	b, err := vm.Pop()
	if err != nil {
		return err
	}
	a, err := vm.Pop()
	if err != nil {
		return err
	}
	return vm.Push(a & b)
}

// Or performs bitwise OR on the top two values.
func (vm *VM) Or() error {
	if len(vm.stack) < 2 {
		return fmt.Errorf("stack underflow: need 2 values for OR")
	}
	b, err := vm.Pop()
	if err != nil {
		return err
	}
	a, err := vm.Pop()
	if err != nil {
		return err
	}
	return vm.Push(a | b)
}

// Xor performs bitwise XOR on the top two values.
func (vm *VM) Xor() error {
	if len(vm.stack) < 2 {
		return fmt.Errorf("stack underflow: need 2 values for XOR")
	}
	b, err := vm.Pop()
	if err != nil {
		return err
	}
	a, err := vm.Pop()
	if err != nil {
		return err
	}
	return vm.Push(a ^ b)
}

// Not performs bitwise NOT on the top value.
func (vm *VM) Not() error {
	if len(vm.stack) < 1 {
		return fmt.Errorf("stack underflow: need 1 value for NOT")
	}
	value, err := vm.Pop()
	if err != nil {
		return err
	}
	return vm.Push(^value)
}

// Shl shifts the top value left by the second value.
func (vm *VM) Shl() error {
	if len(vm.stack) < 2 {
		return fmt.Errorf("stack underflow: need 2 values for SHL")
	}
	b, err := vm.Pop()
	if err != nil {
		return err
	}
	a, err := vm.Pop()
	if err != nil {
		return err
	}
	return vm.Push(a << uint32(b%32))
}

// Eq compares the top two values for equality.
func (vm *VM) Eq() error {
	if len(vm.stack) < 2 {
		return fmt.Errorf("stack underflow: need 2 values for EQ")
	}
	b, err := vm.Pop()
	if err != nil {
		return err
	}
	a, err := vm.Pop()
	if err != nil {
		return err
	}
	if a == b {
		return vm.Push(1)
	}
	return vm.Push(0)
}

// Lt compares if second value is less than top value.
func (vm *VM) Lt() error {
	if len(vm.stack) < 2 {
		return fmt.Errorf("stack underflow: need 2 values for LT")
	}
	b, err := vm.Pop()
	if err != nil {
		return err
	}
	a, err := vm.Pop()
	if err != nil {
		return err
	}
	if a < b {
		return vm.Push(1)
	}
	return vm.Push(0)
}

// Gt compares if second value is greater than top value.
func (vm *VM) Gt() error {
	if len(vm.stack) < 2 {
		return fmt.Errorf("stack underflow: need 2 values for GT")
	}
	b, err := vm.Pop()
	if err != nil {
		return err
	}
	a, err := vm.Pop()
	if err != nil {
		return err
	}
	if a > b {
		return vm.Push(1)
	}
	return vm.Push(0)
}

// CallStack pops an address from stack and calls it (for quotations)
func (vm *VM) CallStack() error {
	if len(vm.stack) < 1 {
		return fmt.Errorf("stack underflow: need address for CALLSTACK")
	}

	addr, err := vm.Pop()
	if err != nil {
		return err
	}

	if addr < 0 || int(addr) >= len(vm.memory) {
		return fmt.Errorf("invalid call address: %d", addr)
	}

	if len(vm.returnStack) >= MaxStackSize {
		return fmt.Errorf("return stack overflow")
	}

	vm.returnStack = append(vm.returnStack, int32(vm.pc))
	vm.pc = uint32(addr)
	return nil
}

// Jmp jumps to the specified address.
func (vm *VM) Jmp() error {
	if int(vm.pc+4) > len(vm.memory) {
		return fmt.Errorf("program counter out of bounds for JMP immediate")
	}
	address := binary.BigEndian.Uint32(vm.memory[vm.pc : vm.pc+4])
	vm.pc = address
	return nil
}

// Jz pops a value and jumps if it's zero.
func (vm *VM) Jz() error {
	if len(vm.stack) < 1 {
		return fmt.Errorf("stack underflow: need 1 value for JZ")
	}
	cond, err := vm.Pop()
	if err != nil {
		return err
	}
	if int(vm.pc+4) > len(vm.memory) {
		return fmt.Errorf("program counter out of bounds for JZ immediate")
	}
	address := binary.BigEndian.Uint32(vm.memory[vm.pc : vm.pc+4])
	if cond == 0 {
		vm.pc = address
	} else {
		vm.pc += 4
	}
	return nil
}

// Jnz pops a value and jumps if it's non-zero.
func (vm *VM) Jnz() error {
	if len(vm.stack) < 1 {
		return fmt.Errorf("stack underflow: need 1 value for JNZ")
	}
	cond, err := vm.Pop()
	if err != nil {
		return err
	}
	if int(vm.pc+4) > len(vm.memory) {
		return fmt.Errorf("program counter out of bounds for JNZ immediate")
	}
	address := binary.BigEndian.Uint32(vm.memory[vm.pc : vm.pc+4])
	if cond != 0 {
		vm.pc = address
	} else {
		vm.pc += 4
	}
	return nil
}

// Call pushes return address to RETURN STACK and jumps to subroutine.
func (vm *VM) Call() error {
	if int(vm.pc+4) > len(vm.memory) {
		return fmt.Errorf("program counter out of bounds for CALL immediate")
	}

	// Push return address to RETURN STACK (not data stack!)
	if len(vm.returnStack) >= MaxStackSize {
		return fmt.Errorf("return stack overflow")
	}
	vm.returnStack = append(vm.returnStack, int32(vm.pc+4))

	// Jump to subroutine
	address := binary.BigEndian.Uint32(vm.memory[vm.pc : vm.pc+4])
	vm.pc = address
	return nil
}

// Ret pops an address from RETURN STACK and returns to it.
func (vm *VM) Ret() error {
	if len(vm.returnStack) < 1 {
		return fmt.Errorf("return stack underflow")
	}

	// Pop from return stack
	address := vm.returnStack[len(vm.returnStack)-1]
	vm.returnStack = vm.returnStack[:len(vm.returnStack)-1]

	vm.pc = uint32(address)
	return nil
}

// Load reads a value from memory and pushes it.
func (vm *VM) Load() error {
	if int(vm.pc+4) > len(vm.memory) {
		return fmt.Errorf("program counter out of bounds for LOAD immediate")
	}
	address := binary.BigEndian.Uint32(vm.memory[vm.pc : vm.pc+4])
	if int(address)+4 > len(vm.memory) {
		return fmt.Errorf("load address out of bounds: %d", address)
	}
	value := int32(binary.BigEndian.Uint32(vm.memory[address : address+4]))
	vm.pc += 4
	return vm.Push(value)
}

// Store pops a value and stores it in memory.
func (vm *VM) Store() error {
	if len(vm.stack) < 1 {
		return fmt.Errorf("stack underflow: need 1 value for STORE")
	}
	value, err := vm.Pop()
	if err != nil {
		return err
	}
	if int(vm.pc+4) > len(vm.memory) {
		return fmt.Errorf("program counter out of bounds for STORE immediate")
	}
	address := binary.BigEndian.Uint32(vm.memory[vm.pc : vm.pc+4])
	if int(address)+4 > len(vm.memory) {
		return fmt.Errorf("store address out of bounds: %d", address)
	}
	binary.BigEndian.PutUint32(vm.memory[address:address+4], uint32(value))
	vm.pc += 4
	return nil
}

// Out pops a value and outputs it.
func (vm *VM) Out() error {
	if len(vm.stack) < 2 {
		return fmt.Errorf("stack underflow: need 2 values for OUT")
	}

	format, _ := vm.Pop() // 0 = number, 1 = character
	value, err := vm.Pop()
	if err != nil {
		return err
	}

	if format == 1 {
		fmt.Printf("%c", value)
	} else {
		fmt.Printf("%d", value)
	}
	return nil
}

// Halt stops the VM.
func (vm *VM) Halt() error {
	vm.running = false
	return nil
}

// ExecuteInstruction executes a single instruction.
func (vm *VM) ExecuteInstruction() (uint32, error) {
	currentPC := vm.pc
	if int(vm.pc) >= len(vm.memory) {
		return currentPC, fmt.Errorf("program counter out of bounds")
	}
	opcode := vm.memory[vm.pc]
	vm.pc++

	if vm.trace {
		fmt.Fprintf(os.Stderr, "VM: PC=%d, Instruction=%s, Stack=%v, ReturnStack=%v\n", currentPC, OpcodeName(opcode), vm.stack, vm.returnStack)
	}

	switch opcode {
	case OpPush:
		if int(vm.pc+3) >= len(vm.memory) {
			return currentPC, fmt.Errorf("push failed: not enough bytes for operand")
		}
		value := int32(binary.BigEndian.Uint32(vm.memory[vm.pc : vm.pc+4]))
		if vm.trace {
			fmt.Fprintf(os.Stderr, "VM: OpPush: Pushing value=%d\n", value)
		}
		vm.stack = append(vm.stack, value)
		vm.pc += 4
	case OpPop:
		if _, err := vm.Pop(); err != nil {
			return currentPC, fmt.Errorf("pop failed: %v", err)
		}
	case OpDup:
		if err := vm.Dup(); err != nil {
			return currentPC, fmt.Errorf("dup failed: %v", err)
		}
	case OpSwap:
		if err := vm.Swap(); err != nil {
			return currentPC, fmt.Errorf("swap failed: %v", err)
		}
	case OpRoll:
		if err := vm.Roll(); err != nil {
			return currentPC, fmt.Errorf("roll failed: %v", err)
		}
	case OpRot:
		if err := vm.Rot(); err != nil {
			return currentPC, fmt.Errorf("rot failed: %v", err)
		}
	case OpAdd:
		if err := vm.Add(); err != nil {
			return currentPC, fmt.Errorf("add failed: %v", err)
		}
	case OpSub:
		if err := vm.Sub(); err != nil {
			return currentPC, fmt.Errorf("sub failed: %v", err)
		}
	case OpMul:
		if err := vm.Mul(); err != nil {
			return currentPC, fmt.Errorf("mul failed: %v", err)
		}
	case OpDiv:
		if err := vm.Div(); err != nil {
			return currentPC, fmt.Errorf("div failed: %v", err)
		}
	case OpMod:
		if err := vm.Mod(); err != nil {
			return currentPC, fmt.Errorf("mod failed: %v", err)
		}
	case OpInc:
		if err := vm.Inc(); err != nil {
			return currentPC, fmt.Errorf("inc failed: %v", err)
		}
	case OpDec:
		if err := vm.Dec(); err != nil {
			return currentPC, fmt.Errorf("dec failed: %v", err)
		}
	case OpNeg:
		if err := vm.Neg(); err != nil {
			return currentPC, fmt.Errorf("neg failed: %v", err)
		}
	case OpAnd:
		if err := vm.And(); err != nil {
			return currentPC, fmt.Errorf("and failed: %v", err)
		}
	case OpOr:
		if err := vm.Or(); err != nil {
			return currentPC, fmt.Errorf("or failed: %v", err)
		}
	case OpXor:
		if err := vm.Xor(); err != nil {
			return currentPC, fmt.Errorf("xor failed: %v", err)
		}
	case OpNot:
		if err := vm.Not(); err != nil {
			return currentPC, fmt.Errorf("not failed: %v", err)
		}
	case OpShl:
		if err := vm.Shl(); err != nil {
			return currentPC, fmt.Errorf("shl failed: %v", err)
		}
	case OpEq:
		if err := vm.Eq(); err != nil {
			return currentPC, fmt.Errorf("eq failed: %v", err)
		}
	case OpLt:
		if err := vm.Lt(); err != nil {
			return currentPC, fmt.Errorf("lt failed: %v", err)
		}
	case OpGt:
		if err := vm.Gt(); err != nil {
			return currentPC, fmt.Errorf("gt failed: %v", err)
		}
	case OpCallStack:
		if len(vm.stack) < 1 {
			return currentPC, fmt.Errorf("callstack failed: stack underflow")
		}
		if len(vm.returnStack) >= MaxStackSize {
			return currentPC, fmt.Errorf("call failed: return stack overflow")
		}
		addr, err := vm.Pop()
		if err != nil {
			return currentPC, fmt.Errorf("callstack failed: %v", err)
		}
		if int(addr) >= len(vm.memory) || int(addr) < int(vm.userMemoryStart) {
			return currentPC, fmt.Errorf("callstack failed: address %d out of bounds", addr)
		}
		returnAddr := int32(vm.pc)
		vm.returnStack = append(vm.returnStack, returnAddr)
		if vm.trace {
			fmt.Fprintf(os.Stderr, "VM: OpCallStack: Pushing return addr=%d, jumping to %d\n", returnAddr, addr)
		}
		vm.pc = uint32(addr)
	case OpJmp:
		if int(vm.pc+3) >= len(vm.memory) {
			return currentPC, fmt.Errorf("jmp failed: not enough bytes for operand")
		}
		addr := int32(binary.BigEndian.Uint32(vm.memory[vm.pc : vm.pc+4]))
		if vm.trace {
			fmt.Fprintf(os.Stderr, "VM: OpJmp: Jumping to %d\n", addr)
		}
		vm.pc = uint32(addr)
	case OpJz:
		if int(vm.pc+3) >= len(vm.memory) {
			return currentPC, fmt.Errorf("jz failed: not enough bytes for operand")
		}
		addr := int32(binary.BigEndian.Uint32(vm.memory[vm.pc : vm.pc+4]))
		if len(vm.stack) < 1 {
			return currentPC, fmt.Errorf("jz failed: stack underflow")
		}
		cond := vm.stack[len(vm.stack)-1]
		vm.stack = vm.stack[:len(vm.stack)-1]
		if cond == 0 {
			if vm.trace {
				fmt.Fprintf(os.Stderr, "VM: OpJz: Condition false, jumping to %d\n", addr)
			}
			vm.pc = uint32(addr)
		} else {
			if vm.trace {
				fmt.Fprintf(os.Stderr, "VM: OpJz: Condition true, skipping jump\n")
			}
			vm.pc += 4
		}
	case OpJnz:
		if int(vm.pc+3) >= len(vm.memory) {
			return currentPC, fmt.Errorf("jnz failed: not enough bytes for operand")
		}
		addr := int32(binary.BigEndian.Uint32(vm.memory[vm.pc : vm.pc+4]))
		if len(vm.stack) < 1 {
			return currentPC, fmt.Errorf("jnz failed: stack underflow")
		}
		cond := vm.stack[len(vm.stack)-1]
		vm.stack = vm.stack[:len(vm.stack)-1]
		if cond != 0 {
			if vm.trace {
				fmt.Fprintf(os.Stderr, "VM: OpJnz: Condition true, jumping to %d\n", addr)
			}
			vm.pc = uint32(addr)
		} else {
			if vm.trace {
				fmt.Fprintf(os.Stderr, "VM: OpJnz: Condition false, skipping jump\n")
			}
			vm.pc += 4
		}
	case OpCall:
		if int(vm.pc+3) >= len(vm.memory) {
			return currentPC, fmt.Errorf("call failed: not enough bytes for operand")
		}
		addr := int32(binary.BigEndian.Uint32(vm.memory[vm.pc : vm.pc+4]))
		vm.returnStack = append(vm.returnStack, int32(vm.pc+4))
		if vm.trace {
			fmt.Fprintf(os.Stderr, "VM: OpCall: Pushing return addr=%d, jumping to %d\n", vm.pc+4, addr)
		}
		vm.pc = uint32(addr)
	case OpRet:
		if len(vm.returnStack) == 0 {
			return currentPC, fmt.Errorf("ret failed: return stack underflow")
		}
		vm.pc = uint32(vm.returnStack[len(vm.returnStack)-1])
		vm.returnStack = vm.returnStack[:len(vm.returnStack)-1]
		if vm.trace {
			fmt.Fprintf(os.Stderr, "VM: OpRet: Returning to addr=%d\n", vm.pc)
		}
	case OpLoad:
		if err := vm.Load(); err != nil {
			return currentPC, fmt.Errorf("load failed: %v", err)
		}
	case OpStore:
		if err := vm.Store(); err != nil {
			return currentPC, fmt.Errorf("store failed: %v", err)
		}
	case OpOut:
		if err := vm.Out(); err != nil {
			return currentPC, fmt.Errorf("out failed: %v", err)
		}
	case OpHalt:
		vm.running = false
		if vm.trace {
			fmt.Fprintf(os.Stderr, "VM: OpHalt: Stopping execution\n")
		}
	default:
		return currentPC, fmt.Errorf("unknown opcode 0x%02X at PC=%d", opcode, currentPC)
	}
	return currentPC, nil
}

func (vm *VM) Run() error {
	for vm.running && int(vm.pc) < len(vm.memory) {
		// Log instruction, PC, and stack for debugging
		if vm.trace {
			fmt.Fprintf(os.Stderr, "PC: %d, Instruction: %s, Stack: %v\n", vm.pc, OpcodeName(vm.memory[vm.pc]), vm.stack)
		}
		_, err := vm.ExecuteInstruction()
		if err != nil {
			return fmt.Errorf("error at PC=%d: %v", vm.pc, err)
		}
	}
	return nil
}

// Step executes a single instruction and returns whether to continue.
func (vm *VM) Step() (bool, error) {
	if !vm.running || int(vm.pc) >= len(vm.memory) {
		return false, nil
	}
	_, err := vm.ExecuteInstruction()
	if err != nil {
		return false, err
	}
	return vm.running && int(vm.pc) < len(vm.memory), nil
}

// DebugInfo returns detailed state for error reporting
func (vm *VM) DebugInfo() string {
	info := fmt.Sprintf("PC: %d (0x%X)\n", vm.pc-vm.userMemoryStart, vm.pc)
	// Convert stack values to relative if they look like addresses
	adjustedStack := make([]int32, len(vm.stack))
	for i, val := range vm.stack {
		if uint32(val) >= vm.userMemoryStart {
			adjustedStack[i] = val - int32(vm.userMemoryStart)
		} else {
			adjustedStack[i] = val
		}
	}
	info += fmt.Sprintf("Stack: %v\n", adjustedStack)
	info += fmt.Sprintf("Stack: %v\n", vm.Stack())
	info += fmt.Sprintf("Return Stack: %v\n", vm.ReturnStack())
	info += fmt.Sprintf("Stack Depth: %d/%d\n", len(vm.stack), MaxStackSize)
	info += fmt.Sprintf("Return Stack Depth: %d/%d\n", len(vm.returnStack), MaxStackSize)
	info += fmt.Sprintf("Reserved Memory: 0x0-0x%X (%d bytes)\n", vm.reservedMemorySize, vm.reservedMemorySize)
	info += fmt.Sprintf("User Memory: 0x%X-0x%X\n", vm.userMemoryStart, len(vm.memory))

	// Show current opcode if available
	if int(vm.pc) < len(vm.memory) {
		currentOpcode := vm.memory[vm.pc]
		info += fmt.Sprintf("\nCurrent Instruction: %s (0x%02X)\n",
			OpcodeName(currentOpcode), currentOpcode)
	}

	// Show nearby bytecode
	if int(vm.pc) < len(vm.memory) {
		start := int(vm.pc)
		if start > 5 {
			start -= 5
		}
		end := int(vm.pc) + 10
		if end > len(vm.memory) {
			end = len(vm.memory)
		}
		info += "\nBytecode around PC:\n"
		for i := start; i < end; i++ {
			marker := " "
			if i == int(vm.pc) {
				marker = ">"
			}
			opcode := vm.memory[i]
			info += fmt.Sprintf("%s %04d: 0x%02X  %s\n",
				marker, i, opcode, OpcodeName(opcode))
		}
	}

	return info
}
