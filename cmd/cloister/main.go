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
	"github.com/hajimehoshi/ebiten/v2/vector"
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

	// Launcher Esc Menu state
	escMenuOpen  bool
	escMenuHover int // 0=none, 1=continue, 2=restart, 3=quit
}

// currentModifiers reads ebiten's modifier state and packs it into the bitmask
// expected by system.InputEvent.Modifiers (mirrors lib/event.lux MOD_*).
func currentModifiers() uint32 {
	var m uint32
	if ebiten.IsKeyPressed(ebiten.KeyShift) ||
		ebiten.IsKeyPressed(ebiten.KeyShiftLeft) ||
		ebiten.IsKeyPressed(ebiten.KeyShiftRight) {
		m |= system.ModShift
	}
	if ebiten.IsKeyPressed(ebiten.KeyControl) ||
		ebiten.IsKeyPressed(ebiten.KeyControlLeft) ||
		ebiten.IsKeyPressed(ebiten.KeyControlRight) {
		m |= system.ModCtrl
	}
	if ebiten.IsKeyPressed(ebiten.KeyAlt) ||
		ebiten.IsKeyPressed(ebiten.KeyAltLeft) ||
		ebiten.IsKeyPressed(ebiten.KeyAltRight) {
		m |= system.ModAlt
	}
	if ebiten.IsKeyPressed(ebiten.KeyMeta) ||
		ebiten.IsKeyPressed(ebiten.KeyMetaLeft) ||
		ebiten.IsKeyPressed(ebiten.KeyMetaRight) {
		m |= system.ModCmd
	}
	return m
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
	case ebiten.KeyPageUp:
		return 21, true
	case ebiten.KeyPageDown:
		return 22, true
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

// repeatingKeyPressed returns true if the key just started being pressed,
// or if it has been held down long enough to repeat.
func repeatingKeyPressed(key ebiten.Key) bool {
	const (
		delay  = 30
		repeat = 3
	)
	d := inpututil.KeyPressDuration(key)
	if d == 1 {
		return true
	}
	if d >= delay && (d-delay)%repeat == 0 {
		return true
	}
	return false
}

func (g *Game) Update() error {
	if g.launcherMode {
		if g.escMenuOpen {
			if inpututil.IsKeyJustPressed(ebiten.KeyArrowUp) {
				g.escMenuHover--
				if g.escMenuHover < 1 {
					g.escMenuHover = 3
				}
			}
			if inpututil.IsKeyJustPressed(ebiten.KeyArrowDown) {
				g.escMenuHover++
				if g.escMenuHover > 3 {
					g.escMenuHover = 1
				}
			}
			if inpututil.IsKeyJustPressed(ebiten.KeyEnter) {
				switch g.escMenuHover {
				case 1: // Continue
					g.escMenuOpen = false
				case 2: // Restart App (behaves like continue in launcher)
					g.escMenuOpen = false
				case 3: // Quit
					os.Exit(0)
				}
			}
			if inpututil.IsKeyJustPressed(ebiten.KeyEscape) {
				g.escMenuOpen = false
			}

			mx, my := ebiten.CursorPosition()
			winW, winH := 960, 720
			menuX := (winW - 200) / 2
			menuY := (winH - 160) / 2

			relX := mx - menuX
			relY := my - menuY

			hover := 0
			if relX >= 20 && relX <= 180 {
				if relY >= 50 && relY <= 80 {
					hover = 1
				} else if relY >= 85 && relY <= 115 {
					hover = 2
				} else if relY >= 120 && relY <= 150 {
					hover = 3
				}
			}
			
			if mx != g.lastMX || my != g.lastMY {
				if hover != 0 {
					g.escMenuHover = hover
				} else {
					g.escMenuHover = 0
				}
				g.lastMX, g.lastMY = mx, my
			}

			if inpututil.IsMouseButtonJustPressed(ebiten.MouseButtonLeft) {
				if hover == 1 || hover == 2 {
					g.escMenuOpen = false
				} else if hover == 3 {
					os.Exit(0)
				} else {
					g.escMenuOpen = false
				}
			}
			return nil
		}

		if inpututil.IsKeyJustPressed(ebiten.KeyEscape) {
			g.escMenuOpen = true
			g.escMenuHover = 1
			return nil
		}

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

	mods := currentModifiers()
	for _, k := range inpututil.AppendPressedKeys(nil) {
		if repeatingKeyPressed(k) {
			if code, ok := translateKey(k); ok {
				g.machine.QueueKeyDownMods(code, mods)
			}
		}
	}

	mx, my := ebiten.CursorPosition()
	if mx != g.lastMX || my != g.lastMY {
		g.machine.QueueMouseMove(int32(mx), int32(my))
		g.lastMX, g.lastMY = mx, my
	}

	wx, wy := ebiten.Wheel()
	if wx != 0 || wy != 0 {
		g.machine.QueueWheel(wx, wy)
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
		ebiten.SetWindowTitle("Cloister")
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

		if g.escMenuOpen {
			winW, winH := float32(960), float32(720)
			menuX := (winW - 200) / 2
			menuY := (winH - 160) / 2

			// 1. Outer Box
			vector.DrawFilledRect(screen, menuX-2, menuY-2, 204, 164, color.RGBA{0, 0, 0, 255}, false)
			vector.DrawFilledRect(screen, menuX, menuY, 200, 160, color.RGBA{255, 255, 255, 255}, false)

			// 2. Title
			ebitenutil.DebugPrintAt(screen, "System Menu", int(menuX+35), int(menuY+15))

			// 3. Continue Button
			vector.DrawFilledRect(screen, menuX+20, menuY+50, 160, 30, color.RGBA{0, 0, 0, 255}, false)
			btnColor1 := color.RGBA{255, 255, 255, 255}
			if g.escMenuHover == 1 {
				btnColor1 = color.RGBA{170, 170, 170, 255}
			}
			vector.DrawFilledRect(screen, menuX+21, menuY+51, 158, 28, btnColor1, false)
			ebitenutil.DebugPrintAt(screen, "Continue", int(menuX+60), int(menuY+56))

			// 4. Restart Button
			vector.DrawFilledRect(screen, menuX+20, menuY+85, 160, 30, color.RGBA{0, 0, 0, 255}, false)
			btnColor2 := color.RGBA{255, 255, 255, 255}
			if g.escMenuHover == 2 {
				btnColor2 = color.RGBA{170, 170, 170, 255}
			}
			vector.DrawFilledRect(screen, menuX+21, menuY+86, 158, 28, btnColor2, false)
			ebitenutil.DebugPrintAt(screen, "Restart App", int(menuX+50), int(menuY+91))

			// 5. Quit Button
			vector.DrawFilledRect(screen, menuX+20, menuY+120, 160, 30, color.RGBA{0, 0, 0, 255}, false)
			btnColor3 := color.RGBA{255, 255, 255, 255}
			if g.escMenuHover == 3 {
				btnColor3 = color.RGBA{170, 170, 170, 255}
			}
			vector.DrawFilledRect(screen, menuX+21, menuY+121, 158, 28, btnColor3, false)
			ebitenutil.DebugPrintAt(screen, "Quit", int(menuX+80), int(menuY+126))
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
		return 960, 720
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
	machine.System.SetResolution(960, 720)

	if svc := machine.System.Services; svc != nil {
		svc.SoundHandler = newSoundHandler(g.audioCtx)
		svc.TitleHandler = func(title string) {
			ebiten.SetWindowTitle(title)
		}
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

	ebiten.SetWindowTitle("Cloister")
	ebiten.SetWindowSize(960, 720)

	fmt.Fprintf(os.Stderr, "Starting NuxVM Launcher\n")
	if err := ebiten.RunGame(game); err != nil {
		fmt.Fprintf(os.Stderr, "Error: %v\n", err)
		os.Exit(1)
	}
}
