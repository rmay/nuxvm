package main

import (
	"fmt"
	"image/color"
	"os"

	"github.com/hajimehoshi/ebiten/v2"
	"github.com/hajimehoshi/ebiten/v2/ebitenutil"
	"github.com/rmay/nuxvm/pkg/system"
)

// appEntry is one row in the Applications launcher. Either luxPath OR launch
// is set: luxPath entries are loaded as a fresh VM via launchLuxApp; launch
// entries dispatch into a host-side Go callback (e.g. the embedded Shell).
type appEntry struct {
	name    string
	luxPath string
	launch  func(g *Game)
}

var appCatalog = []appEntry{
	{name: "Shell", launch: func(g *Game) { g.openShellApp() }},
	{name: "Our Father", luxPath: "apps/our-father.lux"},
}

// openLauncher creates (or focuses) the Applications window. The window fills
// the desktop area below the menubar so it reads as the OS Finder.
func (g *Game) openLauncher() {
	if g.launcherWinID != 0 {
		if g.machine.Services().GetWindowByID(g.launcherWinID) != nil {
			g.machine.Services().FocusWindow(g.launcherWinID)
			return
		}
		g.launcherWinID = 0
	}

	sw := int32(g.machine.System.ScreenWidth())
	sh := int32(g.machine.System.ScreenHeight())
	contentH := sh - int32(TopBarHeight)
	if contentH < 100 {
		contentH = 100
	}

	id, _ := g.machine.Services().CreateWindow("Applications", sw, contentH)
	g.launcherWinID = id
	g.machine.Services().FocusWindow(id)
	g.machine.Services().LayoutSingle(id, 0, 0, sw, contentH)
}

// closeLauncher closes the launcher window and clears the pane layout so the
// Update auto-recovery will lay out the new active window full-desktop.
func (g *Game) closeLauncher() {
	if g.launcherWinID == 0 {
		return
	}
	g.machine.Services().CloseWindow(g.launcherWinID)
	g.machine.Services().ClearPanes()
	g.launcherWinID = 0
}

// drawLauncherContent paints the launcher's content image with a row of
// app entries. Each entry is a clickable rect — the hit rects come from
// launcherEntryRect so renderer and input handler can't drift.
func (g *Game) drawLauncherContent(win *system.WindowRecord, img *ebiten.Image) {
	if img == nil {
		return
	}
	img.Fill(color.RGBA{240, 240, 240, 255})
	for i := range appCatalog {
		r := launcherEntryRect(i)
		ebitenutil.DrawRect(img, float64(r.x), float64(r.y), float64(r.w), float64(r.h), color.RGBA{200, 200, 200, 255})
		strokeRect(img, float32(r.x), float32(r.y), float32(r.w), float32(r.h), color.RGBA{0, 0, 0, 255})
		drawSystemFontText(img, appCatalog[i].name, r.x+10, r.y+10, 2, color.RGBA{0, 0, 0, 255})
	}
}

// launcherEntryRect returns the framebuffer-local hit rect of app entry i.
// Currently a vertical column of 200x60 tiles starting at (20, 20).
func launcherEntryRect(i int) rect {
	return rect{x: 20, y: 20 + i*80, w: 200, h: 60}
}

// handleLauncherClick is called when the user clicks inside the launcher
// window's content area. localX/localY are window-local. Selecting an app
// closes the launcher and dispatches to the entry — either its host-side
// callback or the Lux loader.
func (g *Game) handleLauncherClick(localX, localY int32) {
	for i, entry := range appCatalog {
		r := launcherEntryRect(i)
		if int(localX) >= r.x && int(localX) < r.x+r.w &&
			int(localY) >= r.y && int(localY) < r.y+r.h {
			g.closeLauncher()
			if entry.luxPath != "" {
				if err := g.launchLuxApp(entry.name, entry.luxPath); err != nil {
					fmt.Fprintf(os.Stderr, "launch %s: %v\n", entry.name, err)
				}
			} else if entry.launch != nil {
				entry.launch(g)
			}
			return
		}
	}
}
