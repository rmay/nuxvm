package system_test

import (
	"os"
	"path/filepath"
	"testing"

	"github.com/rmay/nuxvm/pkg/lux"
	"github.com/rmay/nuxvm/pkg/system"
)

// repoRoot walks up from cwd to find go.mod so the .lux file path resolves
// regardless of which test directory go runs us from.
func repoRoot(t *testing.T) string {
	t.Helper()
	dir, err := os.Getwd()
	if err != nil {
		t.Fatalf("getwd: %v", err)
	}
	for {
		if _, err := os.Stat(filepath.Join(dir, "go.mod")); err == nil {
			return dir
		}
		parent := filepath.Dir(dir)
		if parent == dir {
			t.Fatalf("could not find go.mod above %s", dir)
		}
		dir = parent
	}
}

// TestOurFatherAppRendersToWindow exercises the multi-VM path used by the
// Cloister launcher: shared ServiceManager, NewMachineSharedServices, render
// target swap, boot tick. After the boot tick the prayer's framebuffer
// should have been cleared to white and overlaid with black text + divider.
func TestOurFatherAppRendersToWindow(t *testing.T) {
	root := repoRoot(t)
	if err := os.Chdir(root); err != nil {
		t.Fatalf("chdir to repo root: %v", err)
	}

	bytecode, err := lux.LoadProgram("apps/our-father.lux")
	if err != nil {
		t.Fatalf("compile our-father.lux: %v", err)
	}

	// Stand up a host-style ServiceManager via a stub shell machine. The
	// shell here doesn't run anything — it just owns the running services.
	shell := system.NewMachine([]byte{0x1C}, 16*1024*1024) // 0x1C = OpHalt
	services := shell.Services()

	// CreateWindow allocates a content framebuffer of
	// (w-2*border) x (h-chrome-2*border) pixels, so size the window so
	// the prayer's 640x756 layout fits exactly.
	const winW, winH = 640 + 2*system.WinBorderWidth, 756 + system.WinChromeHeight + 2*system.WinBorderWidth
	winID, err := services.CreateWindow("Our Father", winW, winH)
	if err != nil {
		t.Fatalf("create window: %v", err)
	}
	if winID == 0 {
		t.Fatalf("CreateWindow returned 0")
	}

	app := system.NewMachineSharedServices(bytecode, 16*1024*1024, services)

	saved := services.GetActiveWindowID()
	services.SetRenderTarget(winID)
	// Boot tick installs the screen vector, then yields in keep-alive.
	if _, err := app.Tick(); err != nil {
		services.SetRenderTarget(saved)
		t.Fatalf("app boot tick: %v", err)
	}
	// VBlank fires the installed screen vector; next Tick runs DRAW-ALL.
	if err := app.VBlank(); err != nil {
		services.SetRenderTarget(saved)
		t.Fatalf("app VBlank: %v", err)
	}
	if _, err := app.Tick(); err != nil {
		services.SetRenderTarget(saved)
		t.Fatalf("app draw tick: %v", err)
	}
	services.SetRenderTarget(saved)

	win := services.GetWindowByID(winID)
	if win == nil {
		t.Fatalf("window %d disappeared after tick", winID)
	}
	if got, want := len(win.FrameBuf), 640*756*4; got != want {
		t.Fatalf("framebuffer size: got %d, want %d", got, want)
	}

	// SCREEN::clear filled with 0xFFFFFFFF (white opaque). Then text glyphs
	// painted some pixels black. After both, expect:
	//   - some 0xFF bytes (the cleared area)
	//   - some 0x00 bytes in R/G/B channels (text + divider)
	allFF, hasZero := true, false
	for i, b := range win.FrameBuf {
		if b != 0xFF {
			allFF = false
		}
		if i%4 != 3 && b == 0x00 {
			hasZero = true
		}
	}
	if allFF {
		t.Errorf("framebuffer is uniformly 0xFF — text glyphs were not rendered")
	}
	if !hasZero {
		t.Errorf("no zero R/G/B bytes — black text/divider were not drawn")
	}

	// Spot-check the vertical divider at pixel (320, 100): RGB should all be 0.
	const x, y = 320, 100
	off := (y*640 + x) * 4
	if win.FrameBuf[off] != 0 || win.FrameBuf[off+1] != 0 || win.FrameBuf[off+2] != 0 {
		t.Errorf("divider pixel at (%d,%d) not black: rgba=%02x %02x %02x %02x",
			x, y, win.FrameBuf[off], win.FrameBuf[off+1], win.FrameBuf[off+2], win.FrameBuf[off+3])
	}
}
