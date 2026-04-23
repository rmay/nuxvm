package main

import (
	"flag"
	"fmt"
	"image/color"
	"os"

	"github.com/hajimehoshi/ebiten/v2"
	"github.com/hajimehoshi/ebiten/v2/ebitenutil"
	"github.com/hajimehoshi/ebiten/v2/inpututil"
	"github.com/rmay/nuxvm/pkg/system"
)

const (
	screenWidth  = 64
	screenHeight = 32
	screenScale  = 10
)

type Game struct {
	machine   *system.Machine
	showDebug bool
	bootTimer int
	mouseX    int
	mouseY    int
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

	// Handle keyboard input
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

	// Tick the machine (runs until YIELD or HALT)
	running, err := g.machine.Tick()
	if err != nil {
		return err
	}
	if !running {
		os.Exit(0)
	}

	return nil
}

func (g *Game) Draw(screen *ebiten.Image) {
	if g.bootTimer > 0 {
		screen.Fill(color.Black)
		// Center "CLOISTER" (8 chars * 4px = 32px wide)
		drawText(screen, "CLOISTER", (screenWidth-32)/2, (screenHeight-5)/2, color.White)
		return
	}

	// Draw the framebuffer
	fb := g.machine.System.Framebuffer()
	for y := 0; y < screenHeight; y++ {
		for x := 0; x < screenWidth; x++ {
			offset := (y*screenWidth + x) * 4
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
}

func (g *Game) Layout(outsideWidth, outsideHeight int) (int, int) {
	return screenWidth, screenHeight
}

func main() {
	memFlag := flag.Int("mem", 32, "VM memory size in megabytes (max 128)")
	flag.Parse()

	if *memFlag > 128 {
		fmt.Println("Memory size capped at 128MB")
		*memFlag = 128
	}
	memSize := uint32(*memFlag) * 1024 * 1024

	var filename string
	if len(flag.Args()) < 1 {
		filename = "lib/boot.bin"
		// Check if lib/boot.bin exists, if not, try examples/keyboard.bin
		if _, err := os.Stat(filename); os.IsNotExist(err) {
			filename = "examples/keyboard.bin"
		}
	} else {
		filename = flag.Args()[0]
	}

	program, err := os.ReadFile(filename)
	if err != nil {
		if len(flag.Args()) < 1 {
			fmt.Fprintf(os.Stderr, "Default boot program not found (%s). Please provide a program.\n", filename)
		} else {
			fmt.Fprintf(os.Stderr, "Error reading file: %v\n", err)
		}
		os.Exit(1)
	}

	machine := system.NewMachine(program, memSize)
	
	game := &Game{
		machine:   machine,
		bootTimer: 300,
	}

	ebiten.SetWindowSize(screenWidth*screenScale, screenHeight*screenScale)
	ebiten.SetWindowTitle("NUX OS")
	if err := ebiten.RunGame(game); err != nil {
		fmt.Fprintf(os.Stderr, "Error: %v\n", err)
		os.Exit(1)
	}
}
