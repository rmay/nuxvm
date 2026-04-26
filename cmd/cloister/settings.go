package main

import (
	"fmt"
	"image/color"
	"os"
	"strconv"
	"strings"
)

// settingsFileName is the basename of the persisted settings file. It lives
// next to wherever cloister was launched from (cwd at startup), not in the
// repo, so a user running cloister out of /tmp gets /tmp/cloister-settings.lux.
const settingsFileName = "cloister-settings.lux"

// loadSettings reads g.settingsPath and applies any recognized rows to the
// Game's host-side settings. Missing file or parse errors are silent (first
// launch is a no-op).
func (g *Game) loadSettings() {
	if g.settingsPath == "" {
		return
	}
	data, err := os.ReadFile(g.settingsPath)
	if err != nil {
		return
	}
	for _, line := range strings.Split(string(data), "\n") {
		line = strings.TrimSpace(line)
		if line == "" || strings.HasPrefix(line, "(") {
			continue
		}
		fields := strings.Fields(line)
		if len(fields) < 2 {
			continue
		}
		valStr := fields[0]
		base := 10
		if strings.HasPrefix(valStr, "0x") || strings.HasPrefix(valStr, "0X") {
			valStr = valStr[2:]
			base = 16
		}
		val, err := strconv.ParseInt(valStr, base, 64)
		if err != nil {
			continue
		}
		switch strings.ToUpper(fields[1]) {
		case "SET-TEXT-SCALE":
			if val >= 1 && val <= 4 {
				g.textScale = int(val)
			}
		case "SET-DEBUG":
			g.showDebug = val != 0
		case "SET-CLEAR-COLOR":
			g.clearColor = color.RGBA{
				R: uint8((val >> 16) & 0xFF),
				G: uint8((val >> 8) & 0xFF),
				B: uint8(val & 0xFF),
				A: 255,
			}
		case "SET-BG-PATTERN":
			if val >= 0 && int(val) < len(bgPatternTiles) {
				g.bgPattern = int(val)
			}
		case "SET-CLOCK-FORMAT":
			if val >= 0 && val <= 2 {
				g.clockFormat = int(val)
			}
		}
	}
}

// saveSettings writes the current host-side settings to g.settingsPath as Lux
// source. Atomic via tmp+rename. Errors are swallowed: a settings save failure
// shouldn't crash the OS.
func (g *Game) saveSettings() {
	if g.settingsPath == "" {
		return
	}
	colorWord := uint32(g.clearColor.R)<<16 | uint32(g.clearColor.G)<<8 | uint32(g.clearColor.B)
	debugVal := 0
	if g.showDebug {
		debugVal = 1
	}
	body := "( Cloister Settings - auto-generated; edit while Cloister is closed )\n" +
		fmt.Sprintf("%-8d SET-TEXT-SCALE\n", g.textScale) +
		fmt.Sprintf("%-8d SET-DEBUG\n", debugVal) +
		fmt.Sprintf("%-8d SET-CLEAR-COLOR\n", colorWord) +
		fmt.Sprintf("%-8d SET-BG-PATTERN\n", g.bgPattern) +
		fmt.Sprintf("%-8d SET-CLOCK-FORMAT\n", g.clockFormat)

	tmp := g.settingsPath + ".tmp"
	if err := os.WriteFile(tmp, []byte(body), 0644); err != nil {
		return
	}
	_ = os.Rename(tmp, g.settingsPath)
}
