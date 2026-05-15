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

	for py := y; py < y+h; py++ {
		if py < 0 || py >= int32(sh) {
			continue
		}
		for px := x; px < x+w; px++ {
			if px < 0 || px >= int32(sw) {
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
}

// drawCharVFS renders a character using the Chicago 12×12 CFF font at the given integer scale.
func (s *System) drawCharVFS(x, y int32, char byte, color uint32, scale byte) {
	sc := float64(scale)
	if sc < 1 {
		sc = 1
	}
	s.drawCFFRaw(ChicagoCFF, char, x, y, color, 16, sc)
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
