package main

import (
	"image/color"
	"os"

	"github.com/hajimehoshi/ebiten/v2"
	"github.com/hajimehoshi/ebiten/v2/ebitenutil"
	"github.com/hajimehoshi/ebiten/v2/inpututil"
	"github.com/rmay/nuxvm/pkg/system"
)

// paletteCommands lists the host words exposed in the per-window command
// palette. Order is also the display order. The Shell REPL recognizes the
// same names via the same dispatcher (executeHostWord).
var paletteCommands = []string{
	"MAKE-PANE",
	"DESTROY-PANE",
	"NEXT-PANE",
	"PREV-PANE",
	"QUIT-OS",
	"RESTART-OS",
}

// executeHostWord dispatches a Cloister-managed word against the target
// window. Returns true if the word was recognized. targetWin is the window
// that should be considered the "current" pane for word-targeted actions like
// DESTROY-PANE.
func (g *Game) executeHostWord(word string, targetWin system.WindowID) bool {
	switch word {
	case "MAKE-PANE":
		g.makePane()
		return true
	case "DESTROY-PANE":
		g.destroyPane(targetWin)
		return true
	case "NEXT-PANE":
		g.cyclePane(+1)
		return true
	case "PREV-PANE":
		g.cyclePane(-1)
		return true
	case "QUIT-OS":
		os.Exit(0)
		return true
	case "RESTART-OS":
		g.restartRequested = true
		return true
	}
	return false
}

// openCommandPalette enters palette mode targeting the active window.
func (g *Game) openCommandPalette() {
	g.shellMode = ShellCommandPalette
	g.paletteIdx = 0
}

// drawCommandPalette renders the palette overlay. Centered, ~200x200.
func (g *Game) drawCommandPalette(screen *ebiten.Image) {
	sw := int(g.machine.System.ScreenWidth())
	sh := int(g.machine.System.ScreenHeight())
	modalW := 220
	modalH := 30 + len(paletteCommands)*22
	modalX := (sw - modalW) / 2
	modalY := (sh - modalH) / 2

	ebitenutil.DrawRect(screen, float64(modalX), float64(modalY), float64(modalW), float64(modalH), color.RGBA{200, 200, 200, 255})
	strokeRect(screen, float32(modalX), float32(modalY), float32(modalW), float32(modalH), color.Black)
	drawShellText(screen, "Commands", modalX+8, modalY+6, color.Black)
	ebitenutil.DrawLine(screen, float64(modalX), float64(modalY+22), float64(modalX+modalW), float64(modalY+22), color.Black)

	for i, cmd := range paletteCommands {
		rowY := modalY + 26 + i*22
		bgClr := color.RGBA{220, 220, 220, 255}
		fg := color.Color(color.Black)
		if i == g.paletteIdx {
			bgClr = color.RGBA{80, 80, 80, 255}
			fg = color.White
		}
		ebitenutil.DrawRect(screen, float64(modalX+8), float64(rowY), float64(modalW-16), 20, bgClr)
		drawShellText(screen, cmd, modalX+14, rowY+(20-shellFontH)/2, fg)
	}
}

// handleCommandPaletteInput processes keyboard nav and mouse hover/click for
// the palette. Esc / clicking outside closes it.
func (g *Game) handleCommandPaletteInput(justPressed bool) {
	if inpututil.IsKeyJustPressed(ebiten.KeyEscape) {
		g.shellMode = ShellNormal
		return
	}
	if inpututil.IsKeyJustPressed(ebiten.KeyUp) {
		g.paletteIdx--
		if g.paletteIdx < 0 {
			g.paletteIdx = len(paletteCommands) - 1
		}
	}
	if inpututil.IsKeyJustPressed(ebiten.KeyDown) {
		g.paletteIdx = (g.paletteIdx + 1) % len(paletteCommands)
	}

	exec := func(idx int) {
		target := g.machine.Services().GetActiveWindowID()
		g.shellMode = ShellNormal
		g.executeHostWord(paletteCommands[idx], target)
	}

	if inpututil.IsKeyJustPressed(ebiten.KeyEnter) {
		exec(g.paletteIdx)
		return
	}

	mx, my := ebiten.CursorPosition()
	sw := int(g.machine.System.ScreenWidth())
	sh := int(g.machine.System.ScreenHeight())
	modalW := 220
	modalH := 30 + len(paletteCommands)*22
	modalX := (sw - modalW) / 2
	modalY := (sh - modalH) / 2
	for i := range paletteCommands {
		rowY := modalY + 26 + i*22
		if mx >= modalX+8 && mx < modalX+modalW-8 && my >= rowY && my < rowY+20 {
			g.paletteIdx = i
			if justPressed {
				exec(i)
				return
			}
		}
	}
}
