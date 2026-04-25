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

type Game struct {
	machine   *system.Machine
	showDebug bool
	bootTimer int
	mouseX    int
	mouseY    int
}

// luxKeyCodeFromEbiten maps ebiten key codes to Lux key codes (ASCII where applicable)
func luxKeyCodeFromEbiten(k ebiten.Key) int32 {
	switch k {
	case ebiten.KeyA:
		return 97 // 'a'
	case ebiten.KeyB:
		return 98
	case ebiten.KeyC:
		return 99
	case ebiten.KeyD:
		return 100
	case ebiten.KeyE:
		return 101
	case ebiten.KeyF:
		return 102
	case ebiten.KeyG:
		return 103
	case ebiten.KeyH:
		return 104
	case ebiten.KeyI:
		return 105
	case ebiten.KeyJ:
		return 106
	case ebiten.KeyK:
		return 107
	case ebiten.KeyL:
		return 108
	case ebiten.KeyM:
		return 109
	case ebiten.KeyN:
		return 110
	case ebiten.KeyO:
		return 111
	case ebiten.KeyP:
		return 112
	case ebiten.KeyQ:
		return 113
	case ebiten.KeyR:
		return 114
	case ebiten.KeyS:
		return 115
	case ebiten.KeyT:
		return 116
	case ebiten.KeyU:
		return 117
	case ebiten.KeyV:
		return 118
	case ebiten.KeyW:
		return 119
	case ebiten.KeyX:
		return 120
	case ebiten.KeyY:
		return 121
	case ebiten.KeyZ:
		return 122
	case ebiten.Key0:
		return 48
	case ebiten.Key1:
		return 49
	case ebiten.Key2:
		return 50
	case ebiten.Key3:
		return 51
	case ebiten.Key4:
		return 52
	case ebiten.Key5:
		return 53
	case ebiten.Key6:
		return 54
	case ebiten.Key7:
		return 55
	case ebiten.Key8:
		return 56
	case ebiten.Key9:
		return 57
	case ebiten.KeySpace:
		return 32
	case ebiten.KeyEnter:
		return 13
	case ebiten.KeyBackspace:
		return 8
	case ebiten.KeyTab:
		return 9
	case ebiten.KeyEscape:
		return 27
	default:
		return 0 // Unmapped
	}
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

func drawChicagoText(screen *ebiten.Image, text string, x, y, scale int, clr color.Color) {
	if scale <= 0 {
		scale = 1
	}
	charX := x
	for _, r := range text {
		if r < 128 && r >= 0x20 { // Printable ASCII range
			glyph := system.Font[r]
			// Each row of the glyph is a byte where each bit represents a pixel
			// Bits are LSB-first: bit 0 is leftmost pixel, bit 7 is rightmost pixel
			for row := 0; row < 8; row++ {
				bits := glyph[row]
				for col := 0; col < 8; col++ {
					// Check if this pixel is lit (LSB-first)
					if (bits & (1 << col)) != 0 {
						// Draw a scale×scale block for this pixel
						for dy := 0; dy < scale; dy++ {
							for dx := 0; dx < scale; dx++ {
								px := charX + col*scale + dx
								py := y + row*scale + dy
								if px >= 0 && py >= 0 { // Basic bounds check
									screen.Set(px, py, clr)
								}
							}
						}
					}
				}
			}
			// Advance to next character position
			charX += 8 * scale
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

	// Queue keyboard input through the service manager
	for _, k := range inpututil.AppendJustPressedKeys(nil) {
		// Simple key code mapping: for now, map keys to ASCII for basic testing
		keyCode := luxKeyCodeFromEbiten(k)
		if keyCode > 0 {
			g.machine.Services().QueueKeyDown(keyCode)
		}
	}
	for _, k := range inpututil.AppendJustReleasedKeys(nil) {
		keyCode := luxKeyCodeFromEbiten(k)
		if keyCode > 0 {
			g.machine.Services().QueueKeyUp(keyCode)
		}
	}

	// Queue mouse input through the service manager
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

	// Queue mouse input through the service manager
	g.machine.Services().QueueMouseMove(int32(mx), int32(adjustedY))
	g.machine.Services().QueueMouseButton(int32(mx), int32(adjustedY), mBtn, mBtn != 0)

	// Drain all pending input events and dispatch to VM
	g.machine.DrainInputEvents()

	// Trigger V-Blank at the start of every frame
	if err := g.machine.VBlank(); err != nil {
		return err
	}

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
	// Get current dimensions from system
	sw := int(g.machine.System.ScreenWidth())
	sh := int(g.machine.System.ScreenHeight())

	if g.bootTimer > 0 {
		screen.Fill(color.Black)
		// Center "CLOISTER" (8 chars * 4px = 32px wide)
		drawText(screen, "CLOISTER", (sw-32)/2, (sh-5)/2, color.White)
		return
	}

	// Draw all visible windows in Z-order (service manager provides sorted list)
	windows := g.machine.System.Services.ListWindowsSorted()
	for _, win := range windows {
		if !win.Visible {
			continue
		}
		// Draw window framebuffer at (win.X, win.Y+topBarHeight)
		// Clipped to screen bounds
		for y := 0; y < int(win.Height) && int(win.Y)+y+topBarHeight < sh; y++ {
			srcY := y + int(win.ScrollY)
			if srcY >= int(win.Height) {
				break
			}
			screenY := int(win.Y) + y + topBarHeight
			if screenY < topBarHeight {
				continue
			}

			for x := 0; x < int(win.Width) && int(win.X)+x < sw; x++ {
				screenX := int(win.X) + x
				offset := (srcY*int(win.Width) + x) * 4
				if offset+4 <= len(win.FrameBuf) {
					r := win.FrameBuf[offset]
					green := win.FrameBuf[offset+1]
					b := win.FrameBuf[offset+2]
					screen.Set(screenX, screenY, color.RGBA{r, green, b, 255})
				}
			}
		}
	}

	// Draw Mac-style Top Bar (white background with black text)
	ebitenutil.DrawRect(screen, 0, 0, float64(sw), float64(topBarHeight), color.White)
	ebitenutil.DrawLine(screen, 0, float64(topBarHeight), float64(sw), float64(topBarHeight), color.Black)
	drawChicagoText(screen, "%", 4, 2, 2, color.Black) // Scale 2 for top bar, moved up by 1px

	// Draw active window name in center
	winName := g.machine.System.Services.ActiveWindowName()
	if winName == "" {
		winName = "VM"
	}
	// With scale=2, each character is 16 pixels wide (8*2)
	nameX := (sw - len(winName)*16) / 2
	drawChicagoText(screen, winName, nameX, 2, 2, color.Black) // Scale 2 for top bar, moved up by 1px

	timeStr := time.Now().Format("15:04")
	// Position time at right side with proper spacing
	drawChicagoText(screen, timeStr, sw-(len(timeStr)*16)-4, 2, 2, color.Black) // Scale 2 for top bar, moved up by 1px

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
	// Enforce minimum window size of 800x600
	minWidth := 800
	minHeight := 600
	if outsideWidth < minWidth {
		outsideWidth = minWidth
	}
	if outsideHeight < minHeight {
		outsideHeight = minHeight
	}

	// If the user resized the window, we update the internal resolution.
	// We scale down the outside dimensions by screenScale to get logical pixels.
	w := outsideWidth / screenScale
	h := outsideHeight / screenScale
	if w > 0 && h > 0 {
		// Enforce minimum internal resolution as well
		if w < 100 {
			w = 100
		}
		if h < 75 {
			h = 75
		}
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

	// Load boot program
	bootPath := defaultBootPath
	if flag.NArg() > 0 {
		bootPath = flag.Arg(0)
	}
	bootBytecode, err := lux.LoadProgram(bootPath)
	if err != nil {
		fmt.Fprintf(os.Stderr, "Error loading %s: %v\n", bootPath, err)
		os.Exit(1)
	}

	machine := system.NewMachine(bootBytecode, uint32(*memFlag)*1024*1024)

	// Set sandbox root to current working directory for file operations
	cwd, err := os.Getwd()
	if err != nil {
		fmt.Fprintf(os.Stderr, "Error getting working directory: %v\n", err)
		os.Exit(1)
	}
	if err := machine.System.SetSandboxRoot(cwd); err != nil {
		fmt.Fprintf(os.Stderr, "Error setting sandbox root: %v\n", err)
		os.Exit(1)
	}

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
	}

	// Create default window via the service manager (ID for future use)
	_, _ = machine.System.Services.CreateWindow("VM", int32(sw), int32(sh-topBarHeight))

	windowWidth := sw * screenScale
	windowHeight := sh * screenScale

	// Enforce minimum window size of 800x600
	if windowWidth < 800 {
		windowWidth = 800
	}
	if windowHeight < 600 {
		windowHeight = 600
	}

	ebiten.SetWindowSize(windowWidth, windowHeight)
	ebiten.SetWindowResizingMode(ebiten.WindowResizingModeEnabled)
	ebiten.SetWindowTitle("CLOISTER")

	if err := ebiten.RunGame(game); err != nil {
		fmt.Fprintf(os.Stderr, "Error: %v\n", err)
		os.Exit(1)
	}
}
