package main

import (
	"bytes"
	"encoding/binary"
	"flag"
	"fmt"
	"image/color"
	"math"
	"os"

	"github.com/hajimehoshi/ebiten/v2"
	"github.com/hajimehoshi/ebiten/v2/audio"
	"github.com/hajimehoshi/ebiten/v2/inpututil"
	"github.com/rmay/nuxvm/pkg/lux"
	"github.com/rmay/nuxvm/pkg/system"
)

const audioSampleRate = 44100

type Game struct {
	machine *system.Machine
	lastMX  int
	lastMY  int
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
	case ebiten.KeyEnter, ebiten.KeyNumpadEnter:
		return 13, true
	case ebiten.KeyEscape:
		return 27, true
	case ebiten.KeyBackspace, ebiten.KeyDelete:
		return 'c', true
	case ebiten.KeyMinus, ebiten.KeyNumpadSubtract:
		return '-', true
	case ebiten.KeySlash, ebiten.KeyNumpadDivide:
		return '/', true
	case ebiten.KeyNumpadMultiply:
		return '*', true
	case ebiten.KeyNumpadAdd:
		return '+', true
	case ebiten.KeyEqual:
		if shift {
			return '+', true
		}
		return '=', true
	}

	if k >= ebiten.KeyA && k <= ebiten.KeyZ {
		return int32(k-ebiten.KeyA) + 'a', true
	}
	if k >= ebiten.KeyDigit0 && k <= ebiten.KeyDigit9 {
		if shift && k == ebiten.KeyDigit8 {
			return '*', true
		}
		return int32(k-ebiten.KeyDigit0) + '0', true
	}
	if k >= ebiten.KeyNumpad0 && k <= ebiten.KeyNumpad9 {
		return int32(k-ebiten.KeyNumpad0) + '0', true
	}

	return 0, false
}

func (g *Game) Update() error {
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
		return fmt.Errorf("VM halted")
	}
	return nil
}

func (g *Game) Draw(screen *ebiten.Image) {
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

	machine := system.NewMachine(bytecode, uint32(*memFlag)*1024*1024, false)
	machine.System.SetResolution(800, 600)

	audioCtx := audio.NewContext(audioSampleRate)
	if svc := machine.System.Services; svc != nil {
		svc.SoundHandler = newSoundHandler(audioCtx)
	}

	ebiten.SetWindowTitle("NuxVM / Actor 9")
	ebiten.SetWindowSize(800, 600)

	game := &Game{machine: machine}
	fmt.Fprintf(os.Stderr, "Starting NuxVM: %dx%d\n", machine.System.ScreenWidth(), machine.System.ScreenHeight())
	if err := ebiten.RunGame(game); err != nil {
		fmt.Fprintf(os.Stderr, "Error: %v\n", err)
		os.Exit(1)
	}
}
