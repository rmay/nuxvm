//go:build js && wasm

package main

import (
	"encoding/binary"
	"fmt"
	"strings"
	"syscall/js"

	"github.com/rmay/nuxvm/pkg/lux"
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
	p(vm.OpSwap)
	p(vm.OpLt) // FrameWidth-1 < nx  →  nx > FrameWidth-1
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
	p(vm.OpSwap)
	p(vm.OpLt) // FrameHeight-1 < ny  →  ny > FrameHeight-1
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

	// ── 6. Check keyboard — halt if any key held ──────────────────────────────
	p(vm.LoadInstruction(int32(vm.KeyboardStatusAddr))...)
	p(vm.PushInstruction(0)...)
	p(vm.OpEq)
	phExit := reserveJmp(vm.OpJz)

	p(vm.JmpInstruction(loopStart)...)

	patchJmp(phExit, offset())
	p(vm.OpHalt)

	return prog
}

// gameSource is a LUX snake game.
// Arrow keys move the snake; eating apples grows it.
// Hitting a wall or the snake's own body ends the game.
// Sound effects use AudioControlAddr (0x3001): 1=eat, 2=spawn, 3=game_over, 0=silence.
const gameSource = `
( ===  S N A K E  ===
  Arrow keys: 1=up  2=down  3=left  4=right
  Reserved memory:
    0=dir  4=len  8=score  12=hptr  16=ax  20=ay  24=nx  28=ny
    1632=snd_timer
  Ring buffer @ 32: segment i -> x at i*8+32, y at i*8+36  max 200 segs
  RNG device register: 0x3002
  Audio control: 0x3001  (1=eat 2=spawn 3=game_over 0=silence) )

( --- pixel ops --- )
@pixel  SWAP 64 * ROT + 4 * 4096 + STOREI ;
@cls    0 [ DUP 4 * 4096 + 0 SWAP STOREI INC ] 2048 #: DROP ;
@pix-at 64 * + 4 * 4096 + LOADI ;

( --- colors --- )
@SNAKE  0x00FF00 ;
@APPLE  0xFF3300 ;

( --- state accessors --- )
@get-dir    0 LOADI ;  @set-dir    0 STOREI ;
@get-len    4 LOADI ;  @set-len    4 STOREI ;
@get-score  8 LOADI ;  @set-score  8 STOREI ;
@get-hptr  12 LOADI ;  @set-hptr  12 STOREI ;
@get-ax    16 LOADI ;  @set-ax    16 STOREI ;
@get-ay    20 LOADI ;  @set-ay    20 STOREI ;
@get-nx    24 LOADI ;  @set-nx    24 STOREI ;
@get-ny    28 LOADI ;  @set-ny    28 STOREI ;

( --- ring buffer --- )
@seg-xa  8 * 32 + ;
@seg-ya  8 * 36 + ;
@head-x  get-hptr seg-xa LOADI ;
@head-y  get-hptr seg-ya LOADI ;

( --- direction deltas: default 0, overwrite on match --- )
@dx  0 get-dir 4 = [ DROP  1 ] ?
       get-dir 3 = [ DROP -1 ] ? ;
@dy  0 get-dir 2 = [ DROP  1 ] ?
       get-dir 1 = [ DROP -1 ] ? ;

( --- block 180-degree reversal --- )
@try-turn
  DUP get-dir + DUP 3 = SWAP 7 = OR
  [ DROP ] [ set-dir ] ?: ;

( --- keyboard --- )
@process-key  DUP 5 = [ DROP ] [ try-turn ] ?: ;
@handle-keys
  0x3000 LOADI
  DUP 0 = [ DROP ] [ process-key ] ?: ;

( --- pixel at next head position --- )
@pix-new  get-nx get-ny pix-at ;

( --- collision --- )
@lnot  0 = ;
@wall?
  get-nx 0 < get-nx 64 < lnot OR
  get-ny 0 < OR  get-ny 32 < lnot OR ;
@self?  pix-new DUP 0 = lnot SWAP APPLE = lnot AND ;
@apple? pix-new APPLE = ;

( --- audio: 0x3001 trigger: 1=eat 2=spawn 3=game_over 0=silence --- )
@get-snd-timer  1632 LOADI ;
@set-snd-timer  1632 STOREI ;
@snd!           0x3001 STOREI ;
@snd-silence-if-done  get-snd-timer 0 = [ 0 snd! ] ? ;
@snd-tick
  get-snd-timer 0 = lnot
  [ get-snd-timer DEC set-snd-timer  snd-silence-if-done ] ? ;
@play-eat    1 snd!  10 set-snd-timer ;
@play-spawn  2 snd!   6 set-snd-timer ;





( --- advance head: update hptr, store seg, draw --- )
@advance
  get-hptr 1 + 200 MOD
  DUP set-hptr
  DUP get-nx SWAP seg-xa STOREI
  DUP get-ny SWAP seg-ya STOREI
  DROP
  get-nx get-ny SNAKE pixel ;

( --- erase old tail, called after advance --- )
@erase-tail
  get-hptr get-len - 200 + 200 MOD
  DUP seg-xa LOADI SWAP seg-ya LOADI 0 pixel ;

( --- spawn apple at random position, play spawn sound --- )
@spawn-apple
  RND 0x7FFFFFFF AND 64 MOD set-ax
  RND 0x7FFFFFFF AND 32 MOD set-ay
  get-ax get-ay APPLE pixel
  play-spawn ;

( --- game over: sound, clear screen, halt --- )
@game-over  3 snd!  cls HALT ;

( === init: cls MUST run first — #: clobbers addr 0 and 4 as temp vars === )
cls
4 set-dir
3 set-len
0 set-score
2 set-hptr

( initial 3-segment snake at row 16, cols 10-12, heading right )
10  0 seg-xa STOREI   16  0 seg-ya STOREI
11  1 seg-xa STOREI   16  1 seg-ya STOREI
12  2 seg-xa STOREI   16  2 seg-ya STOREI
10 16 SNAKE pixel
11 16 SNAKE pixel
12 16 SNAKE pixel

spawn-apple

( === main loop === )
@tick
  snd-tick
  handle-keys
  head-x dx + set-nx
  head-y dy + set-ny
  wall?  [ game-over ] ?
  self?  [ game-over ] ?
  apple?
  advance
  [ get-len 1 + set-len  get-score 1 + set-score  spawn-apple  play-eat ]
  [ erase-tail ]
  ?:
  YIELD
  tick ;

tick
`

func buildGameProgram() []byte {
	bytecode, err := lux.Compile(gameSource)
	if err != nil {
		panic("game compile error: " + err.Error())
	}
	return bytecode
}

func resetVM() {
	machine = vm.NewVM(buildBounceProgram())
	machine.KeyboardHandler = func() int32 { return keyPressed }
	machine.YieldHandler = func() {}
	machine.SoundHandler = func(soundID int32) {
		js.Global().Call("nux_play_sound", soundID)
	}
	keyPressed = 0
}

// --- LUX REPL state ---

// replStdlib is pre-loaded into every REPL session.
const replStdlib = `@pixel SWAP 64 * ROT + 4 * 4096 + STOREI ;
@cls 0 [ DUP 4 * 4096 + 0 SWAP STOREI INC ] 2048 #: DROP ;
`

type replState struct {
	history     string
	stack       []int32
	definitions []string
	framebuffer []byte
}

func newReplState() *replState {
	return &replState{
		history:     replStdlib,
		definitions: []string{"pixel", "cls"},
		framebuffer: make([]byte, vm.VideoBufferSize),
	}
}

var repl = newReplState()

func replEval(line string) (stack []int32, output string, errStr string) {
	line = strings.TrimSpace(line)
	if line == "" {
		return repl.stack, "", ""
	}

	if strings.HasPrefix(line, "@") {
		if !strings.Contains(line, ";") {
			return repl.stack, "", "word definition must end with ';'"
		}
		repl.history += line + "\n"
		parts := strings.Fields(line[1:])
		if len(parts) >= 1 {
			repl.definitions = append(repl.definitions, parts[0])
		}
		return repl.stack, "defined: " + parts[0], ""
	}

	source := repl.history
	for _, val := range repl.stack {
		source += fmt.Sprintf("%d ", val)
	}
	source += line

	bytecode, err := lux.Compile(source)
	if err != nil {
		return repl.stack, "", err.Error()
	}

	var outBuf strings.Builder
	m := vm.NewVM(bytecode)
	copy(m.Memory()[vm.VideoFramebufferStart:vm.VideoFramebufferStart+vm.VideoBufferSize], repl.framebuffer)
	m.OutputHandler = func(value int32, format int32) {
		if format == 1 {
			outBuf.WriteRune(rune(value))
		} else {
			outBuf.WriteString(fmt.Sprintf("%d", value))
		}
	}
	if err := m.Run(); err != nil {
		return repl.stack, "", err.Error()
	}

	copy(repl.framebuffer, m.Memory()[vm.VideoFramebufferStart:vm.VideoFramebufferStart+vm.VideoBufferSize])
	copy(machine.Memory()[vm.VideoFramebufferStart:vm.VideoFramebufferStart+vm.VideoBufferSize], repl.framebuffer)

	repl.stack = m.Stack()
	return repl.stack, outBuf.String(), ""
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

	js.Global().Set("nux_game_reset", js.FuncOf(func(this js.Value, args []js.Value) any {
		machine = vm.NewVM(buildGameProgram())
		machine.KeyboardHandler = func() int32 { return keyPressed }
		machine.YieldHandler = func() {}
		machine.SoundHandler = func(soundID int32) {
			js.Global().Call("nux_play_sound", soundID)
		}
		keyPressed = 0
		return nil
	}))

	js.Global().Set("nux_get_pc", js.FuncOf(func(this js.Value, args []js.Value) any {
		return int(machine.PC())
	}))

	js.Global().Set("nux_get_op", js.FuncOf(func(this js.Value, args []js.Value) any {
		return machine.LastOpcode()
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

	js.Global().Set("nux_get_audio_buffer", js.FuncOf(func(this js.Value, args []js.Value) any {
		mem := machine.Memory()
		start := vm.AudioSampleBufferAddr
		size := vm.AudioSampleBufferByteSize
		uint8Array := js.Global().Get("Uint8Array").New(size)
		js.CopyBytesToJS(uint8Array, mem[start:start+size])
		return uint8Array
	}))

	js.Global().Set("nux_step_frame", js.FuncOf(func(this js.Value, args []js.Value) any {
		defer func() {
			if r := recover(); r != nil {
				fmt.Printf("PANIC in nux_step_frame: %v\n", r)
			}
		}()

		if machine == nil {
			fmt.Println("VM not initialized")
			return false
		}

		for machine.Running() {
			_, err := machine.Step()
			if err != nil {
				fmt.Printf("VM Error: %v\n", err)
				return false
			}
			// Use string comparison for Opcode as defined in vm.go
			if machine.LastOpcode() == "YIELD" {
				return true
			}
		}
		return false
	}))

	js.Global().Set("nux_set_keydown", js.FuncOf(func(this js.Value, args []js.Value) any {
		if len(args) > 0 {
			keyPressed = int32(args[0].Int())
		}
		return nil
	}))

	js.Global().Set("nux_lux_eval", js.FuncOf(func(this js.Value, args []js.Value) any {
		line := ""
		if len(args) > 0 {
			line = args[0].String()
		}
		stack, output, errStr := replEval(line)
		jsStack := js.Global().Get("Array").New(len(stack))
		for i, v := range stack {
			jsStack.SetIndex(i, int(v))
		}
		result := js.Global().Get("Object").New()
		result.Set("stack", jsStack)
		result.Set("output", output)
		result.Set("error", errStr)
		return result
	}))

	js.Global().Set("nux_lux_reset", js.FuncOf(func(this js.Value, args []js.Value) any {
		repl = newReplState()
		for i := vm.VideoFramebufferStart; i < vm.VideoFramebufferStart+vm.VideoBufferSize; i++ {
			machine.Memory()[i] = 0
		}
		return nil
	}))

	js.Global().Set("nux_lux_get_words", js.FuncOf(func(this js.Value, args []js.Value) any {
		words := js.Global().Get("Array").New(len(repl.definitions))
		for i, w := range repl.definitions {
			words.SetIndex(i, w)
		}
		return words
	}))

	resetVM()
}

func main() {
	fmt.Println("NUXVM WASM module loaded. Ready for JavaScript interaction.")
	runWASM()
	select {}
}
