package main

import (
	"fmt"
	"os"

	"github.com/rmay/nuxvm/pkg/lux"
	"github.com/rmay/nuxvm/pkg/system"
)

// LuxApp is a host-side handle for a launchable .lux program running in its
// own VM. The machine shares the ServiceManager (windows, layout, input,
// sandbox) with the shell VM but has its own CPU, memory, vectors, and
// text-cursor state. Each frame the host swaps the active-window render
// target to app.winID so the app's screen/text writes land in its own
// framebuffer instead of clobbering whatever window the user has focused.
type LuxApp struct {
	name    string
	luxPath string
	machine *system.Machine
	winID   system.WindowID
}

// launchLuxApp loads a .lux program, creates a window for it, instantiates
// a fresh VM with shared Services, runs the program's boot tick (so it can
// paint initial content), and registers the app with Game.
func (g *Game) launchLuxApp(name, luxPath string) error {
	bytecode, err := lux.LoadProgram(luxPath)
	if err != nil {
		return fmt.Errorf("load %s: %w", luxPath, err)
	}

	sw := g.machine.System.ScreenWidth()
	sh := g.machine.System.ScreenHeight()
	contentH := sh - int32(TopBarHeight)
	if contentH < 100 {
		contentH = 100
	}

	services := g.machine.Services()
	winID, err := services.CreateWindow(name, sw, contentH)
	if err != nil {
		return fmt.Errorf("create window: %w", err)
	}

	appMachine := system.NewMachineSharedServices(bytecode, g.memSize, services)

	// Boot tick under the app's own render target so any startup paints land
	// in its window. Restore the previous target afterwards so subsequent
	// shell ticks don't accidentally inherit it.
	saved := services.GetActiveWindowID()
	services.SetRenderTarget(winID)
	if _, tickErr := appMachine.Tick(); tickErr != nil {
		services.SetRenderTarget(saved)
		_ = services.CloseWindow(winID)
		return fmt.Errorf("boot tick: %w", tickErr)
	}
	services.SetRenderTarget(saved)

	g.apps = append(g.apps, &LuxApp{
		name:    name,
		luxPath: luxPath,
		machine: appMachine,
		winID:   winID,
	})

	services.FocusWindow(winID)
	services.LayoutSingle(winID, 0, 0, sw, contentH)
	return nil
}

// tickLuxApps runs one frame's worth of work for each registered Lux app.
// Render target is swapped to each app's window before VBlank/Tick so its
// framebuffer writes hit the right place, and restored at the end.
func (g *Game) tickLuxApps() {
	if len(g.apps) == 0 {
		return
	}
	services := g.machine.Services()
	saved := services.GetActiveWindowID()
	for _, app := range g.apps {
		if app.machine == nil {
			continue
		}
		services.SetRenderTarget(app.winID)
		_ = app.machine.VBlank()
		running, err := app.machine.Tick()
		if err != nil {
			fmt.Fprintf(os.Stderr, "lux app %s tick: %v\n", app.name, err)
			continue
		}
		if !running {
			app.machine.CPU.ClearYield()
		}
		g.wm.MarkDirty(app.winID)
	}
	services.SetRenderTarget(saved)
}

// closeLuxApp tears down the app whose window matches winID. Returns true if
// a matching app was found and removed.
func (g *Game) closeLuxApp(winID system.WindowID) bool {
	for i, app := range g.apps {
		if app.winID != winID {
			continue
		}
		_ = g.machine.Services().CloseWindow(winID)
		g.machine.Services().ClearPanes()
		g.apps = append(g.apps[:i], g.apps[i+1:]...)
		return true
	}
	return false
}
