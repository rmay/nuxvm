package system

import (
	"encoding/binary"
	"io"
)

// fillRect draws a filled rectangle directly to the physical screen pixels.
func (s *System) fillRect(x, y, w, h int32, color uint32) {
	r := byte((color >> 16) & 0xFF)
	g := byte((color >> 8) & 0xFF)
	b := byte(color & 0xFF)
	a := byte(255)

	sw := int(s.screenWidth)
	sh := int(s.screenHeight)

	paneMinX := s.paneX
	paneMinY := s.paneY
	paneMaxX := s.paneX + s.paneW
	paneMaxY := s.paneY + s.paneH

	if s.paneW == 0 {
		paneMaxX = int32(sw)
	}
	if s.paneH == 0 {
		paneMaxY = int32(sh)
	}

	for py := y; py < y+h; py++ {
		if py < paneMinY || py >= paneMaxY || py < 0 || py >= int32(sh) {
			continue
		}
		for px := x; px < x+w; px++ {
			if px < paneMinX || px >= paneMaxX || px < 0 || px >= int32(sw) {
				continue
			}
			offset := (int(py)*sw + int(px)) * 4
			if offset+4 <= len(s.screenPixels) {
				s.screenPixels[offset] = r
				s.screenPixels[offset+1] = g
				s.screenPixels[offset+2] = b
				s.screenPixels[offset+3] = a
			}
		}
	}

	if s.Services != nil {
		if win := s.Services.GetActiveWindow(); win != nil {
			win.Dirty = true
		}
	}
}

// drawRect draws a 1px border rectangle.
func (s *System) drawRect(x, y, w, h int32, color uint32) {
	if w <= 0 || h <= 0 {
		return
	}
	// Top
	s.fillRect(x, y, w, 1, color)
	// Bottom
	s.fillRect(x, y+h-1, w, 1, color)
	// Left
	s.fillRect(x, y, 1, h, color)
	// Right
	s.fillRect(x+w-1, y, 1, h, color)
}

// drawCharVFS renders a character using the system font.
func (s *System) drawCharVFS(x, y int32, char byte, color uint32, scale byte) {
	if s.text.useBasicFont {
		sc := float64(scale)
		if scale >= 6 {
			sc = float64(scale) / 12.0
		}
		if sc <= 0 {
			sc = 1.0
		}
		s.drawCharBasic(x, y, char, color, sc)
		return
	}

	size := scale
	if scale < 6 {
		// Treat as a multiplier for a base size of 16
		size = uint8(float64(scale) * 16.0)
	}
	s.drawCharGo(x, y, char, color, size)

	if s.Services != nil {
		if win := s.Services.GetActiveWindow(); win != nil {
			win.Dirty = true
		}
	}
}

// kbdFile and mouseFile implementation details

func (f *kbdFile) Read(p []byte) (n int, err error) {
	if len(p) < 4 {
		return 0, io.ErrShortBuffer
	}
	select {
	case evt := <-f.s.kbdEvents:
		// [Type (1), Padding (1), KeyCode (2)]
		p[0] = byte(evt.Type)
		p[1] = 0
		binary.LittleEndian.PutUint16(p[2:4], uint16(evt.KeyCode))
		return 4, nil
	default:
		return 0, nil
	}
}

func (f *mouseFile) Read(p []byte) (n int, err error) {
	if len(p) < 8 {
		return 0, io.ErrShortBuffer
	}
	select {
	case evt := <-f.s.mouseEvents:
		// [Type (1), Buttons (1), X (2), Y (2), Padding (2)]
		p[0] = byte(evt.Type)
		p[1] = byte(evt.MouseBtn)
		binary.LittleEndian.PutUint16(p[2:4], uint16(evt.MouseX))
		binary.LittleEndian.PutUint16(p[4:6], uint16(evt.MouseY))
		return 8, nil
	default:
		return 0, nil
	}
}
