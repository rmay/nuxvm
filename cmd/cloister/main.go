package main

import (
	"flag"
	"fmt"
	"image/color"
	"os"
	"time"

	"github.com/hajimehoshi/ebiten/v2"
	"github.com/hajimehoshi/ebiten/v2/ebitenutil"
	"github.com/hajimehoshi/ebiten/v2/inpututil"
	"github.com/rmay/nuxvm/pkg/lux"
	"github.com/rmay/nuxvm/pkg/system"
)

var screenScale = 10

// defaultBootPath is the on-disk Lux program cloister loads when invoked
// without a positional argument. It's relative to cwd so editing lib/boot.lux
// in the repo and re-running cloister picks up the change without a rebuild.
const defaultBootPath = "lib/boot.lux"
const topBarHeight = 24

type Window struct {
	id      int
	name    string
	scrollY int32
}

type Game struct {
	machine     *system.Machine
	showDebug   bool
	bootTimer   int
	mouseX      int
	mouseY      int
	windows     []*Window
	showWinMenu bool
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

func drawChicagoText(screen *ebiten.Image, text string, x, y, s int, clr color.Color) {
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

func (g *Game) activeWindow() *Window {
	if len(g.windows) > 0 {
		return g.windows[0]
	}
	return nil
}

func (g *Game) loadAndRun(filename string) {
	bytecode, err := lux.LoadProgram(filename)
	if err != nil {
		fmt.Sprintf("Error loading %s: %v", filename, err)
		return
	}

	userMemStart := g.machine.CPU.UserMemoryStart()
	mem := g.machine.CPU.Memory()

	if int(userMemStart)+len(bytecode) > len(mem) {
		fmt.Sprintf("Error: %s too large", filename)
		return
	}

	copy(mem[userMemStart:], bytecode)
	g.machine.CPU.WriteVector(0, userMemStart)
	g.machine.CPU.TriggerVector(0)
	fmt.Sprintf("Loaded and started %s", filename)
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

	}

	// Trigger V-Blank at the start of every frame
	if err := g.machine.VBlank(); err != nil {
		return err
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
	// If VM halted, keep host running.
	if !running {
		g.machine.CPU.ClearYield()
	}

	return nil
}

func (g *Game) Draw(screen *ebiten.Image) {
	// ... (window menu rendering)
	if g.showWinMenu {
		// Draw window list dropdown
	}
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
	drawChicagoText(screen, "%", 4, 3, 2, color.Black) // Scale 2 for top bar

	// Draw active window name in center
	winName := win.name
	nameX := (sw - len(winName)*8) / 2
	drawChicagoText(screen, winName, nameX, 3, 2, color.Black) // Scale 2 for top bar

	timeStr := time.Now().Format("15:04")
	drawChicagoText(screen, timeStr, sw-(len(timeStr)*8)-4, 3, 2, color.Black) // Scale 2 for top bar

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

	var machine *system.Machine

	// Wrap the bus to intercept clipboard writes
	bus := &clipboardBus{
		system: machine.System,
		memory: machine.CPU.Memory(),
	}
	machine.CPU.SetBus(bus)

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

	game := &Game{
		machine:   machine,
		bootTimer: 60,
		windows: []*Window{
			{id: 0, name: "VM", scrollY: 0},
		},
	}

	windowWidth := sw * screenScale
	windowHeight := sh * screenScale

	ebiten.SetWindowSize(windowWidth, windowHeight)
	ebiten.SetWindowResizingMode(ebiten.WindowResizingModeEnabled)
	ebiten.SetWindowTitle("CLOISTER")

	if err := ebiten.RunGame(game); err != nil {
		fmt.Fprintf(os.Stderr, "Error: %v\n", err)
		os.Exit(1)
	}
}
