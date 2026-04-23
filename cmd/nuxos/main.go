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
}

func (g *Game) Update() error {
	// Toggle debug overlay
	if inpututil.IsKeyJustPressed(ebiten.KeyF1) {
		g.showDebug = !g.showDebug
	}

	// Trigger V-Blank at the start of every frame
	if err := g.machine.VBlank(); err != nil {
		return err
	}

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

	if g.showDebug {
		cpu := g.machine.CPU
		sys := g.machine.System
		stack := cpu.Stack()
		// Limit stack display to last 8 items
		if len(stack) > 8 {
			stack = stack[len(stack)-8:]
		}

		msg := fmt.Sprintf("PC: 0x%04X\nOP: %s\nStack: %v\n%s",
			cpu.PC(), cpu.LastOpcode(), stack, sys.DebugInfo())
		ebitenutil.DebugPrint(screen, msg)
	}
}

func (g *Game) Layout(outsideWidth, outsideHeight int) (int, int) {
	return screenWidth, screenHeight
}

func main() {
	flag.Parse()

	if len(flag.Args()) < 1 {
		fmt.Println("Usage: nuxos <program.nux>")
		os.Exit(1)
	}

	filename := flag.Args()[0]
	program, err := os.ReadFile(filename)
	if err != nil {
		fmt.Fprintf(os.Stderr, "Error reading file: %v\n", err)
		os.Exit(1)
	}

	machine := system.NewMachine(program)
	
	game := &Game{
		machine: machine,
	}

	ebiten.SetWindowSize(screenWidth*screenScale, screenHeight*screenScale)
	ebiten.SetWindowTitle("NUX OS")
	if err := ebiten.RunGame(game); err != nil {
		fmt.Fprintf(os.Stderr, "Error: %v\n", err)
		os.Exit(1)
	}
}
