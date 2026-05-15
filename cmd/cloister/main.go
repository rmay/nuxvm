package main

import (
	"flag"
	"fmt"
	"image/color"
	"os"

	"github.com/hajimehoshi/ebiten/v2"
	"github.com/hajimehoshi/ebiten/v2/inpututil"
	"github.com/rmay/nuxvm/pkg/lux"
	"github.com/rmay/nuxvm/pkg/system"
)

type Game struct {
	machine *system.Machine
}

func (g *Game) Update() error {
	g.machine.DrainInputEvents()

	// Handle keyboard
	for _, k := range inpututil.AppendPressedKeys(nil) {
		g.machine.QueueKeyDown(int32(k))
	}

	// Handle mouse
	mx, my := ebiten.CursorPosition()
	g.machine.QueueMouseMove(int32(mx), int32(my))
	if inpututil.IsMouseButtonJustPressed(ebiten.MouseButtonLeft) {
		g.machine.QueueMouseButton(int32(mx), int32(my), 1, true)
	}
	if inpututil.IsMouseButtonJustReleased(ebiten.MouseButtonLeft) {
		g.machine.QueueMouseButton(int32(mx), int32(my), 1, false)
	}

	// Tick the machine
	running, err := g.machine.Tick()
	if err != nil {
		fmt.Fprintf(os.Stderr, "VM crash: %v\n", err)
		return err
	}
	if !running {
		return fmt.Errorf("VM halted")
	}
	return nil
}

func (g *Game) Draw(screen *ebiten.Image) {
	// Copy system screenPixels to ebiten screen
	pixels := g.machine.System.ScreenPixels()
	w := int(g.machine.System.ScreenWidth())
	h := int(g.machine.System.ScreenHeight())
	size := w * h * 4

	if pixels != nil && len(pixels) >= size {
		screen.WritePixels(pixels[:size])
	} else {
		// Fallback: clear to dark red to indicate error
		screen.Fill(color.RGBA{64, 0, 0, 255})
	}
}

func (g *Game) Layout(outsideWidth, outsideHeight int) (screenWidth, screenHeight int) {
	sw, sh := int(g.machine.System.ScreenWidth()), int(g.machine.System.ScreenHeight())
	return sw, sh
}

func main() {
	memFlag := flag.Int("mem", 32, "VM memory size in megabytes")
	flag.Parse()

	shellPath := "apps/Shell.bin"
	if flag.NArg() > 0 {
		shellPath = flag.Arg(0)
	}

	bytecode, err := lux.LoadProgram(shellPath)
	if err != nil {
		fmt.Fprintf(os.Stderr, "Error loading %s: %v\n", shellPath, err)
		os.Exit(1)
	}

	machine := system.NewMachine(bytecode, uint32(*memFlag)*1024*1024, true)
	machine.System.SetResolution(800, 600)

	ebiten.SetWindowTitle("NuxVM / Actor 9")
	ebiten.SetWindowSize(800, 600)

	game := &Game{machine: machine}
	fmt.Fprintf(os.Stderr, "Starting NuxVM: %dx%d\n", machine.System.ScreenWidth(), machine.System.ScreenHeight())
	if err := ebiten.RunGame(game); err != nil {
		fmt.Fprintf(os.Stderr, "Error: %v\n", err)
		os.Exit(1)
	}
}
