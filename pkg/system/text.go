package system

import (
	_ "embed"
	"golang.org/x/image/font"
	"golang.org/x/image/font/basicfont"
	"golang.org/x/image/font/gofont/goregular"
	"golang.org/x/image/font/opentype"
	"golang.org/x/image/math/fixed"
	"image"
	"image/color"
	"sync"
)

//go:embed chicago12x12.cff
var ChicagoCFF []byte

var (
	goFontMu    sync.Mutex
	goFontFaces = make(map[uint8]font.Face)
	goFontRoot  *opentype.Font
)

func init() {
	var err error
	goFontRoot, err = opentype.Parse(goregular.TTF)
	if err != nil {
		panic(err)
	}
}

func getGoFace(size uint8) font.Face {
	goFontMu.Lock()
	defer goFontMu.Unlock()

	if size == 0 {
		size = 12
	}
	if face, ok := goFontFaces[size]; ok {
		return face
	}

	face, err := opentype.NewFace(goFontRoot, &opentype.FaceOptions{
		Size:    float64(size),
		DPI:     72,
		Hinting: font.HintingFull,
	})
	if err != nil {
		return basicfont.Face7x13
	}
	goFontFaces[size] = face
	return face
}

// textState holds the mutable registers for the Text device.
type textState struct {
	fontSize     uint8  // font size in points (e.g. 14)
	useCFF       bool   // if true, use Go Regular (replaces Chicago)
	useBasicFont bool   // /sys/draw: if true, DrawChar/DrawString use the 7x13 basicfont
	color        uint32 // 24-bit RGB
	cursorX      uint16 // in pixels
	cursorY      uint16 // in pixels
	lastChar     byte
}

// BasicFontWidth and BasicFontHeight are the native cell dimensions of the
// 7x13 basicfont used when /sys/draw is switched into basic-font mode.
const (
	BasicFontWidth  = 7
	BasicFontHeight = 13
)

func (s *System) screenImage() *image.RGBA {
	return &image.RGBA{
		Pix:    s.screenPixels,
		Stride: int(s.screenWidth) * 4,
		Rect:   image.Rect(0, 0, int(s.screenWidth), int(s.screenHeight)),
	}
}

func (s *System) drawCharGo(x, y int32, char byte, colorVal uint32, size uint8) {
	face := getGoFace(size)
	
	r := uint8(colorVal >> 16)
	g := uint8(colorVal >> 8)
	b := uint8(colorVal)
	
	// We need to account for the font baseline.
	metrics := face.Metrics()
	ascent := metrics.Ascent.Ceil()
	
	d := &font.Drawer{
		Dst:  s.screenImage(),
		Src:  image.NewUniform(color.RGBA{r, g, b, 255}),
		Face: face,
		Dot:  fixed.Point26_6{X: fixed.I(int(x)), Y: fixed.I(int(y) + ascent)},
	}

	d.DrawString(string(rune(char)))
}

func (s *System) drawCharBasic(x, y int32, char byte, color uint32, sc float64) {
	if sc <= 0 {
		sc = 1.0
	}
	r := byte(color >> 16)
	g := byte(color >> 8)
	b := byte(color)

	sw := int(s.screenWidth)
	sh := int(s.screenHeight)
	paneMinX := int(s.paneX)
	paneMinY := int(s.paneY)
	paneMaxX := int(s.paneX + s.paneW)
	paneMaxY := int(s.paneY + s.paneH)
	if s.paneW == 0 {
		paneMaxX = sw
	}
	if s.paneH == 0 {
		paneMaxY = sh
	}

	glyph := Font[char]
	for row := 0; row < BasicFontHeight; row++ {
		bits := glyph[row]
		if bits == 0 {
			continue
		}
		for col := 0; col < BasicFontWidth; col++ {
			if bits&(1<<col) == 0 {
				continue
			}
			startX := int(float64(col) * sc)
			endX := int(float64(col+1) * sc)
			startY := int(float64(row) * sc)
			endY := int(float64(row+1) * sc)
			for py := startY; py < endY; py++ {
				yy := int(y) + py
				if yy < paneMinY || yy >= paneMaxY || yy < 0 || yy >= sh {
					continue
				}
				for px := startX; px < endX; px++ {
					xx := int(x) + px
					if xx < paneMinX || xx >= paneMaxX || xx < 0 || xx >= sw {
						continue
					}
					off := (yy*sw + xx) * 4
					if off+4 > len(s.screenPixels) {
						continue
					}
					s.screenPixels[off] = r
					s.screenPixels[off+1] = g
					s.screenPixels[off+2] = b
					s.screenPixels[off+3] = 255
				}
			}
		}
	}

	if s.Services != nil {
		if win := s.Services.GetActiveWindow(); win != nil {
			win.Dirty = true
		}
	}
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
	div := 12.0
	if t.useCFF {
		div = 16.0
	}
	return float64(t.fontSize) / div
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

// drawChar renders one ASCII glyph into the physical framebuffer.
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
	originX := int(s.text.cursorX)
	originY := int(s.text.cursorY)
	sw := int(s.screenWidth)
	sh := int(s.screenHeight)

	paneMinX := int(s.paneX)
	paneMinY := int(s.paneY)
	paneMaxX := int(s.paneX + s.paneW)
	paneMaxY := int(s.paneY + s.paneH)
	if s.paneW == 0 {
		paneMaxX = sw
	}
	if s.paneH == 0 {
		paneMaxY = sh
	}

	// If the start of the cell is completely off-screen, still advance.
	if originX >= sw || originY >= sh || originX >= paneMaxX || originY >= paneMaxY {
		s.text.cursorX += uint16(cellW)
		return
	}

	r := byte(s.text.color >> 16)
	g := byte(s.text.color >> 8)
	b := byte(s.text.color)

	fb := s.screenPixels

	for row := 0; row < 13; row++ {
		bits := glyph[row]
		if bits == 0 {
			continue
		}
		startY := float64(row) * scale
		endY := float64(row+1) * scale
		for py := int(startY); py < int(endY); py++ {
			y := originY + py
			if y < paneMinY || y >= paneMaxY || y >= sh || y < 0 {
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
					if x < paneMinX || x >= paneMaxX || x >= sw || x < 0 {
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
	switch c {
	case '\n':
		s.text.cursorX = 0
		s.text.cursorY += uint16(s.text.fontSize)
		return
	case '\r':
		s.text.cursorX = 0
		return
	}

	face := getGoFace(s.text.fontSize)
	adv, ok := face.GlyphAdvance(rune(c))
	if !ok {
		return
	}
	width := adv.Ceil()

	s.drawCharGo(int32(s.text.cursorX), int32(s.text.cursorY), c, s.text.color, s.text.fontSize)
	s.text.cursorX += uint16(width)
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

// drawCFFRaw renders a character from a CFF data slice into the physical framebuffer.
func (s *System) drawCFFRaw(data []byte, char byte, x, y int32, color uint32, tileSize int, scale float64) {
	// Fallback to Go font if this was intended for Chicago
	if len(data) == len(ChicagoCFF) {
		s.drawCharGo(x, y, char, color, uint8(16.0*scale))
		return
	}
	
	// Keep original manual drawing for user-loaded fonts in RAM
	if len(data) < 256 {
		return
	}

	width := int(data[char])
	if width == 0 && char != ' ' {
		return
	}

	fb := s.screenPixels
	sw := int(s.screenWidth)
	sh := int(s.screenHeight)

	paneMinX := int(s.paneX)
	paneMinY := int(s.paneY)
	paneMaxX := int(s.paneX + s.paneW)
	paneMaxY := int(s.paneY + s.paneH)
	if s.paneW == 0 {
		paneMaxX = sw
	}
	if s.paneH == 0 {
		paneMaxY = sh
	}

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
					yPixel := int(y) + py
					if yPixel < paneMinY || yPixel >= paneMaxY || yPixel >= sh || yPixel < 0 {
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
							xPixel := int(x) + px
							if xPixel < paneMinX || xPixel >= paneMaxX || xPixel >= sw || xPixel < 0 {
								continue
							}
							offset := (int(yPixel)*sw + int(xPixel)) * 4
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
