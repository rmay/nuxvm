package main

import (
	"image/color"
	"strings"

	"github.com/hajimehoshi/ebiten/v2"
	"github.com/rmay/nuxvm/pkg/luxrepl"
	"github.com/rmay/nuxvm/pkg/system"
)

// ShellApp is the host-side controller for an embedded luxrepl Terminal Shell
// running inside a Cloister window. There can be more than one (created via
// MAKE-PANE) but for v1 we treat the original as a singleton.
type ShellApp struct {
	winID      system.WindowID
	repl       *luxrepl.REPL
	scrollback []string // recent rendered lines (top-down, oldest first)
	inputLine  string   // current line being typed
	prompt     string
	history    []string
	historyIdx int // -1 if not navigating history
}

const shellMaxScrollback = 500

// openShellApp creates (or focuses) the singleton Shell window.
func (g *Game) openShellApp() {
	if g.shellApp != nil {
		if g.machine.Services().GetWindowByID(g.shellApp.winID) != nil {
			g.machine.Services().FocusWindow(g.shellApp.winID)
			g.machine.Services().LayoutSingle(g.shellApp.winID,
				0, 0, g.machine.System.ScreenWidth(), g.machine.System.ScreenHeight()-int32(TopBarHeight))
			return
		}
		g.shellApp = nil
	}

	sw := g.machine.System.ScreenWidth()
	sh := g.machine.System.ScreenHeight()
	contentH := sh - int32(TopBarHeight)
	if contentH < 100 {
		contentH = 100
	}
	id, _ := g.machine.Services().CreateWindow("Shell", sw, contentH)
	g.machine.Services().FocusWindow(id)
	g.machine.Services().LayoutSingle(id, 0, 0, sw, contentH)

	sa := &ShellApp{
		winID:  id,
		prompt: "lux> ",
	}
	sa.repl = luxrepl.New(func(s string) { sa.appendOutput(s) })
	sa.scrollback = append(sa.scrollback, "LUX REPL 280K  -  type 'help' for commands")
	sa.scrollback = append(sa.scrollback, "Words: MAKE-PANE  DESTROY-PANE  NEXT-PANE  PREV-PANE  QUIT-OS  RESTART-OS")
	g.shellApp = sa
}

// handleChar appends a typed character to the input buffer.
func (sa *ShellApp) handleChar(ch rune) {
	if ch < 0x20 || ch == 0x7F {
		return
	}
	sa.inputLine += string(ch)
	sa.historyIdx = -1
}

// handleBackspace trims the last rune from the input buffer.
func (sa *ShellApp) handleBackspace() {
	if sa.inputLine == "" {
		return
	}
	r := []rune(sa.inputLine)
	sa.inputLine = string(r[:len(r)-1])
	sa.historyIdx = -1
}

// handleUp navigates to the previous history entry.
func (sa *ShellApp) handleUp() {
	if len(sa.history) == 0 {
		return
	}
	if sa.historyIdx == -1 {
		sa.historyIdx = len(sa.history) - 1
	} else if sa.historyIdx > 0 {
		sa.historyIdx--
	}
	sa.inputLine = sa.history[sa.historyIdx]
}

// handleDown navigates to the next history entry.
func (sa *ShellApp) handleDown() {
	if sa.historyIdx == -1 {
		return
	}
	if sa.historyIdx < len(sa.history)-1 {
		sa.historyIdx++
		sa.inputLine = sa.history[sa.historyIdx]
	} else {
		sa.historyIdx = -1
		sa.inputLine = ""
	}
}

// handleEnter commits the current input line. Host words are intercepted and
// run on the Game (not the embedded VM); everything else goes through luxrepl.
func (sa *ShellApp) handleEnter(g *Game) {
	line := sa.inputLine
	sa.inputLine = ""
	sa.historyIdx = -1
	if strings.TrimSpace(line) != "" {
		sa.history = append(sa.history, line)
		if len(sa.history) > 100 {
			sa.history = sa.history[1:]
		}
	}
	sa.appendOutput(sa.prompt + line + "\n")

	// Auto-scroll to bottom
	if win := g.machine.Services().GetWindowByID(sa.winID); win != nil {
		win.ScrollY = 0
	}

	if sa.handleHostWord(g, line) {
		return
	}
	sa.repl.Eval(line)
}

// handleHostWord matches Cloister-managed words (pane and OS lifecycle) that
// the embedded REPL has no way to express. Returns true if the line was
// handled here. Match is case-insensitive against the trimmed line.
func (sa *ShellApp) handleHostWord(g *Game, line string) bool {
	return g.executeHostWord(strings.ToUpper(strings.TrimSpace(line)), sa.winID)
}

func (sa *ShellApp) appendOutput(s string) {
	current := ""
	for _, ch := range s {
		if ch == '\n' {
			sa.scrollback = append(sa.scrollback, current)
			current = ""
		} else {
			current += string(ch)
		}
	}
	if current != "" {
		sa.scrollback = append(sa.scrollback, current)
	}
	if len(sa.scrollback) > shellMaxScrollback {
		sa.scrollback = sa.scrollback[len(sa.scrollback)-shellMaxScrollback:]
	}
}

// drawShellContent renders the scrollback + prompt + input cursor into the
// shell window's content image.
func (g *Game) drawShellContent(win *system.WindowRecord, img *ebiten.Image) {
	if g.shellApp == nil || img == nil {
		return
	}
	img.Fill(color.RGBA{0, 0, 0, 255})
	fg := color.RGBA{255, 255, 255, 255}
	const lineH = 15
	
	// Inner height available for text (excluding scrollbars)
	innerH := int(win.ContRgn.Height()) - system.WinScrollbarSize
	
	display := append([]string{}, g.shellApp.scrollback...)
	display = append(display, g.shellApp.prompt+g.shellApp.inputLine+"_")

	// Total virtual height
	totalH := int32(len(display) * lineH)
	win.ContentHeight = totalH

	// ScrollY=0 is bottom.
	// If totalH <= innerH, we just draw from top (y=2).
	if totalH <= int32(innerH) {
		for i, line := range display {
			drawSystemFontText(img, line, 4, i*lineH+2, 1, fg)
		}
		return
	}

	// If totalH > innerH, viewport is [totalH - innerH - ScrollY, totalH - ScrollY]
	viewBottom := int(totalH) - int(win.ScrollY)
	viewTop := viewBottom - innerH

	for i, line := range display {
		lineY := i * lineH
		if lineY+lineH > viewTop && lineY < viewBottom {
			drawY := lineY - viewTop
			// Don't draw if the baseline+descent would go into the scrollbar gutter
			if drawY < innerH {
				drawSystemFontText(img, line, 4, drawY, 1, fg)
			}
		}
	}
}

// makePane splits the active pane vertically. The right half is a new Shell
// window. For v1 the user can only have one secondary shell, but the layout
// system supports any number of panes.
func (g *Game) makePane() {
	sw := g.machine.System.ScreenWidth()
	sh := g.machine.System.ScreenHeight()
	contentH := sh - int32(TopBarHeight)
	if contentH < 100 {
		contentH = 100
	}

	leftID := g.machine.Services().GetActiveWindowID()
	if leftID == 0 {
		return
	}
	rightID, _ := g.machine.Services().CreateWindow("Shell", sw/2, contentH)
	g.machine.Services().LayoutSplit(leftID, rightID, 0, 0, sw, contentH, true)
}

// destroyPane closes the calling shell window's pane and restores a single
// pane layout for whichever pane remains.
func (g *Game) destroyPane(winID system.WindowID) {
	panes := g.machine.Services().ListPanes()
	if len(panes) <= 1 {
		return
	}
	g.machine.Services().CloseWindow(winID)
	if g.shellApp != nil && g.shellApp.winID == winID {
		g.shellApp = nil
	}
	g.machine.Services().ClearPanes()
}

// cyclePane focuses the next (dir=+1) or previous (dir=-1) pane.
func (g *Game) cyclePane(dir int) {
	panes := g.machine.Services().ListPanes()
	if len(panes) < 2 {
		return
	}
	active := g.machine.Services().GetActiveWindowID()
	idx := -1
	for i, p := range panes {
		if p.WinID == active {
			idx = i
			break
		}
	}
	if idx < 0 {
		idx = 0
	}
	next := (idx + dir + len(panes)) % len(panes)
	g.machine.Services().FocusWindow(panes[next].WinID)
}
