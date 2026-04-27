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

// drawChar renders one ASCII glyph into the active window's framebuffer.
// Cursor advances; newline/carriage-return are honoured. Non-printable codes are dropped.
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
	sw := int(s.getScreenWidth())
	sh := int(s.getScreenHeight())

	// If the cell is completely off-screen, drop it but still advance the
	// cursor so the caller's bookkeeping stays consistent.
	if originX >= sw || originY >= sh {
		s.advanceCursor()
		return
	}

	r := byte(s.text.color >> 16)
	g := byte(s.text.color >> 8)
	b := byte(s.text.color)

	fb := s.getActiveFramebuffer()
	if fb == nil {
		s.advanceCursor()
		return
	}

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
	cols := int(s.getScreenWidth()) / cellPx
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
// Codepoints below 0x20 (control chars) are left zeroed.
var Font [256][8]byte

// chicago.png is laid out as a 16-column grid of 8x8 cells. Cell (0,0) holds
// 0x20 (space), so the printable ASCII range 0x20–0x7F fills the top 6 rows
// (96 glyphs). Cells past row 6 are extras (high-ASCII / Mac Roman) and
// currently unused — we just don't load them.
const fontFirstCodepoint = 0x20
const fontGridCols = 16
const fontCellSize = 8

// fontIsLitPixel returns whether (x,y) in the chicago.png is a "lit" (foreground)
// pixel. The PNG uses three palette colors: a pale background, a dark green
// foreground, and bright magenta gutters / unused regions. Pixels are lit
// iff red < ~half — that selects only the dark-green glyph color and rejects
// both the pale BG and the magenta gutter.
func fontIsLitPixel(r, g, b uint32) bool {
	return r < 0x8000 && g > 0x4000 && b < 0x8000
}

func init() {
	img, err := png.Decode(bytes.NewReader(chicagoPNG))
	if err != nil {
		panic("failed to decode chicago.png: " + err.Error())
	}

	bounds := img.Bounds()
	maxIdx := (bounds.Dy() / fontCellSize) * fontGridCols // total cells in the sheet

	for codepoint := fontFirstCodepoint; codepoint < 0x100; codepoint++ {
		idx := codepoint - fontFirstCodepoint
		if idx >= maxIdx {
			break
		}
		cellX := (idx % fontGridCols) * fontCellSize
		cellY := (idx / fontGridCols) * fontCellSize
		for row := 0; row < fontCellSize; row++ {
			var rowByte byte
			for col := 0; col < fontCellSize; col++ {
				r, g, b, _ := img.At(cellX+col, cellY+row).RGBA()
				if fontIsLitPixel(r, g, b) {
					rowByte |= 1 << col
				}
			}
			Font[codepoint][row] = rowByte
		}
	}
}
