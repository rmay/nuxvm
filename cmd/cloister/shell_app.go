package main

import (
	"image/color"
	"strings"

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
}

const shellMaxScrollback = 500

// openShellApp creates (or focuses) the singleton Shell window.
func (g *Game) openShellApp() {
	if g.shellApp != nil {
		if g.machine.Services().GetWindowByID(g.shellApp.winID) != nil {
			g.machine.Services().FocusWindow(g.shellApp.winID)
			g.machine.Services().LayoutSingle(g.shellApp.winID,
				0, 0, g.machine.System.ScreenWidth(), g.machine.System.ScreenHeight()-int32(topBarHeight)-int32(WinChromeHeight))
			return
		}
		g.shellApp = nil
	}

	sw := g.machine.System.ScreenWidth()
	sh := g.machine.System.ScreenHeight()
	contentH := sh - int32(topBarHeight) - int32(WinChromeHeight)
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
}

// handleBackspace trims the last rune from the input buffer.
func (sa *ShellApp) handleBackspace() {
	if sa.inputLine == "" {
		return
	}
	r := []rune(sa.inputLine)
	sa.inputLine = string(r[:len(r)-1])
}

// handleEnter commits the current input line. Host words are intercepted and
// run on the Game (not the embedded VM); everything else goes through luxrepl.
func (sa *ShellApp) handleEnter(g *Game) {
	line := sa.inputLine
	sa.inputLine = ""
	sa.appendOutput(sa.prompt + line + "\n")

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
// shell window's framebuffer. Content area is shrunk by WinScrollbarSize on
// the right and bottom so it doesn't draw under the scrollbar gutters.
func (g *Game) drawShellContent(win *system.Window) {
	if g.shellApp == nil {
		return
	}
	clearWindow(win, color.RGBA{0, 0, 0, 255})
	fg := color.RGBA{255, 255, 255, 255}
	const lineH = 10 // Chicago 8 + 2 leading
	innerH := int(win.Height) - WinScrollbarSize
	maxLines := innerH / lineH
	if maxLines < 1 {
		maxLines = 1
	}

	display := append([]string{}, g.shellApp.scrollback...)
	display = append(display, g.shellApp.prompt+g.shellApp.inputLine+"_")

	// ScrollY is interpreted as a backwards offset in lines (positive ScrollY
	// shows older content). 0 = bottom (most recent).
	scrollLines := int(win.ScrollY) / lineH
	if scrollLines < 0 {
		scrollLines = 0
	}
	end := len(display) - scrollLines
	if end < 1 {
		end = 1
	}
	start := end - maxLines
	if start < 0 {
		start = 0
	}
	y := 4
	for i := start; i < end; i++ {
		drawChicagoTextInWindow(win, display[i], 4, y, 1, fg)
		y += lineH
	}
}

// makePane splits the active pane vertically. The right half is a new Shell
// window. For v1 the user can only have one secondary shell, but the layout
// system supports any number of panes.
func (g *Game) makePane() {
	sw := g.machine.System.ScreenWidth()
	sh := g.machine.System.ScreenHeight()
	contentH := sh - int32(topBarHeight) - int32(WinChromeHeight)
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
