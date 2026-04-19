// Bouncing-pixel demo for the NUXVM device I/O system.
//
// The VM program runs a tight loop that:
//   1. Erases the previous pixel in the video framebuffer (via OpStoreI)
//   2. Updates x/y position with wall-bounce logic
//   3. Draws the new pixel (via OpStoreI)
//   4. Yields to the host (OpYield) — triggers rendering + frame-rate sleep
//   5. Reads the keyboard register; halts when a key is pressed
//
// Run: go run ./examples/deviceio/demo/
package main

import (
	"encoding/binary"
	"fmt"
	"os"
	"strings"
	"time"

	"github.com/rmay/nuxvm/pkg/vm"
	"golang.org/x/term"
)

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

// buildBounceProgram assembles the bytecode for the bouncing-pixel loop.
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
	push(0); store(addrX)
	push(0); store(addrY)
	push(1); store(addrDX)
	push(1); store(addrDY)

	loopStart := offset()

	// ── 1. Erase old pixel: PUSH colorOff; <addr>; STOREI ────────────────────
	push(colorOff)
	load(addrY); push(vm.FrameWidth); p(vm.OpMul)
	load(addrX); p(vm.OpAdd)
	push(4); p(vm.OpMul)
	push(int32(vm.VideoFramebufferStart)); p(vm.OpAdd)
	storei()

	// ── 2. Update X with wall bounce ─────────────────────────────────────────
	load(addrX); load(addrDX); p(vm.OpAdd) // nx on stack

	p(vm.OpDup); push(0); p(vm.OpLt)
	phLtZeroX := reserveJmp(vm.OpJz)
	// nx < 0 → clamp to 0, reverse dx
	p(vm.OpPop); push(0); push(1); store(addrDX)
	phSkipHiX := reserveJmp(vm.OpJmp)

	patchJmp(phLtZeroX, offset())
	p(vm.OpDup); push(int32(vm.FrameWidth - 1)); p(vm.OpSwap); p(vm.OpLt)
	phHiX := reserveJmp(vm.OpJz)
	// nx > FrameWidth-1 → clamp, reverse dx
	p(vm.OpPop); push(int32(vm.FrameWidth - 1)); push(-1); store(addrDX)

	patchJmp(phSkipHiX, offset())
	patchJmp(phHiX, offset())
	store(addrX) // save new x

	// ── 3. Update Y with wall bounce ─────────────────────────────────────────
	load(addrY); load(addrDY); p(vm.OpAdd) // ny on stack

	p(vm.OpDup); push(0); p(vm.OpLt)
	phLtZeroY := reserveJmp(vm.OpJz)
	p(vm.OpPop); push(0); push(1); store(addrDY)
	phSkipHiY := reserveJmp(vm.OpJmp)

	patchJmp(phLtZeroY, offset())
	p(vm.OpDup); push(int32(vm.FrameHeight - 1)); p(vm.OpSwap); p(vm.OpLt)
	phHiY := reserveJmp(vm.OpJz)
	p(vm.OpPop); push(int32(vm.FrameHeight - 1)); push(-1); store(addrDY)

	patchJmp(phSkipHiY, offset())
	patchJmp(phHiY, offset())
	store(addrY)

	// ── 4. Draw new pixel ─────────────────────────────────────────────────────
	push(colorOn)
	load(addrY); push(vm.FrameWidth); p(vm.OpMul)
	load(addrX); p(vm.OpAdd)
	push(4); p(vm.OpMul)
	push(int32(vm.VideoFramebufferStart)); p(vm.OpAdd)
	storei()

	// ── 5. Yield (render frame + throttle) ───────────────────────────────────
	p(vm.OpYield)

	// ── 6. Check keyboard; exit if pressed ───────────────────────────────────
	p(vm.LoadInstruction(int32(vm.KeyboardStatusAddr))...)
	p(vm.PushInstruction(0)...)
	p(vm.OpEq)
	phExit := reserveJmp(vm.OpJz)

	p(vm.JmpInstruction(loopStart)...)

	patchJmp(phExit, offset())
	p(vm.OpHalt)

	return prog
}

func main() {
	// Put the terminal in raw mode so any keypress (no Enter needed) exits.
	fd := int(os.Stdin.Fd())
	oldState, err := term.MakeRaw(fd)
	if err != nil {
		fmt.Fprintf(os.Stderr, "warning: could not set raw mode (%v); press Enter to exit\n", err)
	} else {
		defer term.Restore(fd, oldState)
	}

	// Feed keypresses into a buffered channel.
	keysCh := make(chan struct{}, 1)
	go func() {
		buf := make([]byte, 1)
		os.Stdin.Read(buf)
		select {
		case keysCh <- struct{}{}:
		default:
		}
	}()

	machine := vm.NewVM(buildBounceProgram())

	machine.KeyboardHandler = func() int32 {
		select {
		case <-keysCh:
			return 1
		default:
			return 0
		}
	}

	const fps = 15
	frame := time.Duration(time.Second / fps)

	// render prints a frame. In raw mode \n does not reset the column, so we
	// replace every \n with \r\n throughout all output.
	crlf := func(s string) string { return strings.ReplaceAll(s, "\n", "\r\n") }

	machine.YieldHandler = func() {
		fmt.Print("\033[H" +
			crlf(vm.RenderFramebuffer(machine.Memory())) +
			"\r\n  NUXVM device I/O demo \u2014 press any key to exit\r\n")
		time.Sleep(frame)
	}

	// Hide the cursor while animating; restore it on exit.
	fmt.Print("\033[?25l")
	defer fmt.Print("\033[?25h\r\n")

	// Clear the screen once and position the cursor at home.
	fmt.Print("\033[2J\033[H")

	if err := machine.Run(); err != nil {
		term.Restore(fd, oldState)
		fmt.Fprintf(os.Stderr, "\r\nVM error: %v\r\n", err)
		os.Exit(1)
	}

	// Final render after halt.
	fmt.Print("\033[H" +
		crlf(vm.RenderFramebuffer(machine.Memory())) +
		"\r\n  Done.\r\n")
}
