package main

import (
	"bytes"
	"encoding/binary"
	"flag"
	"fmt"
	"image/color"
	"math"
	"os"
	"path"
	"strings"

	"github.com/hajimehoshi/ebiten/v2"
	"github.com/hajimehoshi/ebiten/v2/audio"
	"github.com/hajimehoshi/ebiten/v2/ebitenutil"
	"github.com/hajimehoshi/ebiten/v2/inpututil"
	"github.com/rmay/nuxvm/pkg/lux"
	"github.com/rmay/nuxvm/pkg/system"
	"github.com/rmay/nuxvm/pkg/vm"
)

const audioSampleRate = 44100

type Game struct {
	machine *system.Machine
	lastMX  int
	lastMY  int

	// Launcher state
	launcherMode  bool
	apps          []string
	selectedIndex int
	memSize       int
	audioCtx      *audio.Context
}

// translateKey maps an ebiten Key to the integer keycode that Lux apps see.
// Letters/digits become ASCII (lowercase); arrows use the dedicated 17-20
// codes that Snake.lux and other apps key off.
func translateKey(k ebiten.Key) (int32, bool) {
	shift := ebiten.IsKeyPressed(ebiten.KeyShift)

	switch k {
	case ebiten.KeyArrowUp:
		return 17, true
	case ebiten.KeyArrowDown:
		return 18, true
	case ebiten.KeyArrowLeft:
		return 19, true
	case ebiten.KeyArrowRight:
		return 20, true
	case ebiten.KeySpace:
		return 32, true
	case ebiten.KeyTab:
		return 9, true
	case ebiten.KeyEnter, ebiten.KeyNumpadEnter:
		return 13, true
	case ebiten.KeyEscape:
		return 27, true
	case ebiten.KeyBackspace, ebiten.KeyDelete:
		return 8, true
	}

	if k >= ebiten.KeyA && k <= ebiten.KeyZ {
		if shift {
			return int32(k-ebiten.KeyA) + 'A', true
		}
		return int32(k-ebiten.KeyA) + 'a', true
	}

	if k >= ebiten.KeyDigit0 && k <= ebiten.KeyDigit9 {
		if shift {
			shifted := ")!@#$%^&*("
			return int32(shifted[k-ebiten.KeyDigit0]), true
		}
		return int32(k-ebiten.KeyDigit0) + '0', true
	}

	symbolMap := map[ebiten.Key]struct{ un, sh int32 }{
		ebiten.KeyMinus:        {'-', '_'},
		ebiten.KeyEqual:        {'=', '+'},
		ebiten.KeyLeftBracket:  {'[', '{'},
		ebiten.KeyRightBracket: {']', '}'},
		ebiten.KeyBackslash:    {'\\', '|'},
		ebiten.KeySemicolon:    {';', ':'},
		ebiten.KeyQuote:        {'\'', '"'},
		ebiten.KeyComma:        {',', '<'},
		ebiten.KeyPeriod:       {'.', '>'},
		ebiten.KeySlash:        {'/', '?'},
		ebiten.KeyBackquote:    {'`', '~'},
	}

	if s, ok := symbolMap[k]; ok {
		if shift {
			return s.sh, true
		}
		return s.un, true
	}

	if k >= ebiten.KeyNumpad0 && k <= ebiten.KeyNumpad9 {
		return int32(k-ebiten.KeyNumpad0) + '0', true
	}

	return 0, false
}

func (g *Game) Update() error {
	if g.launcherMode {
		if inpututil.IsKeyJustPressed(ebiten.KeyArrowUp) {
			g.selectedIndex--
			if g.selectedIndex < 0 {
				g.selectedIndex = len(g.apps) - 1
			}
		}
		if inpututil.IsKeyJustPressed(ebiten.KeyArrowDown) {
			g.selectedIndex++
			if g.selectedIndex >= len(g.apps) {
				g.selectedIndex = 0
			}
		}
		if inpututil.IsKeyJustPressed(ebiten.KeyEnter) {
			if len(g.apps) > 0 {
				appPath := path.Join("apps", g.apps[g.selectedIndex])
				if err := g.loadApp(appPath); err != nil {
					fmt.Fprintf(os.Stderr, "Error loading %s: %v\n", appPath, err)
				} else {
					g.launcherMode = false
				}
			}
		}
		return nil
	}

	g.machine.DrainInputEvents()

	for _, k := range inpututil.AppendJustPressedKeys(nil) {
		if code, ok := translateKey(k); ok {
			g.machine.QueueKeyDown(code)
		}
	}

	mx, my := ebiten.CursorPosition()
	if mx != g.lastMX || my != g.lastMY {
		g.machine.QueueMouseMove(int32(mx), int32(my))
		g.lastMX, g.lastMY = mx, my
	}
	if inpututil.IsMouseButtonJustPressed(ebiten.MouseButtonLeft) {
		g.machine.QueueMouseButton(int32(mx), int32(my), 1, true)
	}
	if inpututil.IsMouseButtonJustReleased(ebiten.MouseButtonLeft) {
		g.machine.QueueMouseButton(int32(mx), int32(my), 1, false)
	}

	running, err := g.machine.Tick()
	if err != nil {
		fmt.Fprintf(os.Stderr, "VM crash: %v\n", err)
		return err
	}
	if !running {
		g.launcherMode = true
		return nil
	}
	return nil
}

func (g *Game) Draw(screen *ebiten.Image) {
	if g.launcherMode {
		screen.Fill(color.RGBA{20, 20, 20, 255})
		ebitenutil.DebugPrintAt(screen, "--- NUXVM LAUNCHER ---", 20, 20)
		for i, app := range g.apps {
			prefix := "  "
			if i == g.selectedIndex {
				prefix = "> "
			}
			ebitenutil.DebugPrintAt(screen, prefix+app, 20, 50+i*20)
		}
		return
	}

	pixels := g.machine.System.ScreenPixels()
	w := int(g.machine.System.ScreenWidth())
	h := int(g.machine.System.ScreenHeight())
	size := w * h * 4

	if pixels != nil && len(pixels) >= size {
		screen.WritePixels(pixels[:size])
	} else {
		screen.Fill(color.RGBA{64, 0, 0, 255})
	}
}

func (g *Game) Layout(outsideWidth, outsideHeight int) (screenWidth, screenHeight int) {
	if g.launcherMode {
		return 800, 600
	}
	sw, sh := int(g.machine.System.ScreenWidth()), int(g.machine.System.ScreenHeight())
	return sw, sh
}

// makeTone synthesises a short stereo 16-bit PCM tone at the given frequency.
// duration is in seconds. The waveform fades out so successive plays don't
// click.
func makeTone(freq float64, duration float64) []byte {
	samples := int(audioSampleRate * duration)
	buf := bytes.NewBuffer(make([]byte, 0, samples*4))
	for i := 0; i < samples; i++ {
		t := float64(i) / float64(audioSampleRate)
		env := 1.0
		fade := 0.02
		if t < fade {
			env = t / fade
		} else if t > duration-fade {
			env = (duration - t) / fade
		}
		v := int16(env * 0.25 * 32767 * math.Sin(2*math.Pi*freq*t))
		_ = binary.Write(buf, binary.LittleEndian, v)
		_ = binary.Write(buf, binary.LittleEndian, v)
	}
	return buf.Bytes()
}

func newSoundHandler(ctx *audio.Context) func(int32) {
	return func(soundID int32) {
		if soundID <= 0 {
			return
		}
		pcm := makeTone(float64(soundID), 0.12)
		player := ctx.NewPlayerFromBytes(pcm)
		player.Play()
	}
}

func (g *Game) loadApp(appPath string) error {
	bytecode, err := lux.LoadProgram(appPath, int32(vm.GraphicalBaseAddress))
	if err != nil {
		return err
	}

	machine := system.NewMachine(bytecode, vm.GraphicalBaseAddress, uint32(g.memSize)*1024*1024, false)
	machine.System.SetResolution(800, 600)

	if svc := machine.System.Services; svc != nil {
		svc.SoundHandler = newSoundHandler(g.audioCtx)
	}

	g.machine = machine
	return nil
}

func main() {
	memFlag := flag.Int("mem", 32, "VM memory size in megabytes")
	flag.Parse()

	game := &Game{
		memSize:  *memFlag,
		audioCtx: audio.NewContext(audioSampleRate),
	}

	// Always load the apps list so it's available if we drop back to the launcher
	files, err := os.ReadDir("apps")
	if err == nil {
		for _, f := range files {
			if !f.IsDir() && strings.HasSuffix(f.Name(), ".bin") {
				game.apps = append(game.apps, f.Name())
			}
		}
	}

	if flag.NArg() > 0 {
		shellPath := flag.Arg(0)
		if err := game.loadApp(shellPath); err != nil {
			fmt.Fprintf(os.Stderr, "Error loading %s: %v\n", shellPath, err)
			os.Exit(1)
		}
	} else {
		game.launcherMode = true
		if err != nil {
			fmt.Fprintf(os.Stderr, "Error reading apps directory: %v\n", err)
			os.Exit(1)
		}
		if len(game.apps) == 0 {
			fmt.Fprintf(os.Stderr, "No .bin files found in apps/\n")
			os.Exit(1)
		}
	}

	ebiten.SetWindowTitle("NuxVM / Actor 9")
	ebiten.SetWindowSize(800, 600)

	fmt.Fprintf(os.Stderr, "Starting NuxVM Launcher\n")
	if err := ebiten.RunGame(game); err != nil {
		fmt.Fprintf(os.Stderr, "Error: %v\n", err)
		os.Exit(1)
	}
}
