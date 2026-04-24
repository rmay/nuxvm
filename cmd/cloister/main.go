package main

import (
	"flag"
	"fmt"
	"image/color"
	"os"
	"strings"

	"github.com/hajimehoshi/ebiten/v2"
	"github.com/hajimehoshi/ebiten/v2/ebitenutil"
	"github.com/hajimehoshi/ebiten/v2/inpututil"
	"github.com/rmay/nuxvm/pkg/lux"
	"github.com/rmay/nuxvm/pkg/system"
	"github.com/rmay/nuxvm/pkg/vm"
)

var screenScale = 10

type GraphicalREPL struct {
	active  bool
	history string
	input   string
	log     []string
}

type Game struct {
	machine   *system.Machine
	showDebug bool
	bootTimer int
	mouseX    int
	mouseY    int
	replChan  chan int32
	repl      *GraphicalREPL
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

func drawMouse(screen *ebiten.Image, x, y int) {
	white := color.RGBA{255, 255, 255, 255}
	screen.Set(x, y-1, white)
	screen.Set(x, y+1, white)
	screen.Set(x-1, y, white)
	screen.Set(x+1, y, white)
	screen.Set(x, y, color.RGBA{0, 0, 0, 255})
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
		g.repl.history += line + "\n"
		g.repl.log = append(g.repl.log, "Defined word")
		return
	}

	source := g.repl.history + line
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

	// Trigger V-Blank at the start of every frame
	if err := g.machine.VBlank(); err != nil {
		return err
	}

	// Handle REPL input
	if g.repl != nil && g.repl.active {
		chars := ebiten.AppendInputChars(nil)
		for _, char := range chars {
			if char >= 32 && char <= 126 {
				g.repl.input += string(char)
			}
		}

		if inpututil.IsKeyJustPressed(ebiten.KeyBackspace) {
			if len(g.repl.input) > 0 {
				g.repl.input = g.repl.input[:len(g.repl.input)-1]
			}
		}

		if inpututil.IsKeyJustPressed(ebiten.KeyEnter) {
			line := strings.TrimSpace(g.repl.input)
			if line != "" {
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
		// Handle keyboard input for VM
		chars := ebiten.AppendInputChars(nil)
		for _, char := range chars {
			g.machine.PushKey(int32(char))
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
	g.machine.MoveMouse(int32(mx), int32(my))
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

	// Draw the framebuffer
	fb := g.machine.System.Framebuffer()
	for y := 0; y < sh; y++ {
		for x := 0; x < sw; x++ {
			offset := (y*sw + x) * 4
			if offset+4 <= len(fb) {
				r := fb[offset]
				green := fb[offset+1]
				b := fb[offset+2]
				screen.Set(x, y, color.RGBA{r, green, b, 255})
			}
		}
	}

	drawMouse(screen, g.mouseX, g.mouseY)

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
		// Keep last 15 lines of log
		startIdx := 0
		if len(g.repl.log) > 15 {
			startIdx = len(g.repl.log) - 15
		}
		for i := startIdx; i < len(g.repl.log); i++ {
			msg += g.repl.log[i] + "\n"
		}
		msg += "lux> " + g.repl.input + "_"
		ebitenutil.DebugPrintAt(screen, msg, 2, 2)
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

func main() {
	memFlag := flag.Int("mem", 32, "VM memory size in megabytes (max 128)")
	widthFlag := flag.Int("w", 80, "Initial screen width")
	heightFlag := flag.Int("h", 80, "Initial screen height")
	flag.Parse()

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
		*widthFlag = 256
		*heightFlag = 192
		screenScale = 2
		program = []byte{vm.OpHalt}
	} else {
		filename := flag.Args()[0]
		program, err = os.ReadFile(filename)
		if err != nil {
			fmt.Fprintf(os.Stderr, "Error reading file: %v\n", err)
			os.Exit(1)
		}
	}

	machine := system.NewMachine(program, memSize)
	machine.System.SetResolution(int32(*widthFlag), int32(*heightFlag))

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
	}

	// We set window size according to the mode
	windowWidth := *widthFlag * screenScale
	windowHeight := *heightFlag * screenScale
	if replMode {
		// Use a smaller scale for REPL mode so it fits typical screens nicely.
		windowWidth = *widthFlag * 2
		windowHeight = *heightFlag * 2
	}
	
	ebiten.SetWindowSize(windowWidth, windowHeight)
	ebiten.SetWindowResizingMode(ebiten.WindowResizingModeEnabled)
	ebiten.SetWindowTitle("CLOISTER")
	if err := ebiten.RunGame(game); err != nil {
		fmt.Fprintf(os.Stderr, "Error: %v\n", err)
		os.Exit(1)
	}
}
