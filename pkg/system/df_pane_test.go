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

// TestDFPaneSplit verifies the DelineatioFenestra pane model:
// declaring two horizontal panes and filling each with a different
// color leaves the framebuffer's left half red and right half blue.
func TestDFPaneSplit(t *testing.T) {
	root := repoRoot(t)
	if err := os.Chdir(root); err != nil {
		t.Fatalf("chdir: %v", err)
	}

	src := `INCLUDE "lib/delineatiofenestra.lux"
INCLUDE "lib/system.lux"
IMPORT DELINEATIOFENESTRA AS DF
IMPORT SCREEN

@DRAW-ALL
    2 0 DF::use-pane-h  -65536 DF::clear-screen
    2 1 DF::use-pane-h  -16776961 DF::clear-screen
;

@keep-alive YIELD keep-alive ;

[ DRAW-ALL ] SCREEN::vector!
keep-alive
HALT
`
	tmp := filepath.Join(t.TempDir(), "df_pane_split.lux")
	if err := os.WriteFile(tmp, []byte(src), 0o644); err != nil {
		t.Fatalf("write tmp lux: %v", err)
	}
	bytecode, err := lux.LoadProgram(tmp)
	if err != nil {
		t.Fatalf("compile: %v", err)
	}

	shell := system.NewMachine([]byte{0x1C}, 16*1024*1024)
	services := shell.Services()

	const winW, winH = 200 + 2*system.WinBorderWidth, 200 + system.WinChromeHeight + 2*system.WinBorderWidth
	winID, err := services.CreateWindow("PaneSplit", winW, winH)
	if err != nil {
		t.Fatalf("create window: %v", err)
	}

	app := system.NewMachineSharedServices(bytecode, 16*1024*1024, services)

	saved := services.GetActiveWindowID()
	services.SetRenderTarget(winID)
	if _, err := app.Tick(); err != nil {
		services.SetRenderTarget(saved)
		t.Fatalf("boot tick: %v", err)
	}
	if err := app.VBlank(); err != nil {
		services.SetRenderTarget(saved)
		t.Fatalf("vblank: %v", err)
	}
	if _, err := app.Tick(); err != nil {
		services.SetRenderTarget(saved)
		t.Fatalf("draw tick: %v", err)
	}
	services.SetRenderTarget(saved)

	win := services.GetWindowByID(winID)
	if win == nil {
		t.Fatalf("window vanished")
	}
	const W, H = 200, 200
	if got, want := len(win.FrameBuf), W*H*4; got != want {
		t.Fatalf("framebuffer size %d, want %d", got, want)
	}

	// Red is encoded as int32 -65536 = 0xFFFF0000 → bytes [FF FF 00 00]
	// (BE put32 of the int32). Blue is -16776961 = 0xFF0000FF →
	// bytes [FF 00 00 FF].
	red := color.RGBA{0xFF, 0xFF, 0x00, 0x00}
	blue := color.RGBA{0xFF, 0x00, 0x00, 0xFF}
	checkPx := func(label string, x, y int, want color.RGBA) {
		t.Helper()
		off := (y*W + x) * 4
		gotU := binary.BigEndian.Uint32(win.FrameBuf[off : off+4])
		want32 := uint32(want.R)<<24 | uint32(want.G)<<16 | uint32(want.B)<<8 | uint32(want.A)
		if gotU != want32 {
			t.Errorf("%s pixel (%d,%d): got 0x%08X want 0x%08X", label, x, y, gotU, want32)
		}
	}
	checkPx("left-pane top-left", 0, 0, red)
	checkPx("left-pane bottom-right", 99, 199, red)
	checkPx("right-pane top-left", 100, 0, blue)
	checkPx("right-pane bottom-right", 199, 199, blue)
}

// TestFramebufferOOBSilent: a write past the active window's
// framebuffer must NOT fault — the bus silently drops it. Without
// this, an aborted CPU step leaves a TriggerVector-pushed PC on the
// return stack and the VM overflows after ~1024 frames.
func TestFramebufferOOBSilent(t *testing.T) {
	root := repoRoot(t)
	if err := os.Chdir(root); err != nil {
		t.Fatalf("chdir: %v", err)
	}

	// Push (color=1, addr=0x4000 + 1_000_000_000) and STOREI; expect
	// the VM to keep running. 0x1B = OpHalt is at the end as a sentinel.
	// Bytecode: push 1, push 0x4000+10^9, storei, halt.
	// (We cheat by writing raw bytecode rather than compiling Lux.)
	be := func(v uint32) []byte {
		return []byte{byte(v >> 24), byte(v >> 16), byte(v >> 8), byte(v)}
	}
	prog := []byte{}
	prog = append(prog, 0x00) // OpPush
	prog = append(prog, be(1)...)
	prog = append(prog, 0x00) // OpPush
	// 0x100000 sits in the fb-routed range [0x4000, 0x504000) but past
	// the 100x100 window's actual framebuffer (~30KB). The bus must
	// silently no-op rather than fault.
	prog = append(prog, be(0x100000)...)
	prog = append(prog, 0x1F) // OpStoreI
	prog = append(prog, 0x1C) // OpHalt

	shell := system.NewMachine([]byte{0x1C}, 4*1024*1024)
	services := shell.Services()
	winID, err := services.CreateWindow("OOB", 100, 100)
	if err != nil {
		t.Fatalf("create window: %v", err)
	}
	app := system.NewMachineSharedServices(prog, 4*1024*1024, services)

	saved := services.GetActiveWindowID()
	services.SetRenderTarget(winID)
	defer services.SetRenderTarget(saved)
	if _, err := app.Tick(); err != nil {
		t.Fatalf("oob STOREI faulted (should be silent): %v", err)
	}
}

