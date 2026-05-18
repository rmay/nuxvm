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

// FontRenderer defines the interface for drawing characters to an image.
type FontRenderer interface {
	DrawGlyph(dst *image.RGBA, x, y int32, char byte, color color.RGBA, scale float64) (advance int)
	MeasureGlyph(char byte, scale float64) (width int, height int)
}

// BasicFontRenderer uses the hardcoded 7x13 basicfont.
type BasicFontRenderer struct{}

func (r *BasicFontRenderer) DrawGlyph(dst *image.RGBA, x, y int32, char byte, col color.RGBA, scale float64) int {
	if scale >= 6 {
		scale = scale / 12.0
	}
	if scale <= 0 {
		scale = 1.0
	}
	glyph := Font[char]
	bounds := dst.Bounds()

	for row := 0; row < BasicFontHeight; row++ {
		bits := glyph[row]
		if bits == 0 {
			continue
		}
		startY := float64(row) * scale
		endY := float64(row+1) * scale
		for py := int(startY); py < int(endY); py++ {
			yy := int(y) + py
			if yy < bounds.Min.Y || yy >= bounds.Max.Y {
				continue
			}
			for colIdx := 0; colIdx < BasicFontWidth; colIdx++ {
				if bits&(1<<colIdx) == 0 {
					continue
				}
				startX := float64(colIdx) * scale
				endX := float64(colIdx+1) * scale
				for px := int(startX); px < int(endX); px++ {
					xx := int(x) + px
					if xx < bounds.Min.X || xx >= bounds.Max.X {
						continue
					}
					offset := (yy*dst.Stride/4 + xx) * 4
					dst.Pix[offset] = col.R
					dst.Pix[offset+1] = col.G
					dst.Pix[offset+2] = col.B
					dst.Pix[offset+3] = col.A
				}
			}
		}
	}
	return int(float64(BasicFontWidth) * scale)
}

func (r *BasicFontRenderer) MeasureGlyph(char byte, scale float64) (int, int) {
	if scale >= 6 {
		scale = scale / 12.0
	}
	if scale <= 0 {
		scale = 1.0
	}
	return int(float64(BasicFontWidth) * scale), int(float64(BasicFontHeight) * scale)
}

// TTFFontRenderer uses Go Regular (TTF).
type TTFFontRenderer struct {
	Size uint8
}

func (r *TTFFontRenderer) DrawGlyph(dst *image.RGBA, x, y int32, char byte, col color.RGBA, scale float64) int {
	size := r.Size
	if size == 0 {
		size = 12
	}
	// If scale is provided (and not 1.0), it overrides or multiplies the size.
	// For TTF, we treat scale as the point size if it's large, or a multiplier if it's small.
	if scale > 0 && scale != 1.0 {
		if scale < 6 {
			size = uint8(float64(size) * scale)
		} else {
			size = uint8(scale)
		}
	}

	face := getGoFace(size)
	metrics := face.Metrics()
	ascent := metrics.Ascent.Ceil()

	d := &font.Drawer{
		Dst:  dst,
		Src:  image.NewUniform(col),
		Face: face,
		Dot:  fixed.Point26_6{X: fixed.I(int(x)), Y: fixed.I(int(y) + ascent)},
	}

	d.DrawString(string(rune(char)))

	adv, _ := face.GlyphAdvance(rune(char))
	return adv.Ceil()
}

func (r *TTFFontRenderer) MeasureGlyph(char byte, scale float64) (int, int) {
	size := r.Size
	if size == 0 {
		size = 12
	}
	if scale > 0 && scale != 1.0 {
		if scale < 6 {
			size = uint8(float64(size) * scale)
		} else {
			size = uint8(scale)
		}
	}
	face := getGoFace(size)
	adv, _ := face.GlyphAdvance(rune(char))
	metrics := face.Metrics()
	return adv.Ceil(), metrics.Height.Ceil()
}

// CFFFontRenderer parses and draws CFF byte slices.
type CFFFontRenderer struct {
	Data     []byte
	TileSize int
}

func (r *CFFFontRenderer) DrawGlyph(dst *image.RGBA, x, y int32, char byte, col color.RGBA, scale float64) int {
	if scale >= 6 {
		scale = scale / float64(r.TileSize)
	}
	if scale <= 0 {
		scale = 1.0
	}
	if len(r.Data) < 256 {
		return 0
	}
	// Special case: if this is our Chicago fallback, use TTF for better quality
	if len(r.Data) == len(ChicagoCFF) {
		ttf := &TTFFontRenderer{Size: 16}
		return ttf.DrawGlyph(dst, x, y, char, col, scale*float64(r.TileSize))
	}

	width := int(r.Data[char])
	if width == 0 && char != ' ' {
		return 0
	}

	bounds := dst.Bounds()
	numVTiles := r.TileSize / 8
	numHTiles := r.TileSize / 8
	tileCount := numHTiles * numVTiles
	glyphDataOffset := 256 + int(char)*tileCount*8

	if glyphDataOffset+tileCount*8 > len(r.Data) {
		return 0
	}

	idx := 0
	for tx := 0; tx < numHTiles; tx++ {
		for ty := 0; ty < numVTiles; ty++ {
			for rowInTile := 0; rowInTile < 8; rowInTile++ {
				bits := r.Data[glyphDataOffset+idx]
				idx++
				if bits == 0 {
					continue
				}

				rowAbs := float64(ty*8 + rowInTile)
				startY := rowAbs * scale
				endY := (rowAbs + 1) * scale

				for py := int(startY); py < int(endY); py++ {
					yPixel := int(y) + py
					if yPixel < bounds.Min.Y || yPixel >= bounds.Max.Y {
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
							if xPixel < bounds.Min.X || xPixel >= bounds.Max.X {
								continue
							}
							offset := (yPixel*dst.Stride/4 + xPixel) * 4
							dst.Pix[offset] = col.R
							dst.Pix[offset+1] = col.G
							dst.Pix[offset+2] = col.B
							dst.Pix[offset+3] = col.A
						}
					}
				}
			}
		}
	}
	return int(float64(width) * scale)
}

func (r *CFFFontRenderer) MeasureGlyph(char byte, scale float64) (int, int) {
	if scale >= 6 {
		scale = scale / float64(r.TileSize)
	}
	if scale <= 0 {
		scale = 1.0
	}
	if len(r.Data) < 256 {
		return 0, 0
	}
	if len(r.Data) == len(ChicagoCFF) {
		ttf := &TTFFontRenderer{Size: 16}
		return ttf.MeasureGlyph(char, scale*float64(r.TileSize))
	}
	width := int(r.Data[char])
	return int(float64(width) * scale), int(float64(r.TileSize) * scale)
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

// GetFontRenderer returns the active font renderer based on the current state.
func (s *System) GetFontRenderer() FontRenderer {
	if s.text.useCFF {
		return &TTFFontRenderer{Size: s.text.fontSize}
	}
	if s.text.useBasicFont {
		return &BasicFontRenderer{}
	}
	// Default to Chicago CFF (which currently redirects to TTF in our unified plan)
	// when useBasicFont is explicitly set to false (e.g. via VFS SetFont 0).
	return &CFFFontRenderer{Data: ChicagoCFF, TileSize: 16}
}

// DrawGlyph is the unified entry point for drawing a character.
func (s *System) DrawGlyph(dst *image.RGBA, x, y int32, char byte, colorVal uint32, scale float64) int {
	renderer := s.GetFontRenderer()
	c := color.RGBA{
		R: uint8(colorVal >> 16),
		G: uint8(colorVal >> 8),
		B: uint8(colorVal),
		A: 255,
	}

	// Apply pane clipping if drawing to the main screen
	if dst.Pix == nil || len(dst.Pix) == 0 {
		return 0
	}

	return renderer.DrawGlyph(dst, x, y, char, c, scale)
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
	// Note: in the unified pipeline, DrawChar passes this scale to DrawGlyph.
	// Since DrawGlyph now handles scale >= 6 as point size, we could just return fontSize.
	// But to keep MMIO semantics (where it might be a multiplier if small), let's be careful.
	return float64(t.fontSize)
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

	renderer := s.GetFontRenderer()
	scale := s.text.getScale()

	// Handle control characters
	switch c {
	case '\n':
		_, h := renderer.MeasureGlyph('A', scale)
		s.text.cursorX = 0
		s.text.cursorY += uint16(h)
		return
	case '\r':
		s.text.cursorX = 0
		return
	}

	if c < 0x20 || c > 0x7E {
		return
	}

	// Respect pane clipping for screen drawing
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

	screen := s.screenImage()
	// Create a sub-image for the pane
	sub := screen.SubImage(image.Rect(paneMinX, paneMinY, paneMaxX, paneMaxY)).(*image.RGBA)

	advance := s.DrawGlyph(sub, int32(s.text.cursorX)-int32(paneMinX), int32(s.text.cursorY)-int32(paneMinY), c, s.text.color, scale)
	s.text.cursorX += uint16(advance)

	if s.Services != nil {
		if win := s.Services.GetActiveWindow(); win != nil {
			win.Dirty = true
		}
	}
}

// drawCFFRaw renders a character from a CFF data slice into the physical framebuffer.
func (s *System) drawCFFRaw(data []byte, char byte, x, y int32, colorVal uint32, tileSize int, scale float64) {
	renderer := &CFFFontRenderer{Data: data, TileSize: tileSize}
	col := color.RGBA{
		R: uint8(colorVal >> 16),
		G: uint8(colorVal >> 8),
		B: uint8(colorVal),
		A: 255,
	}

	// For raw CFF drawing, we target the whole screen but respect panes
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

	screen := s.screenImage()
	sub := screen.SubImage(image.Rect(paneMinX, paneMinY, paneMaxX, paneMaxY)).(*image.RGBA)

	renderer.DrawGlyph(sub, x-int32(paneMinX), y-int32(paneMinY), char, col, scale)
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
