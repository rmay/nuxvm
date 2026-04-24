package system

import (
	"bytes"
	_ "embed"
	"image/png"
)

//go:embed chicago.png
var chicagoPNG []byte

// The Text device renders characters into the framebuffer using a baked 8x8
// ASCII font. State lives alongside the rest of the System in system.go; this
// file carries just the glyph table and the draw routine so system.go stays
// focused on the port-routing logic.
//
// Cell coordinates are in *cells*, not pixels; a cell is 8*scale pixels on
// each side. The cursor advances one cell per printable character and wraps
// to the next row at the right edge. Printing past the bottom edge is a
// silent no-op — scrolling would require mutating the framebuffer on every
// newline, which is more than this device is meant to do.

// textState holds the mutable registers for the Text device.
type textState struct {
	scale    uint8  // cell scale, 1..8; clamped on write
	color    uint32 // 24-bit RGB
	cursorX  uint16 // in cells
	cursorY  uint16
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

// drawChar renders one ASCII glyph into screenPixels. Cursor advances;
// newline/carriage-return are honoured. Non-printable codes are dropped.
func (s *System) drawChar(c byte) {
	s.text.lastChar = c

	switch c {
	case '\n':
		s.text.cursorX = 0
		s.text.cursorY++
		return
	case '\r':
		s.text.cursorX = 0
		return
	}
	if c < 0x20 || c > 0x7E {
		return
	}

	glyph := Font[c]
	scale := int(s.text.scale)
	if scale < 1 {
		scale = 1
	}
	cellPx := 8 * scale
	originX := int(s.text.cursorX) * cellPx
	originY := int(s.text.cursorY) * cellPx
	sw := int(s.screenWidth)
	sh := int(s.screenHeight)

	// If the cell is completely off-screen, drop it but still advance the
	// cursor so the caller's bookkeeping stays consistent.
	if originX >= sw || originY >= sh {
		s.advanceCursor()
		return
	}

	r := byte(s.text.color >> 16)
	g := byte(s.text.color >> 8)
	b := byte(s.text.color)

	for row := 0; row < 8; row++ {
		bits := glyph[row]
		if bits == 0 {
			continue
		}
		for col := 0; col < 8; col++ {
			// Font rows are LSB-first: bit 0 is the leftmost pixel.
			if bits&(1<<col) == 0 {
				continue
			}
			// Paint a scale×scale block for each lit source pixel.
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
					if offset+4 > len(s.screenPixels) {
						continue
					}
					s.screenPixels[offset] = r
					s.screenPixels[offset+1] = g
					s.screenPixels[offset+2] = b
					s.screenPixels[offset+3] = 0xFF
				}
			}
		}
	}

	s.advanceCursor()
}

// advanceCursor moves the cursor one cell to the right and wraps to the next
// row at the right edge. Going off the bottom is allowed but future draws to
// that row will be clipped.
func (s *System) advanceCursor() {
	scale := int(s.text.scale)
	if scale < 1 {
		scale = 1
	}
	cellPx := 8 * scale
	cols := int(s.screenWidth) / cellPx
	if cols < 1 {
		cols = 1
	}
	s.text.cursorX++
	if int(s.text.cursorX) >= cols {
		s.text.cursorX = 0
		s.text.cursorY++
	}
}

// Font is a compact 8x8 bitmap font covering printable ASCII (0x20–0x7E).
var Font [128][8]byte

func init() {
	img, err := png.Decode(bytes.NewReader(chicagoPNG))
	if err != nil {
		panic("failed to decode chicago.png: " + err.Error())
	}
	
	for i := 0; i < 128; i++ {
		cellX := (i % 16) * 8
		cellY := (i / 16) * 8
		for row := 0; row < 8; row++ {
			var rowByte byte
			for col := 0; col < 8; col++ {
				r, _, _, _ := img.At(cellX+col, cellY+row).RGBA()
				if r > 0x7FFF {
					rowByte |= (1 << col)
				}
			}
			Font[i][row] = rowByte
		}
	}
}
