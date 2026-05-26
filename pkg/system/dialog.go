package system

import (
	"image"
	"image/color"
	"path"
	"sort"
	"strings"
	"time"
)

type fileEntry struct {
	name  string
	isDir bool
}

type fileDialogModal struct {
	s             *System
	replyChan     chan string
	entries       []fileEntry
	selected      int
	path          string
	scrollPos     int
	lastClickTime time.Time
	lastClickIdx  int
}

var (
	iconFloppy = []uint16{
		0b1111111111110000,
		0b1000000000011000,
		0b1011111111010100,
		0b1010000001010010,
		0b1010000001010001,
		0b1011111111010001,
		0b1000000000010001,
		0b1000000000010001,
		0b1001111110010001,
		0b1001000010010001,
		0b1001000010010001,
		0b1001111110010001,
		0b1000000000010001,
		0b1111111111110001,
		0b0000000000001111,
	}
	iconFolder = []uint16{
		0b0111000000000000,
		0b1000111111111100,
		0b1000000000000110,
		0b1111111111111111,
		0b1000000000000001,
		0b1000000000000001,
		0b1000000000000001,
		0b1000000000000001,
		0b1000000000000001,
		0b1000000000000001,
		0b1111111111111111,
	}
	iconDoc = []uint16{
		0b1111111111000000,
		0b1000000001100000,
		0b1000000001010000,
		0b1000000001111000,
		0b1000000000000100,
		0b1000000000000100,
		0b1000000000000100,
		0b1000000000000100,
		0b1000000000000100,
		0b1000000000000100,
		0b1111111111111100,
	}
	iconUpArrow = []uint16{
		0b000001000000,
		0b000011100000,
		0b000111110000,
		0b001111111000,
		0b011111111100,
	}
	iconDownArrow = []uint16{
		0b011111111100,
		0b001111111000,
		0b000111110000,
		0b000011100000,
		0b000001000000,
	}
)

func newFileDialogModal(s *System, replyChan chan string) *fileDialogModal {
	m := &fileDialogModal{
		s:         s,
		replyChan: replyChan,
		path:      ".",
	}
	m.refreshFiles()
	return m
}

func (m *fileDialogModal) refreshFiles() {
	if m.s.Services == nil {
		m.entries = []fileEntry{{name: "error: services not available", isDir: false}}
		return
	}
	
	names, err := m.s.Services.listDirectoryLocked(m.path)
	if err != nil {
		m.entries = []fileEntry{{name: "error: " + err.Error(), isDir: false}}
		return
	}

	m.entries = nil
	if m.path != "." && m.path != "/" {
		m.entries = append(m.entries, fileEntry{name: "..", isDir: true})
	}

	for _, name := range names {
		isDir := strings.HasSuffix(name, "/")
		cleanName := strings.TrimSuffix(name, "/")
		m.entries = append(m.entries, fileEntry{name: cleanName, isDir: isDir})
	}
	
	sort.Slice(m.entries, func(i, j int) bool {
		priority := func(e fileEntry) int {
			if e.name == ".." {
				return 0
			}
			isHidden := strings.HasPrefix(e.name, ".")
			if e.isDir {
				if isHidden {
					return 1
				}
				return 2
			}
			if isHidden {
				return 3
			}
			return 4
		}

		pi := priority(m.entries[i])
		pj := priority(m.entries[j])

		if pi != pj {
			return pi < pj
		}
		// Case-insensitive alphabetical
		li := strings.ToLower(m.entries[i].name)
		lj := strings.ToLower(m.entries[j].name)
		if li != lj {
			return li < lj
		}
		return m.entries[i].name < m.entries[j].name
	})
}

func (m *fileDialogModal) openSelected() bool {
	if m.selected < 0 || m.selected >= len(m.entries) {
		return true
	}

	entry := m.entries[m.selected]
	if entry.isDir {
		if entry.name == ".." {
			m.path = path.Dir(m.path)
		} else {
			m.path = path.Join(m.path, entry.name)
		}
		m.selected = 0
		m.scrollPos = 0
		m.refreshFiles()
		return true // Still active
	}

	// It's a file
	fullPath := path.Join(m.path, entry.name)
	m.replyChan <- fullPath
	return false // Close modal
}

func (m *fileDialogModal) Update(evt *InputEvent) bool {
	winW := m.s.getScreenWidthLocked()
	winH := m.s.getScreenHeightLocked()
	dw, dh := int32(400), int32(300)
	dx := (winW - dw) / 2
	dy := (winH - dh) / 2

	lbX, lbY, lbW, lbH := dx+20, dy+40, int32(330), int32(210)
	sbW := int32(20)
	itemH := int32(20)
	visibleCount := int(lbH / itemH)

	if evt.Type == InputMouseDown {
		// 1. Buttons
		btnW, btnH := int32(70), int32(30)
		bx, by := dx+dw-100, dy+dh-45

		// Open
		if evt.MouseX >= bx && evt.MouseX <= bx+btnW && evt.MouseY >= by && evt.MouseY <= by+btnH {
			return m.openSelected()
		}

		// Cancel
		bx -= 90
		if evt.MouseX >= bx && evt.MouseX <= bx+btnW && evt.MouseY >= by && evt.MouseY <= by+btnH {
			m.replyChan <- "cancel"
			return false
		}

		// 2. Scrollbar
		if evt.MouseX >= lbX+lbW-sbW && evt.MouseX <= lbX+lbW {
			if evt.MouseY >= lbY && evt.MouseY <= lbY+sbW {
				// Up Arrow
				m.scrollPos--
				if m.scrollPos < 0 {
					m.scrollPos = 0
				}
			} else if evt.MouseY >= lbY+lbH-sbW && evt.MouseY <= lbY+lbH {
				// Down Arrow
				m.scrollPos++
				if m.scrollPos > len(m.entries)-visibleCount {
					m.scrollPos = len(m.entries) - visibleCount
					if m.scrollPos < 0 {
						m.scrollPos = 0
					}
				}
			}
		}

		// 3. File List
		if evt.MouseX >= lbX && evt.MouseX <= lbX+lbW-sbW {
			if evt.MouseY >= lbY && evt.MouseY <= lbY+lbH {
				idx := int((evt.MouseY - lbY) / itemH)
				if idx >= 0 && idx < visibleCount {
					actualIdx := idx + m.scrollPos
					if actualIdx < len(m.entries) {
						now := time.Now()
						if actualIdx == m.lastClickIdx && now.Sub(m.lastClickTime) < 500*time.Millisecond {
							m.selected = actualIdx
							return m.openSelected()
						}
						m.selected = actualIdx
						m.lastClickIdx = actualIdx
						m.lastClickTime = now
					}
				}
			}
		}
	}

	if evt.Type == InputWheel {
		m.scrollPos -= int(evt.WheelY)
		if m.scrollPos < 0 {
			m.scrollPos = 0
		}
		maxScroll := len(m.entries) - visibleCount
		if maxScroll < 0 {
			maxScroll = 0
		}
		if m.scrollPos > maxScroll {
			m.scrollPos = maxScroll
		}
		return true
	}

	if evt.Type == InputMouseMove {
		return true // Consume all mouse moves while modal is active
	}

	if evt.Type == InputKeyDown {
		if evt.KeyCode == 17 { // Arrow Up
			m.selected--
			if m.selected < 0 {
				m.selected = 0
			}
			if m.selected < m.scrollPos {
				m.scrollPos = m.selected
			}
		} else if evt.KeyCode == 18 { // Arrow Down
			m.selected++
			if m.selected >= len(m.entries) {
				m.selected = len(m.entries) - 1
			}
			if m.selected >= m.scrollPos+visibleCount {
				m.scrollPos = m.selected - visibleCount + 1
			}
		} else if evt.KeyCode == 13 { // Enter
			return m.openSelected()
		} else if evt.KeyCode == 27 { // Esc
			m.replyChan <- "cancel"
			return false
		}
	}

	return true
}

func (m *fileDialogModal) drawBitmap(dst *image.RGBA, x, y int32, bitmap []uint16, colorVal uint32) {
	r := byte((colorVal >> 16) & 0xFF)
	g := byte((colorVal >> 8) & 0xFF)
	b := byte(colorVal & 0xFF)
	a := byte(255)

	bounds := dst.Bounds()

	for row, bits := range bitmap {
		for col := 0; col < 16; col++ {
			if (bits >> (15 - col)) & 1 != 0 {
				px, py := x+int32(col), y+int32(row)
				if px < int32(bounds.Min.X) || px >= int32(bounds.Max.X) || py < int32(bounds.Min.Y) || py >= int32(bounds.Max.Y) {
					continue
				}
				offset := (int(py)*dst.Stride/4 + int(px)) * 4
				if offset >= 0 && offset+4 <= len(dst.Pix) {
					dst.Pix[offset] = r
					dst.Pix[offset+1] = g
					dst.Pix[offset+2] = b
					dst.Pix[offset+3] = a
				}
			}
		}
	}
}

func (m *fileDialogModal) Draw(fb []byte, w, h int32) {
	// Use parameters w, h directly instead of calling getScreenWidthLocked
	dw, dh := int32(400), int32(300)
	dx := (w - dw) / 2
	dy := (h - dh) / 2

	dst := &image.RGBA{
		Pix:    fb,
		Stride: int(w) * 4,
		Rect:   image.Rect(0, 0, int(w), int(h)),
	}

	// 1. Double-bordered main box
	m.fillRect(dst, dx, dy, dw, dh, 0xFFFFFF)
	m.drawRect(dst, dx, dy, dw, dh, 0x000000)
	m.drawRect(dst, dx+2, dy+2, dw-4, dh-4, 0x000000)

	// 2. Title Bar (Floating)
	titleW := int32(160)
	tx := dx + (dw-titleW)/2
	ty := dy - 12
	m.fillRect(dst, tx, ty, titleW, 24, 0xFFFFFF)
	m.drawRect(dst, tx, ty, titleW, 24, 0x000000)
	m.drawBitmap(dst, tx+6, ty+4, iconFloppy, 0x000000)
	m.drawStr(dst, tx+28, ty+6, "System Startup", 0x000000)

	// 3. File List Box
	lbX, lbY, lbW, lbH := dx+20, dy+40, int32(330), int32(210)
	m.drawRect(dst, lbX, lbY, lbW, lbH, 0x000000)
	
	// Scrollbar area
	sbW := int32(20)
	m.drawRect(dst, lbX+lbW-sbW, lbY, sbW, lbH, 0x000000)
	// Up Arrow Box
	m.drawRect(dst, lbX+lbW-sbW, lbY, sbW, sbW, 0x000000)
	m.drawBitmap(dst, lbX+lbW-sbW+4, lbY+7, iconUpArrow, 0x000000)
	// Down Arrow Box
	m.drawRect(dst, lbX+lbW-sbW, lbY+lbH-sbW, sbW, sbW, 0x000000)
	m.drawBitmap(dst, lbX+lbW-sbW+4, lbY+lbH-sbW+7, iconDownArrow, 0x000000)

	// Entries
	itemH := int32(20)
	visibleCount := int(lbH / itemH)
	for i := 0; i < visibleCount; i++ {
		idx := i + m.scrollPos
		if idx >= len(m.entries) {
			break
		}
		entry := m.entries[idx]
		
		color := uint32(0x000000)
		if idx == m.selected {
			m.fillRect(dst, lbX+1, lbY+1+int32(i)*itemH, lbW-sbW-1, itemH-1, 0x000000)
			color = 0xFFFFFF
		}

		icon := iconDoc
		if entry.isDir {
			icon = iconFolder
		}
		m.drawBitmap(dst, lbX+5, lbY+4+int32(i)*itemH, icon, color)
		m.drawStr(dst, lbX+25, lbY+4+int32(i)*itemH, entry.name, color)
	}

	// 4. Buttons
	// Open (Default - thick border)
	btnW, btnH := int32(70), int32(30)
	bx, by := dx+dw-100, dy+dh-45
	m.drawRect(dst, bx-2, by-2, btnW+4, btnH+4, 0x000000) // Thick border
	m.fillRect(dst, bx, by, btnW, btnH, 0x000000)
	m.fillRect(dst, bx+1, by+1, btnW-2, btnH-2, 0xEEEEEE)
	m.drawStr(dst, bx+15, by+7, "Open", 0x000000)

	// Cancel
	bx -= 90
	m.fillRect(dst, bx, by, btnW, btnH, 0x000000)
	m.fillRect(dst, bx+1, by+1, btnW-2, btnH-2, 0xEEEEEE)
	m.drawStr(dst, bx+10, by+7, "Cancel", 0x000000)
}

func (m *fileDialogModal) fillRect(dst *image.RGBA, x, y, w, h int32, colorVal uint32) {
	r := byte((colorVal >> 16) & 0xFF)
	g := byte((colorVal >> 8) & 0xFF)
	b := byte(colorVal & 0xFF)
	a := byte(255)

	bounds := dst.Bounds()

	for py := y; py < y+h; py++ {
		if py < int32(bounds.Min.Y) || py >= int32(bounds.Max.Y) {
			continue
		}
		for px := x; px < x+w; px++ {
			if px < int32(bounds.Min.X) || px >= int32(bounds.Max.X) {
				continue
			}
			offset := (int(py)*dst.Stride/4 + int(px)) * 4
			if offset >= 0 && offset+4 <= len(dst.Pix) {
				dst.Pix[offset] = r
				dst.Pix[offset+1] = g
				dst.Pix[offset+2] = b
				dst.Pix[offset+3] = a
			}
		}
	}
}

func (m *fileDialogModal) drawRect(dst *image.RGBA, x, y, w, h int32, color uint32) {
	m.fillRect(dst, x, y, w, 1, color)
	m.fillRect(dst, x, y+h-1, w, 1, color)
	m.fillRect(dst, x, y, 1, h, color)
	m.fillRect(dst, x+w-1, y, 1, h, color)
}

func (m *fileDialogModal) drawStr(dst *image.RGBA, x, y int32, s string, color uint32) {
	renderer := &BasicFontRenderer{}
	for i := 0; i < len(s); i++ {
		renderer.DrawGlyph(dst, x+int32(i*7), y, s[i], colorFromUint32(color), 1.0)
	}
}

func colorFromUint32(c uint32) color.RGBA {
	return color.RGBA{
		R: uint8(c >> 16),
		G: uint8(c >> 8),
		B: uint8(c),
		A: 255,
	}
}
