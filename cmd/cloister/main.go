package main

import (
	"encoding/binary"
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
// vectors, and acts as the substrate that user apps run on Top of (System 6
// Finder analog). It's relative to cwd so editing lib/shell.lux in the repo
// and re-running cloister picks up the change without a rebuild.
const defaultShellPath = "lib/shell.lux"
const TopBarHeight = 24

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
	machine         *system.Machine
	wm              *WindowManager
	showDebug       bool
	bootTimer       int
	mouseX          int
	mouseY          int
	dragging        bool
	dragWinID       system.WindowID
	dragOffX        int
	dragOffY        int
	draggingScroll  bool
	draggingScrollH bool
	scrollGrabY     int
	scrollGrabX     int
	wasLeftDown     bool
	clearColor      color.RGBA
	textScale       int    // 1-4
	bgPattern       int    // 0=solid, 1=50% gray, 2=dots, 3=stripes
	clockFormat     int    // 0=24h, 1=12h, 2=12h-AMPM
	settingsPath    string // absolute path of cloister-settings.lux (cwd at launch)

	launcherWinID system.WindowID // 0 if launcher is not open
	shellApp      *ShellApp       // singleton; nil until first launch

	apps []*LuxApp // .lux programs launched from the Applications launcher; each owns its own VM and window

	// Restart-OS support: when set, Top of next Update rebuilds the machine
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

	screenWidth  int
	screenHeight int

	// Open menu state (for app window menus)
	openMenuWinID system.WindowID // 0 if no menu is open
	openMenuIdx   int              // which menu item is hovered (-1 if none)
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

// menubarHitTest returns true if (x,y) is over the % glyph in the Top bar.
// The glyph is drawn at (4,4) with scale 1, so the hit rect is roughly 0..16, 0..TopBarHeight.
func menubarHitTest(x, y int) bool {
	return x >= 0 && x < 18 && y >= 0 && y < TopBarHeight
}

func measureSystemFontText(s string, scale int) int {
	return len(s) * shellFontW * scale
}

// strokeRect draws a 1px outline of a rectangle. ebitenutil.DrawRect fills,
// which is the wrong tool for a border.
func strokeRect(screen *ebiten.Image, x, y, w, h float32, clr color.Color) {
	vector.StrokeRect(screen, x, y, w, h, 1, clr, false)
}

// drawSystemFontText renders s with basicfont.Face7x13. (x, y) is the Top-Left of
// the glyph cell; basicfont's baseline sits 11px below the Top.
const shellFontW = 7
const shellFontH = 13
const shellFontAscent = 11

func drawSystemFontText(screen *ebiten.Image, s string, x, y, scale int, clr color.Color) {
	text.Draw(screen, s, basicfont.Face7x13, x, y+shellFontAscent, clr)
}

func drawShellText(screen *ebiten.Image, s string, x, y int, clr color.Color) {
	drawSystemFontText(screen, s, x, y, 1, clr)
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
// Chicago font convention (main.go drawSystemFontText).
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

// luxAppForWinID finds the LuxApp whose window matches winID, or nil.
func (g *Game) luxAppForWinID(winID system.WindowID) *LuxApp {
	for _, app := range g.apps {
		if app.winID == winID {
			return app
		}
	}
	return nil
}

// getCStringFromMem reads a NUL-terminated F-string from the VM's big-endian memory.
// Returns "" if ptr is 0 or out of bounds.
func getCStringFromMem(mem []byte, ptr uint32) string {
	if ptr == 0 || ptr >= uint32(len(mem)) {
		return ""
	}
	end := ptr
	for end < uint32(len(mem)) && mem[end] != 0 {
		end++
	}
	return string(mem[ptr:end])
}

// MenuItem represents one menu item read from VM memory
type MenuItem struct {
	KeyChar  int32
	TextPtr  uint32
	Callback uint32
	Text     string
}

// readMenuFromMem reads a menu table from VM memory at tablePtr.
// Returns (title, items) or ("", nil) if invalid.
func readMenuFromMem(mem []byte, tablePtr uint32) (string, []MenuItem) {
	const (
		headerSize = 12
		entrySize  = 16
		maxItems   = 8
	)

	if tablePtr+headerSize > uint32(len(mem)) {
		return "", nil
	}

	titlePtr := binary.BigEndian.Uint32(mem[tablePtr:])
	itemCount := binary.BigEndian.Uint32(mem[tablePtr+4:])

	if itemCount > maxItems {
		itemCount = maxItems
	}

	title := getCStringFromMem(mem, titlePtr)
	items := make([]MenuItem, itemCount)

	for i := uint32(0); i < itemCount; i++ {
		itemAddr := tablePtr + headerSize + i*entrySize
		if itemAddr+entrySize > uint32(len(mem)) {
			break
		}

		keyChar := int32(binary.BigEndian.Uint32(mem[itemAddr:]))
		textPtr := binary.BigEndian.Uint32(mem[itemAddr+4:])
		callback := binary.BigEndian.Uint32(mem[itemAddr+8:])

		items[i] = MenuItem{
			KeyChar:  keyChar,
			TextPtr:  textPtr,
			Callback: callback,
			Text:     getCStringFromMem(mem, textPtr),
		}
	}

	return title, items
}

// drawDeskTopBackground paints the deskTop area below the menubar. Pattern 0 is
// just a solid fill; patterns 1-3 tile an 8x8 1-bit overlay (black) over the
// solid color.
func (g *Game) drawDeskTopBackground(screen *ebiten.Image, sw, sh int) {
	screen.Fill(g.clearColor)
	if g.bgPattern <= 0 || g.bgPattern >= len(bgPatternTiles) {
		return
	}
	tile := bgPatternTiles[g.bgPattern]
	black := color.RGBA{0, 0, 0, 255}
	for y := TopBarHeight; y < sh; y++ {
		row := tile[(y-TopBarHeight)&7]
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

	LeftDown := ebiten.IsMouseButtonPressed(ebiten.MouseButtonLeft)
	justPressed := LeftDown && !g.wasLeftDown
	justReleased := !LeftDown && g.wasLeftDown
	g.wasLeftDown = LeftDown

	// Shell/menu input takes priority
	if g.shellMode != ShellNormal {
		g.handleShellInput(justPressed)
		// Don't process window input while shell is active
		return nil
	}

	// Clear drag state if user clicked somewhere
	if justPressed && (g.dragging || g.draggingScroll || g.draggingScrollH) {
		g.dragging = false
		g.draggingScroll = false
		g.draggingScrollH = false
		g.dragWinID = 0
	}

	// Drag release
	if justReleased && (g.dragging || g.draggingScroll || g.draggingScrollH) {
		g.dragging = false
		g.draggingScroll = false
		g.draggingScrollH = false
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
			if inpututil.IsKeyJustPressed(ebiten.KeyUp) {
				g.shellApp.handleUp()
			}
			if inpututil.IsKeyJustPressed(ebiten.KeyDown) {
				g.shellApp.handleDown()
			}
			g.wm.MarkDirty(g.shellApp.winID)
		} else {
			// Route keyboard input to focused window's machine
			activeWinID := g.machine.Services().GetActiveWindowID()
			var targetMachine *system.Machine
			if app := g.luxAppForWinID(activeWinID); app != nil {
				targetMachine = app.machine
			} else {
				targetMachine = g.machine
			}

			for _, k := range inpututil.AppendJustPressedKeys(nil) {
				keyCode := luxKeyCodeFromEbiten(k)
				if keyCode > 0 {
					targetMachine.QueueKeyDown(keyCode)
				}
			}
			for _, k := range inpututil.AppendJustReleasedKeys(nil) {
				keyCode := luxKeyCodeFromEbiten(k)
				if keyCode > 0 {
					if targetMachine == g.machine {
						// Shell still uses old Services queue
						g.machine.Services().QueueKeyUp(keyCode)
					} else {
						// For now, LuxApp doesn't need key up events
						_ = keyCode
					}
				}
			}
		}
	}

	// Mouse input with hit-testing and window routing
	mx, my := ebiten.CursorPosition()
	g.mouseX, g.mouseY = mx, my

	// Drag move (window)
	if g.dragging && LeftDown {
		newX := int32(mx - g.dragOffX)
		newY := int32(my - TopBarHeight - g.dragOffY)
		if newY < 0 {
			newY = 0
		}
		g.machine.Services().DirectMoveWindow(g.dragWinID, newX, newY)
	}

	// Drag move (scroll)
	if g.draggingScroll && LeftDown {
		if win := g.machine.Services().GetWindowByID(g.dragWinID); win != nil {
			trackYRel, trackH, _, thumbH, _, _, _, _ := g.getScrollGeometry(win)
			trackTop := int(win.ContRgn.Top) + trackYRel

			// New thumb Top position based on mouse and grab offset
			newThumbY := my - g.scrollGrabY

			// Clamp thumb Top to track bounds
			if newThumbY < trackTop {
				newThumbY = trackTop
			}
			if newThumbY > trackTop+trackH-thumbH {
				newThumbY = trackTop + trackH - thumbH
			}

			// Map thumb position to ScrollY
			relThumbPos := float64(newThumbY - trackTop)
			scrollPct := 1.0 - (relThumbPos / float64(trackH-thumbH))

			maxScroll := win.ContentHeight - win.ContRgn.Height()
			if maxScroll < 0 {
				maxScroll = 0
			}
			win.ScrollY = int32(scrollPct * float64(maxScroll))
			g.wm.MarkDirty(g.dragWinID)
		}
	}

	// Drag move (scroll H)
	if g.draggingScrollH && LeftDown {
		if win := g.machine.Services().GetWindowByID(g.dragWinID); win != nil {
			_, _, _, _, trackXRel, trackW, _, thumbW := g.getScrollGeometry(win)
			trackLeft := int(win.ContRgn.Left) + trackXRel

			newThumbX := mx - g.scrollGrabX
			if newThumbX < trackLeft {
				newThumbX = trackLeft
			}
			if newThumbX > trackLeft+trackW-thumbW {
				newThumbX = trackLeft + trackW - thumbW
			}

			relThumbPos := float64(newThumbX - trackLeft)
			scrollPct := relThumbPos / float64(trackW-thumbW)

			maxScrollX := win.ContentWidth - win.ContRgn.Width()
			if maxScrollX < 0 {
				maxScrollX = 0
			}
			win.ScrollX = int32(scrollPct * float64(maxScrollX))
			g.wm.MarkDirty(g.dragWinID)
		}
	}

	// Hit test on new click or for mouse move
	if !g.dragging && !g.draggingScroll && !g.draggingScrollH {
		windows := g.machine.System.Services.ListWindowsSorted()
		hit := g.wm.HitTest(mx, my, TopBarHeight, windows)

		if justPressed {
			switch hit.Zone {
			case HitZoneTitleBar:
				g.focusAndShow(hit.WinID)
			case HitZoneCloseButton:
				g.closeWindowByID(hit.WinID)
			case HitZonePrevButton:
				g.cycleFocus(-1)
			case HitZoneNextButton:
				g.cycleFocus(+1)
			case HitZoneScrollUp:
				g.adjustScroll(hit.WinID, +WinScrollLineStep, 0)
			case HitZoneScrollDown:
				g.adjustScroll(hit.WinID, -WinScrollLineStep, 0)
			case HitZoneScrollLeft:
				g.adjustScroll(hit.WinID, 0, -WinScrollLineStep)
			case HitZoneScrollRight:
				g.adjustScroll(hit.WinID, 0, +WinScrollLineStep)
			case HitZoneScrollTrack:
				if win := g.machine.System.Services.GetWindowByID(hit.WinID); win != nil {
					_, _, thumbYRel, thumbH, _, _, _, _ := g.getScrollGeometry(win)
					thumbY := int(win.ContRgn.Top) + thumbYRel
					if my >= thumbY && my < thumbY+thumbH {
						g.draggingScroll = true
						g.dragWinID = hit.WinID
						g.scrollGrabY = my - thumbY
					}
				}
			case HitZoneScrollTrackH:
				if win := g.machine.System.Services.GetWindowByID(hit.WinID); win != nil {
					_, _, _, _, _, _, thumbXRel, thumbW := g.getScrollGeometry(win)
					thumbX := int(win.ContRgn.Left) + thumbXRel
					if mx >= thumbX && mx < thumbX+thumbW {
						g.draggingScrollH = true
						g.dragWinID = hit.WinID
						g.scrollGrabX = mx - thumbX
					}
				}
			case HitZoneGrowBox:
				// no-op in v1
			case HitZoneMenuBar:
				// If clicking the title, toggle the menu
				if app := g.luxAppForWinID(hit.WinID); app != nil {
					if win := g.machine.Services().GetWindowByID(hit.WinID); win != nil {
						mem := app.machine.CPU.Memory()
						title, _ := readMenuFromMem(mem, win.MenuTablePtr)
						titleW := measureSystemFontText(title, 1) + 12
						if hit.LocalX >= 50 && hit.LocalX < 50+titleW {
							if g.openMenuWinID == hit.WinID {
								g.openMenuWinID = 0
								g.openMenuIdx = -1
							} else {
								g.openMenuWinID = hit.WinID
								g.openMenuIdx = -1
							}
							g.wm.MarkDirty(hit.WinID)
							return nil
						}
					}
				}
				// Otherwise, close any open menu
				g.openMenuWinID = 0
				g.openMenuIdx = -1
			case HitZoneContent:
				// If a menu item is hovered, execute it
				if g.openMenuWinID == hit.WinID && g.openMenuIdx >= 0 {
					if win := g.machine.Services().GetWindowByID(hit.WinID); win != nil {
						if app := g.luxAppForWinID(hit.WinID); app != nil {
							mem := app.machine.CPU.Memory()
							_, items := readMenuFromMem(mem, win.MenuTablePtr)
							if g.openMenuIdx < len(items) {
								item := items[g.openMenuIdx]
								app.machine.CPU.Push(int32(item.Callback))
								app.machine.CPU.TriggerVector(system.MenuVectorIdx)
								g.openMenuWinID = 0
								g.openMenuIdx = -1
								g.wm.MarkDirty(hit.WinID)
								return nil
							}
						}
					}
				}

				// Close any open menu first
				if g.openMenuWinID != 0 {
					g.openMenuWinID = 0
					g.openMenuIdx = -1
				}
				g.focusAndShow(hit.WinID)
				if hit.WinID == g.launcherWinID {
					g.handleLauncherClick(int32(hit.LocalX), int32(hit.LocalY))
				} else if g.shellApp != nil && hit.WinID == g.shellApp.winID {
					// Shell windows are host-rendered; don't forward clicks
					// into the VM input queue.
				} else if app := g.luxAppForWinID(hit.WinID); app != nil {
					app.machine.QueueMouseButton(int32(hit.LocalX), int32(hit.LocalY), 1, true)
				} else {
					g.machine.Services().QueueMouseButton(int32(hit.LocalX), int32(hit.LocalY), 1, true)
				}
			}
		} else if g.openMenuWinID != 0 && hit.WinID == g.openMenuWinID {
			// Track hover over open menu dropdown
			if win := g.machine.Services().GetWindowByID(g.openMenuWinID); win != nil {
				if app := g.luxAppForWinID(g.openMenuWinID); app != nil {
					mem := app.machine.CPU.Memory()
					_, items := readMenuFromMem(mem, win.MenuTablePtr)

					// Calculate which item is under the mouse
					dropX := int(win.ContRgn.Left) + 4
					dropW := 150
					dropY := int(win.ContRgn.Top)
					
					if mx >= dropX && mx < dropX+dropW {
						itemIdx := (my - dropY - 2) / 16
						if itemIdx < 0 || itemIdx >= len(items) {
							g.openMenuIdx = -1
						} else {
							g.openMenuIdx = itemIdx
						}
					} else {
						g.openMenuIdx = -1
					}
				}
			}
		} else if hit.Zone == HitZoneContent {
			// Mouse move within content area of hit window
			activeID := g.machine.Services().GetActiveWindowID()
			if hit.WinID == activeID {
				if app := g.luxAppForWinID(hit.WinID); app != nil {
					app.machine.QueueMouseMove(int32(hit.LocalX), int32(hit.LocalY))
				} else {
					g.machine.Services().QueueMouseMove(int32(hit.LocalX), int32(hit.LocalY))
				}
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
		hit := g.wm.HitTest(mx, my, TopBarHeight, windows)
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

	// Detect resolution changes and trigger re-layout
	sw := int(g.machine.System.ScreenWidth())
	sh := int(g.machine.System.ScreenHeight())
	if sw != g.screenWidth || sh != g.screenHeight {
		g.screenWidth = sw
		g.screenHeight = sh
		// If we have panes, clear them to force a re-layout at the bottom of Update
		g.machine.Services().ClearPanes()
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
			contentH := int32(sh - TopBarHeight)
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
	g.drawDeskTopBackground(screen, sw, sh)

	// Get all windows for sync but only render visible panes
	windows := g.machine.System.Services.ListWindowsSorted()
	g.wm.SyncImages(windows)
	activeID := g.machine.Services().GetActiveWindowID()

	// Refresh content for host-rendered windows (launcher, shell)
	if g.launcherWinID != 0 {
		if win := g.machine.Services().GetWindowByID(g.launcherWinID); win != nil {
			if img := g.wm.ContentImage(g.launcherWinID); img != nil {
				g.drawLauncherContent(win, img)
			}
		}
	}
	if g.shellApp != nil {
		if win := g.machine.Services().GetWindowByID(g.shellApp.winID); win != nil {
			if img := g.wm.ContentImage(g.shellApp.winID); img != nil {
				g.drawShellContent(win, img)
			}
		}
	}

	// Render only the panes (visible windows in the layout)
	panes := g.machine.Services().ListPanes()
	for _, pane := range panes {
		win := g.machine.Services().GetWindowByID(pane.WinID)
		if win == nil {
			continue
		}
		// Draw window chrome (title bar, border, close/prev/next buttons)
		g.drawWindowChrome(screen, win, win.ID == activeID)
		// Draw window menu bar (if present)
		if app := g.luxAppForWinID(win.ID); app != nil && win.MenuTablePtr != 0 {
			g.drawWindowMenuBar(screen, win, app)
		}
		// Draw window content from cached image
		if img := g.wm.ContentImage(win.ID); img != nil {
			op := &ebiten.DrawImageOptions{}
			op.GeoM.Translate(float64(win.ContRgn.Left), float64(win.ContRgn.Top))
			screen.DrawImage(img, op)
		}
		// Scrollbars paint *after* the framebuffer so the gutter is visible
		// (otherwise the per-window content image overdraws it).
		g.drawWindowScrollbars(screen, win)
		// Draw open menu dropdown (if one is open on this window)
		if app := g.luxAppForWinID(win.ID); app != nil && g.openMenuWinID == win.ID {
			g.drawOpenMenuDropdown(screen, win, app)
		}
	}

	// Draw Mac-style Top menubar
	ebitenutil.DrawRect(screen, 0, 0, float64(sw), float64(TopBarHeight), color.White)
	ebitenutil.DrawLine(screen, 0, float64(TopBarHeight), float64(sw), float64(TopBarHeight), color.Black)
	textY := (TopBarHeight - shellFontH) / 2
	drawShellText(screen, "%", 4, textY, color.Black)

	winName := "Cloister"

	nameX := (sw - measureSystemFontText(winName, 1)) / 2
	drawShellText(screen, winName, nameX, textY, color.Black)

	timeStr := time.Now().Format(clockFormatString(g.clockFormat))
	drawShellText(screen, timeStr, sw-measureSystemFontText(timeStr, 1)-4, textY, color.Black)

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
	if g.mouseY >= TopBarHeight {
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

		// Draw debug info with system font
		ebitenutil.DrawRect(screen, 0, TopBarHeight, 250, 400, color.RGBA{0, 0, 0, 180})
		drawSystemFontText(screen, msg, 4, TopBarHeight+4, 1, color.White)
	}
}

func (g *Game) drawWindowMenuBar(screen *ebiten.Image, win *system.WindowRecord, app *LuxApp) {
	if win.MenuTablePtr == 0 || app == nil {
		return
	}

	mem := app.machine.CPU.Memory()
	title, items := readMenuFromMem(mem, win.MenuTablePtr)

	if title == "" || len(items) == 0 {
		return
	}

	// Menu bar is integrated into the chrome (title bar).
	// Chrome is at struc.Top + 1, WinChromeHeight tall.
	menuBarY := int(win.StrucRgn.Top) + 1
	menuBarX := int(win.StrucRgn.Left) + 50 // Offset by 50 to avoid close/arrow buttons

	// Draw menu title on the left of the chrome
	titleX := menuBarX
	titleY := menuBarY + (system.WinChromeHeight-shellFontH)/2

	// Highlight title if menu is open
	if g.openMenuWinID == win.ID {
		titleW := measureSystemFontText(title, 1) + 8
		ebitenutil.DrawRect(screen, float64(titleX-2), float64(titleY-1),
			float64(titleW), float64(shellFontH+2), color.RGBA{100, 100, 100, 255})
		drawShellText(screen, title, titleX, titleY, color.White)
	} else {
		drawShellText(screen, title, titleX, titleY, color.Black)
	}
	}
func (g *Game) drawOpenMenuDropdown(screen *ebiten.Image, win *system.WindowRecord, app *LuxApp) {
	if g.openMenuWinID != win.ID || app == nil {
		return
	}

	mem := app.machine.CPU.Memory()
	_, items := readMenuFromMem(mem, win.MenuTablePtr)

	if len(items) == 0 {
		return
	}

	// Dropdown box: white background with black border, items spaced vertically
	dropX := int(win.ContRgn.Left) + 4
	dropY := int(win.ContRgn.Top)
	dropW := 150
	dropH := len(items)*16 + 4

	ebitenutil.DrawRect(screen, float64(dropX), float64(dropY),
		float64(dropW), float64(dropH), color.White)
	strokeRect(screen, float32(dropX), float32(dropY), float32(dropW), float32(dropH), color.Black)

	// Draw items
	for i, item := range items {
		itemY := dropY + 2 + i*16
		itemH := 14

		// Hover highlight
		if i == g.openMenuIdx {
			ebitenutil.DrawRect(screen, float64(dropX), float64(itemY),
				float64(dropW), float64(itemH),
				color.RGBA{100, 149, 237, 255}) // cornflower blue
		}

		drawShellText(screen, item.Text, dropX+4, itemY, color.Black)
	}
}

func (g *Game) drawWindowChrome(screen *ebiten.Image, win *system.WindowRecord, isActive bool) {
	struc := win.StrucRgn
	cont := win.ContRgn

	// Outer border (1px all sides including chrome)
	ebitenutil.DrawRect(screen, float64(struc.Left), float64(struc.Top), float64(struc.Width()), float64(struc.Height()), color.RGBA{0, 0, 0, 255})

	// Title bar area: between struc.Top and cont.Top (minus border)
	titleX := float64(struc.Left + system.WinBorderWidth)
	titleY := float64(struc.Top + system.WinBorderWidth)
	titleW := float64(struc.Width() - 2*system.WinBorderWidth)
	titleH := float64(cont.Top - struc.Top - system.WinBorderWidth)

	// Title bar fill
	var titleClr color.RGBA
	if isActive {
		titleClr = color.RGBA{170, 170, 170, 255} // medium gray
	} else {
		titleClr = color.RGBA{210, 210, 210, 255} // light gray
	}
	ebitenutil.DrawRect(screen, titleX, titleY, titleW, titleH, titleClr)

	// Close button (red square with darker border).
	btnY := titleY + float64(WinCloseBtnY) - float64(WinCloseBtnSize)/2
	closeX := titleX + float64(WinCloseBtnX) - float64(WinCloseBtnSize)/2
	ebitenutil.DrawRect(screen, closeX, btnY, float64(WinCloseBtnSize), float64(WinCloseBtnSize), color.RGBA{204, 51, 51, 255})
	ebitenutil.DrawRect(screen, closeX+1, btnY+1, float64(WinCloseBtnSize-2), float64(WinCloseBtnSize-2), color.RGBA{170, 0, 0, 255})

	// Prev / Next window buttons
	for _, b := range [...]struct {
		cx    int
		label string
	}{
		{WinPrevBtnX, "<"},
		{WinNextBtnX, ">"},
	} {
		bx := titleX + float64(b.cx) - float64(WinCloseBtnSize)/2
		ebitenutil.DrawRect(screen, bx, btnY, float64(WinCloseBtnSize), float64(WinCloseBtnSize), color.RGBA{170, 170, 170, 255})
		ebitenutil.DrawRect(screen, bx+1, btnY+1, float64(WinCloseBtnSize-2), float64(WinCloseBtnSize-2), color.RGBA{220, 220, 220, 255})
		drawShellText(screen, b.label, int(bx)+1, int(btnY)-2, color.Black)
	}

	// Window title centered in chrome
	nameX := int(titleX) + (int(titleW)-measureSystemFontText(win.Name, 1))/2
	nameY := int(titleY) + (int(titleH)-shellFontH)/2
	drawShellText(screen, win.Name, nameX, nameY, color.Black)

	// Horizontal line separating chrome from content
	ebitenutil.DrawLine(screen, float64(cont.Left), float64(cont.Top-1), float64(cont.Right), float64(cont.Top-1), color.RGBA{0, 0, 0, 255})
}

// getScrollGeometry calculates the track and thumb positions for a window.
// trackY/trackX are relative to cont.Top / cont.Left.
func (g *Game) getScrollGeometry(win *system.WindowRecord) (trackY, trackH, thumbY, thumbH, trackX, trackW, thumbX, thumbW int) {
	// Vertical
	trackH = int(win.ContRgn.Height()) - system.WinScrollbarSize - 2*WinScrollArrowH
	trackY = WinScrollArrowH

	if win.ContentHeight <= win.ContRgn.Height() {
		thumbY, thumbH = trackY, trackH
	} else {
		thumbH = int(float64(win.ContRgn.Height()) / float64(win.ContentHeight) * float64(trackH))
		if thumbH < 10 {
			thumbH = 10
		}
		maxScroll := float64(win.ContentHeight - win.ContRgn.Height())
		scrollPct := float64(win.ScrollY) / maxScroll
		thumbY = trackY + int((1.0-scrollPct)*float64(trackH-thumbH))
	}

	// Horizontal
	trackW = int(win.ContRgn.Width()) - system.WinScrollbarSize - 2*WinScrollArrowH
	trackX = WinScrollArrowH

	if win.ContentWidth <= win.ContRgn.Width() {
		thumbX, thumbW = trackX, trackW
	} else {
		thumbW = int(float64(win.ContRgn.Width()) / float64(win.ContentWidth) * float64(trackW))
		if thumbW < 10 {
			thumbW = 10
		}
		maxScrollX := float64(win.ContentWidth - win.ContRgn.Width())
		scrollPctX := float64(win.ScrollX) / maxScrollX
		// 0% scroll -> Left of track, 100% scroll -> Right of track
		thumbX = trackX + int(scrollPctX*float64(trackW-thumbW))
	}

	return
}

// drawWindowScrollbars paints the Right-edge vertical track + arrows, the
// Bottom-edge horizontal track, and the grow box.
func (g *Game) drawWindowScrollbars(screen *ebiten.Image, win *system.WindowRecord) {
	cont := win.ContRgn
	black := color.RGBA{0, 0, 0, 255}
	gray := color.RGBA{170, 170, 170, 255}
	white := color.RGBA{255, 255, 255, 255}

	_, _, thumbYRel, thumbH, _, _, thumbXRel, thumbW := g.getScrollGeometry(win)

	// Vertical scrollbar gutter (excluding grow box and arrows)
	vbX := int(cont.Right) - system.WinScrollbarSize
	vbY := int(cont.Top)
	vbH := int(cont.Height()) - system.WinScrollbarSize - 1
	ebitenutil.DrawRect(screen, float64(vbX), float64(vbY), float64(system.WinScrollbarSize), float64(vbH), white)
	drawDitherFill(screen, vbX, vbY+WinScrollArrowH, system.WinScrollbarSize, vbH-2*WinScrollArrowH)
	// Up/down arrow buttons
	ebitenutil.DrawRect(screen, float64(vbX), float64(vbY), float64(system.WinScrollbarSize), float64(WinScrollArrowH), gray)
	ebitenutil.DrawRect(screen, float64(vbX), float64(vbY+vbH-WinScrollArrowH), float64(system.WinScrollbarSize), float64(WinScrollArrowH), gray)
	// Arrow glyphs
	drawShellText(screen, "^", vbX+4, vbY+1, black)
	drawShellText(screen, "v", vbX+4, vbY+vbH-WinScrollArrowH+1, black)

	thumbY := int(cont.Top) + thumbYRel
	ebitenutil.DrawRect(screen, float64(vbX+1), float64(thumbY), float64(system.WinScrollbarSize-2), float64(thumbH), gray)
	strokeRect(screen, float32(vbX+1), float32(thumbY), float32(system.WinScrollbarSize-2), float32(thumbH), black)

	// Border between content and scrollbar
	ebitenutil.DrawLine(screen, float64(vbX), float64(vbY), float64(vbX), float64(vbY+vbH), black)

	// Horizontal scrollbar gutter (excluding grow box)
	hbX := int(cont.Left)
	hbY := int(cont.Bottom) - system.WinScrollbarSize - 1
	hbW := int(cont.Width()) - system.WinScrollbarSize
	ebitenutil.DrawRect(screen, float64(hbX), float64(hbY), float64(hbW), float64(system.WinScrollbarSize), white)
	drawDitherFill(screen, hbX+WinScrollArrowH, hbY, hbW-2*WinScrollArrowH, system.WinScrollbarSize)
	ebitenutil.DrawRect(screen, float64(hbX), float64(hbY), float64(WinScrollArrowH), float64(system.WinScrollbarSize), gray)
	ebitenutil.DrawRect(screen, float64(hbX+hbW-WinScrollArrowH), float64(hbY), float64(WinScrollArrowH), float64(system.WinScrollbarSize), gray)
	drawShellText(screen, "<", hbX+4, hbY+1, black)
	drawShellText(screen, ">", hbX+hbW-WinScrollArrowH+4, hbY+1, black)

	thumbX := int(cont.Left) + thumbXRel
	ebitenutil.DrawRect(screen, float64(thumbX), float64(hbY+1), float64(thumbW), float64(system.WinScrollbarSize-2), gray)
	strokeRect(screen, float32(thumbX), float32(hbY+1), float32(thumbW), float32(system.WinScrollbarSize-2), black)

	ebitenutil.DrawLine(screen, float64(hbX), float64(hbY), float64(hbX+hbW), float64(hbY), black)

	// Grow box (Bottom-Right corner, decorative)
	gbX := int(cont.Right) - system.WinScrollbarSize
	gbY := int(cont.Bottom) - system.WinScrollbarSize
	ebitenutil.DrawRect(screen, float64(gbX), float64(gbY), float64(system.WinScrollbarSize), float64(system.WinScrollbarSize), gray)
	strokeRect(screen, float32(gbX), float32(gbY), float32(system.WinScrollbarSize), float32(system.WinScrollbarSize), black)
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
	appleX, appleY := 5, TopBarHeight
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
// LeftBtn/RightBtn are the ‹ ›-style steppers (rows 0/3/4).
// pillRect is the ON/OFF toggle (row 1) or color swatch+arrow combo (row 2).
func (g *Game) settingsRowGeom(idx int) (rowRect, LeftBtn, RightBtn, pillRect rect) {
	modalX, modalY, _, _, _ := g.settingsModalGeom()
	rowY := modalY + settingsTitleBarH + 8 + idx*settingsRowSpacing
	rowRect = rect{x: modalX + 10, y: rowY, w: 260, h: settingsRowH}
	LeftBtn = rect{x: modalX + 145, y: rowY, w: 14, h: settingsRowH}
	RightBtn = rect{x: modalX + 200, y: rowY, w: 14, h: settingsRowH}
	pillRect = rect{x: modalX + 145, y: rowY, w: 70, h: settingsRowH}
	return
}

// applySettingsAction mutates the host setting at row idx by direction:
// dir -1 = step Left, +1 = step Right, 0 = primary (toggle/cycle).
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
		rowRect, LeftBtn, RightBtn, pillRect := g.settingsRowGeom(i)
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
			if LeftBtn.contains(mx, my) {
				g.applySettingsAction(i, -1)
			} else if RightBtn.contains(mx, my) {
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
	listX, listY := 50, TopBarHeight+50
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
	subX, subY := 260, TopBarHeight+50+g.windowsIdx*20
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
	x, y := 5, TopBarHeight
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

// drawSettingsRow renders one row of the settings panel: label on the Left,
// control(s) on the Right. The hit rects for each control come from
// settingsRowGeom so the renderer never goes out of sync with the input handler.
func (g *Game) drawSettingsRow(screen *ebiten.Image, idx int, label string) {
	rowRect, LeftBtn, RightBtn, pillRect := g.settingsRowGeom(idx)

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
		g.drawStepper(screen, LeftBtn, RightBtn, fgClr, fmt.Sprintf("%d", g.textScale))
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
		g.drawStepper(screen, LeftBtn, RightBtn, fgClr, bgPatternLabel(g.bgPattern))
	case 4: // Clock: ‹ label ›
		g.drawStepper(screen, LeftBtn, RightBtn, fgClr, clockFormatLabel(g.clockFormat))
	}
}

// drawStepper renders a ‹ value › control. The button rects come from
// settingsRowGeom and are also the input hit rects.
func (g *Game) drawStepper(screen *ebiten.Image, LeftBtn, RightBtn rect, fg color.Color, value string) {
	textY := LeftBtn.y + (LeftBtn.h-shellFontH)/2
	drawShellText(screen, "<", LeftBtn.x+3, textY, fg)
	drawShellText(screen, value, LeftBtn.x+LeftBtn.w+4, textY, fg)
	drawShellText(screen, ">", RightBtn.x+3, textY, fg)
}

// drawPill renders an ON/OFF style pill button.
func (g *Game) drawPill(screen *ebiten.Image, r rect, label string) {
	ebitenutil.DrawRect(screen, float64(r.x), float64(r.y), float64(r.w), float64(r.h), color.RGBA{220, 220, 220, 255})
	strokeRect(screen, float32(r.x), float32(r.y), float32(r.w), float32(r.h), color.Black)
	textX := r.x + (r.w-measureSystemFontText(label, 1))/2
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

		label := fmt.Sprintf("%s", win.Name)
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
	textX := x + (btnW-measureSystemFontText(label, 1))/2
	drawShellText(screen, label, textX, y+(btnH-shellFontH)/2, color.Black)
}

// adjustScroll bumps the target window's ScrollY or ScrollX by delta.
func (g *Game) adjustScroll(id system.WindowID, deltaY, deltaX int32) {
	win := g.machine.Services().GetWindowByID(id)
	if win == nil {
		return
	}
	if deltaY != 0 {
		win.ScrollY += deltaY
		maxScroll := win.ContentHeight - win.ContRgn.Height()
		if maxScroll < 0 {
			maxScroll = 0
		}
		if win.ScrollY > maxScroll {
			win.ScrollY = maxScroll
		}
		if win.ScrollY < 0 {
			win.ScrollY = 0
		}
	}
	if deltaX != 0 {
		win.ScrollX += deltaX
		maxScrollX := win.ContentWidth - win.ContRgn.Width()
		if maxScrollX < 0 {
			maxScrollX = 0
		}
		if win.ScrollX > maxScrollX {
			win.ScrollX = maxScrollX
		}
		if win.ScrollX < 0 {
			win.ScrollX = 0
		}
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
	contentH := sh - int32(TopBarHeight)
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
		g.machine.System.SetOSResolution(int32(w), int32(h))
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
	memFlag := flag.Int("mem", 12, "VM memory size in megabytes (max 128)")
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
