package system_test

import (
	"encoding/binary"
	"image/color"
	"os"
	"path/filepath"
	"testing"

	"github.com/rmay/nuxvm/pkg/lux"
	"github.com/rmay/nuxvm/pkg/system"
)

// TestDFPaneSplit verifies that DF can split the screen into panes
// and that clear-screen correctly fills the active pane.
func TestDFPaneSplit(t *testing.T) {
	origDir, _ := os.Getwd()
	t.Cleanup(func() { os.Chdir(origDir) })

	root := repoRoot(t)
	if err := os.Chdir(root); err != nil {
		t.Fatalf("chdir: %v", err)
	}

	src := `INCLUDE "lib/delineatiofenestra.lux"
INCLUDE "lib/system.lux"
IMPORT DELINEATIOFENESTRA AS DF
IMPORT SCREEN

@DRAW-ALL
    init-locals
    0xFF0000 0 0 SCREEN::pixel!
    0 2 DF::use-pane-h  0xFF0000 DF::clear-screen
    1 2 DF::use-pane-h  0x0000FF DF::clear-screen
;

@keep-alive [ 1 ] [ YIELD ] |: ;

[ DRAW-ALL ] SCREEN::vector!
init-locals
DRAW-ALL
keep-alive
HALT
`
	tmp := filepath.Join(t.TempDir(), "df_pane_split.lux")
	if err := os.WriteFile(tmp, []byte(src), 0o644); err != nil {
		t.Fatalf("write tmp lux: %v", err)
	}

	bytecode, err := lux.Compile(src)
	if err != nil {
		t.Fatalf("compile: %v", err)
	}

	// Setup shared services
	services := system.NewServiceManager()
	services.StartAllServices()
	const winW, winH = 200, 200
	winID, err := services.CreateWindow("PaneSplit", winW, winH)
	if err != nil {
		t.Fatalf("create window: %v", err)
	}
	services.LayoutSingle(winID, 0, 0, 200, 200)

	// Create app VM
	app := system.NewMachineSharedServices(bytecode, 16*1024*1024, services)

	saved := services.GetActiveWindowID()
	services.SetRenderTarget(winID)
	defer services.SetRenderTarget(saved)

	if _, err := app.Tick(); err != nil {
		t.Fatalf("boot tick: %v", err)
	}
	if err := app.VBlank(); err != nil {
		t.Fatalf("vblank: %v", err)
	}

	win := services.GetWindowByID(winID)
	if win == nil {
		t.Fatalf("window vanished")
	}
	const W, H = 200, 200
	if got, want := len(win.FrameBuf), W*H*4; got != want {
		t.Fatalf("framebuffer size %d, want %d", got, want)
	}

	red := color.RGBA{0xFF, 0x00, 0x00, 0xFF}
	blue := color.RGBA{0x00, 0x00, 0xFF, 0xFF}

	checkPx := func(label string, x, y int, want color.RGBA) {
		t.Helper()
		off := (y*W + x) * 4
		gotU := binary.BigEndian.Uint32(win.FrameBuf[off : off+4])
		got := color.RGBA{byte(gotU >> 24), byte(gotU >> 16), byte(gotU >> 8), byte(gotU)}
		if got != want {
			t.Errorf("%s pixel (%d,%d): got %v want %v", label, x, y, got, want)
		}
	}

	// Left half should be Red (0xFF0000)
	checkPx("left-pane top-left", 0, 0, red)
	checkPx("left-pane bottom-right", 99, 199, red)

	// Right half should be Blue (0x0000FF)
	checkPx("right-pane top-left", 100, 0, blue)
	checkPx("right-pane bottom-right", 199, 199, blue)
}
