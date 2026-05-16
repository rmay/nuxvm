package system

import (
	_ "embed"
	"golang.org/x/image/font/basicfont"
	"image"
)

//go:embed chicago12x12.cff
var ChicagoCFF []byte

// The Text device renders characters into the framebuffer using the Ebiten
// debug font (7x13) or the new Chicago 12x12 CFF font.
//
// Cursor coordinates (cursorX, cursorY) are now in PIXELS, not cells.
// This allows for arbitrary text placement.

// textState holds the mutable registers for the Text device.
type textState struct {
	fontSize uint8  // font size in points (e.g. 14)
	useCFF   bool   // if true, use Chicago 12x12
	color    uint32 // 24-bit RGB
	cursorX  uint16 // in pixels
	cursorY  uint16 // in pixels
	lastChar byte
}

// attrPacked returns the attr register value (useCFF<<31 | fontSize<<24 | color).
func (t *textState) attrPacked() uint32 {
	var v uint32 = (uint32(t.fontSize) << 24) | (t.color & 0xFFFFFF)
	if t.useCFF {
		v |= 0x80000000
	}
	return v
}

func (t *textState) getScale() float64 {
	if t.fontSize == 0 {
		return 1.0
	}
	return float64(t.fontSize) / 12.0
}

// setAttr decodes an attr-register write.
func (t *textState) setAttr(v uint32) {
	t.useCFF = (v & 0x80000000) != 0
	fontSize := (v >> 24) & 0x7F
	if fontSize == 0 {
		fontSize = 12
	}
	t.fontSize = uint8(fontSize)
	t.color = v & 0xFFFFFF
}

// cursorPacked returns the cursor register value (x<<16 | y).
func (t *textState) cursorPacked() uint32 {
	return (uint32(t.cursorX) << 16) | uint32(t.cursorY)
}

func (t *textState) setCursor(v uint32) {
	t.cursorX = uint16(v >> 16)
	t.cursorY = uint16(v & 0xFFFF)
}

// drawChar renders one ASCII glyph into the active window's framebuffer.
// Cursor advances by the width of the character (7*scale or CFF-width*scale).
func (s *System) drawChar(c byte) {
	s.text.lastChar = c

	if s.text.useCFF {
		s.drawCharCFF(c)
		return
	}

	scale := s.text.getScale()
	cellW := int(7 * scale)
	cellH := int(13 * scale)

	switch c {
	case '\n':
		s.text.cursorX = 0
		s.text.cursorY += uint16(cellH)
		return
	case '\r':
		s.text.cursorX = 0
		return
	}
	if c < 0x20 || c > 0x7E {
		return
	}

	glyph := Font[c]
	originX := int(s.text.cursorX) + int(s.paneX)
	originY := int(s.text.cursorY) + int(s.paneY)
	sw := int(s.getScreenWidth())
	sh := int(s.getScreenHeight())

	paneMaxX := int(s.paneX + s.paneW)
	paneMaxY := int(s.paneY + s.paneH)

	// If the start of the cell is completely off-screen, still advance.
	if originX >= sw || originY >= sh || originX >= paneMaxX || originY >= paneMaxY {
		s.text.cursorX += uint16(cellW)
		return
	}

	r := byte(s.text.color >> 16)
	g := byte(s.text.color >> 8)
	b := byte(s.text.color)

	fb := s.getActiveFramebuffer()
	if fb == nil {
		s.text.cursorX += uint16(cellW)
		return
	}

	for row := 0; row < 13; row++ {
		bits := glyph[row]
		if bits == 0 {
			continue
		}
		startY := float64(row) * scale
		endY := float64(row+1) * scale
		for py := int(startY); py < int(endY); py++ {
			y := originY + py
			if y < int(s.paneY) || y >= paneMaxY || y >= sh {
				continue
			}
			for col := 0; col < 7; col++ {
				if bits&(1<<col) == 0 {
					continue
				}
				startX := float64(col) * scale
				endX := float64(col+1) * scale
				for px := int(startX); px < int(endX); px++ {
					x := originX + px
					if x < int(s.paneX) || x >= paneMaxX || x >= sw {
						continue
					}
					offset := (y*sw + x) * 4
					if offset+4 > len(fb) {
						continue
					}
					fb[offset] = r
					fb[offset+1] = g
					fb[offset+2] = b
					fb[offset+3] = 0xFF
				}
			}
		}
	}

	s.text.cursorX += uint16(cellW)
	if s.Services != nil {
		if win := s.Services.GetActiveWindow(); win != nil {
			win.Dirty = true
		}
	}
}

func (s *System) drawCharCFF(c byte) {
	scale := s.text.getScale()

	switch c {
	case '\n':
		s.text.cursorX = 0
		s.text.cursorY += uint16(16.0 * scale) // Chicago 12x12 is in 16x16 tiles
		return
	case '\r':
		s.text.cursorX = 0
		return
	}

	if int(c) >= 256 {
		return
	}

	width := int(ChicagoCFF[c])
	if width == 0 {
		// Use a default width for space if not set
		if c == ' ' {
			width = 4
		} else {
			return
		}
	}

	s.drawCFFRaw(ChicagoCFF, c, int32(s.text.cursorX), int32(s.text.cursorY), s.text.color, 16, scale)
	s.text.cursorX += uint16(float64(width) * scale)
	if s.Services != nil {
		if win := s.Services.GetActiveWindow(); win != nil {
			win.Dirty = true
		}
	}
}

// drawCFF renders a character from a Cloister Font Format (CFF) font in memory.
func (s *System) drawCFF(fontPtr uint32, char byte, x, y int32, color uint32, tileSize int) {
	s.drawCFFMagnified(fontPtr, char, x, y, color, tileSize, 1.0)
}

// drawCFFMagnified renders a character from a CFF font with optional scaling.
func (s *System) drawCFFMagnified(fontPtr uint32, char byte, x, y int32, color uint32, tileSize int, scale float64) {
	if fontPtr == 0 || int(fontPtr)+256 >= len(s.memory) {
		return
	}
	s.drawCFFRaw(s.memory[fontPtr:], char, x, y, color, tileSize, scale)
}

// drawCFFRaw renders a character from a CFF data slice.
func (s *System) drawCFFRaw(data []byte, char byte, x, y int32, color uint32, tileSize int, scale float64) {
	if len(data) < 256 {
		return
	}

	width := int(data[char])
	if width == 0 && char != ' ' {
		return
	}

	fb := s.getActiveFramebuffer()
	if fb == nil {
		return
	}

	sw := int(s.getScreenWidth())
	sh := int(s.getScreenHeight())

	paneMinX := int(s.paneX)
	paneMaxX := int(s.paneX + s.paneW)
	paneMinY := int(s.paneY)
	paneMaxY := int(s.paneY + s.paneH)

	r := byte(color >> 16)
	g := byte(color >> 8)
	b := byte(color)

	numVTiles := tileSize / 8
	numHTiles := tileSize / 8
	tileCount := numHTiles * numVTiles
	glyphDataOffset := 256 + int(char)*tileCount*8

	if glyphDataOffset+tileCount*8 > len(data) {
		return
	}

	idx := 0
	for tx := 0; tx < numHTiles; tx++ {
		for ty := 0; ty < numVTiles; ty++ {
			for rowInTile := 0; rowInTile < 8; rowInTile++ {
				bits := data[glyphDataOffset+idx]
				idx++
				if bits == 0 {
					continue
				}

				rowAbs := float64(ty*8 + rowInTile)
				startY := rowAbs * scale
				endY := (rowAbs + 1) * scale

				for py := int(startY); py < int(endY); py++ {
					yPixel := int(y) + py + paneMinY
					if yPixel < paneMinY || yPixel >= paneMaxY || yPixel >= sh {
						continue
					}
					for colInTile := 0; colInTile < 8; colInTile++ {
						if bits&(0x80>>colInTile) == 0 {
							continue
						}
						colAbs := float64(tx*8 + colInTile)
						startX := colAbs * scale
						endX := (colAbs + 1) * scale

						for px := int(startX); px < int(endX); px++ {
							xPixel := int(x) + px + paneMinX
							if xPixel < paneMinX || xPixel >= paneMaxX || xPixel >= sw {
								continue
							}
							offset := (yPixel*sw + xPixel) * 4
							if offset+4 > len(fb) {
								continue
							}
							fb[offset] = r
							fb[offset+1] = g
							fb[offset+2] = b
							fb[offset+3] = 0xFF
						}
					}
				}
			}
		}
	}
}


// Font is a 7x13 bitmap font covering printable ASCII (0x20–0x7E).
var Font [256][13]byte

func init() {
	face := basicfont.Face7x13
	mask := face.Mask.(*image.Alpha)
	for _, r := range face.Ranges {
		for runeVal := r.Low; runeVal <= r.High; runeVal++ {
			if runeVal >= 256 {
				continue
			}
			offset := (r.Offset + int(runeVal-r.Low)) * face.Height
			for row := 0; row < face.Height; row++ {
				var rowByte uint16
				for col := 0; col < face.Width; col++ {
					if mask.AlphaAt(col, offset+row).A > 0 {
						rowByte |= 1 << col
					}
				}
				Font[byte(runeVal)][row] = byte(rowByte)
			}
		}
	}
}
