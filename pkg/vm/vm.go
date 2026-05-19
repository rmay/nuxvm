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
	ReservedMemorySize = 0x4000  // 16KB (0x0000-0x3FFF)
	DeviceMemoryOffset = 0x10000 // Device ports start at 0x10000
	DeviceMemorySize   = 0x1000  // 4KB for device ports (0x10000-0x10FFF)

	// Video Framebuffer: Starting at 0x11000.
	// Support up to 1280x1024 pixels (~5MB).
	VideoFramebufferStart = 0x11000
	VideoMaxBufferSize    = 1280 * 1024 * 4
	VideoFramebufferEnd   = VideoFramebufferStart + VideoMaxBufferSize

	// User memory base addresses.
	// Headless mode starts right after device memory.
	// Graphical mode starts after the maximum possible video framebuffer to avoid collisions.
	HeadlessBaseAddress  = 0x11000
	GraphicalBaseAddress = 0x600000 // 6MB decimal
)

// Device memory configuration (device port logic moved to Bus layer)
// Note: Device port SEMANTICS (vector handling) moved to system.Bus layer.
// These constants remain for bytecode generation and testing.
const (
	// For compiler: RNG register address used by built-in RND word
	RNGDataAddr = DeviceMemoryOffset + 0x0084 // 0x3084

	// For tests: device address constants (used by tests, can be int32 or uint32 as needed)
	ControllerPort       = DeviceMemoryOffset + 0x0040
	ControllerStatusAddr = ControllerPort + 4
	AudioPort            = DeviceMemoryOffset + 0x0030
	AudioControlAddr     = AudioPort + 4
	ConsolePort          = DeviceMemoryOffset + 0x0010

	FirstAvailableDeviceAddr = DeviceMemoryOffset + 0x0100
)

// VM represents the stack-based virtual machine.
type VM struct {
	stack              []int32 // Stack for 32-bit integers
	returnStack        []int32 // Return stack for return addresses
	memory             []byte  // RAM (Reserved + User Memory)
	pc                 uint32  // Program counter (32-bit address)
	running            bool    // VM execution state
	reservedMemorySize uint32  // Size of reserved memory region
	userMemoryStart    uint32  // Start of user-accessible memory
	trace              bool
	traceCount         int

	vectors [16]uint32 // Interrupt/Jump vectors
	bus     Bus        // External I/O bus

	// OutputHandler is called by OpOut instead of writing to stdout.
	OutputHandler func(value int32, format int32)

	lastOpcode byte
	halted     bool
	fp         int // Index into locals stack
	locals     []int32
	loopStack  []int32

	// Trailing ring of executed PCs (for post-mortem diagnostics).
	pcRing    [64]uint32
	pcRingIdx int
}

// MaxLocalsSize defines the maximum number of local variables.
const MaxLocalsSize = 4096

// MaxLoopStackSize defines the maximum depth of nested loops.
const MaxLoopStackSize = 1024

// NewVM initializes a new VM with the given program and base address.
func NewVM(program []byte, baseAddress uint32, trace ...bool) *VM {
	// Allocate RAM: baseAddress + program size
	// We no longer force a minimum 8MB allocation.
	memSize := baseAddress + uint32(len(program))
	totalMemory := make([]byte, memSize)

	// Copy program to user memory area
	copy(totalMemory[baseAddress:], program)

	traceEnabled := false
	if len(trace) > 0 {
		traceEnabled = trace[0]
	}

	return &VM{
		stack:              make([]int32, 0, MaxStackSize),
		returnStack:        make([]int32, 0, MaxReturnStackSize),
		memory:             totalMemory,
		pc:                 baseAddress, // Start execution at user memory
		running:            true,
		reservedMemorySize: ReservedMemorySize,
		userMemoryStart:    baseAddress,
		trace:              traceEnabled,
		locals:             make([]int32, 0, MaxLocalsSize),
		loopStack:          make([]int32, 0, MaxLoopStackSize),
		fp:                 -1,
	}
}

// NewVMWithMemorySize creates a VM with a fixed memory size and base address.
func NewVMWithMemorySize(program []byte, baseAddress uint32, totalSize uint32, trace ...bool) *VM {
	if totalSize < baseAddress+uint32(len(program)) {
		totalSize = baseAddress + uint32(len(program))
	}

	totalMemory := make([]byte, totalSize)
	copy(totalMemory[baseAddress:], program)

	traceEnabled := false
	if len(trace) > 0 {
		traceEnabled = trace[0]
	}

	return &VM{
		stack:              make([]int32, 0, MaxStackSize),
		returnStack:        make([]int32, 0, MaxReturnStackSize),
		memory:             totalMemory,
		pc:                 baseAddress,
		running:            true,
		reservedMemorySize: ReservedMemorySize,
		userMemoryStart:    baseAddress,
		trace:              traceEnabled,
		locals:             make([]int32, 0, MaxLocalsSize),
		loopStack:          make([]int32, 0, MaxLoopStackSize),
		fp:                 -1,
	}
}

// NewVMWithReservedMemory creates a VM with custom reserved memory size and base address.
func NewVMWithReservedMemory(program []byte, baseAddress uint32, reservedSize uint32, trace ...bool) *VM {
	memSize := baseAddress + uint32(len(program))
	totalMemory := make([]byte, memSize)

	// Copy program to user memory area
	copy(totalMemory[baseAddress:], program)

	traceEnabled := false
	if len(trace) > 0 {
		traceEnabled = trace[0]
	}

	return &VM{
		stack:              make([]int32, 0, MaxStackSize),
		returnStack:        make([]int32, 0, MaxReturnStackSize),
		memory:             totalMemory,
		pc:                 baseAddress, // Start execution at user memory
		running:            true,
		reservedMemorySize: reservedSize,
		userMemoryStart:    baseAddress,
		trace:              traceEnabled,
		locals:             make([]int32, 0, MaxLocalsSize),
		loopStack:          make([]int32, 0, MaxLoopStackSize),
		fp:                 -1,
	}
}

// SetBus connects the VM to an external I/O bus for MMIO.
func (vm *VM) SetBus(bus Bus) {
	vm.bus = bus
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

// Memory returns a direct slice of the VM's memory.
// The device framebuffer lives at [VideoFramebufferStart : VideoFramebufferEnd].
func (vm *VM) Memory() []byte {
	return vm.memory
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

// Halt stops the VM from running.
func (vm *VM) Halt() {
	vm.running = false
}

// WriteVector sets a jump address for a specific vector index.
func (vm *VM) WriteVector(index int, address uint32) error {
	if index < 0 || index >= len(vm.vectors) {
		return fmt.Errorf("invalid vector index: %d", index)
	}
	vm.vectors[index] = address
	return nil
}

// GetVector returns the address stored in a vector register.
func (vm *VM) GetVector(index int) uint32 {
	if index < 0 || index >= len(vm.vectors) {
		return 0
	}
	return vm.vectors[index]
}

// SetVector sets a vector register (used by Bus callbacks).
func (vm *VM) SetVector(index int, addr uint32) {
	if index >= 0 && index < len(vm.vectors) {
		vm.vectors[index] = addr
	}
}

// TriggerVector calls the specified vector, pushing the current PC onto the
// return stack so the handler's terminating RET resumes the interrupted code.
// Sets the VM to running state if it was halted.
func (vm *VM) TriggerVector(index int) error {
	if index < 0 || index >= len(vm.vectors) {
		return fmt.Errorf("invalid vector index: %d", index)
	}

	addr := vm.vectors[index]
	if addr == 0 {
		return nil // Vector not set
	}

	if addr >= uint32(len(vm.memory)) {
		return fmt.Errorf("vector address 0x%X out of bounds", addr)
	}

	if len(vm.returnStack) >= MaxReturnStackSize {
		return fmt.Errorf("return stack overflow on vector trigger")
	}
	vm.returnStack = append(vm.returnStack, int32(vm.pc))
	vm.pc = addr
	vm.running = true
	return nil
}

// LastOpcode returns the name of the most recently executed opcode.
func (vm *VM) LastOpcode() string {
	return OpcodeName(vm.lastOpcode)
}

// Running returns whether the VM is currently running
func (vm *VM) Running() bool {
	return vm.running
}

func (vm *VM) SetRunning(r bool) {
	vm.running = r
}

// Halted returns whether the VM has halted.
func (vm *VM) Halted() bool {
	return vm.halted
}

// Yielded returns true if the last executed instruction was YIELD.
func (vm *VM) Yielded() bool {
	return vm.lastOpcode == OpYield
}

// ClearYield resets the yield state so the VM can continue after yielding.
func (vm *VM) ClearYield() {
	vm.lastOpcode = 0xFF // Set to an invalid opcode to clear yield status
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
	if vm.trace {
		fmt.Fprintf(os.Stderr, "VM: Pop: value=%d, newStack=%v\n", value, vm.stack)
	}
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

// Rot rotates the top three values.
func (vm *VM) Rot() error {
	if len(vm.stack) < 3 {
		return fmt.Errorf("stack underflow: need 3 values for ROT")
	}
	n := len(vm.stack)
	a := vm.stack[n-3]
	b := vm.stack[n-2]
	c := vm.stack[n-1]

	vm.stack[n-3] = b
	vm.stack[n-2] = c
	vm.stack[n-1] = a
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

// Shr logical right shift: shifts second value right by top value bits (unsigned semantics).
func (vm *VM) Shr() error {
	if len(vm.stack) < 2 {
		return fmt.Errorf("stack underflow: need 2 values for SHR")
	}
	b, err := vm.Pop()
	if err != nil {
		return err
	}
	a, err := vm.Pop()
	if err != nil {
		return err
	}
	// Logical right shift: treat as uint32, shift, convert back to int32
	ua := uint32(a)
	return vm.Push(int32(ua >> uint32(b%32)))
}

// Sar arithmetic right shift: shifts second value right by top value bits (signed semantics).
func (vm *VM) Sar() error {
	if len(vm.stack) < 2 {
		return fmt.Errorf("stack underflow: need 2 values for SAR")
	}
	b, err := vm.Pop()
	if err != nil {
		return err
	}
	a, err := vm.Pop()
	if err != nil {
		return err
	}
	// Arithmetic right shift: Go's >> on int32 already sign-extends
	return vm.Push(a >> uint32(b%32))
}

// Neq checks if two values are not equal.
func (vm *VM) Neq() error {
	if len(vm.stack) < 2 {
		return fmt.Errorf("stack underflow: need 2 values for NEQ")
	}
	b, err := vm.Pop()
	if err != nil {
		return err
	}
	a, err := vm.Pop()
	if err != nil {
		return err
	}
	if a != b {
		return vm.Push(1)
	}
	return vm.Push(0)
}

// Lte checks if second value is less than or equal to top value.
func (vm *VM) Lte() error {
	if len(vm.stack) < 2 {
		return fmt.Errorf("stack underflow: need 2 values for LTE")
	}
	b, err := vm.Pop()
	if err != nil {
		return err
	}
	a, err := vm.Pop()
	if err != nil {
		return err
	}
	if a <= b {
		return vm.Push(1)
	}
	return vm.Push(0)
}

// Gte checks if second value is greater than or equal to top value.
func (vm *VM) Gte() error {
	if len(vm.stack) < 2 {
		return fmt.Errorf("stack underflow: need 2 values for GTE")
	}
	b, err := vm.Pop()
	if err != nil {
		return err
	}
	a, err := vm.Pop()
	if err != nil {
		return err
	}
	if a >= b {
		return vm.Push(1)
	}
	return vm.Push(0)
}

func (vm *VM) Pick() error {
	if len(vm.stack) < 1 {
		return fmt.Errorf("stack underflow: need index for PICK")
	}
	n, err := vm.Pop()
	if err != nil {
		return err
	}
	if n < 0 || int(n) >= len(vm.stack) {
		return fmt.Errorf("pick: index %d out of range (stack depth %d)", n, len(vm.stack))
	}
	// Stack is [... stack[n] ... top]; we want to copy stack[n] to top
	val := vm.stack[len(vm.stack)-1-int(n)]
	return vm.Push(val)
}

func (vm *VM) Roll() error {
	if len(vm.stack) < 1 {
		return fmt.Errorf("stack underflow: need index for ROLL")
	}
	n, err := vm.Pop()
	if err != nil {
		return err
	}
	if n < 0 || int(n) >= len(vm.stack) {
		return fmt.Errorf("roll: index %d out of range (stack depth %d)", n, len(vm.stack))
	}
	if n == 0 {
		return nil
	}
	// Stack is [... stack[n] ... top]; we want to rotate stack[n] to top
	realIdx := len(vm.stack) - 1 - int(n)
	val := vm.stack[realIdx]
	copy(vm.stack[realIdx:], vm.stack[realIdx+1:])
	vm.stack[len(vm.stack)-1] = val
	return nil
}

// Divmod performs division and modulo, pushing both quotient and remainder.
func (vm *VM) Divmod() error {
	if len(vm.stack) < 2 {
		return fmt.Errorf("stack underflow: need 2 values for DIVMOD")
	}
	b, err := vm.Pop()
	if err != nil {
		return err
	}
	a, err := vm.Pop()
	if err != nil {
		return err
	}
	if b == 0 {
		return fmt.Errorf("divmod: division by zero")
	}
	// Push quotient then remainder (so remainder ends up on top)
	if err := vm.Push(a / b); err != nil {
		return err
	}
	return vm.Push(a % b)
}

// Abs pushes the absolute value of the top element.
func (vm *VM) Abs() error {
	if len(vm.stack) < 1 {
		return fmt.Errorf("stack underflow: need 1 value for ABS")
	}
	val, err := vm.Pop()
	if err != nil {
		return err
	}
	if val < 0 {
		return vm.Push(-val)
	}
	return vm.Push(val)
}

// Min pushes the minimum of the top two values.
func (vm *VM) Min() error {
	if len(vm.stack) < 2 {
		return fmt.Errorf("stack underflow: need 2 values for MIN")
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
		return vm.Push(a)
	}
	return vm.Push(b)
}

// Max pushes the maximum of the top two values.
func (vm *VM) Max() error {
	if len(vm.stack) < 2 {
		return fmt.Errorf("stack underflow: need 2 values for MAX")
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
		return vm.Push(a)
	}
	return vm.Push(b)
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

	if len(vm.returnStack) >= MaxReturnStackSize {
		return fmt.Errorf("return stack overflow")
	}

	vm.returnStack = append(vm.returnStack, int32(vm.pc))
	vm.pc = uint32(addr)
	return nil
}

// Jmp jumps to the specified address.
func (vm *VM) Jmp() error {
	if int(vm.pc+3) >= len(vm.memory) {
		return fmt.Errorf("jmp failed: program counter out of bounds")
	}
	addr := int32(binary.BigEndian.Uint32(vm.memory[vm.pc : vm.pc+4]))
	if vm.trace {
		fmt.Fprintf(os.Stderr, "VM: OpJmp: Jumping to %d", addr)
	}
	vm.pc = uint32(addr)
	return nil
}

// Jz pops a value and jumps if it's zero.
func (vm *VM) Jz() error {
	if int(vm.pc+3) >= len(vm.memory) {
		return fmt.Errorf("jz failed: program counter out of bounds")
	}
	addr := int32(binary.BigEndian.Uint32(vm.memory[vm.pc : vm.pc+4]))
	if len(vm.stack) < 1 {
		return fmt.Errorf("jz failed: stack underflow")
	}
	cond := vm.stack[len(vm.stack)-1]
	vm.stack = vm.stack[:len(vm.stack)-1]
	if cond == 0 {
		if vm.trace {
			fmt.Fprintf(os.Stderr, "VM: OpJz: Condition true, jumping to %d", addr)
		}
		vm.pc = uint32(addr)
	} else {
		if vm.trace {
			fmt.Fprintf(os.Stderr, "VM: OpJz: Condition false, skipping jump")
		}
		vm.pc += 4
	}
	return nil
}

// Jnz pops a value and jumps if it's non-zero.
func (vm *VM) Jnz() error {
	if int(vm.pc+3) >= len(vm.memory) {
		return fmt.Errorf("jnz failed: program counter out of bounds")
	}
	addr := int32(binary.BigEndian.Uint32(vm.memory[vm.pc : vm.pc+4]))
	if len(vm.stack) < 1 {
		return fmt.Errorf("jnz failed: stack underflow")
	}
	cond := vm.stack[len(vm.stack)-1]
	vm.stack = vm.stack[:len(vm.stack)-1]
	if cond != 0 {
		if vm.trace {
			fmt.Fprintf(os.Stderr, "VM: OpJnz: Condition true, jumping to %d", addr)
		}
		vm.pc = uint32(addr)
	} else {
		if vm.trace {
			fmt.Fprintf(os.Stderr, "VM: OpJnz: Condition false, skipping jump")
		}
		vm.pc += 4
	}
	return nil
}

// Call pushes return address to RETURN STACK and jumps to subroutine.
func (vm *VM) Call() error {
	if int(vm.pc+3) >= len(vm.memory) {
		return fmt.Errorf("call failed: program counter out of bounds")
	}
	addr := int32(binary.BigEndian.Uint32(vm.memory[vm.pc : vm.pc+4]))
	if len(vm.returnStack) >= MaxReturnStackSize {
		return fmt.Errorf("return stack overflow")
	}
	vm.returnStack = append(vm.returnStack, int32(vm.pc+4))
	if vm.trace {
		fmt.Fprintf(os.Stderr, "VM: OpCall: Pushing return addr=%d, jumping to %d", vm.pc+4, addr)
	}
	vm.pc = uint32(addr)
	return nil
}

// Ret pops an address from RETURN STACK and returns to it.
func (vm *VM) Ret() error {
	if len(vm.returnStack) == 0 {
		return fmt.Errorf("ret failed: return stack underflow")
	}
	vm.pc = uint32(vm.returnStack[len(vm.returnStack)-1])
	vm.returnStack = vm.returnStack[:len(vm.returnStack)-1]
	if vm.trace {
		fmt.Fprintf(os.Stderr, "VM: OpRet: Returning to addr=%d", vm.pc)
	}
	return nil
}

// Load reads a value from memory and pushes it.
func (vm *VM) Load() error {
	if int(vm.pc+3) >= len(vm.memory) {
		return fmt.Errorf("load failed: program counter out of bounds")
	}
	address := binary.BigEndian.Uint32(vm.memory[vm.pc : vm.pc+4])
	vm.pc += 4

	// Check if the address is within the device memory region or video framebuffer
	if vm.isDeviceAddr(address) {
		// It's a device memory access, call device handler
		value, err := vm.handleDeviceRead(address)
		if err != nil {
			return fmt.Errorf("device read error at address 0x%04X: %v", address, err)
		}
		return vm.Push(value)
	}

	// Standard memory access
	if int(address)+4 > len(vm.memory) {
		return fmt.Errorf("load address 0x%04X out of bounds", address)
	}
	value := int32(binary.BigEndian.Uint32(vm.memory[address : address+4]))
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
	if int(vm.pc+3) >= len(vm.memory) {
		return fmt.Errorf("store failed: program counter out of bounds")
	}
	address := binary.BigEndian.Uint32(vm.memory[vm.pc : vm.pc+4])
	vm.pc += 4

	// Check if the address is within the device memory region or video framebuffer
	if vm.isDeviceAddr(address) {
		// It's a device memory access, call device handler
		err := vm.handleDeviceWrite(address, value)
		if err != nil {
			return fmt.Errorf("device write error at address 0x%04X: %v", address, err)
		}
		return nil
	}

	// Standard memory access
	if int(address)+4 > len(vm.memory) {
		return fmt.Errorf("store address 0x%04X out of bounds", address)
	}
	binary.BigEndian.PutUint32(vm.memory[address:address+4], uint32(value))
	return nil
}

// JmpStack pops an address from STACK and jumps to it.
func (vm *VM) JmpStack() error {
	if len(vm.stack) < 1 {
		return fmt.Errorf("jmpstack failed: stack underflow")
	}
	addr := vm.stack[len(vm.stack)-1]
	vm.stack = vm.stack[:len(vm.stack)-1]
	if addr < 0 || int(addr) >= len(vm.memory) {
		return fmt.Errorf("jmpstack failed: address %d out of bounds", addr)
	}
	if vm.trace {
		fmt.Fprintf(os.Stderr, "VM: OpJmpStack: Jumping to addr=%d", addr)
	}
	vm.pc = uint32(addr)
	return nil
}

// Over copies the second item on STACK to the top.
func (vm *VM) Over() error {
	if len(vm.stack) < 2 {
		return fmt.Errorf("over failed: stack underflow")
	}
	value := vm.stack[len(vm.stack)-2]
	return vm.Push(value)
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

	if vm.OutputHandler != nil {
		vm.OutputHandler(value, format)
		return nil
	}

	if format == 1 {
		fmt.Printf("%c", value)
	} else {
		fmt.Printf("%d", value)
	}
	return nil
}

// ExecuteInstruction executes a single instruction.
func (vm *VM) ExecuteInstruction() (uint32, error) {
	currentPC := vm.pc
	if int(vm.pc) >= len(vm.memory) {
		return currentPC, fmt.Errorf("program counter out of bounds")
	}
	opcode := vm.memory[vm.pc]
	vm.lastOpcode = opcode
	vm.pcRing[vm.pcRingIdx] = vm.pc
	vm.pcRingIdx = (vm.pcRingIdx + 1) % len(vm.pcRing)
	vm.pc++

	if vm.trace {
		vm.traceCount++
		if vm.traceCount > 1000 {
			vm.trace = false
			fmt.Fprintf(os.Stderr, "\nVM: Trace limit (1000) reached, tracing disabled\n")
		} else {
			fmt.Fprintf(os.Stderr, "\nVM: PC=%d, Instruction=%s, Stack=%v, ReturnStack=%v", currentPC, OpcodeName(opcode), vm.stack, vm.returnStack)
		}
	}

	switch opcode {
	case OpPush:
		if int(vm.pc+3) >= len(vm.memory) {
			return currentPC, fmt.Errorf("push failed: program counter out of bounds")
		}
		value := int32(binary.BigEndian.Uint32(vm.memory[vm.pc : vm.pc+4]))
		if vm.trace {
			fmt.Fprintf(os.Stderr, "VM: OpPush: Pushing value=%d", value)
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
	case OpOver:
		if err := vm.Over(); err != nil {
			return currentPC, fmt.Errorf("over failed: %v", err)
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
	case OpShr:
		if err := vm.Shr(); err != nil {
			return currentPC, fmt.Errorf("shr failed: %v", err)
		}
	case OpSar:
		if err := vm.Sar(); err != nil {
			return currentPC, fmt.Errorf("sar failed: %v", err)
		}
	case OpJnz:
		if err := vm.Jnz(); err != nil {
			return currentPC, fmt.Errorf("jnz failed: %v", err)
		}
	case OpNeg:
		if err := vm.Neg(); err != nil {
			return currentPC, fmt.Errorf("neg failed: %v", err)
		}
	case OpGt:
		if err := vm.Gt(); err != nil {
			return currentPC, fmt.Errorf("gt failed: %v", err)
		}
	case OpNeq:
		if err := vm.Neq(); err != nil {
			return currentPC, fmt.Errorf("neq failed: %v", err)
		}
	case OpLte:
		if err := vm.Lte(); err != nil {
			return currentPC, fmt.Errorf("lte failed: %v", err)
		}
	case OpGte:
		if err := vm.Gte(); err != nil {
			return currentPC, fmt.Errorf("gte failed: %v", err)
		}
	case OpPick:
		if err := vm.Pick(); err != nil {
			return currentPC, fmt.Errorf("pick failed: %v", err)
		}
	case OpRoll:
		if err := vm.Roll(); err != nil {
			return currentPC, fmt.Errorf("roll failed: %v", err)
		}
	case OpDivmod:
		if err := vm.Divmod(); err != nil {
			return currentPC, fmt.Errorf("divmod failed: %v", err)
		}
	case OpAbs:
		if err := vm.Abs(); err != nil {
			return currentPC, fmt.Errorf("abs failed: %v", err)
		}
	case OpMin:
		if err := vm.Min(); err != nil {
			return currentPC, fmt.Errorf("min failed: %v", err)
		}
	case OpMax:
		if err := vm.Max(); err != nil {
			return currentPC, fmt.Errorf("max failed: %v", err)
		}
	case OpCallStack:
		if len(vm.stack) < 1 {
			return currentPC, fmt.Errorf("callstack failed: stack underflow")
		}
		if len(vm.returnStack) >= MaxReturnStackSize {
			return currentPC, fmt.Errorf("call failed: return stack overflow")
		}
		addr, err := vm.Pop()
		if err != nil {
			return currentPC, fmt.Errorf("callstack failed: %v", err)
		}
		if addr < 0 || int(addr) >= len(vm.memory) {
			return currentPC, fmt.Errorf("callstack failed: address %d out of bounds", addr)
		}
		returnAddr := int32(vm.pc)
		vm.returnStack = append(vm.returnStack, returnAddr)
		if vm.trace {
			fmt.Fprintf(os.Stderr, "VM: OpCallStack: Pushing return addr=%d, jumping to %d", returnAddr, addr)
		}
		vm.pc = uint32(addr)
	case OpJmp:
		if int(vm.pc+3) >= len(vm.memory) {
			return currentPC, fmt.Errorf("jmp failed: program counter out of bounds")
		}
		addr := int32(binary.BigEndian.Uint32(vm.memory[vm.pc : vm.pc+4]))
		if vm.trace {
			fmt.Fprintf(os.Stderr, "VM: OpJmp: Jumping to %d", addr)
		}
		vm.pc = uint32(addr)
	case OpJz:
		if int(vm.pc+3) >= len(vm.memory) {
			return currentPC, fmt.Errorf("jz failed: program counter out of bounds")
		}
		addr := int32(binary.BigEndian.Uint32(vm.memory[vm.pc : vm.pc+4]))
		if len(vm.stack) < 1 {
			return currentPC, fmt.Errorf("jz failed: stack underflow")
		}
		cond := vm.stack[len(vm.stack)-1]
		vm.stack = vm.stack[:len(vm.stack)-1]
		if cond == 0 {
			if vm.trace {
				fmt.Fprintf(os.Stderr, "VM: OpJz: Condition true, jumping to %d", addr)
			}
			vm.pc = uint32(addr)
		} else {
			if vm.trace {
				fmt.Fprintf(os.Stderr, "VM: OpJz: Condition false, skipping jump")
			}
			vm.pc += 4
		}
	case OpCall:
		if int(vm.pc+3) >= len(vm.memory) {
			return currentPC, fmt.Errorf("call failed: program counter out of bounds")
		}
		addr := int32(binary.BigEndian.Uint32(vm.memory[vm.pc : vm.pc+4]))
		if len(vm.returnStack) >= MaxReturnStackSize {
			return currentPC, fmt.Errorf("return stack overflow")
		}
		vm.returnStack = append(vm.returnStack, int32(vm.pc+4))
		if vm.trace {
			fmt.Fprintf(os.Stderr, "VM: OpCall: Pushing return addr=%d, jumping to %d", vm.pc+4, addr)
		}
		vm.pc = uint32(addr)
	case OpRet:
		if len(vm.returnStack) == 0 {
			return currentPC, fmt.Errorf("ret failed: return stack underflow")
		}
		vm.pc = uint32(vm.returnStack[len(vm.returnStack)-1])
		vm.returnStack = vm.returnStack[:len(vm.returnStack)-1]
		if vm.trace {
			fmt.Fprintf(os.Stderr, "VM: OpRet: Returning to addr=%d", vm.pc)
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
		vm.halted = true
	case OpYield:
		vm.running = false
	case OpLoadI:
		addr, err := vm.Pop()
		if err != nil {
			return currentPC, fmt.Errorf("loadi failed: %v", err)
		}
		uaddr := uint32(addr)
		if vm.isDeviceAddr(uaddr) {
			val, err := vm.handleDeviceRead(uaddr)
			if err != nil {
				return currentPC, fmt.Errorf("loadi device read failed: %v", err)
			}
			vm.stack = append(vm.stack, val)
		} else if addr >= 0 && int(addr)+4 <= len(vm.memory) {
			vm.stack = append(vm.stack, int32(binary.BigEndian.Uint32(vm.memory[addr:addr+4])))
		} else {
			return currentPC, fmt.Errorf("loadi failed: address %d (0x%X) out of bounds", addr, uaddr)
		}
	case OpStoreI:
		addr, err := vm.Pop()
		if err != nil {
			return currentPC, fmt.Errorf("storei failed: %v", err)
		}
		value, err := vm.Pop()
		if err != nil {
			return currentPC, fmt.Errorf("storei failed: %v", err)
		}
		uaddr := uint32(addr)
		if vm.isDeviceAddr(uaddr) {
			if err := vm.handleDeviceWrite(uaddr, value); err != nil {
				return currentPC, fmt.Errorf("storei device write failed: %v", err)
			}
		} else if addr >= 0 && int(addr)+4 <= len(vm.memory) {
			binary.BigEndian.PutUint32(vm.memory[addr:addr+4], uint32(value))
		} else {
			return currentPC, fmt.Errorf("storei failed: address %d (0x%X) out of bounds", addr, uaddr)
		}
	case OpJmpStack:
		if err := vm.JmpStack(); err != nil {
			return currentPC, fmt.Errorf("jmpstack failed: %v", err)
		}
	case OpPushR:
		if len(vm.stack) < 1 {
			return currentPC, fmt.Errorf("pushr failed: stack underflow")
		}
		if len(vm.loopStack) >= MaxLoopStackSize {
			return currentPC, fmt.Errorf("pushr failed: loop stack overflow")
		}
		val, _ := vm.Pop()
		vm.loopStack = append(vm.loopStack, val)
	case OpPopR:
		if len(vm.loopStack) == 0 {
			return currentPC, fmt.Errorf("popr failed: loop stack underflow")
		}
		if len(vm.stack) >= MaxStackSize {
			return currentPC, fmt.Errorf("popr failed: stack overflow")
		}
		val := vm.loopStack[len(vm.loopStack)-1]
		vm.loopStack = vm.loopStack[:len(vm.loopStack)-1]
		vm.stack = append(vm.stack, val)
	case OpPeekR:
		if len(vm.loopStack) == 0 {
			return currentPC, fmt.Errorf("peekr failed: loop stack underflow")
		}
		if len(vm.stack) >= MaxStackSize {
			return currentPC, fmt.Errorf("peekr failed: stack overflow")
		}
		val := vm.loopStack[len(vm.loopStack)-1]
		vm.stack = append(vm.stack, val)
	case OpFrame:
		if len(vm.stack) < 1 {
			return currentPC, fmt.Errorf("frame failed: stack underflow")
		}
		n, _ := vm.Pop()
		if vm.trace {
			fmt.Fprintf(os.Stderr, "VM: OpFrame: n=%d, stackBeforeItems=%v\n", n, vm.stack)
		}
		if len(vm.stack) < int(n) {
			return currentPC, fmt.Errorf("frame failed: stack underflow for %d items (n=%d, stack=%v)", n, n, vm.stack)
		}
		if len(vm.locals)+int(n)+1 > MaxLocalsSize {
			return currentPC, fmt.Errorf("frame failed: locals overflow")
		}

		oldFP := int32(vm.fp)
		vm.fp = len(vm.locals)
		vm.locals = append(vm.locals, oldFP)

		// Copy n items from stack to locals
		// Order: v_n ... v1. v1 is at top of main stack.
		// We want local@ 0 to be v1.
		for i := 0; i < int(n); i++ {
			val, _ := vm.Pop()
			vm.locals = append(vm.locals, val)
		}
	case OpUnframe:
		if len(vm.stack) < 1 {
			return currentPC, fmt.Errorf("unframe failed: stack underflow (missing n)")
		}
		_, _ = vm.Pop() // Pop n
		if vm.fp < 0 {
			return currentPC, fmt.Errorf("unframe failed: no active frame")
		}
		oldFP := vm.locals[vm.fp]
		vm.locals = vm.locals[:vm.fp]
		vm.fp = int(oldFP)
	case OpLocalGet:
		if len(vm.stack) < 1 {
			return currentPC, fmt.Errorf("localget failed: stack underflow")
		}
		offset, err := vm.Pop()
		if err != nil {
			return currentPC, fmt.Errorf("localget failed: %v", err)
		}
		if vm.fp < 0 {
			return currentPC, fmt.Errorf("localget failed: no active frame")
		}
		idx := vm.fp + 1 + int(offset)
		if idx < 0 || idx >= len(vm.locals) {
			return currentPC, fmt.Errorf("localget failed: offset %d out of bounds (fp=%d, size=%d)", offset, vm.fp, len(vm.locals))
		}
		vm.stack = append(vm.stack, vm.locals[idx])
	case OpLocalSet:
		if len(vm.stack) < 2 {
			return currentPC, fmt.Errorf("localset failed: stack underflow")
		}
		offset, err := vm.Pop()
		if err != nil {
			return currentPC, fmt.Errorf("localset failed: %v", err)
		}
		val, err := vm.Pop()
		if err != nil {
			return currentPC, fmt.Errorf("localset failed: %v", err)
		}
		if vm.fp < 0 {
			return currentPC, fmt.Errorf("localset failed: no active frame")
		}
		idx := vm.fp + 1 + int(offset)
		if idx < 0 || idx >= len(vm.locals) {
			return currentPC, fmt.Errorf("localset failed: offset %d out of bounds", offset)
		}
		vm.locals[idx] = val
	case OpPeekR2:
		if len(vm.loopStack) < 2 {
			return currentPC, fmt.Errorf("peekr2 failed: loop stack underflow")
		}
		if len(vm.stack)+2 >= MaxStackSize {
			return currentPC, fmt.Errorf("peekr2 failed: stack overflow")
		}
		b := vm.loopStack[len(vm.loopStack)-1]
		a := vm.loopStack[len(vm.loopStack)-2]
		vm.stack = append(vm.stack, a, b)
	default:
		vm.pc-- // Rewind to faulty opcode
		vm.running = false
		return currentPC, fmt.Errorf("unknown opcode 0x%02X at PC=%d", opcode, currentPC)
	}
	return currentPC, nil
}

// Step executes a single instruction and returns whether to continue.
func (vm *VM) Step() (bool, error) {
	if !vm.running {
		return false, nil
	}
	if int(vm.pc) >= len(vm.memory) {
		return false, fmt.Errorf("program counter out of bounds")
	}
	_, err := vm.ExecuteInstruction()
	if err != nil {
		return false, err
	}
	return vm.running, nil
}

func (vm *VM) Run() error {
	for vm.running {
		_, err := vm.Step()
		if err != nil {
			return fmt.Errorf("error at PC=%d: %v", vm.pc, err)
		}
	}
	return nil
}

func (vm *VM) StackDump(limit int) []int32 {
	n := len(vm.stack)
	if n == 0 {
		return nil
	}
	if limit > n {
		limit = n
	}
	// Create a copy to avoid mutation by the caller
	res := make([]int32, limit)
	copy(res, vm.stack[n-limit:])
	return res
}

// RecentPCs returns the last N executed PCs in chronological order (oldest first).
func (vm *VM) RecentPCs() []uint32 {
	n := len(vm.pcRing)
	out := make([]uint32, 0, n)
	for i := 0; i < n; i++ {
		pc := vm.pcRing[(vm.pcRingIdx+i)%n]
		if pc != 0 {
			out = append(out, pc)
		}
	}
	return out
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
	info += fmt.Sprintf("Stack Depth: %d/%d", len(vm.stack), MaxStackSize)
	info += fmt.Sprintf("Return Stack Depth: %d/%d\\n", len(vm.returnStack), MaxReturnStackSize) // Corrected to MaxReturnStackSize
	info += fmt.Sprintf("Reserved Memory: 0x0-0x%X (%d bytes)", vm.reservedMemorySize, vm.reservedMemorySize)
	info += fmt.Sprintf("User Memory: 0x%X-0x%X", vm.userMemoryStart, len(vm.memory))

	// Show current opcode if available
	if int(vm.pc) < len(vm.memory) {
		currentOpcode := vm.memory[vm.pc]
		info += fmt.Sprintf("\\nCurrent Instruction: %s (0x%02X)\n",
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
		info += "\\nBytecode around PC:\n"
		for i := start; i < end; i++ {
			marker := " "
			if i == int(vm.pc) {
				marker = ">"
			}
			opcode := vm.memory[i]
			info += fmt.Sprintf("%s %04d: 0x%02X  %s\\n",
				marker, i, opcode, OpcodeName(opcode))
		}
	}

	return info
}

// isDeviceAddr checks if an address is in device memory or video framebuffer range.
func (vm *VM) isDeviceAddr(addr uint32) bool {
	if addr >= vm.userMemoryStart {
		return false
	}
	return (addr >= DeviceMemoryOffset && addr < DeviceMemoryOffset+DeviceMemorySize) ||
		(addr >= VideoFramebufferStart && addr < VideoFramebufferEnd)
}

// handleDeviceRead delegates device memory reads to the Bus.
func (vm *VM) handleDeviceRead(address uint32) (int32, error) {
	if vm.bus != nil {
		return vm.bus.Read(address)
	}
	return 0, fmt.Errorf("no bus: device read at 0x%04X", address)
}

// handleDeviceWrite delegates device memory writes to the Bus.
func (vm *VM) handleDeviceWrite(address uint32, value int32) error {
	if vm.bus != nil {
		return vm.bus.Write(address, value)
	}
	return fmt.Errorf("no bus: device write at 0x%04X", address)
}
