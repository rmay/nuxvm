package system

import (
	"image"
	"golang.org/x/image/font/basicfont"
)

// The Text device renders characters into the framebuffer using the Ebiten
// debug font (7x13).
//
// Cursor coordinates (cursorX, cursorY) are now in PIXELS, not cells.
// This allows for arbitrary text placement.

// textState holds the mutable registers for the Text device.
type textState struct {
	scale    uint8  // scale factor (1..8)
	color    uint32 // 24-bit RGB
	cursorX  uint16 // in pixels
	cursorY  uint16 // in pixels
	lastChar byte
}

// attrPacked returns the attr register value (scale<<24 | color).
func (t *textState) attrPacked() uint32 {
	return (uint32(t.scale) << 24) | (t.color & 0xFFFFFF)
}

// setAttr decodes an attr-register write.
func (t *textState) setAttr(v uint32) {
	scale := (v >> 24) & 0xFF
	if scale == 0 {
		scale = 1
	}
	if scale > 8 {
		scale = 8
	}
	t.scale = uint8(scale)
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
// Cursor advances by the width of the character (7*scale).
func (s *System) drawChar(c byte) {
	s.text.lastChar = c

	scale := int(s.text.scale)
	if scale < 1 {
		scale = 1
	}
	cellW := 7 * scale
	cellH := 13 * scale

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
	originX := int(s.text.cursorX)
	originY := int(s.text.cursorY)
	sw := int(s.getScreenWidth())
	sh := int(s.getScreenHeight())

	// If the start of the cell is completely off-screen, still advance.
	if originX >= sw || originY >= sh {
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
		for col := 0; col < 7; col++ {
			if bits&(1<<col) == 0 {
				continue
			}
			for dy := 0; dy < scale; dy++ {
				y := originY + row*scale + dy
				if y < 0 || y >= sh {
					continue
				}
				for dx := 0; dx < scale; dx++ {
					x := originX + col*scale + dx
					if x < 0 || x >= sw {
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
