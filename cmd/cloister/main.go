package main

import (
	"flag"
	"fmt"
	"image/color"
	"os"
	"strings"
	"time"

	"github.com/atotto/clipboard"
	"github.com/hajimehoshi/ebiten/v2"
	"github.com/hajimehoshi/ebiten/v2/ebitenutil"
	"github.com/hajimehoshi/ebiten/v2/inpututil"
	"github.com/rmay/nuxvm/pkg/lux"
	"github.com/rmay/nuxvm/pkg/system"
	"github.com/rmay/nuxvm/pkg/vm"
)

var screenScale = 10

// defaultBootPath is the on-disk Lux program cloister loads when invoked
// without a positional argument. It's relative to cwd so editing lib/boot.lux
// in the repo and re-running cloister picks up the change without a rebuild.
const defaultBootPath = "lib/boot.lux"
const topBarHeight = 14

type Window struct {
	id      int
	name    string
	scrollY int32
}

type GraphicalREPL struct {
	active   bool
	wordDefs string   // accumulator of @word ... ; definitions prepended to each compile
	input    string
	log      []string
	scrollOffset int  // offset into log for scrolling

	// Line history for up/down navigation. `lines` holds every committed
	// input; `lineIdx` is a cursor where `len(lines)` represents the draft
	// slot (the in-progress typed text). `draft` stashes the user's typed
	// text when they start browsing history so Down-arrow can restore it.
	lines   []string
	lineIdx int
	draft   string
}

type Game struct {
	machine      *system.Machine
	showDebug    bool
	bootTimer    int
	mouseX       int
	mouseY       int
	replChan     chan int32
	repl         *GraphicalREPL
	windows      []Window
	activeWinIdx int
}

var font = map[rune][5]byte{
	'C': {0x7, 0x4, 0x4, 0x4, 0x7},
	'L': {0x4, 0x4, 0x4, 0x4, 0x7},
	'O': {0x7, 0x5, 0x5, 0x5, 0x7},
	'I': {0x7, 0x2, 0x2, 0x2, 0x7},
	'S': {0x7, 0x4, 0x7, 0x1, 0x7},
	'T': {0x7, 0x2, 0x2, 0x2, 0x2},
	'E': {0x7, 0x4, 0x7, 0x4, 0x7},
	'R': {0x7, 0x5, 0x7, 0x6, 0x5},
}

func drawText(screen *ebiten.Image, text string, x, y int, clr color.Color) {
	for i, r := range text {
		if glyph, ok := font[r]; ok {
			for row := 0; row < 5; row++ {
				for col := 0; col < 3; col++ {
					if glyph[row]&(1<<(2-col)) != 0 {
						screen.Set(x+i*4+col, y+row, clr)
					}
				}
			}
		}
	}
}

func drawChicagoText(screen *ebiten.Image, text string, x, y int, clr color.Color) {
	for i, r := range text {
		if r < 128 {
			glyph := system.Font[r]
			for row := 0; row < 8; row++ {
				for col := 0; col < 8; col++ {
					if glyph[row]&(1<<col) != 0 {
						screen.Set(x+i*8+col, y+row, clr)
					}
				}
			}
		}
	}
}

func drawMouse(screen *ebiten.Image, x, y int) {
	white := color.RGBA{255, 255, 255, 255}
	screen.Set(x, y-1, white)
	screen.Set(x, y+1, white)
	screen.Set(x-1, y, white)
	screen.Set(x+1, y, white)
	screen.Set(x, y, color.RGBA{0, 0, 0, 255})
}

// replHistoryUp moves one step back into the line history. The first up-press
// while the draft is active stashes it so down-arrow can restore it.
func (g *Game) replHistoryUp() {
	r := g.repl
	if len(r.lines) == 0 {
		return
	}
	if r.lineIdx == len(r.lines) {
		r.draft = r.input
	}
	if r.lineIdx > 0 {
		r.lineIdx--
		r.input = r.lines[r.lineIdx]
	}
}

// replHistoryDown moves forward into history, or restores the stashed draft
// once we step past the most recent entry.
func (g *Game) replHistoryDown() {
	r := g.repl
	if r.lineIdx >= len(r.lines) {
		return
	}
	r.lineIdx++
	if r.lineIdx == len(r.lines) {
		r.input = r.draft
		r.draft = ""
	} else {
		r.input = r.lines[r.lineIdx]
	}
}

func (g *Game) activeWindow() *Window {
	if g.activeWinIdx < len(g.windows) {
		return &g.windows[g.activeWinIdx]
	}
	return &g.windows[0]
}

func (g *Game) executeREPL(line string) {
	if line == "" {
		return
	}

	switch line {
	case "exit", "quit", "q":
		os.Exit(0)
	case "help", "?":
		g.repl.log = append(g.repl.log, "═══ LUX REPL Commands ═══")
		g.repl.log = append(g.repl.log, "  help, ?          - Show this help")
		g.repl.log = append(g.repl.log, "  exit, quit, q    - Exit CLOISTER")
		g.repl.log = append(g.repl.log, "  clear            - Clear terminal screen")
		g.repl.log = append(g.repl.log, "  clearstack, cs   - Clear the stack")
		g.repl.log = append(g.repl.log, "  stack, .s        - Show current stack")
		g.repl.log = append(g.repl.log, "  drop             - Drop top stack value")
		return
	case "clearstack", "cs":
		for {
			if _, err := g.machine.CPU.Pop(); err != nil {
				break
			}
		}
		g.repl.log = append(g.repl.log, "  Stack cleared")
		return
	case "drop":
		if _, err := g.machine.CPU.Pop(); err != nil {
			g.repl.log = append(g.repl.log, "  Stack is empty")
		} else {
			stack := g.machine.CPU.Stack()
			if len(stack) == 0 {
				g.repl.log = append(g.repl.log, "  Stack: []")
			} else {
				g.repl.log = append(g.repl.log, fmt.Sprintf("  Stack: %v", stack))
			}
		}
		return
	case "stack", ".s":
		stack := g.machine.CPU.Stack()
		if len(stack) == 0 {
			g.repl.log = append(g.repl.log, "  Stack: []")
		} else {
			g.repl.log = append(g.repl.log, fmt.Sprintf("  Stack: %v", stack))
		}
		return
	}

	// Handle word definitions
	if strings.HasPrefix(line, "@") {
		if !strings.HasSuffix(strings.TrimSpace(line), ";") {
			g.repl.log = append(g.repl.log, "Error: Word definition must end with ';'")
			return
		}
		g.repl.wordDefs += line + "\n"
		g.repl.log = append(g.repl.log, "Defined word")
		return
	}

	source := g.repl.wordDefs + line
	bytecode, err := lux.Compile(source)
	if err != nil {
		g.repl.log = append(g.repl.log, fmt.Sprintf("Compile error: %v", err))
		return
	}

	// Inject bytecode at UserMemoryStart
	userMemStart := g.machine.CPU.UserMemoryStart()
	mem := g.machine.CPU.Memory()
	
	if int(userMemStart)+len(bytecode) > len(mem) {
		g.repl.log = append(g.repl.log, "Error: Bytecode too large for user memory")
		return
	}
	
	copy(mem[userMemStart:], bytecode)
	
	// Set vector 0 to start of user memory and trigger it
	g.machine.CPU.WriteVector(0, userMemStart)
	g.machine.CPU.TriggerVector(0)
}

func (g *Game) Update() error {
	// Handle boot timer
	if g.bootTimer > 0 {
		g.bootTimer--
		return nil
	}

	// Toggle debug overlay
	if inpututil.IsKeyJustPressed(ebiten.KeyF1) {
		g.showDebug = !g.showDebug
	}

	// Toggle between windows
	if inpututil.IsKeyJustPressed(ebiten.KeyF2) {
		if g.repl != nil {
			// Toggle between VM (index 0) and REPL (index 1)
			if g.activeWinIdx == 0 {
				g.activeWinIdx = 1
				g.repl.active = true
			} else {
				g.activeWinIdx = 0
				g.repl.active = false
			}
		}
	}

	// Trigger V-Blank at the start of every frame
	if err := g.machine.VBlank(); err != nil {
		return err
	}

	// Handle REPL input
	if g.repl != nil && g.repl.active {
		chars := ebiten.AppendInputChars(nil)
		for _, char := range chars {
			if char >= 32 && char <= 126 {
				// Typing escapes history-browse mode.
				g.repl.lineIdx = len(g.repl.lines)
				g.repl.draft = ""
				g.repl.input += string(char)
			}
		}

		if inpututil.IsKeyJustPressed(ebiten.KeyBackspace) {
			if len(g.repl.input) > 0 {
				g.repl.lineIdx = len(g.repl.lines)
				g.repl.draft = ""
				g.repl.input = g.repl.input[:len(g.repl.input)-1]
			}
		}

		if (ebiten.IsKeyPressed(ebiten.KeyControl) || ebiten.IsKeyPressed(ebiten.KeyMeta)) && inpututil.IsKeyJustPressed(ebiten.KeyV) {
			if text, err := clipboard.ReadAll(); err == nil && text != "" {
				g.repl.lineIdx = len(g.repl.lines)
				g.repl.draft = ""
				g.repl.input += text
			}
		}

		if inpututil.IsKeyJustPressed(ebiten.KeyArrowUp) {
			g.replHistoryUp()
		}
		if inpututil.IsKeyJustPressed(ebiten.KeyArrowDown) {
			g.replHistoryDown()
		}

		if inpututil.IsKeyJustPressed(ebiten.KeyEnter) {
			line := strings.TrimSpace(g.repl.input)
			if line != "" {
				// Record the submitted line in history, skipping consecutive
				// duplicates so the arrow keys move meaningfully.
				if len(g.repl.lines) == 0 || g.repl.lines[len(g.repl.lines)-1] != line {
					g.repl.lines = append(g.repl.lines, line)
				}
				g.repl.lineIdx = len(g.repl.lines)
				g.repl.draft = ""

				g.repl.log = append(g.repl.log, "lux> "+line)
				if line == "clear" {
					g.repl.log = []string{}
				} else {
					g.executeREPL(line)
				}
				g.repl.input = ""
			}
		}
	} else {
		// Handle Paste (Ctrl+V / Cmd+V)
		if (ebiten.IsKeyPressed(ebiten.KeyControl) || ebiten.IsKeyPressed(ebiten.KeyMeta)) && inpututil.IsKeyJustPressed(ebiten.KeyV) {
			if text, err := clipboard.ReadAll(); err == nil && text != "" {
				for _, char := range text {
					g.machine.PushKey(int32(char))
				}
			}
		} else {
			// Handle keyboard input for VM
			chars := ebiten.AppendInputChars(nil)
			for _, char := range chars {
				g.machine.PushKey(int32(char))
			}
		}

		// Handle some special keys
		if inpututil.IsKeyJustPressed(ebiten.KeyBackspace) {
			g.machine.PushKey(8)
		}
		if inpututil.IsKeyJustPressed(ebiten.KeyEnter) {
			g.machine.PushKey(13)
		}
		if inpututil.IsKeyJustPressed(ebiten.KeyEscape) {
			g.machine.PushKey(27)
		}
	}

	// Handle scrolling (mouse wheel)
	_, dy := ebiten.Wheel()
	if dy != 0 {
		win := g.activeWindow()
		if g.repl != nil && g.repl.active {
			// Scroll REPL log
			g.repl.scrollOffset -= int(dy * 3)
			if g.repl.scrollOffset < 0 {
				g.repl.scrollOffset = 0
			}
			maxScroll := len(g.repl.log) - 15
			if maxScroll < 0 {
				maxScroll = 0
			}
			if g.repl.scrollOffset > maxScroll {
				g.repl.scrollOffset = maxScroll
			}
		} else {
			// Scroll VM window
			win.scrollY -= int32(dy * 3)
			if win.scrollY < 0 {
				win.scrollY = 0
			}
			maxScroll := int32(topBarHeight)
			if win.scrollY > maxScroll {
				win.scrollY = maxScroll
			}
		}
	}

	// Handle mouse input
	mx, my := ebiten.CursorPosition()
	g.mouseX, g.mouseY = mx, my
	var mBtn uint32
	if ebiten.IsMouseButtonPressed(ebiten.MouseButtonLeft) {
		mBtn |= 1
	}
	if ebiten.IsMouseButtonPressed(ebiten.MouseButtonRight) {
		mBtn |= 2
	}
	if ebiten.IsMouseButtonPressed(ebiten.MouseButtonMiddle) {
		mBtn |= 4
	}
	adjustedY := my - topBarHeight
	if adjustedY < 0 {
		adjustedY = 0
	}
	g.machine.MoveMouse(int32(mx), int32(adjustedY))
	g.machine.PushMouseButton(mBtn)

	// Tick the machine (runs until YIELD or HALT)
	running, err := g.machine.Tick()
	if err != nil {
		return err
	}
	if !running && (g.repl == nil || !g.repl.active) {
		os.Exit(0)
	}

	return nil
}

func (g *Game) Draw(screen *ebiten.Image) {
	// Get current dimensions from system
	sw := int(g.machine.System.ScreenWidth())
	sh := int(g.machine.System.ScreenHeight())

	if g.bootTimer > 0 {
		screen.Fill(color.Black)
		// Center "CLOISTER" (8 chars * 4px = 32px wide)
		drawText(screen, "CLOISTER", (sw-32)/2, (sh-5)/2, color.White)
		return
	}

	// Draw the framebuffer in content area (offset by topBarHeight)
	fb := g.machine.System.Framebuffer()
	win := g.activeWindow()
	contentHeight := sh - topBarHeight
	for y := 0; y < contentHeight; y++ {
		srcY := y + int(win.scrollY)
		if srcY >= sh {
			break
		}
		for x := 0; x < sw; x++ {
			offset := (srcY*sw + x) * 4
			if offset+4 <= len(fb) {
				r := fb[offset]
				green := fb[offset+1]
				b := fb[offset+2]
				screen.Set(x, y+topBarHeight, color.RGBA{r, green, b, 255})
			}
		}
	}

	// Draw Mac-style Top Bar
	ebitenutil.DrawRect(screen, 0, 0, float64(sw), float64(topBarHeight), color.White)
	ebitenutil.DrawLine(screen, 0, float64(topBarHeight), float64(sw), float64(topBarHeight), color.Black)
	drawChicagoText(screen, "%", 4, 3, color.Black)

	// Draw active window name in center
	winName := win.name
	nameX := (sw - len(winName)*8) / 2
	drawChicagoText(screen, winName, nameX, 3, color.Black)

	timeStr := time.Now().Format("15:04")
	drawChicagoText(screen, timeStr, sw-(len(timeStr)*8)-4, 3, color.Black)

	// Draw mouse cursor (only in content area)
	if g.mouseY >= topBarHeight {
		drawMouse(screen, g.mouseX, g.mouseY)
	}

	if g.showDebug {
		cpu := g.machine.CPU
		sys := g.machine.System
		stack := cpu.Stack()
		// Limit stack display to last 8 items
		if len(stack) > 8 {
			stack = stack[len(stack)-8:]
		}

		// CPU info
		msg := fmt.Sprintf("PC: 0x%04X\nOP: %s\nStack: %v\n",
			cpu.PC(), cpu.LastOpcode(), stack)

		// MMIO Registers
		msg += "\nMMIO Registers:\n"
		for _, reg := range sys.MMIORegisters() {
			msg += fmt.Sprintf("%-9s: 0x%08X (%d)\n", reg.Name, uint32(reg.Value), reg.Value)
		}

		ebitenutil.DebugPrint(screen, msg)
	}

	if g.repl != nil && g.repl.active {
		msg := ""
		// Show log lines with scroll offset; show up to ~15 lines
		maxLines := 15
		startIdx := g.repl.scrollOffset
		if len(g.repl.log) > maxLines {
			if startIdx > len(g.repl.log)-maxLines {
				startIdx = len(g.repl.log) - maxLines
			}
		}
		if startIdx < 0 {
			startIdx = 0
		}
		for i := startIdx; i < len(g.repl.log) && i < startIdx+maxLines; i++ {
			msg += g.repl.log[i] + "\n"
		}
		msg += "lux> " + g.repl.input + "_"
		ebitenutil.DebugPrintAt(screen, msg, 2, topBarHeight+2)
	}
}

func (g *Game) Layout(outsideWidth, outsideHeight int) (int, int) {
	// If the user resized the window, we update the internal resolution.
	// We scale down the outside dimensions by screenScale to get logical pixels.
	w := outsideWidth / screenScale
	h := outsideHeight / screenScale
	if w > 0 && h > 0 {
		g.machine.System.SetResolution(int32(w), int32(h))
	}
	return int(g.machine.System.ScreenWidth()), int(g.machine.System.ScreenHeight())
}

type clipboardBus struct {
	system *system.System
	memory []byte
}

func (c *clipboardBus) Read(address uint32) (int32, error) {
	return c.system.Read(address)
}

func (c *clipboardBus) Write(address uint32, value int32) error {
	if address == 0x30A0 {
		ptr := uint32(value)
		var text []byte
		for ptr < uint32(len(c.memory)) && c.memory[ptr] != 0 {
			text = append(text, c.memory[ptr])
			ptr++
		}
		clipboard.WriteAll(string(text))
		return nil
	}
	return c.system.Write(address, value)
}

func main() {
	memFlag := flag.Int("mem", 32, "VM memory size in megabytes (max 128)")
	widthFlag := flag.Int("w", 0, "Screen width override (0 = defer to boot.lux)")
	heightFlag := flag.Int("h", 0, "Screen height override (0 = defer to boot.lux)")
	scaleFlag := flag.Int("scale", 0, "Window pixel scale override (0 = defer to boot.lux)")
	flag.Parse()

	// Detect whether the user passed -w / -h / -scale explicitly, so those
	// values override whatever boot.lux writes during its first tick.
	explicit := map[string]bool{}
	flag.Visit(func(f *flag.Flag) { explicit[f.Name] = true })

	if *memFlag > 128 {
		fmt.Println("Memory size capped at 128MB")
		*memFlag = 128
	}
	memSize := uint32(*memFlag) * 1024 * 1024

	var program []byte
	var err error
	var replMode bool

	if len(flag.Args()) < 1 {
		replMode = true
		// Try to load the default boot program; fall back to a no-op halt
		// if it isn't next to us.
		if src, readErr := os.ReadFile(defaultBootPath); readErr == nil {
			program, err = lux.Compile(string(src))
			if err != nil {
				fmt.Fprintf(os.Stderr, "compile %s: %v\n", defaultBootPath, err)
				os.Exit(1)
			}
		} else {
			program = []byte{vm.OpHalt}
		}
	} else {
		filename := flag.Args()[0]
		program, err = lux.LoadProgram(filename)
		if err != nil {
			fmt.Fprintf(os.Stderr, "Error: %v\n", err)
			os.Exit(1)
		}
	}

	machine := system.NewMachine(program, memSize)

	// Wrap the bus to intercept clipboard writes
	bus := &clipboardBus{
		system: machine.System,
		memory: machine.CPU.Memory(),
	}
	machine.CPU.SetBus(bus)

	// Pin the File device's sandbox to the directory CLOISTER was launched
	// from. Any attempt to read/write/stat/delete a path that escapes this
	// root (via .., an absolute path, or a symlink) is rejected with -1.
	cwd, err := os.Getwd()
	if err != nil {
		fmt.Fprintf(os.Stderr, "Error: cannot determine launch directory: %v\n", err)
		os.Exit(1)
	}
	if err := machine.SetSandboxRoot(cwd); err != nil {
		fmt.Fprintf(os.Stderr, "Error: cannot pin sandbox root: %v\n", err)
		os.Exit(1)
	}

	// Let the VM run up to its first YIELD / HALT so a program's startup
	// code (e.g. boot.lux setting SCR_W / SCR_H / TEXT cell-size) has a
	// chance to populate the MMIO registers before we size the host window.
	if _, err := machine.Tick(); err != nil {
		fmt.Fprintf(os.Stderr, "Error during boot tick: %v\n", err)
		os.Exit(1)
	}

	// CLI flags override whatever the program wrote during the boot tick.
	if explicit["w"] {
		machine.System.SetResolution(int32(*widthFlag), machine.System.ScreenHeight())
	}
	if explicit["h"] {
		machine.System.SetResolution(machine.System.ScreenWidth(), int32(*heightFlag))
	}

	// Read back the VM's chosen screen size and text scale to pick a window
	// scale.  The text-device scale doubles nicely as the window zoom factor
	// because it's the same intent: "how large should a logical pixel be?"
	sw := int(machine.System.ScreenWidth())
	sh := int(machine.System.ScreenHeight())
	if sw <= 0 || sh <= 0 {
		sw, sh = 80, 80
		machine.System.SetResolution(int32(sw), int32(sh))
	}
	textScale := machine.System.TextScale()
	if textScale < 1 {
		textScale = 1
	}
	screenScale = textScale
	if explicit["scale"] && *scaleFlag > 0 {
		screenScale = *scaleFlag
	}

	var repl *GraphicalREPL
	if replMode {
		repl = &GraphicalREPL{
			active: true,
			log:    []string{"LUX REPL Mode Active"},
		}

		var lineBuffer string
		machine.CPU.OutputHandler = func(value int32, format int32) {
			if format == 1 {
				if value == '\n' {
					if lineBuffer != "" {
						repl.log = append(repl.log, lineBuffer)
						lineBuffer = ""
					}
				} else {
					lineBuffer += string(rune(value))
				}
			} else {
				lineBuffer += fmt.Sprintf("%d ", value)
			}
			
			// Flush the buffer if there's no newline after some output
			if len(lineBuffer) > 0 && format != 1 {
				repl.log = append(repl.log, lineBuffer)
				lineBuffer = ""
			} else if format == 1 && value != '\n' && len(lineBuffer) > 50 {
				repl.log = append(repl.log, lineBuffer)
				lineBuffer = ""
			}
		}
	}

	game := &Game{
		machine:   machine,
		bootTimer: 120,
		repl:      repl,
		windows: []Window{
			{id: 0, name: "VM", scrollY: 0},
			{id: 1, name: "REPL", scrollY: 0},
		},
		activeWinIdx: 0,
	}

	windowWidth := sw * screenScale
	windowHeight := sh * screenScale

	ebiten.SetWindowSize(windowWidth, windowHeight)
	ebiten.SetWindowResizingMode(ebiten.WindowResizingModeEnabled)
	ebiten.SetWindowTitle("CLOISTER")
	_ = replMode // kept for future REPL-specific tweaks
	if err := ebiten.RunGame(game); err != nil {
		fmt.Fprintf(os.Stderr, "Error: %v\n", err)
		os.Exit(1)
	}
}
