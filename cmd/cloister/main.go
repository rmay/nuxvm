package main

import (
	"flag"
	"fmt"
	"image/color"
	"os"
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

// defaultBootPath is the on-disk Lux program cloister loads when invoked
// without a positional argument. It's relative to cwd so editing lib/boot.lux
// in the repo and re-running cloister picks up the change without a rebuild.
const defaultBootPath = "lib/boot.lux"
const topBarHeight = 24

type ShellMode int

const (
	ShellNormal ShellMode = iota
	ShellAppleMenu
	ShellSettings
	ShellWindowsList
	ShellWindowRowMenu
	ShellQuitConfirm
)

type Game struct {
	machine      *system.Machine
	wm           *WindowManager
	showDebug    bool
	bootTimer    int
	mouseX       int
	mouseY       int
	dragging     bool
	dragWinID    system.WindowID
	dragOffX     int
	dragOffY     int
	wasLeftDown  bool
	clearColor   color.RGBA
	textScale    int // 1-4

	// Shell/menu state
	shellMode        ShellMode
	appleIdx         int // 0=Settings, 1=Windows, 2=Quit
	settingsIdx      int // which setting row (0=scale, 1=debug, 2=color)
	windowsIdx       int // which window in list
	windowRowMenu    int // 0=Focus, 1=Close
	windowRowMenuWin system.WindowID // which window the row menu is over
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

func drawMouse(screen *ebiten.Image, x, y int) {
	white := color.RGBA{255, 255, 255, 255}
	screen.Set(x, y-1, white)
	screen.Set(x, y+1, white)
	screen.Set(x-1, y, white)
	screen.Set(x+1, y, white)
	screen.Set(x, y, color.RGBA{0, 0, 0, 255})
}

func (g *Game) loadAndRun(filename string) {
	bytecode, err := lux.LoadProgram(filename)
	if err != nil {
		fmt.Sprintf("Error loading %s: %v", filename, err)
		return
	}

	userMemStart := g.machine.CPU.UserMemoryStart()
	mem := g.machine.CPU.Memory()

	if int(userMemStart)+len(bytecode) > len(mem) {
		fmt.Sprintf("Error: %s too large", filename)
		return
	}

	copy(mem[userMemStart:], bytecode)
	g.machine.CPU.WriteVector(0, userMemStart)
	g.machine.CPU.TriggerVector(0)
	fmt.Sprintf("Loaded and started %s", filename)
}

func (g *Game) Update() error {
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
		g.handleShellInput()
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

	// Cycle through windows
	if inpututil.IsKeyJustPressed(ebiten.KeyF2) {
		g.machine.Services().CycleWindows()
	}

	// Queue keyboard input through the service manager (only if shell is not active)
	if g.shellMode == ShellNormal {
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
				g.machine.Services().FocusWindow(hit.WinID)
				if win := g.machine.System.Services.GetWindowByID(hit.WinID); win != nil {
					g.dragging = true
					g.dragWinID = hit.WinID
					g.dragOffX = mx - int(win.X)
					g.dragOffY = my - topBarHeight - int(win.Y)
				}
			case HitZoneCloseButton:
				g.machine.Services().CloseWindow(hit.WinID)
			case HitZoneContent:
				g.machine.Services().FocusWindow(hit.WinID)
				g.machine.Services().QueueMouseButton(int32(hit.LocalX), int32(hit.LocalY), 1, true)
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

	// Fill background with clear color
	screen.Fill(g.clearColor)

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
		// Draw window chrome (title bar, border, close button)
		g.drawWindowChrome(screen, win, win.ID == activeID)
		// Draw window content from cached image
		if img := g.wm.ContentImage(win.ID); img != nil {
			op := &ebiten.DrawImageOptions{}
			op.GeoM.Translate(float64(win.X), float64(int(win.Y)+topBarHeight+WinChromeHeight))
			screen.DrawImage(img, op)
		}
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

	timeStr := time.Now().Format("15:04")
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
		titleClr = color.RGBA{170, 170, 170, 255}  // medium gray
	} else {
		titleClr = color.RGBA{210, 210, 210, 255}  // light gray
	}
	ebitenutil.DrawRect(screen, x, chromeTopY, w, chromeH, titleClr)

	// Close button (red square with darker border)
	btnX := x + float64(WinCloseBtnX) - float64(WinCloseBtnSize)/2
	btnY := chromeTopY + float64(WinCloseBtnY) - float64(WinCloseBtnSize)/2
	ebitenutil.DrawRect(screen, btnX, btnY, float64(WinCloseBtnSize), float64(WinCloseBtnSize), color.RGBA{204, 51, 51, 255})
	ebitenutil.DrawRect(screen, btnX+1, btnY+1, float64(WinCloseBtnSize-2), float64(WinCloseBtnSize-2), color.RGBA{170, 0, 0, 255})

	// Window title centered in chrome
	nameX := int(x) + (int(w)-len(win.Name)*shellFontW)/2
	nameY := int(chromeTopY) + (WinChromeHeight-shellFontH)/2
	drawShellText(screen, win.Name, nameX, nameY, color.Black)

	// Horizontal line separating chrome from content
	ebitenutil.DrawLine(screen, x, chromeTopY+chromeH, x+w, chromeTopY+chromeH, color.RGBA{0, 0, 0, 255})
}

// ============= Shell/Menu Input Handler =============

func (g *Game) handleShellInput() {
	mx, my := ebiten.CursorPosition()
	leftDown := ebiten.IsMouseButtonPressed(ebiten.MouseButtonLeft)
	justPressed := leftDown && !g.wasLeftDown

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
		g.handleAppleMenuInput()
	case ShellSettings:
		g.handleSettingsInput()
	case ShellWindowsList:
		g.handleWindowsListInput()
	case ShellWindowRowMenu:
		g.handleWindowRowMenuInput()
	case ShellQuitConfirm:
		g.handleQuitConfirmInput()
	}
}

func (g *Game) handleAppleMenuInput() {
	// Up/Down navigation
	if inpututil.IsKeyJustPressed(ebiten.KeyUp) {
		g.appleIdx--
		if g.appleIdx < 0 {
			g.appleIdx = 2
		}
	}
	if inpututil.IsKeyJustPressed(ebiten.KeyDown) {
		g.appleIdx = (g.appleIdx + 1) % 3
	}

	// Enter to select
	if inpututil.IsKeyJustPressed(ebiten.KeyEnter) {
		switch g.appleIdx {
		case 0:
			g.shellMode = ShellSettings
			g.settingsIdx = 0
		case 1:
			g.shellMode = ShellWindowsList
			g.windowsIdx = 0
		case 2:
			g.shellMode = ShellQuitConfirm
		}
	}

	// Mouse hover and click
	mx, my := ebiten.CursorPosition()
	appleX, appleY := 20, topBarHeight+10
	itemHeight := 18
	for i := 0; i < 3; i++ {
		itemY := appleY + i*itemHeight
		if mx >= appleX && mx < appleX+100 && my >= itemY && my < itemY+itemHeight {
			g.appleIdx = i
			if ebiten.IsMouseButtonPressed(ebiten.MouseButtonLeft) && !g.wasLeftDown {
				switch i {
				case 0:
					g.shellMode = ShellSettings
					g.settingsIdx = 0
				case 1:
					g.shellMode = ShellWindowsList
					g.windowsIdx = 0
				case 2:
					g.shellMode = ShellQuitConfirm
				}
			}
		}
	}
}

func (g *Game) handleSettingsInput() {
	// Up/Down navigation
	if inpututil.IsKeyJustPressed(ebiten.KeyUp) {
		g.settingsIdx--
		if g.settingsIdx < 0 {
			g.settingsIdx = 2
		}
	}
	if inpututil.IsKeyJustPressed(ebiten.KeyDown) {
		g.settingsIdx = (g.settingsIdx + 1) % 3
	}

	// Left/Right to adjust settings
	if inpututil.IsKeyJustPressed(ebiten.KeyLeft) {
		switch g.settingsIdx {
		case 0:
			if g.textScale > 1 {
				g.textScale--
			}
		case 1:
			// debug toggle handled with space
		case 2:
			// color cycle - handled below
		}
	}
	if inpututil.IsKeyJustPressed(ebiten.KeyRight) {
		switch g.settingsIdx {
		case 0:
			if g.textScale < 4 {
				g.textScale++
			}
		}
	}

	// Space to toggle debug
	if inpututil.IsKeyJustPressed(ebiten.KeySpace) && g.settingsIdx == 1 {
		g.showDebug = !g.showDebug
	}

	// Enter to cycle through color presets
	if inpututil.IsKeyJustPressed(ebiten.KeyEnter) && g.settingsIdx == 2 {
		cycleColor := func() {
			if g.clearColor.R == 255 && g.clearColor.G == 255 && g.clearColor.B == 255 {
				g.clearColor = color.RGBA{220, 220, 220, 255} // light gray
			} else if g.clearColor.R == 220 && g.clearColor.G == 220 && g.clearColor.B == 220 {
				g.clearColor = color.RGBA{173, 216, 230, 255} // light blue
			} else if g.clearColor.R == 173 && g.clearColor.G == 216 && g.clearColor.B == 230 {
				g.clearColor = color.RGBA{0, 0, 0, 255} // black
			} else {
				g.clearColor = color.RGBA{255, 255, 255, 255} // white
			}
		}
		cycleColor()
	}

	// Escape goes back to Apple menu
	if inpututil.IsKeyJustPressed(ebiten.KeyEscape) {
		g.shellMode = ShellAppleMenu
		g.appleIdx = 0
	}
}

func (g *Game) handleWindowsListInput() {
	wins := g.machine.Services().ListWindowsSorted()
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
	}

	// Mouse hover and click
	mx, my := ebiten.CursorPosition()
	listX, listY := 50, topBarHeight+50
	rowHeight := 20
	for i, win := range wins {
		itemY := listY + i*rowHeight
		if mx >= listX && mx < listX+200 && my >= itemY && my < itemY+rowHeight {
			g.windowsIdx = i
			if ebiten.IsMouseButtonPressed(ebiten.MouseButtonLeft) && !g.wasLeftDown {
				g.shellMode = ShellWindowRowMenu
				g.windowRowMenu = 0
				g.windowRowMenuWin = win.ID
			}
		}
	}

	// Escape goes back to Apple menu
	if inpututil.IsKeyJustPressed(ebiten.KeyEscape) {
		g.shellMode = ShellAppleMenu
		g.appleIdx = 1
	}
}

func (g *Game) handleWindowRowMenuInput() {
	// Left/Right or Up/Down to switch between Focus/Close
	if inpututil.IsKeyJustPressed(ebiten.KeyLeft) || inpututil.IsKeyJustPressed(ebiten.KeyUp) {
		g.windowRowMenu = 0
	}
	if inpututil.IsKeyJustPressed(ebiten.KeyRight) || inpututil.IsKeyJustPressed(ebiten.KeyDown) {
		g.windowRowMenu = 1
	}

	// Enter to execute
	if inpututil.IsKeyJustPressed(ebiten.KeyEnter) {
		switch g.windowRowMenu {
		case 0: // Focus
			g.machine.Services().FocusWindow(g.windowRowMenuWin)
			g.shellMode = ShellNormal
		case 1: // Close
			g.machine.Services().CloseWindow(g.windowRowMenuWin)
			g.shellMode = ShellWindowsList
			g.windowsIdx = 0
		}
	}

	// Mouse hover and click
	mx, my := ebiten.CursorPosition()
	subX, subY := 260, topBarHeight+50+g.windowsIdx*20
	itemWidth := 60
	for i := 0; i < 2; i++ {
		x := subX + i*itemWidth
		if mx >= x && mx < x+itemWidth && my >= subY && my < subY+20 {
			g.windowRowMenu = i
			if ebiten.IsMouseButtonPressed(ebiten.MouseButtonLeft) && !g.wasLeftDown {
				switch i {
				case 0: // Focus
					g.machine.Services().FocusWindow(g.windowRowMenuWin)
					g.shellMode = ShellNormal
				case 1: // Close
					g.machine.Services().CloseWindow(g.windowRowMenuWin)
					g.shellMode = ShellWindowsList
					g.windowsIdx = 0
				}
			}
		}
	}

	// Escape goes back to Windows list
	if inpututil.IsKeyJustPressed(ebiten.KeyEscape) {
		g.shellMode = ShellWindowsList
	}
}

func (g *Game) handleQuitConfirmInput() {
	// Enter or Y to confirm quit
	if inpututil.IsKeyJustPressed(ebiten.KeyEnter) || inpututil.IsKeyJustPressed(ebiten.KeyY) {
		os.Exit(0)
	}

	// N or Escape to cancel
	if inpututil.IsKeyJustPressed(ebiten.KeyN) || inpututil.IsKeyJustPressed(ebiten.KeyEscape) {
		g.shellMode = ShellAppleMenu
		g.appleIdx = 2
	}

	// Mouse click on Quit button
	mx, my := ebiten.CursorPosition()
	sw := int(g.machine.System.ScreenWidth())
	sh := int(g.machine.System.ScreenHeight())
	quitBtnX := (sw - 200) / 2
	quitBtnY := (sh - 100) / 2 + 40
	if mx >= quitBtnX && mx < quitBtnX+90 && my >= quitBtnY && my < quitBtnY+20 {
		if ebiten.IsMouseButtonPressed(ebiten.MouseButtonLeft) && !g.wasLeftDown {
			os.Exit(0)
		}
	}

	// Cancel button
	cancelBtnX := quitBtnX + 100
	if mx >= cancelBtnX && mx < cancelBtnX+90 && my >= quitBtnY && my < quitBtnY+20 {
		if ebiten.IsMouseButtonPressed(ebiten.MouseButtonLeft) && !g.wasLeftDown {
			g.shellMode = ShellAppleMenu
			g.appleIdx = 2
		}
	}
}

// ============= Shell/Menu Rendering =============

func (g *Game) drawAppleMenu(screen *ebiten.Image) {
	items := []string{"Settings", "Current Windows", "Quit"}
	x, y := 20, topBarHeight+10
	itemHeight := 18
	itemWidth := 120

	for i, item := range items {
		itemY := y + i*itemHeight
		bgClr := color.RGBA{180, 180, 180, 255}
		fgClr := color.Color(color.Black)
		if i == g.appleIdx {
			bgClr = color.RGBA{80, 80, 80, 255}
			fgClr = color.White
		}
		ebitenutil.DrawRect(screen, float64(x), float64(itemY), float64(itemWidth), float64(itemHeight), bgClr)
		strokeRect(screen, float32(x), float32(itemY), float32(itemWidth), float32(itemHeight), color.Black)
		drawShellText(screen, item, x+5, itemY+(itemHeight-shellFontH)/2, fgClr)
	}
}

func (g *Game) drawSettingsModal(screen *ebiten.Image) {
	sw := int(g.machine.System.ScreenWidth())
	sh := int(g.machine.System.ScreenHeight())

	modalW, modalH := 280, 160
	modalX := (sw - modalW) / 2
	modalY := (sh - modalH) / 2

	ebitenutil.DrawRect(screen, float64(modalX), float64(modalY), float64(modalW), float64(modalH), color.RGBA{200, 200, 200, 255})
	strokeRect(screen, float32(modalX), float32(modalY), float32(modalW), float32(modalH), color.Black)

	drawShellText(screen, "Settings", modalX+10, modalY+10, color.Black)

	rowY := modalY + 40
	g.drawSettingsRow(screen, modalX+10, rowY, "Text Scale:", 0)
	drawShellText(screen, "<", modalX+150, rowY+(20-shellFontH)/2, color.Black)
	drawShellText(screen, string(rune('0'+byte(g.textScale))), modalX+165, rowY+(20-shellFontH)/2, color.Black)
	drawShellText(screen, ">", modalX+180, rowY+(20-shellFontH)/2, color.Black)

	rowY += 30
	g.drawSettingsRow(screen, modalX+10, rowY, "Debug:", 1)
	if g.showDebug {
		drawShellText(screen, "ON", modalX+150, rowY+(20-shellFontH)/2, color.Black)
	} else {
		drawShellText(screen, "OFF", modalX+150, rowY+(20-shellFontH)/2, color.Black)
	}

	rowY += 30
	g.drawSettingsRow(screen, modalX+10, rowY, "Color:", 2)
	ebitenutil.DrawRect(screen, float64(modalX+150), float64(rowY), 20, 16, g.clearColor)
	strokeRect(screen, float32(modalX+150), float32(rowY), 20, 16, color.Black)
}

func (g *Game) drawSettingsRow(screen *ebiten.Image, x, y int, label string, idx int) {
	bgClr := color.RGBA{180, 180, 180, 255}
	fgClr := color.Color(color.Black)
	if idx == g.settingsIdx {
		bgClr = color.RGBA{80, 80, 80, 255}
		fgClr = color.White
	}
	ebitenutil.DrawRect(screen, float64(x), float64(y), 260, 20, bgClr)
	drawShellText(screen, label, x+5, y+(20-shellFontH)/2, fgClr)
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
	widthFlag := flag.Int("w", 0, "Screen width override (0 = defer to boot.lux)")
	heightFlag := flag.Int("h", 0, "Screen height override (0 = defer to boot.lux)")
	scaleFlag := flag.Int("scale", 0, "Window pixel scale override (0 = defer to boot.lux)")
	flag.Parse()

	// Detect whether the user passed -w / -h / -scale explicitly, so those
	// values override whatever boot.lux writes during its first tick.
	explicit := map[string]bool{}
	flag.Visit(func(f *flag.Flag) { explicit[f.Name] = true })

	if *memFlag > 128 {
		fmt.Println("Memory size capped at 128MB")
		*memFlag = 128
	}

	// Load boot program
	bootPath := defaultBootPath
	if flag.NArg() > 0 {
		bootPath = flag.Arg(0)
	}
	bootBytecode, err := lux.LoadProgram(bootPath)
	if err != nil {
		fmt.Fprintf(os.Stderr, "Error loading %s: %v\n", bootPath, err)
		os.Exit(1)
	}

	machine := system.NewMachine(bootBytecode, uint32(*memFlag)*1024*1024)

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
	// code (e.g. boot.lux setting SCR_W / SCR_H / TEXT cell-size) has a
	// chance to populate the MMIO registers before we size the host window.
	if _, err := machine.Tick(); err != nil {
		fmt.Fprintf(os.Stderr, "Error during boot tick: %v\n", err)
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
		machine:   machine,
		wm:        NewWindowManager(machine.System.Services),
		bootTimer: 60,
		textScale: 1,
		clearColor: color.RGBA{255, 255, 255, 255}, // white
	}

	// Create default window via the service manager (ID for future use)
	_, _ = machine.System.Services.CreateWindow("VM", int32(sw), int32(sh-topBarHeight))

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
