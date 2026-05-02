package system_test

import (
	"os"
	"testing"

	"github.com/rmay/nuxvm/pkg/lux"
	"github.com/rmay/nuxvm/pkg/system"
)

func TestIlluminatorApp(t *testing.T) {
	root := repoRoot(t)
	if err := os.Chdir(root); err != nil {
		t.Fatalf("chdir to repo root: %v", err)
	}

	// Ensure the CFF file exists where Illuminator expects it
	if _, err := os.Stat("pkg/system/chicago.cff"); err != nil {
		t.Fatalf("pkg/system/chicago.cff missing: %v. Run cmd/png2cff first.", err)
	}

	bytecode, err := lux.LoadProgram("apps/Illuminator.lux")
	if err != nil {
		t.Fatalf("compile Illuminator.lux: %v", err)
	}

	// Stand up a host-style ServiceManager via a stub shell machine.
	shell := system.NewMachine([]byte{0x1C}, 16*1024*1024) // 0x1C = OpHalt
	services := shell.Services()

	// CreateWindow allocates a content framebuffer of
	// (w-2*border) x (h-chrome-2*border) pixels. Size the window so the
	// 640x640 glyph grid fits exactly in the content area.
	const winW, winH = 640 + 2*system.WinBorderWidth, 640 + system.WinChromeHeight + 2*system.WinBorderWidth
	winID, err := services.CreateWindow("Illuminator", winW, winH)
	if err != nil {
		t.Fatalf("create window: %v", err)
	}

	// DO NOT ENABLE TRACE HERE THE OUTPUT IS TOO LARGE
	app := system.NewMachineSharedServices(bytecode, 12*1024*1024, services)

	saved := services.GetActiveWindowID()
	services.SetRenderTarget(winID)

	// Boot tick loads the font and installs the screen vector, then yields.
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
		t.Fatalf("window %d disappeared", winID)
	}

	// Check if anything was drawn. Grid starts at (10, 10).
	// We expect some pixels to be white (0xFFFFFF).
	hasWhite := false
	for i := 0; i < len(win.FrameBuf); i += 4 {
		if win.FrameBuf[i] == 0xFF && win.FrameBuf[i+1] == 0xFF && win.FrameBuf[i+2] == 0xFF {
			hasWhite = true
			break
		}
	}

	if !hasWhite {
		t.Errorf("framebuffer is empty — no glyphs rendered")
	}

	// Spot check a specific glyph.
	// Space (cp 32) is the first glyph at (10, 10) if -first 32 was used,
	// but WIDTH_TABLE starts at 0.
	// If WIDTH_TABLE[32] has width > 0, it should be at (10, 10).
	// In chicago.png, '!' is cp 33. It should be at x=10 + 38 = 48, y=10.
	// Let's check around (48, 10) for any pixels.
}
