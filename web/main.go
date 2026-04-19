package main

import (
	"encoding/binary"
	"fmt"
	"syscall/js"

	"github.com/rmay/nuxvm/pkg/vm"
)

var machine *vm.VM
var keyPressed int32

// Reserved-memory layout (addresses within the reserved region, 0x0000–0x0FFF).
const (
	addrX  = uint32(0)
	addrY  = uint32(4)
	addrDX = uint32(8)
	addrDY = uint32(12)
)

const (
	colorOn  = int32(0xFFFFFF)
	colorOff = int32(0)
)

func buildBounceProgram() []byte {
	prog := []byte{}
	base := int32(vm.UserMemoryOffset)
	offset := func() int32 { return base + int32(len(prog)) }

	p := func(b ...byte) { prog = append(prog, b...) }
	push := func(v int32) { p(vm.PushInstruction(v)...) }
	load := func(a uint32) { p(vm.LoadInstruction(int32(a))...) }
	store := func(a uint32) { p(vm.StoreInstruction(int32(a))...) }
	storei := func() { p(vm.OpStoreI) }
	patchJmp := func(ph int, target int32) {
		binary.BigEndian.PutUint32(prog[ph+1:ph+5], uint32(target))
	}
	reserveJmp := func(op byte) int {
		ph := len(prog)
		p(op, 0, 0, 0, 0)
		return ph
	}

	// ── Initialise state in reserved memory ──────────────────────────────────
	push(0)
	store(addrX)
	push(0)
	store(addrY)
	push(1)
	store(addrDX)
	push(1)
	store(addrDY)

	loopStart := offset()

	// ── 1. Erase old pixel ───────────────────────────────────────────────────
	push(colorOff)
	load(addrY)
	push(vm.FrameWidth)
	p(vm.OpMul)
	load(addrX)
	p(vm.OpAdd)
	push(4)
	p(vm.OpMul)
	push(int32(vm.VideoFramebufferStart))
	p(vm.OpAdd)
	storei()

	// ── 2. Update X with wall bounce ─────────────────────────────────────────
	load(addrX)
	load(addrDX)
	p(vm.OpAdd) // nx on stack
	p(vm.OpDup)
	push(0)
	p(vm.OpLt)
	phLtZeroX := reserveJmp(vm.OpJz)
	p(vm.OpPop)
	push(0)
	push(1)
	store(addrDX)
	phSkipHiX := reserveJmp(vm.OpJmp)
	patchJmp(phLtZeroX, offset())
	p(vm.OpDup)
	push(int32(vm.FrameWidth - 1))
	p(vm.OpGt)
	phHiX := reserveJmp(vm.OpJz)
	p(vm.OpPop)
	push(int32(vm.FrameWidth - 1))
	push(-1)
	store(addrDX)
	patchJmp(phSkipHiX, offset())
	patchJmp(phHiX, offset())
	store(addrX)

	// ── 3. Update Y with wall bounce ─────────────────────────────────────────
	load(addrY)
	load(addrDY)
	p(vm.OpAdd) // ny on stack
	p(vm.OpDup)
	push(0)
	p(vm.OpLt)
	phLtZeroY := reserveJmp(vm.OpJz)
	p(vm.OpPop)
	push(0)
	push(1)
	store(addrDY)
	phSkipHiY := reserveJmp(vm.OpJmp)
	patchJmp(phLtZeroY, offset())
	p(vm.OpDup)
	push(int32(vm.FrameHeight - 1))
	p(vm.OpGt)
	phHiY := reserveJmp(vm.OpJz)
	p(vm.OpPop)
	push(int32(vm.FrameHeight - 1))
	push(-1)
	store(addrDY)
	patchJmp(phSkipHiY, offset())
	patchJmp(phHiY, offset())
	store(addrY)

	// ── 4. Draw new pixel ─────────────────────────────────────────────────────
	push(colorOn)
	load(addrY)
	push(vm.FrameWidth)
	p(vm.OpMul)
	load(addrX)
	p(vm.OpAdd)
	push(4)
	p(vm.OpMul)
	push(int32(vm.VideoFramebufferStart))
	p(vm.OpAdd)
	storei()

	// ── 5. Yield ─────────────────────────────────────────────────────────────
	p(vm.OpYield)

	// ── 6. Check keyboard ────────────────────────────────────────────────────
	p(vm.LoadInstruction(int32(vm.KeyboardStatusAddr))...)
	phExit := reserveJmp(vm.OpJnz)

	p(vm.JmpInstruction(loopStart)...)

	patchJmp(phExit, offset())
	p(vm.OpHalt)

	return prog
}

func resetVM() {
	machine = vm.NewVM(buildBounceProgram())
	machine.KeyboardHandler = func() int32 { return keyPressed }
	machine.YieldHandler = func() {}
	keyPressed = 0
}

// --- WASM Exports ---

func runWASM() {
	js.Global().Set("nux_step", js.FuncOf(func(this js.Value, args []js.Value) any {
		if !machine.Running() {
			return false
		}
		_, err := machine.Step()
		if err != nil {
			fmt.Printf("VM Error: %v", err)
			return false
		}
		return machine.Running()
	}))

	js.Global().Set("nux_reset", js.FuncOf(func(this js.Value, args []js.Value) any {
		resetVM()
		return nil
	}))

	js.Global().Set("nux_get_pc", js.FuncOf(func(this js.Value, args []js.Value) any {
		return int(machine.PC())
	}))

	js.Global().Set("nux_get_stack", js.FuncOf(func(this js.Value, args []js.Value) any {
		stack := machine.Stack()
		jsStack := js.Global().Get("Array").New(len(stack))
		for i, v := range stack {
			jsStack.SetIndex(i, int(v))
		}
		return jsStack
	}))

	js.Global().Set("nux_get_memory", js.FuncOf(func(this js.Value, args []js.Value) any {
		mem := machine.Memory()
		uint8Array := js.Global().Get("Uint8Array").New(len(mem))
		js.CopyBytesToJS(uint8Array, mem)
		return uint8Array
	}))

	js.Global().Set("nux_set_keydown", js.FuncOf(func(this js.Value, args []js.Value) any {
		if len(args) > 0 {
			if args[0].Bool() {
				keyPressed = 1
			} else {
				keyPressed = 0
			}
		}
		return nil
	}))

	resetVM()
}

// main function is required for the Go toolchain but not directly called by WASM.
// The actual WASM entry point is runWASM, invoked via syscall/js.
// This select{} keeps the Go program running indefinitely, which is necessary
// for the syscall/js interaction to function correctly.
func main() {
	fmt.Println("NUXVM WASM module loaded. Ready for JavaScript interaction.")
	runWASM() // Call the WASM entry point
	select {} // Keep the program running
}
