package main

import (
	"flag"
	"fmt"
	"image/color"
	"os"
	"path/filepath"
	"time"

	"github.com/hajimehoshi/ebiten/v2"
	"github.com/hajimehoshi/ebiten/v2/ebitenutil"
	"github.com/hajimehoshi/ebiten/v2/inpututil"
	"github.com/hajimehoshi/ebiten/v2/text"
	"github.com/hajimehoshi/ebiten/v2/vector"
	"github.com/rmay/nuxvm/pkg/lux"
	"github.com/rmay/nuxvm/pkg/system"
	"golang.org/x/image/font/basicfont"
)

var screenScale = 10

// defaultShellPath is the on-disk Lux program cloister loads when invoked
// without a positional argument. The shell stays alive forever, owns system
// vectors, and acts as the substrate that user apps run on top of (System 6
// Finder analog). It's relative to cwd so editing lib/shell.lux in the repo
// and re-running cloister picks up the change without a rebuild.
const defaultShellPath = "lib/shell.lux"
const topBarHeight = 24

type ShellMode int

const (
	ShellNormal ShellMode = iota
	ShellAppleMenu
	ShellSettings
	ShellWindowsList
	ShellWindowRowMenu
	ShellQuitConfirm
	ShellCommandPalette
)

// Apple-menu geometry: kept as constants so handler hit-tests and the
// drawing routine can't drift apart silently.
const (
	appleMenuItemCount = 4 // Applications, Settings, Current Windows, Quit
	appleMenuWidth     = 140
)

type Game struct {
	machine     *system.Machine
	wm          *WindowManager
	showDebug   bool
	bootTimer   int
	mouseX      int
	mouseY      int
	dragging    bool
	dragWinID   system.WindowID
	dragOffX    int
	dragOffY    int
	wasLeftDown bool
	clearColor   color.RGBA
	textScale    int    // 1-4
	bgPattern    int    // 0=solid, 1=50% gray, 2=dots, 3=stripes
	clockFormat  int    // 0=24h, 1=12h, 2=12h-AMPM
	settingsPath string // absolute path of cloister-settings.lux (cwd at launch)

	launcherWinID system.WindowID // 0 if launcher is not open
	shellApp      *ShellApp       // singleton; nil until first launch

	apps []*LuxApp // .lux programs launched from the Applications launcher; each owns its own VM and window

	// Restart-OS support: when set, top of next Update rebuilds the machine
	// from shellPath and resets WM/launcher/shell state. Cloister keeps running.
	restartRequested bool
	shellPath        string
	memSize          uint32

	// Shell/menu state
	shellMode        ShellMode
	appleIdx         int             // 0=Applications, 1=Settings, 2=Windows, 3=Quit
	settingsIdx      int             // which setting row (0=scale, 1=debug, 2=color, 3=pattern, 4=clock)
	windowsIdx       int             // which window in list
	windowRowMenu    int             // 0=Focus, 1=Close
	windowRowMenuWin system.WindowID // which window the row menu is over

	paletteIdx int // selected command in the command palette
}

// luxKeyCodeFromEbiten maps ebiten key codes to Lux key codes (ASCII where applicable)
func luxKeyCodeFromEbiten(k ebiten.Key) int32 {
	switch k {
	case ebiten.KeyA:
		return 97 // 'a'
	case ebiten.KeyB:
		return 98
	case ebiten.KeyC:
		return 99
	case ebiten.KeyD:
		return 100
	case ebiten.KeyE:
		return 101
	case ebiten.KeyF:
		return 102
	case ebiten.KeyG:
		return 103
	case ebiten.KeyH:
		return 104
	case ebiten.KeyI:
		return 105
	case ebiten.KeyJ:
		return 106
	case ebiten.KeyK:
		return 107
	case ebiten.KeyL:
		return 108
	case ebiten.KeyM:
		return 109
	case ebiten.KeyN:
		return 110
	case ebiten.KeyO:
		return 111
	case ebiten.KeyP:
		return 112
	case ebiten.KeyQ:
		return 113
	case ebiten.KeyR:
		return 114
	case ebiten.KeyS:
		return 115
	case ebiten.KeyT:
		return 116
	case ebiten.KeyU:
		return 117
	case ebiten.KeyV:
		return 118
	case ebiten.KeyW:
		return 119
	case ebiten.KeyX:
		return 120
	case ebiten.KeyY:
		return 121
	case ebiten.KeyZ:
		return 122
	case ebiten.Key0:
		return 48
	case ebiten.Key1:
		return 49
	case ebiten.Key2:
		return 50
	case ebiten.Key3:
		return 51
	case ebiten.Key4:
		return 52
	case ebiten.Key5:
		return 53
	case ebiten.Key6:
		return 54
	case ebiten.Key7:
		return 55
	case ebiten.Key8:
		return 56
	case ebiten.Key9:
		return 57
	case ebiten.KeySpace:
		return 32
	case ebiten.KeyEnter:
		return 13
	case ebiten.KeyBackspace:
		return 8
	case ebiten.KeyTab:
		return 9
	case ebiten.KeyEscape:
		return 27
	default:
		return 0 // Unmapped
	}
}

var font = map[rune][5]byte{
	'C': {0x7, 0x4, 0x4, 0x4, 0x7},
	'L': {0x4, 0x4, 0x4, 0x4, 0x7},
	'O': {0x7, 0x5, 0x5, 0x5, 0x7},
	'I': {0x7, 0x2, 0x2, 0x2, 0x7},
	'S': {0x7, 0x4, 0x7, 0x1, 0x7},
	'T': {0x7, 0x2, 0x2, 0x2, 0x2},
	'E': {0x7, 0x4, 0x7, 0x4, 0x7},
	'R': {0x7, 0x5, 0x7, 0x6, 0x5},
}

func drawText(screen *ebiten.Image, text string, x, y int, clr color.Color) {
	for i, r := range text {
		if glyph, ok := font[r]; ok {
			for row := 0; row < 5; row++ {
				for col := 0; col < 3; col++ {
					if glyph[row]&(1<<(2-col)) != 0 {
						screen.Set(x+i*4+col, y+row, clr)
					}
				}
			}
		}
	}
}

// menubarHitTest returns true if (x,y) is over the % glyph in the top bar.
// The glyph is drawn at (4,4) with scale 1, so the hit rect is roughly 0..16, 0..topBarHeight.
func menubarHitTest(x, y int) bool {
	return x >= 0 && x < 18 && y >= 0 && y < topBarHeight
}

// drawShellText renders s with basicfont.Face7x13. (x, y) is the top-left of
// the glyph cell; basicfont's baseline sits 11px below the top.
const shellFontW = 7
const shellFontH = 13
const shellFontAscent = 11

func drawShellText(screen *ebiten.Image, s string, x, y int, clr color.Color) {
	text.Draw(screen, s, basicfont.Face7x13, x, y+shellFontAscent, clr)
}

// strokeRect draws a 1px outline of a rectangle. ebitenutil.DrawRect fills,
// which is the wrong tool for a border.
func strokeRect(screen *ebiten.Image, x, y, w, h float32, clr color.Color) {
	vector.StrokeRect(screen, x, y, w, h, 1, clr, false)
}

func drawChicagoText(screen *ebiten.Image, s string, x, y, scale int, clr color.Color) {
	if scale <= 0 {
		scale = 1
	}
	charX := x
	for _, r := range s {
		if r < 128 && r >= 0x20 { // Printable ASCII range
			glyph := system.Font[r]
			// Each row of the glyph is a byte where each bit represents a pixel
			// Bits are LSB-first: bit 0 is leftmost pixel, bit 7 is rightmost pixel
			for row := 0; row < 8; row++ {
				bits := glyph[row]
				for col := 0; col < 8; col++ {
					// Check if this pixel is lit (LSB-first)
					if (bits & (1 << col)) != 0 {
						// Draw a scale×scale block for this pixel
						for dy := 0; dy < scale; dy++ {
							for dx := 0; dx < scale; dx++ {
								px := charX + col*scale + dx
								py := y + row*scale + dy
								if px >= 0 && py >= 0 { // Basic bounds check
									screen.Set(px, py, clr)
								}
							}
						}
					}
				}
			}
			// Advance to next character position
			charX += 8 * scale
		}
	}
}

// clockFormatString maps a Game.clockFormat value to a Go time-format string.
// Index 0 = 24h, 1 = 12h, 2 = 12h with AM/PM.
func clockFormatString(idx int) string {
	switch idx {
	case 1:
		return "3:04"
	case 2:
		return "3:04 PM"
	default:
		return "15:04"
	}
}

func clockFormatLabel(idx int) string {
	switch idx {
	case 1:
		return "12h"
	case 2:
		return "12h AM/PM"
	default:
		return "24h"
	}
}

// 8x8 background pattern tiles. LSB-first per row, matching the existing
// Chicago font convention (main.go drawChicagoText).
var bgPatternTiles = [4][8]byte{
	{}, // 0: solid (unused; pattern 0 short-circuits)
	{0xAA, 0x55, 0xAA, 0x55, 0xAA, 0x55, 0xAA, 0x55}, // 1: 50% gray dither
	{0x00, 0x10, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00}, // 2: sparse dots
	{0xFF, 0x00, 0x00, 0x00, 0xFF, 0x00, 0x00, 0x00}, // 3: horizontal stripes
}

func bgPatternLabel(idx int) string {
	switch idx {
	case 1:
		return "Gray"
	case 2:
		return "Dots"
	case 3:
		return "Stripes"
	default:
		return "Solid"
	}
}

// drawDesktopBackground paints the desktop area below the menubar. Pattern 0 is
// just a solid fill; patterns 1-3 tile an 8x8 1-bit overlay (black) over the
// solid color.
func (g *Game) drawDesktopBackground(screen *ebiten.Image, sw, sh int) {
	screen.Fill(g.clearColor)
	if g.bgPattern <= 0 || g.bgPattern >= len(bgPatternTiles) {
		return
	}
	tile := bgPatternTiles[g.bgPattern]
	black := color.RGBA{0, 0, 0, 255}
	for y := topBarHeight; y < sh; y++ {
		row := tile[(y-topBarHeight)&7]
		if row == 0 {
			continue
		}
		for x := 0; x < sw; x++ {
			if row&(1<<(x&7)) != 0 {
				screen.Set(x, y, black)
			}
		}
	}
}

func drawMouse(screen *ebiten.Image, x, y int) {
	white := color.RGBA{255, 255, 255, 255}
	screen.Set(x, y-1, white)
	screen.Set(x, y+1, white)
	screen.Set(x-1, y, white)
	screen.Set(x+1, y, white)
	screen.Set(x, y, color.RGBA{0, 0, 0, 255})
}

func (g *Game) Update() error {
	// Handle pending RESTART-OS: rebuild the VM from the shell program and
	// reset every host-side cache that was pinned to the old machine.
	if g.restartRequested {
		g.restartRequested = false
		if err := g.restartMachine(); err != nil {
			return err
		}
	}

	// Handle boot timer
	if g.bootTimer > 0 {
		g.bootTimer--
		return nil
	}

	leftDown := ebiten.IsMouseButtonPressed(ebiten.MouseButtonLeft)
	justPressed := leftDown && !g.wasLeftDown
	justReleased := !leftDown && g.wasLeftDown
	g.wasLeftDown = leftDown

	// Shell/menu input takes priority
	if g.shellMode != ShellNormal {
		g.handleShellInput(justPressed)
		// Don't process window input while shell is active
		return nil
	}

	// Clear drag state if user clicked somewhere
	if justPressed && g.dragging {
		g.dragging = false
		g.dragWinID = 0
	}

	// Drag release
	if justReleased && g.dragging {
		g.dragging = false
		g.dragWinID = 0
	}

	// Clicking the % glyph opens the apple menu, even when no shell is active.
	// Without this, the click falls through to wm.HitTest and grabs the
	// underlying window's title bar (since the window frame extends under the
	// menubar).
	mxPre, myPre := ebiten.CursorPosition()
	if justPressed && menubarHitTest(mxPre, myPre) {
		g.shellMode = ShellAppleMenu
		g.appleIdx = 0
		return nil
	}

	// Toggle debug overlay
	if inpututil.IsKeyJustPressed(ebiten.KeyF1) {
		g.showDebug = !g.showDebug
	}

	// Ctrl+P opens the command palette against the active window. Held in the
	// shell mode state machine so it intercepts subsequent input.
	if inpututil.IsKeyJustPressed(ebiten.KeyP) &&
		(ebiten.IsKeyPressed(ebiten.KeyControl) || ebiten.IsKeyPressed(ebiten.KeyControlLeft) || ebiten.IsKeyPressed(ebiten.KeyControlRight)) {
		g.openCommandPalette()
		return nil
	}

	// Cycle through windows
	if inpututil.IsKeyJustPressed(ebiten.KeyF2) {
		g.cycleFocus(+1)
	}

	// Queue keyboard input — when a Shell window is active, the host eats every
	// key and feeds it into the embedded REPL instead of the VM. Otherwise
	// keys flow into the main VM via the service queue as usual.
	if g.shellMode == ShellNormal {
		shellActive := g.shellApp != nil &&
			g.machine.Services().GetActiveWindowID() == g.shellApp.winID &&
			g.machine.Services().GetWindowByID(g.shellApp.winID) != nil
		if shellActive {
			for _, ch := range ebiten.AppendInputChars(nil) {
				g.shellApp.handleChar(ch)
			}
			if inpututil.IsKeyJustPressed(ebiten.KeyEnter) {
				g.shellApp.handleEnter(g)
			}
			if inpututil.IsKeyJustPressed(ebiten.KeyBackspace) {
				g.shellApp.handleBackspace()
			}
			g.wm.MarkDirty(g.shellApp.winID)
		} else {
			for _, k := range inpututil.AppendJustPressedKeys(nil) {
				keyCode := luxKeyCodeFromEbiten(k)
				if keyCode > 0 {
					g.machine.Services().QueueKeyDown(keyCode)
				}
			}
			for _, k := range inpututil.AppendJustReleasedKeys(nil) {
				keyCode := luxKeyCodeFromEbiten(k)
				if keyCode > 0 {
					g.machine.Services().QueueKeyUp(keyCode)
				}
			}
		}
	}

	// Mouse input with hit-testing and window routing
	mx, my := ebiten.CursorPosition()
	g.mouseX, g.mouseY = mx, my

	// Drag move
	if g.dragging && leftDown {
		newX := int32(mx - g.dragOffX)
		newY := int32(my - topBarHeight - g.dragOffY)
		if newY < 0 {
			newY = 0
		}
		g.machine.Services().DirectMoveWindow(g.dragWinID, newX, newY)
	}

	// Hit test on new click or for mouse move
	if !g.dragging {
		windows := g.machine.System.Services.ListWindowsSorted()
		hit := g.wm.HitTest(mx, my, topBarHeight, windows)

		if justPressed {
			switch hit.Zone {
			case HitZoneTitleBar:
				g.focusAndShow(hit.WinID)
				if win := g.machine.System.Services.GetWindowByID(hit.WinID); win != nil {
					g.dragging = true
					g.dragWinID = hit.WinID
					g.dragOffX = mx - int(win.X)
					g.dragOffY = my - topBarHeight - int(win.Y)
				}
			case HitZoneCloseButton:
				g.closeWindowByID(hit.WinID)
			case HitZonePrevButton:
				g.cycleFocus(-1)
			case HitZoneNextButton:
				g.cycleFocus(+1)
			case HitZoneScrollUp:
				g.adjustScroll(hit.WinID, -WinScrollLineStep)
			case HitZoneScrollDown:
				g.adjustScroll(hit.WinID, +WinScrollLineStep)
			case HitZoneScrollTrack, HitZoneGrowBox:
				// no-op in v1 — track click could page later, grow box could resize
			case HitZoneContent:
				g.focusAndShow(hit.WinID)
				if hit.WinID == g.launcherWinID {
					g.handleLauncherClick(int32(hit.LocalX), int32(hit.LocalY))
				} else if g.shellApp != nil && hit.WinID == g.shellApp.winID {
					// Shell windows are host-rendered; don't forward clicks
					// into the VM input queue.
				} else {
					g.machine.Services().QueueMouseButton(int32(hit.LocalX), int32(hit.LocalY), 1, true)
				}
			}
		} else if hit.Zone == HitZoneContent {
			// Mouse move within content area of hit window
			activeID := g.machine.Services().GetActiveWindowID()
			if hit.WinID == activeID {
				g.machine.Services().QueueMouseMove(int32(hit.LocalX), int32(hit.LocalY))
			}
		}
	}

	// Right/middle button for active window content
	var mBtn uint32
	if ebiten.IsMouseButtonPressed(ebiten.MouseButtonRight) {
		mBtn |= 2
	}
	if ebiten.IsMouseButtonPressed(ebiten.MouseButtonMiddle) {
		mBtn |= 4
	}
	if mBtn != 0 {
		windows := g.machine.System.Services.ListWindowsSorted()
		hit := g.wm.HitTest(mx, my, topBarHeight, windows)
		if hit.Zone == HitZoneContent {
			g.machine.Services().QueueMouseButton(int32(hit.LocalX), int32(hit.LocalY), mBtn, true)
		}
	}

	// Drain all pending input events and dispatch to VM
	g.machine.DrainInputEvents()

	// Trigger V-Blank at the start of every frame
	if err := g.machine.VBlank(); err != nil {
		return err
	}

	// Tick the machine (runs until YIELD or HALT)
	running, err := g.machine.Tick()
	if err != nil {
		return err
	}
	// If VM halted, keep host running.
	if !running {
		g.machine.CPU.ClearYield()
	}

	// Mark active window dirty so next Draw() uploads the framebuffer
	g.wm.MarkDirty(g.machine.Services().GetActiveWindowID())

	// Tick every spawned Lux app under its own render target. Each app's
	// machine writes to its own window's framebuffer; the helper handles
	// the activeWinID save/restore.
	g.tickLuxApps()

	// Ensure panes are initialized: if panes is empty but we have windows, set up a single pane for the active window
	panes := g.machine.Services().ListPanes()
	if len(panes) == 0 {
		windows := g.machine.Services().ListWindowsSorted()
		if len(windows) > 0 {
			activeID := g.machine.Services().GetActiveWindowID()
			sw := int(g.machine.System.ScreenWidth())
			sh := int(g.machine.System.ScreenHeight())
			contentH := int32(sh - topBarHeight)
			g.machine.Services().LayoutSingle(activeID, 0, 0, int32(sw), contentH)
		}
	}

	return nil
}

func (g *Game) Draw(screen *ebiten.Image) {
	// Get current dimensions from system
	sw := int(g.machine.System.ScreenWidth())
	sh := int(g.machine.System.ScreenHeight())

	if g.bootTimer > 0 {
		screen.Fill(g.clearColor)
		// Center "CLOISTER" (8 chars * 4px = 32px wide)
		drawText(screen, "CLOISTER", (sw-32)/2, (sh-5)/2, color.White)
		return
	}

	// Fill background (solid color or patterned tile fill)
	g.drawDesktopBackground(screen, sw, sh)

	// Refresh framebuffers for host-rendered windows (launcher, shell)
	if g.launcherWinID != 0 {
		if win := g.machine.Services().GetWindowByID(g.launcherWinID); win != nil {
			g.drawLauncherContent(win)
			g.wm.MarkDirty(g.launcherWinID)
		}
	}
	if g.shellApp != nil {
		if win := g.machine.Services().GetWindowByID(g.shellApp.winID); win != nil {
			g.drawShellContent(win)
			g.wm.MarkDirty(g.shellApp.winID)
		}
	}

	// Get all windows for sync but only render visible panes
	windows := g.machine.System.Services.ListWindowsSorted()
	g.wm.SyncImages(windows)
	activeID := g.machine.Services().GetActiveWindowID()

	// Render only the panes (visible windows in the layout)
	panes := g.machine.Services().ListPanes()
	for _, pane := range panes {
		win := g.machine.Services().GetWindowByID(pane.WinID)
		if win == nil {
			continue
		}
		// Draw window chrome (title bar, border, close/prev/next buttons)
		g.drawWindowChrome(screen, win, win.ID == activeID)
		// Draw window content from cached image
		if img := g.wm.ContentImage(win.ID); img != nil {
			op := &ebiten.DrawImageOptions{}
			op.GeoM.Translate(float64(win.X), float64(int(win.Y)+topBarHeight+WinChromeHeight))
			screen.DrawImage(img, op)
		}
		// Scrollbars paint *after* the framebuffer so the gutter is visible
		// (otherwise the per-window content image overdraws it).
		g.drawWindowScrollbars(screen, win)
	}

	// Draw Mac-style top menubar
	ebitenutil.DrawRect(screen, 0, 0, float64(sw), float64(topBarHeight), color.White)
	ebitenutil.DrawLine(screen, 0, float64(topBarHeight), float64(sw), float64(topBarHeight), color.Black)
	textY := (topBarHeight - shellFontH) / 2
	drawShellText(screen, "%", 4, textY, color.Black)

	winName := g.machine.System.Services.ActiveWindowName()
	if winName == "" {
		winName = "Cloister"
	}
	nameX := (sw - len(winName)*shellFontW) / 2
	drawShellText(screen, winName, nameX, textY, color.Black)

	timeStr := time.Now().Format(clockFormatString(g.clockFormat))
	drawShellText(screen, timeStr, sw-len(timeStr)*shellFontW-4, textY, color.Black)

	// Draw shell/modal overlays
	switch g.shellMode {
	case ShellAppleMenu:
		g.drawAppleMenu(screen)
	case ShellSettings:
		g.drawSettingsModal(screen)
	case ShellWindowsList:
		g.drawWindowsModal(screen)
	case ShellWindowRowMenu:
		g.drawWindowsModal(screen)
		// TODO: draw window row submenu overlay
	case ShellQuitConfirm:
		g.drawQuitConfirm(screen)
	case ShellCommandPalette:
		g.drawCommandPalette(screen)
	}

	// Draw mouse cursor (only in content area)
	if g.mouseY >= topBarHeight {
		drawMouse(screen, g.mouseX, g.mouseY)
	}

	if g.showDebug {
		cpu := g.machine.CPU
		sys := g.machine.System
		stack := cpu.Stack()
		// Limit stack display to last 8 items
		if len(stack) > 8 {
			stack = stack[len(stack)-8:]
		}

		// CPU info
		msg := fmt.Sprintf("PC: 0x%04X\nOP: %s\nStack: %v\n",
			cpu.PC(), cpu.LastOpcode(), stack)

		// MMIO Registers
		msg += "\nMMIO Registers:\n"
		for _, reg := range sys.MMIORegisters() {
			msg += fmt.Sprintf("%-9s: 0x%08X (%d)\n", reg.Name, uint32(reg.Value), reg.Value)
		}

		ebitenutil.DebugPrint(screen, msg)
	}
}

func (g *Game) drawWindowChrome(screen *ebiten.Image, win *system.Window, isActive bool) {
	x := float64(win.X)
	// Screen Y of chrome top
	chromeTopY := float64(int(win.Y) + topBarHeight)
	w := float64(win.Width)
	h := float64(win.Height)
	chromeH := float64(WinChromeHeight)

	// Outer border (1px all sides including chrome)
	ebitenutil.DrawRect(screen, x-1, chromeTopY-1, w+2, chromeH+h+2, color.RGBA{0, 0, 0, 255})

	// Title bar fill
	var titleClr color.RGBA
	if isActive {
		titleClr = color.RGBA{170, 170, 170, 255} // medium gray
	} else {
		titleClr = color.RGBA{210, 210, 210, 255} // light gray
	}
	ebitenutil.DrawRect(screen, x, chromeTopY, w, chromeH, titleClr)

	// Close button (red square with darker border).
	btnY := chromeTopY + float64(WinCloseBtnY) - float64(WinCloseBtnSize)/2
	closeX := x + float64(WinCloseBtnX) - float64(WinCloseBtnSize)/2
	ebitenutil.DrawRect(screen, closeX, btnY, float64(WinCloseBtnSize), float64(WinCloseBtnSize), color.RGBA{204, 51, 51, 255})
	ebitenutil.DrawRect(screen, closeX+1, btnY+1, float64(WinCloseBtnSize-2), float64(WinCloseBtnSize-2), color.RGBA{170, 0, 0, 255})

	// Prev / Next window buttons — same 8x8 footprint, gray with a < / > glyph.
	for _, b := range [...]struct {
		cx    int
		label string
	}{
		{WinPrevBtnX, "<"},
		{WinNextBtnX, ">"},
	} {
		bx := x + float64(b.cx) - float64(WinCloseBtnSize)/2
		ebitenutil.DrawRect(screen, bx, btnY, float64(WinCloseBtnSize), float64(WinCloseBtnSize), color.RGBA{170, 170, 170, 255})
		ebitenutil.DrawRect(screen, bx+1, btnY+1, float64(WinCloseBtnSize-2), float64(WinCloseBtnSize-2), color.RGBA{220, 220, 220, 255})
		drawShellText(screen, b.label, int(bx)+1, int(btnY)-2, color.Black)
	}

	// Window title centered in chrome
	nameX := int(x) + (int(w)-len(win.Name)*shellFontW)/2
	nameY := int(chromeTopY) + (WinChromeHeight-shellFontH)/2
	drawShellText(screen, win.Name, nameX, nameY, color.Black)

	// Horizontal line separating chrome from content
	ebitenutil.DrawLine(screen, x, chromeTopY+chromeH, x+w, chromeTopY+chromeH, color.RGBA{0, 0, 0, 255})
}

// drawWindowScrollbars paints the right-edge vertical track + arrows, the
// bottom-edge horizontal track, and the grow box. Tracks use a 50% gray dither
// (bgPatternTiles[1]) for the SE feel. The thumb is a fixed-size medium-gray
// rect for v1 (no proportional sizing yet).
func (g *Game) drawWindowScrollbars(screen *ebiten.Image, win *system.Window) {
	chromeTopY := int(win.Y) + topBarHeight
	contentTop := chromeTopY + WinChromeHeight
	contentLeft := int(win.X)
	contentW := int(win.Width)
	contentH := int(win.Height)
	black := color.RGBA{0, 0, 0, 255}
	gray := color.RGBA{170, 170, 170, 255}
	white := color.RGBA{255, 255, 255, 255}

	// Vertical scrollbar gutter (excluding grow box and arrows)
	vbX := contentLeft + contentW - WinScrollbarSize
	vbY := contentTop
	vbH := contentH - WinScrollbarSize
	ebitenutil.DrawRect(screen, float64(vbX), float64(vbY), float64(WinScrollbarSize), float64(vbH), white)
	drawDitherFill(screen, vbX, vbY+WinScrollArrowH, WinScrollbarSize, vbH-2*WinScrollArrowH)
	// Up/down arrow buttons
	ebitenutil.DrawRect(screen, float64(vbX), float64(vbY), float64(WinScrollbarSize), float64(WinScrollArrowH), gray)
	ebitenutil.DrawRect(screen, float64(vbX), float64(vbY+vbH-WinScrollArrowH), float64(WinScrollbarSize), float64(WinScrollArrowH), gray)
	// Arrow glyphs
	drawShellText(screen, "^", vbX+4, vbY+1, black)
	drawShellText(screen, "v", vbX+4, vbY+vbH-WinScrollArrowH+1, black)
	// Thumb (fixed 30px starting just below the up arrow, offset by ScrollY)
	thumbY := vbY + WinScrollArrowH + int(win.ScrollY/4)
	if thumbY+30 > vbY+vbH-WinScrollArrowH {
		thumbY = vbY + vbH - WinScrollArrowH - 30
	}
	if thumbY < vbY+WinScrollArrowH {
		thumbY = vbY + WinScrollArrowH
	}
	ebitenutil.DrawRect(screen, float64(vbX+1), float64(thumbY), float64(WinScrollbarSize-2), 30, gray)
	strokeRect(screen, float32(vbX+1), float32(thumbY), float32(WinScrollbarSize-2), 30, black)
	// Border between content and scrollbar
	ebitenutil.DrawLine(screen, float64(vbX), float64(vbY), float64(vbX), float64(vbY+vbH), black)

	// Horizontal scrollbar gutter (excluding grow box) — visual only
	hbX := contentLeft
	hbY := contentTop + contentH - WinScrollbarSize
	hbW := contentW - WinScrollbarSize
	ebitenutil.DrawRect(screen, float64(hbX), float64(hbY), float64(hbW), float64(WinScrollbarSize), white)
	drawDitherFill(screen, hbX+WinScrollArrowH, hbY, hbW-2*WinScrollArrowH, WinScrollbarSize)
	ebitenutil.DrawRect(screen, float64(hbX), float64(hbY), float64(WinScrollArrowH), float64(WinScrollbarSize), gray)
	ebitenutil.DrawRect(screen, float64(hbX+hbW-WinScrollArrowH), float64(hbY), float64(WinScrollArrowH), float64(WinScrollbarSize), gray)
	drawShellText(screen, "<", hbX+4, hbY+1, black)
	drawShellText(screen, ">", hbX+hbW-WinScrollArrowH+4, hbY+1, black)
	ebitenutil.DrawLine(screen, float64(hbX), float64(hbY), float64(hbX+hbW), float64(hbY), black)

	// Grow box (bottom-right corner, decorative)
	gbX := contentLeft + contentW - WinScrollbarSize
	gbY := contentTop + contentH - WinScrollbarSize
	ebitenutil.DrawRect(screen, float64(gbX), float64(gbY), float64(WinScrollbarSize), float64(WinScrollbarSize), gray)
	strokeRect(screen, float32(gbX), float32(gbY), float32(WinScrollbarSize), float32(WinScrollbarSize), black)
}

// drawDitherFill paints a 50%-gray dither pattern over the rect. Used for
// scrollbar tracks to mimic the System 6 / SE look.
func drawDitherFill(screen *ebiten.Image, x, y, w, h int) {
	if w <= 0 || h <= 0 {
		return
	}
	black := color.RGBA{0, 0, 0, 255}
	for py := 0; py < h; py++ {
		for px := 0; px < w; px++ {
			if (px+py)&1 == 0 {
				screen.Set(x+px, y+py, black)
			}
		}
	}
}

// ============= Shell/Menu Input Handler =============

func (g *Game) handleShellInput(justPressed bool) {
	mx, my := ebiten.CursorPosition()

	// Check for % glyph click to toggle/close menu
	if justPressed && menubarHitTest(mx, my) {
		if g.shellMode == ShellAppleMenu {
			g.shellMode = ShellNormal
		} else if g.shellMode == ShellNormal {
			g.shellMode = ShellAppleMenu
			g.appleIdx = 0
		}
		return
	}

	// Escape always closes shell
	if inpututil.IsKeyJustPressed(ebiten.KeyEscape) {
		g.shellMode = ShellNormal
		return
	}

	switch g.shellMode {
	case ShellAppleMenu:
		g.handleAppleMenuInput(justPressed)
	case ShellSettings:
		g.handleSettingsInput(justPressed)
	case ShellWindowsList:
		g.handleWindowsListInput(justPressed)
	case ShellWindowRowMenu:
		g.handleWindowRowMenuInput(justPressed)
	case ShellQuitConfirm:
		g.handleQuitConfirmInput(justPressed)
	case ShellCommandPalette:
		g.handleCommandPaletteInput(justPressed)
	}
}

func (g *Game) handleAppleMenuInput(justPressed bool) {
	// Up/Down navigation
	if inpututil.IsKeyJustPressed(ebiten.KeyUp) {
		g.appleIdx--
		if g.appleIdx < 0 {
			g.appleIdx = appleMenuItemCount - 1
		}
	}
	if inpututil.IsKeyJustPressed(ebiten.KeyDown) {
		g.appleIdx = (g.appleIdx + 1) % appleMenuItemCount
	}

	selectIndex := func(i int) {
		switch i {
		case 0:
			g.shellMode = ShellNormal
			g.openLauncher()
		case 1:
			g.shellMode = ShellSettings
			g.settingsIdx = 0
		case 2:
			g.shellMode = ShellWindowsList
			g.windowsIdx = 0
		case 3:
			g.shellMode = ShellQuitConfirm
		}
	}

	// Enter to select
	if inpututil.IsKeyJustPressed(ebiten.KeyEnter) {
		selectIndex(g.appleIdx)
		return
	}

	// Mouse hover and click
	mx, my := ebiten.CursorPosition()
	appleX, appleY := 20, topBarHeight+10
	itemHeight := 18
	for i := 0; i < appleMenuItemCount; i++ {
		itemY := appleY + i*itemHeight
		if mx >= appleX && mx < appleX+appleMenuWidth && my >= itemY && my < itemY+itemHeight {
			g.appleIdx = i
			if justPressed {
				selectIndex(i)
				return
			}
		}
	}
}

const settingsRowCount = 5

// Row indices: 0=text scale, 1=debug, 2=color, 3=pattern, 4=clock.

func cycleClearColor(c color.RGBA) color.RGBA {
	switch {
	case c.R == 255 && c.G == 255 && c.B == 255:
		return color.RGBA{220, 220, 220, 255} // light gray
	case c.R == 220 && c.G == 220 && c.B == 220:
		return color.RGBA{173, 216, 230, 255} // light blue
	case c.R == 173 && c.G == 216 && c.B == 230:
		return color.RGBA{0, 0, 0, 255} // black
	default:
		return color.RGBA{255, 255, 255, 255} // white
	}
}

type rect struct{ x, y, w, h int }

func (r rect) contains(x, y int) bool {
	return x >= r.x && x < r.x+r.w && y >= r.y && y < r.y+r.h
}

// settingsModalGeom describes the modal frame and the close-button hit rect.
// Both the renderer and the input handler call this so they can't drift apart.
func (g *Game) settingsModalGeom() (modalX, modalY, modalW, modalH int, closeBtn rect) {
	sw := int(g.machine.System.ScreenWidth())
	sh := int(g.machine.System.ScreenHeight())
	modalW, modalH = 280, 240
	modalX = (sw - modalW) / 2
	modalY = (sh - modalH) / 2
	closeBtn = rect{x: modalX + modalW - 18, y: modalY + 5, w: 12, h: 12}
	return
}

const settingsTitleBarH = 22
const settingsRowSpacing = 30
const settingsRowH = 22

// settingsRowGeom returns the hit rectangles for row idx.
// rowRect spans the full row (used for hover and Up/Down highlight).
// leftBtn/rightBtn are the ‹ ›-style steppers (rows 0/3/4).
// pillRect is the ON/OFF toggle (row 1) or color swatch+arrow combo (row 2).
func (g *Game) settingsRowGeom(idx int) (rowRect, leftBtn, rightBtn, pillRect rect) {
	modalX, modalY, _, _, _ := g.settingsModalGeom()
	rowY := modalY + settingsTitleBarH + 8 + idx*settingsRowSpacing
	rowRect = rect{x: modalX + 10, y: rowY, w: 260, h: settingsRowH}
	leftBtn = rect{x: modalX + 145, y: rowY, w: 14, h: settingsRowH}
	rightBtn = rect{x: modalX + 200, y: rowY, w: 14, h: settingsRowH}
	pillRect = rect{x: modalX + 145, y: rowY, w: 70, h: settingsRowH}
	return
}

// applySettingsAction mutates the host setting at row idx by direction:
// dir -1 = step left, +1 = step right, 0 = primary (toggle/cycle).
// Saves on any change.
func (g *Game) applySettingsAction(idx, dir int) {
	changed := false
	switch idx {
	case 0: // Text Scale
		if dir < 0 && g.textScale > 1 {
			g.textScale--
			changed = true
		} else if dir > 0 && g.textScale < 4 {
			g.textScale++
			changed = true
		}
	case 1: // Debug
		if dir == 0 {
			g.showDebug = !g.showDebug
			changed = true
		}
	case 2: // Color
		if dir == 0 {
			g.clearColor = cycleClearColor(g.clearColor)
			changed = true
		}
	case 3: // Pattern
		if dir < 0 {
			g.bgPattern--
			if g.bgPattern < 0 {
				g.bgPattern = len(bgPatternTiles) - 1
			}
			changed = true
		} else if dir > 0 {
			g.bgPattern = (g.bgPattern + 1) % len(bgPatternTiles)
			changed = true
		}
	case 4: // Clock
		if dir < 0 {
			g.clockFormat--
			if g.clockFormat < 0 {
				g.clockFormat = 2
			}
			changed = true
		} else if dir > 0 {
			g.clockFormat = (g.clockFormat + 1) % 3
			changed = true
		}
	}
	if changed {
		g.saveSettings()
	}
}

func (g *Game) handleSettingsInput(justPressed bool) {
	// Up/Down navigation
	if inpututil.IsKeyJustPressed(ebiten.KeyUp) {
		g.settingsIdx--
		if g.settingsIdx < 0 {
			g.settingsIdx = settingsRowCount - 1
		}
	}
	if inpututil.IsKeyJustPressed(ebiten.KeyDown) {
		g.settingsIdx = (g.settingsIdx + 1) % settingsRowCount
	}

	if inpututil.IsKeyJustPressed(ebiten.KeyLeft) {
		g.applySettingsAction(g.settingsIdx, -1)
	}
	if inpututil.IsKeyJustPressed(ebiten.KeyRight) {
		g.applySettingsAction(g.settingsIdx, +1)
	}
	if inpututil.IsKeyJustPressed(ebiten.KeySpace) || inpututil.IsKeyJustPressed(ebiten.KeyEnter) {
		g.applySettingsAction(g.settingsIdx, 0)
	}

	// Mouse interaction
	mx, my := ebiten.CursorPosition()

	_, _, _, _, closeBtn := g.settingsModalGeom()
	if justPressed && closeBtn.contains(mx, my) {
		g.shellMode = ShellAppleMenu
		g.appleIdx = 1
		return
	}

	for i := 0; i < settingsRowCount; i++ {
		rowRect, leftBtn, rightBtn, pillRect := g.settingsRowGeom(i)
		if !rowRect.contains(mx, my) {
			continue
		}
		g.settingsIdx = i
		if !justPressed {
			break
		}
		switch i {
		case 1, 2: // Debug toggle / Color cycle: click the pill or anywhere in row
			if pillRect.contains(mx, my) {
				g.applySettingsAction(i, 0)
			}
		default: // ‹ › steppers
			if leftBtn.contains(mx, my) {
				g.applySettingsAction(i, -1)
			} else if rightBtn.contains(mx, my) {
				g.applySettingsAction(i, +1)
			}
		}
		break
	}

	// Escape goes back to Apple menu
	if inpututil.IsKeyJustPressed(ebiten.KeyEscape) {
		g.shellMode = ShellAppleMenu
		g.appleIdx = 1
	}
}

func (g *Game) handleWindowsListInput(justPressed bool) {
	wins := g.machine.Services().ListWindowsSorted()

	// Escape goes back to Apple menu
	if inpututil.IsKeyJustPressed(ebiten.KeyEscape) {
		g.shellMode = ShellAppleMenu
		g.appleIdx = 2
		return
	}

	if len(wins) == 0 {
		return
	}

	// Up/Down navigation
	if inpututil.IsKeyJustPressed(ebiten.KeyUp) {
		g.windowsIdx--
		if g.windowsIdx < 0 {
			g.windowsIdx = len(wins) - 1
		}
	}
	if inpututil.IsKeyJustPressed(ebiten.KeyDown) {
		g.windowsIdx = (g.windowsIdx + 1) % len(wins)
	}

	// Enter opens row menu
	if inpututil.IsKeyJustPressed(ebiten.KeyEnter) && g.windowsIdx < len(wins) {
		g.shellMode = ShellWindowRowMenu
		g.windowRowMenu = 0
		g.windowRowMenuWin = wins[g.windowsIdx].ID
		return
	}

	// Mouse hover and click
	mx, my := ebiten.CursorPosition()
	listX, listY := 50, topBarHeight+50
	rowHeight := 20
	for i, win := range wins {
		itemY := listY + i*rowHeight
		if mx >= listX && mx < listX+200 && my >= itemY && my < itemY+rowHeight {
			g.windowsIdx = i
			if justPressed {
				g.shellMode = ShellWindowRowMenu
				g.windowRowMenu = 0
				g.windowRowMenuWin = win.ID
				return
			}
		}
	}
}

func (g *Game) handleWindowRowMenuInput(justPressed bool) {
	// Escape goes back to Windows list
	if inpututil.IsKeyJustPressed(ebiten.KeyEscape) {
		g.shellMode = ShellWindowsList
		return
	}

	// Left/Right or Up/Down to switch between Focus/Close
	if inpututil.IsKeyJustPressed(ebiten.KeyLeft) || inpututil.IsKeyJustPressed(ebiten.KeyUp) {
		g.windowRowMenu = 0
	}
	if inpututil.IsKeyJustPressed(ebiten.KeyRight) || inpututil.IsKeyJustPressed(ebiten.KeyDown) {
		g.windowRowMenu = 1
	}

	execute := func(i int) {
		switch i {
		case 0: // Focus
			g.focusAndShow(g.windowRowMenuWin)
			g.shellMode = ShellNormal
		case 1: // Close
			g.closeWindowByID(g.windowRowMenuWin)
			g.shellMode = ShellWindowsList
			g.windowsIdx = 0
		}
	}

	// Enter to execute
	if inpututil.IsKeyJustPressed(ebiten.KeyEnter) {
		execute(g.windowRowMenu)
		return
	}

	// Mouse hover and click
	mx, my := ebiten.CursorPosition()
	subX, subY := 260, topBarHeight+50+g.windowsIdx*20
	itemWidth := 60
	for i := 0; i < 2; i++ {
		x := subX + i*itemWidth
		if mx >= x && mx < x+itemWidth && my >= subY && my < subY+20 {
			g.windowRowMenu = i
			if justPressed {
				execute(i)
				return
			}
		}
	}
}

func (g *Game) handleQuitConfirmInput(justPressed bool) {
	// Enter or Y to confirm quit
	if inpututil.IsKeyJustPressed(ebiten.KeyEnter) || inpututil.IsKeyJustPressed(ebiten.KeyY) {
		os.Exit(0)
	}

	// N or Escape to cancel
	if inpututil.IsKeyJustPressed(ebiten.KeyN) || inpututil.IsKeyJustPressed(ebiten.KeyEscape) {
		g.shellMode = ShellAppleMenu
		g.appleIdx = 3
		return
	}

	// The drawn modal is 240x120 centered (drawQuitConfirm); mirror that geometry
	// so the hit rects don't drift if the layout changes in one place but not the other.
	sw := int(g.machine.System.ScreenWidth())
	sh := int(g.machine.System.ScreenHeight())
	modalW, modalH := 240, 120
	modalX := (sw - modalW) / 2
	modalY := (sh - modalH) / 2
	quitBtnX := modalX + 20
	cancelBtnX := modalX + 130
	btnY := modalY + 60

	if !justPressed {
		return
	}
	mx, my := ebiten.CursorPosition()
	if mx >= quitBtnX && mx < quitBtnX+80 && my >= btnY && my < btnY+20 {
		os.Exit(0)
	}
	if mx >= cancelBtnX && mx < cancelBtnX+80 && my >= btnY && my < btnY+20 {
		g.shellMode = ShellAppleMenu
		g.appleIdx = 3
	}
}

// ============= Shell/Menu Rendering =============

func (g *Game) drawAppleMenu(screen *ebiten.Image) {
	items := []string{"Applications", "Settings", "Current Windows", "Quit"}
	x, y := 20, topBarHeight+10
	itemHeight := 18

	for i, item := range items {
		itemY := y + i*itemHeight
		bgClr := color.RGBA{180, 180, 180, 255}
		fgClr := color.Color(color.Black)
		if i == g.appleIdx {
			bgClr = color.RGBA{80, 80, 80, 255}
			fgClr = color.White
		}
		ebitenutil.DrawRect(screen, float64(x), float64(itemY), float64(appleMenuWidth), float64(itemHeight), bgClr)
		strokeRect(screen, float32(x), float32(itemY), float32(appleMenuWidth), float32(itemHeight), color.Black)
		drawShellText(screen, item, x+5, itemY+(itemHeight-shellFontH)/2, fgClr)
	}
}

func (g *Game) drawSettingsModal(screen *ebiten.Image) {
	modalX, modalY, modalW, modalH, closeBtn := g.settingsModalGeom()

	// Modal frame
	ebitenutil.DrawRect(screen, float64(modalX), float64(modalY), float64(modalW), float64(modalH), color.RGBA{200, 200, 200, 255})
	strokeRect(screen, float32(modalX), float32(modalY), float32(modalW), float32(modalH), color.Black)

	// Title bar
	ebitenutil.DrawRect(screen, float64(modalX), float64(modalY), float64(modalW), float64(settingsTitleBarH), color.RGBA{170, 170, 170, 255})
	ebitenutil.DrawLine(screen, float64(modalX), float64(modalY+settingsTitleBarH), float64(modalX+modalW), float64(modalY+settingsTitleBarH), color.Black)
	drawShellText(screen, "Settings", modalX+8, modalY+(settingsTitleBarH-shellFontH)/2, color.Black)

	// Close button
	ebitenutil.DrawRect(screen, float64(closeBtn.x), float64(closeBtn.y), float64(closeBtn.w), float64(closeBtn.h), color.RGBA{204, 51, 51, 255})
	strokeRect(screen, float32(closeBtn.x), float32(closeBtn.y), float32(closeBtn.w), float32(closeBtn.h), color.Black)
	drawShellText(screen, "x", closeBtn.x+3, closeBtn.y+(closeBtn.h-shellFontH)/2, color.White)

	labels := [settingsRowCount]string{"Text Scale:", "Debug:", "Color:", "Pattern:", "Clock:"}
	for i, label := range labels {
		g.drawSettingsRow(screen, i, label)
	}
}

// drawSettingsRow renders one row of the settings panel: label on the left,
// control(s) on the right. The hit rects for each control come from
// settingsRowGeom so the renderer never goes out of sync with the input handler.
func (g *Game) drawSettingsRow(screen *ebiten.Image, idx int, label string) {
	rowRect, leftBtn, rightBtn, pillRect := g.settingsRowGeom(idx)

	bgClr := color.RGBA{180, 180, 180, 255}
	fgClr := color.Color(color.Black)
	if idx == g.settingsIdx {
		bgClr = color.RGBA{80, 80, 80, 255}
		fgClr = color.White
	}
	ebitenutil.DrawRect(screen, float64(rowRect.x), float64(rowRect.y), float64(rowRect.w), float64(rowRect.h), bgClr)
	drawShellText(screen, label, rowRect.x+5, rowRect.y+(rowRect.h-shellFontH)/2, fgClr)

	textY := rowRect.y + (rowRect.h-shellFontH)/2
	switch idx {
	case 0: // Text Scale: ‹ N ›
		g.drawStepper(screen, leftBtn, rightBtn, fgClr, fmt.Sprintf("%d", g.textScale))
	case 1: // Debug: pill
		state := "OFF"
		if g.showDebug {
			state = "ON"
		}
		g.drawPill(screen, pillRect, state)
	case 2: // Color: swatch + ▶
		swatchX := pillRect.x
		swatchY := rowRect.y + 3
		ebitenutil.DrawRect(screen, float64(swatchX), float64(swatchY), 24, 16, g.clearColor)
		strokeRect(screen, float32(swatchX), float32(swatchY), 24, 16, color.Black)
		drawShellText(screen, ">", swatchX+30, textY, fgClr)
	case 3: // Pattern: ‹ label ›
		g.drawStepper(screen, leftBtn, rightBtn, fgClr, bgPatternLabel(g.bgPattern))
	case 4: // Clock: ‹ label ›
		g.drawStepper(screen, leftBtn, rightBtn, fgClr, clockFormatLabel(g.clockFormat))
	}
}

// drawStepper renders a ‹ value › control. The button rects come from
// settingsRowGeom and are also the input hit rects.
func (g *Game) drawStepper(screen *ebiten.Image, leftBtn, rightBtn rect, fg color.Color, value string) {
	textY := leftBtn.y + (leftBtn.h-shellFontH)/2
	drawShellText(screen, "<", leftBtn.x+3, textY, fg)
	drawShellText(screen, value, leftBtn.x+leftBtn.w+4, textY, fg)
	drawShellText(screen, ">", rightBtn.x+3, textY, fg)
}

// drawPill renders an ON/OFF style pill button.
func (g *Game) drawPill(screen *ebiten.Image, r rect, label string) {
	ebitenutil.DrawRect(screen, float64(r.x), float64(r.y), float64(r.w), float64(r.h), color.RGBA{220, 220, 220, 255})
	strokeRect(screen, float32(r.x), float32(r.y), float32(r.w), float32(r.h), color.Black)
	textX := r.x + (r.w-len(label)*shellFontW)/2
	drawShellText(screen, label, textX, r.y+(r.h-shellFontH)/2, color.Black)
}

func (g *Game) drawWindowsModal(screen *ebiten.Image) {
	sw := int(g.machine.System.ScreenWidth())
	sh := int(g.machine.System.ScreenHeight())

	wins := g.machine.Services().ListWindowsSorted()

	modalW, modalH := 300, 100+len(wins)*25
	if modalH > 300 {
		modalH = 300
	}
	modalX := (sw - modalW) / 2
	modalY := (sh - modalH) / 2

	ebitenutil.DrawRect(screen, float64(modalX), float64(modalY), float64(modalW), float64(modalH), color.RGBA{200, 200, 200, 255})
	strokeRect(screen, float32(modalX), float32(modalY), float32(modalW), float32(modalH), color.Black)

	drawShellText(screen, "Windows", modalX+10, modalY+10, color.Black)

	if len(wins) == 0 {
		drawShellText(screen, "(no windows)", modalX+20, modalY+40, color.Black)
		return
	}

	for i, win := range wins {
		rowY := modalY + 40 + i*25
		bgClr := color.RGBA{180, 180, 180, 255}
		fgClr := color.Color(color.Black)
		if i == g.windowsIdx {
			bgClr = color.RGBA{80, 80, 80, 255}
			fgClr = color.White
		}
		ebitenutil.DrawRect(screen, float64(modalX+10), float64(rowY), float64(modalW-20), 20, bgClr)

		label := fmt.Sprintf("%s (%d)", win.Name, win.ID)
		drawShellText(screen, label, modalX+15, rowY+(20-shellFontH)/2, fgClr)
	}
}

func (g *Game) drawQuitConfirm(screen *ebiten.Image) {
	sw := int(g.machine.System.ScreenWidth())
	sh := int(g.machine.System.ScreenHeight())

	modalW, modalH := 240, 120
	modalX := (sw - modalW) / 2
	modalY := (sh - modalH) / 2

	ebitenutil.DrawRect(screen, float64(modalX), float64(modalY), float64(modalW), float64(modalH), color.RGBA{200, 200, 200, 255})
	strokeRect(screen, float32(modalX), float32(modalY), float32(modalW), float32(modalH), color.Black)

	drawShellText(screen, "Quit Cloister?", modalX+20, modalY+20, color.Black)

	quitBtnX := modalX + 20
	quitBtnY := modalY + 60
	cancelBtnX := modalX + 130

	g.drawButton(screen, quitBtnX, quitBtnY, "Quit")
	g.drawButton(screen, cancelBtnX, quitBtnY, "Cancel")
}

func (g *Game) drawButton(screen *ebiten.Image, x, y int, label string) {
	btnW, btnH := 80, 20
	bgClr := color.RGBA{180, 180, 180, 255}
	ebitenutil.DrawRect(screen, float64(x), float64(y), float64(btnW), float64(btnH), bgClr)
	strokeRect(screen, float32(x), float32(y), float32(btnW), float32(btnH), color.Black)
	textX := x + (btnW-len(label)*shellFontW)/2
	drawShellText(screen, label, textX, y+(btnH-shellFontH)/2, color.Black)
}

// adjustScroll bumps the target window's ScrollY by delta. Apps interpret
// ScrollY however they want; the host just exposes the line/page button hits.
func (g *Game) adjustScroll(id system.WindowID, delta int32) {
	win := g.machine.Services().GetWindowByID(id)
	if win == nil {
		return
	}
	win.ScrollY += delta
	if win.ScrollY < 0 {
		win.ScrollY = 0
	}
	g.wm.MarkDirty(id)
}

// focusAndShow moves user focus to winID and, if the window isn't already a
// visible pane, replaces the layout with a single full-screen pane on it.
// This is the only correct way to "switch to" a window — bare FocusWindow
// only bumps Z-order, which the pane-driven Draw loop ignores.
func (g *Game) focusAndShow(winID system.WindowID) {
	sm := g.machine.Services()
	if sm.GetWindowByID(winID) == nil {
		return
	}
	sm.FocusWindow(winID)
	for _, p := range sm.ListPanes() {
		if p.WinID == winID {
			return
		}
	}
	sw := g.machine.System.ScreenWidth()
	sh := g.machine.System.ScreenHeight()
	contentH := sh - int32(topBarHeight) - int32(WinChromeHeight)
	if contentH < 100 {
		contentH = 100
	}
	sm.LayoutSingle(winID, 0, 0, sw, contentH)
}

// cycleFocus advances focus by dir steps through the Z-ordered window list
// (dir=+1 = next, -1 = previous), wrapping at the ends, and shows the result.
func (g *Game) cycleFocus(dir int) {
	wins := g.machine.Services().ListWindowsSorted()
	n := len(wins)
	if n == 0 {
		return
	}
	active := g.machine.Services().GetActiveWindowID()
	idx := 0
	for i, w := range wins {
		if w.ID == active {
			idx = i
			break
		}
	}
	next := wins[((idx+dir)%n+n)%n]
	g.focusAndShow(next.ID)
}

// closeWindowByID closes whichever kind of window winID belongs to (launcher,
// Lux app, shell, or a plain VM-owned window) and tears down the matching
// host-side state. Used by both the chrome close button and the Apple-menu
// Close action so they can't drift apart.
func (g *Game) closeWindowByID(winID system.WindowID) {
	if winID == g.launcherWinID {
		g.closeLauncher()
		return
	}
	if g.closeLuxApp(winID) {
		return
	}
	if g.shellApp != nil && winID == g.shellApp.winID {
		g.shellApp = nil
	}
	g.machine.Services().CloseWindow(winID)
}

// restartMachine reloads the shell program and rebuilds the VM/window state.
// Cloister itself keeps running so the Ebiten host window stays alive.
func (g *Game) restartMachine() error {
	bytecode, err := lux.LoadProgram(g.shellPath)
	if err != nil {
		return fmt.Errorf("restart load %s: %w", g.shellPath, err)
	}
	machine := system.NewMachine(bytecode, g.memSize)

	cwd, err := os.Getwd()
	if err == nil {
		_ = machine.System.SetSandboxRoot(cwd)
	}

	bus := &clipboardBus{system: machine.System, memory: machine.CPU.Memory()}
	machine.CPU.SetBus(bus)

	if _, err := machine.Tick(); err != nil {
		return fmt.Errorf("restart boot tick: %w", err)
	}

	g.machine = machine
	g.wm = NewWindowManager(machine.System.Services)
	g.launcherWinID = 0
	g.shellApp = nil
	g.apps = nil
	g.shellMode = ShellNormal
	g.bootTimer = 60
	return nil
}

func (g *Game) Layout(outsideWidth, outsideHeight int) (int, int) {
	// Enforce minimum window size of 800x600
	minWidth := 800
	minHeight := 600
	if outsideWidth < minWidth {
		outsideWidth = minWidth
	}
	if outsideHeight < minHeight {
		outsideHeight = minHeight
	}

	// If the user resized the window, we update the internal resolution.
	// We scale down the outside dimensions by screenScale to get logical pixels.
	w := outsideWidth / screenScale
	h := outsideHeight / screenScale
	if w > 0 && h > 0 {
		// Enforce minimum internal resolution as well
		if w < 100 {
			w = 100
		}
		if h < 75 {
			h = 75
		}
		g.machine.System.SetResolution(int32(w), int32(h))
	}
	return int(g.machine.System.ScreenWidth()), int(g.machine.System.ScreenHeight())
}

type clipboardBus struct {
	system *system.System
	memory []byte
}

func (c *clipboardBus) Read(address uint32) (int32, error) {
	return c.system.Read(address)
}

func (c *clipboardBus) Write(address uint32, value int32) error {
	if address == 0x30A0 {
		ptr := uint32(value)
		var text []byte
		for ptr < uint32(len(c.memory)) && c.memory[ptr] != 0 {
			text = append(text, c.memory[ptr])
			ptr++
		}
		return nil
	}
	return c.system.Write(address, value)
}

func main() {
	memFlag := flag.Int("mem", 32, "VM memory size in megabytes (max 128)")
	widthFlag := flag.Int("w", 0, "Screen width override (0 = defer to shell.lux)")
	heightFlag := flag.Int("h", 0, "Screen height override (0 = defer to shell.lux)")
	scaleFlag := flag.Int("scale", 0, "Window pixel scale override (0 = defer to shell.lux)")
	flag.Parse()

	// Detect whether the user passed -w / -h / -scale explicitly, so those
	// values override whatever shell.lux writes during its first tick.
	explicit := map[string]bool{}
	flag.Visit(func(f *flag.Flag) { explicit[f.Name] = true })

	if *memFlag > 128 {
		fmt.Println("Memory size capped at 128MB")
		*memFlag = 128
	}

	// Load shell program
	shellPath := defaultShellPath
	if flag.NArg() > 0 {
		shellPath = flag.Arg(0)
	}
	shellBytecode, err := lux.LoadProgram(shellPath)
	if err != nil {
		fmt.Fprintf(os.Stderr, "Error loading %s: %v\n", shellPath, err)
		os.Exit(1)
	}

	machine := system.NewMachine(shellBytecode, uint32(*memFlag)*1024*1024)

	// Set sandbox root to current working directory for file operations
	cwd, err := os.Getwd()
	if err != nil {
		fmt.Fprintf(os.Stderr, "Error getting working directory: %v\n", err)
		os.Exit(1)
	}
	if err := machine.System.SetSandboxRoot(cwd); err != nil {
		fmt.Fprintf(os.Stderr, "Error setting sandbox root: %v\n", err)
		os.Exit(1)
	}

	// Wrap the bus to intercept clipboard writes
	bus := &clipboardBus{
		system: machine.System,
		memory: machine.CPU.Memory(),
	}
	machine.CPU.SetBus(bus)

	// Let the VM run up to its first YIELD / HALT so a program's startup
	// code (e.g. shell.lux setting SCR_W / SCR_H / TEXT cell-size) has a
	// chance to populate the MMIO registers before we size the host window.
	if _, err := machine.Tick(); err != nil {
		fmt.Fprintf(os.Stderr, "Error during shell startup tick: %v\n", err)
		os.Exit(1)
	}

	// CLI flags override whatever the program wrote during the boot tick.
	if explicit["w"] {
		machine.System.SetResolution(int32(*widthFlag), machine.System.ScreenHeight())
	}
	if explicit["h"] {
		machine.System.SetResolution(machine.System.ScreenWidth(), int32(*heightFlag))
	}

	// Read back the VM's chosen screen size and text scale to pick a window
	// scale.  The text-device scale doubles nicely as the window zoom factor
	// because it's the same intent: "how large should a logical pixel be?"
	sw := int(machine.System.ScreenWidth())
	sh := int(machine.System.ScreenHeight())
	if sw <= 0 || sh <= 0 {
		sw, sh = 80, 80
		machine.System.SetResolution(int32(sw), int32(sh))
	}
	textScale := machine.System.TextScale()
	if textScale < 1 {
		textScale = 1
	}
	screenScale = textScale
	if explicit["scale"] && *scaleFlag > 0 {
		screenScale = *scaleFlag
	}

	game := &Game{
		machine:      machine,
		wm:           NewWindowManager(machine.System.Services),
		bootTimer:    60,
		textScale:    1,
		clearColor:   color.RGBA{255, 255, 255, 255}, // white
		settingsPath: filepath.Join(cwd, settingsFileName),
		shellPath:    shellPath,
		memSize:      uint32(*memFlag) * 1024 * 1024,
	}
	game.loadSettings()

	windowWidth := sw * screenScale
	windowHeight := sh * screenScale

	// Enforce minimum window size of 800x600
	if windowWidth < 800 {
		windowWidth = 800
	}
	if windowHeight < 600 {
		windowHeight = 600
	}

	ebiten.SetWindowSize(windowWidth, windowHeight)
	ebiten.SetWindowResizingMode(ebiten.WindowResizingModeEnabled)
	ebiten.SetWindowTitle("CLOISTER")

	if err := ebiten.RunGame(game); err != nil {
		fmt.Fprintf(os.Stderr, "Error: %v\n", err)
		os.Exit(1)
	}
}
