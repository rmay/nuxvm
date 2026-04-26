package main

import (
	"fmt"
	"image/color"
	"os"

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
	contentH := sh - int32(topBarHeight) - int32(WinChromeHeight)
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

// drawLauncherContent paints the launcher's window framebuffer with a row of
// app entries. Each entry is a clickable rect — the hit rects come from
// launcherEntryRect so renderer and input handler can't drift.
func (g *Game) drawLauncherContent(win *system.Window) {
	clearWindow(win, color.RGBA{240, 240, 240, 255})
	for i := range appCatalog {
		r := launcherEntryRect(i)
		fillRectInWindow(win, r, color.RGBA{200, 200, 200, 255})
		strokeRectInWindow(win, r, color.RGBA{0, 0, 0, 255})
		drawChicagoTextInWindow(win, appCatalog[i].name, r.x+10, r.y+10, 2, color.RGBA{0, 0, 0, 255})
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

// ---- Window-framebuffer drawing helpers (RGBA, LSB-first to match Chicago font) ----

func setPixelInWindow(win *system.Window, x, y int, c color.RGBA) {
	if x < 0 || y < 0 || x >= int(win.Width) || y >= int(win.Height) {
		return
	}
	off := (y*int(win.Width) + x) * 4
	win.FrameBuf[off+0] = c.R
	win.FrameBuf[off+1] = c.G
	win.FrameBuf[off+2] = c.B
	win.FrameBuf[off+3] = c.A
}

func clearWindow(win *system.Window, c color.RGBA) {
	for y := 0; y < int(win.Height); y++ {
		for x := 0; x < int(win.Width); x++ {
			setPixelInWindow(win, x, y, c)
		}
	}
}

func fillRectInWindow(win *system.Window, r rect, c color.RGBA) {
	for y := r.y; y < r.y+r.h; y++ {
		for x := r.x; x < r.x+r.w; x++ {
			setPixelInWindow(win, x, y, c)
		}
	}
}

func strokeRectInWindow(win *system.Window, r rect, c color.RGBA) {
	for x := r.x; x < r.x+r.w; x++ {
		setPixelInWindow(win, x, r.y, c)
		setPixelInWindow(win, x, r.y+r.h-1, c)
	}
	for y := r.y; y < r.y+r.h; y++ {
		setPixelInWindow(win, r.x, y, c)
		setPixelInWindow(win, r.x+r.w-1, y, c)
	}
}

// drawChicagoTextInWindow renders ASCII text into a window framebuffer using
// the same 8x8 Chicago glyphs the host uses for menus.
func drawChicagoTextInWindow(win *system.Window, s string, x, y, scale int, c color.RGBA) {
	if scale <= 0 {
		scale = 1
	}
	charX := x
	for _, r := range s {
		if r >= 0x20 && r < 128 {
			glyph := system.Font[r]
			for row := 0; row < 8; row++ {
				bits := glyph[row]
				for col := 0; col < 8; col++ {
					if bits&(1<<col) != 0 {
						for dy := 0; dy < scale; dy++ {
							for dx := 0; dx < scale; dx++ {
								setPixelInWindow(win, charX+col*scale+dx, y+row*scale+dy, c)
							}
						}
					}
				}
			}
			charX += 8 * scale
		}
	}
}
