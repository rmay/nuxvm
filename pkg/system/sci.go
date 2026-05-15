package system

import (
	"fmt"
	"os"
)

// System Call Interface (SCI) command handlers
// These implement the Lux SCI word interface for interacting with OS services

func (s *System) handleSCICommand() {
	cmd := s.sciCommand
	arg1 := s.sciArg1
	arg2 := s.sciArg2

	// Clear result
	s.sciResult = 0

	switch cmd {
	// VFS Primitives
	case SCIVFSOpen:
		s.handleSCIVFSOpen(arg1)
	case SCIVFSClose:
		s.handleSCIVFSClose(arg1)
	case SCIVFSRead:
		// arg1: fd (high 16) | length (low 16)
		// arg2: buffer pointer
		fd := arg1 >> 16
		length := arg1 & 0xFFFF
		s.handleSCIVFSRead(fd, arg2, length)
	case SCIVFSWrite:
		// arg1: fd (high 16) | length (low 16)
		// arg2: buffer pointer
		fd := arg1 >> 16
		length := arg1 & 0xFFFF
		s.handleSCIVFSWrite(fd, arg2, length)
	case SCIVFSBind:
		// arg1: fd
		// arg2: target path pointer
		s.handleSCIVFSBind(arg1, arg2)

	// Sound
	case SCIPlaySound:
		s.handleSCIPlaySound(arg1)

	// Process
	case SCIYield:
		s.handleSCIYield()
	case SCIGetPID:
		s.handleSCIGetPID()
	case SCIGetActiveWin:
		s.handleSCIGetActiveWin()
	case SCIDrawCFF:
		s.handleSCIDrawCFF(arg1, arg2)
	case SCIDebugPrint:
		s.handleSCIDebugPrint(arg1)
	}
}

// VFS Handlers

func (s *System) handleSCIVFSOpen(pathPtr int32) {
	path := s.cstring(uint32(pathPtr))
	fd, err := s.vfs.Open(s, path)
	if err != nil {
		s.sciResult = -1
		return
	}
	s.sciResult = fd
}

func (s *System) handleSCIVFSClose(fd int32) {
	err := s.vfs.Close(fd)
	if err != nil {
		s.sciResult = -1
		return
	}
	s.sciResult = 0
}

func (s *System) handleSCIVFSRead(fd int32, bufPtr int32, length int32) {
	if length <= 0 || bufPtr < 0 || int(bufPtr)+int(length) > len(s.memory) {
		s.sciResult = -1
		return
	}

	buf := s.memory[bufPtr : bufPtr+length]
	n, err := s.vfs.Read(fd, buf)
	if err != nil && err.Error() != "EOF" {
		s.sciResult = -1
		return
	}
	s.sciResult = int32(n)
}

func (s *System) handleSCIVFSWrite(fd int32, bufPtr int32, length int32) {
	if length <= 0 || bufPtr < 0 || int(bufPtr)+int(length) > len(s.memory) {
		s.sciResult = -1
		return
	}

	buf := s.memory[bufPtr : bufPtr+length]
	n, err := s.vfs.Write(fd, buf)
	if err != nil {
		s.sciResult = -1
		return
	}
	s.sciResult = int32(n)
}

func (s *System) handleSCIVFSBind(fd int32, pathPtr int32) {
	// TODO: Implement BIND logic in vfs.go
	s.sciResult = -1
}

// handleSCIDebugPrint(ptr) prints a null-terminated string to host stderr
func (s *System) handleSCIDebugPrint(ptr int32) {
	str := s.cstring(uint32(ptr))
	fmt.Fprintf(os.Stderr, "[LUX-DEBUG] %s\n", str)
	s.sciResult = 0
}

// Window Management Handlers

// handleSCICreateWin(namePtr, size) -> winID
// Creates a new window with the given name. namePtr points to a null-terminated string in memory.
func (s *System) handleSCICreateWin(namePtr int32, size int32) {
	if s.Services == nil {
		s.sciResult = -1
		return
	}

	// Extract null-terminated name from memory
	name := s.cstring(uint32(namePtr))

	// Create window with default dimensions (800x600 or size parameter)
	width := int32(800)
	height := int32(600)
	if size > 0 {
		width = size >> 16
		height = size & 0xFFFF
	}

	winID, err := s.Services.CreateWindow(name, width, height)
	if err != nil {
		s.sciResult = -1
		return
	}

	s.sciResult = int32(winID)
}

// handleSCICloseWin(winID) -> status
func (s *System) handleSCICloseWin(winID int32) {
	if s.Services == nil {
		s.sciResult = -1
		return
	}

	s.Services.windowMu.Lock()
	delete(s.Services.windows, WindowID(winID))
	if s.Services.activeWinID == WindowID(winID) {
		s.Services.activeWinID = s.Services.pickBestActive()
	}
	s.Services.windowMu.Unlock()
	s.sciResult = 0
}

// handleSCIMoveWin(winID, position) -> status
// With layout-driven sizing, this is now a no-op. Apps cannot move windows.
func (s *System) handleSCIMoveWin(winID int32, position int32) {
	s.sciResult = 0
}

// handleSCIDrawRect(winID, rectData)
// Drawing primitives are handled by direct framebuffer access for now
func (s *System) handleSCIDrawRect(winID int32, rectData int32) {
	if s.Services != nil {
		if win := s.Services.GetWindowByID(WindowID(winID)); win != nil {
			win.Dirty = true
		}
	}
	s.sciResult = 0
}

// handleSCISetPixel(winID, pixelData)
// pixelData: x in bits 15..0, y in bits 31..16, color in arg2
func (s *System) handleSCISetPixel(winID int32, pixelData int32) {
	if s.Services == nil {
		s.sciResult = -1
		return
	}

	win := s.Services.GetWindowByID(WindowID(winID))
	if win == nil {
		s.sciResult = -1
		return
	}

	x := pixelData & 0xFFFF
	y := (pixelData >> 16) & 0xFFFF
	color := s.sciArg2

	winW := win.ContRgn.Width()
	winH := win.ContRgn.Height()

	if x >= 0 && x < winW && y >= 0 && y < winH {
		offset := (int(y)*int(winW) + int(x)) * 4
		if offset+4 <= len(win.FrameBuf) {
			win.FrameBuf[offset] = byte((color >> 16) & 0xFF)  // R
			win.FrameBuf[offset+1] = byte((color >> 8) & 0xFF) // G
			win.FrameBuf[offset+2] = byte(color & 0xFF)        // B
			win.FrameBuf[offset+3] = 255                       // A
			win.Dirty = true
		}
	}
	s.sciResult = 0
}

// handleSCIGetWinSize(winID) -> (width << 16 | height)
func (s *System) handleSCIGetWinSize(winID int32) {
	if s.Services == nil {
		s.sciResult = 0
		return
	}

	win := s.Services.GetWindowByID(WindowID(winID))
	if win == nil {
		s.sciResult = 0
		return
	}

	s.sciResult = (win.ContRgn.Width() << 16) | win.ContRgn.Height()
}

// handleSCIFocusWin(winID) -> status
func (s *System) handleSCIFocusWin(winID int32) {
	if s.Services == nil {
		s.sciResult = -1
		return
	}

	s.Services.windowMu.Lock()
	if win, exists := s.Services.windows[WindowID(winID)]; exists {
		s.Services.activeWinID = WindowID(winID)
		win.ZOrder = s.Services.maxZOrder() + 1
		s.sciResult = 0
	} else {
		s.sciResult = -1
	}
	s.Services.windowMu.Unlock()
}

// Input Handlers

// handleSCIPollEvent() -> event
// Returns next input event or 0 if none available
// Event format: type in bits 31..24, data in lower bits
func (s *System) handleSCIPollEvent() {
	if s.Services == nil {
		s.sciResult = 0
		return
	}

	evt := s.Services.PollEvent()
	if evt == nil {
		s.sciResult = 0
		return
	}

	// Pack event: type (8 bits) | data (24 bits)
	s.sciResult = (int32(evt.Type) << 24) | (evt.KeyCode & 0xFFFFFF)
}

// File I/O Handlers

// handleSCIOpenFile(pathPtr) -> fileHandle
func (s *System) handleSCIOpenFile(pathPtr int32) {
	if s.Services == nil {
		s.sciResult = -1
		return
	}

	// Extract null-terminated path from memory
	var path string
	if pathPtr >= 0 && int(pathPtr) < len(s.memory) {
		for i := int(pathPtr); i < len(s.memory) && s.memory[i] != 0; i++ {
			path += string(s.memory[i])
		}
	}

	handle, err := s.Services.OpenFile(path)
	if err != nil {
		s.sciResult = -1
	} else {
		s.sciResult = handle
	}
}

// handleSCIReadFile(fileHandle, length) -> bytesRead
func (s *System) handleSCIReadFile(fileHandle int32, length int32) {
	if s.Services == nil {
		s.sciResult = -1
		return
	}

	// s.sciArg2 should contain the buffer pointer in VM memory
	bufPtr := uint32(s.sciArg2)
	if bufPtr+uint32(length) > uint32(len(s.memory)) {
		s.sciResult = -1
		return
	}

	data, err := s.Services.ReadFile(fileHandle, length)
	if err != nil {
		s.sciResult = -1
		return
	}

	copy(s.memory[bufPtr:], data)
	s.sciResult = int32(len(data))
}

// handleSCIWriteFile(fileHandle, length) -> bytesWritten
func (s *System) handleSCIWriteFile(fileHandle int32, length int32) {
	if s.Services == nil {
		s.sciResult = -1
		return
	}

	// s.sciArg2 should contain the buffer pointer in VM memory
	bufPtr := uint32(s.sciArg2)
	if bufPtr+uint32(length) > uint32(len(s.memory)) {
		s.sciResult = -1
		return
	}

	data := make([]byte, length)
	copy(data, s.memory[bufPtr:bufPtr+uint32(length)])

	n, err := s.Services.WriteFile(fileHandle, data)
	if err != nil {
		s.sciResult = -1
		return
	}

	s.sciResult = n
}

// handleSCICloseFile(fileHandle) -> status
func (s *System) handleSCICloseFile(fileHandle int32) {
	if s.Services == nil {
		s.sciResult = -1
		return
	}

	err := s.Services.CloseFile(fileHandle)
	if err != nil {
		s.sciResult = -1
	} else {
		s.sciResult = 0
	}
}

// Sound Handlers

// handleSCIPlaySound(soundID) -> status
func (s *System) handleSCIPlaySound(soundID int32) {
	if s.SoundHandler != nil {
		s.SoundHandler(soundID)
	}
	s.sciResult = 0
}

// Process Control Handlers

// handleSCIYield() - yields control back to the host
func (s *System) handleSCIYield() {
	s.yielded = true
	s.sciResult = 0
}

// handleSCIGetPID() -> processID
func (s *System) handleSCIGetPID() {
	// For now, return a fixed PID of 1 (single process)
	s.sciResult = 1
}

// cstring reads a null-terminated string from memory starting at ptr
func (s *System) cstring(ptr uint32) string {
	var result string
	if ptr >= 0 && int(ptr) < len(s.memory) {
		for i := int(ptr); i < len(s.memory) && s.memory[i] != 0; i++ {
			result += string(s.memory[i])
		}
	}
	return result
}

// handleSCIDrawText(winID, textPtr) renders null-terminated text into a window's framebuffer
func (s *System) handleSCIDrawText(winID int32, textPtr int32) {
	if s.Services == nil {
		s.sciResult = -1
		return
	}

	win := s.Services.GetWindowByID(WindowID(winID))
	if win == nil {
		s.sciResult = -1
		return
	}

	text := s.cstring(uint32(textPtr))
	for _, c := range []byte(text) {
		s.drawCharToWindow(win, c)
	}
	win.Dirty = true
	s.sciResult = 0
}

// drawCharToWindow renders a character into a window's framebuffer at the current cursor position
func (s *System) drawCharToWindow(win *WindowRecord, c byte) {
	glyph := Font[c]
	scale := s.text.getScale()

	originX := int(s.text.cursorX)
	originY := int(s.text.cursorY)

	winW := int(win.ContRgn.Width())
	winH := int(win.ContRgn.Height())

	// Bounds check: stay within window framebuffer
	if originX >= winW || originY >= winH {
		s.advanceCursorInWindow(win)
		return
	}

	r := byte(s.text.color >> 16)
	g := byte(s.text.color >> 8)
	b := byte(s.text.color)

	for row := 0; row < 13; row++ {
		bits := glyph[row]
		if bits == 0 {
			continue
		}
		startY := float64(row) * scale
		endY := float64(row+1) * scale
		for py := int(startY); py < int(endY); py++ {
			y := originY + py
			if y < 0 || y >= winH {
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
					if x < 0 || x >= winW {
						continue
					}
					offset := (y*winW + x) * 4
					if offset+4 <= len(win.FrameBuf) {
						win.FrameBuf[offset] = r
						win.FrameBuf[offset+1] = g
						win.FrameBuf[offset+2] = b
						win.FrameBuf[offset+3] = 255
					}
				}
			}
		}
	}

	s.advanceCursorInWindow(win)
}

// advanceCursorInWindow moves cursor one cell right, wrapping at window edge
func (s *System) advanceCursorInWindow(win *WindowRecord) {
	winW := int(win.ContRgn.Width())
	scale := s.text.getScale()
	cellW := int(7 * scale)
	cellH := int(13 * scale)

	s.text.cursorX += uint16(cellW)
	if int(s.text.cursorX)+cellW > winW {
		s.text.cursorX = 0
		s.text.cursorY += uint16(cellH)
	}
}

// handleSCIGetActiveWin() -> active window ID
func (s *System) handleSCIGetActiveWin() {
	if s.Services == nil {
		s.sciResult = 0
		return
	}
	s.sciResult = int32(s.Services.GetActiveWindowID())
}

// handleSCIDrawCFF(fontPtr, packedData)
// packedData: char in 31..24, x in 23..12, y in 11..0
// Uses current text color and font size (scaling).
// Tile size is hardcoded to 16 for Chicago compatibility.
func (s *System) handleSCIDrawCFF(fontPtr int32, data int32) {
	char := byte((uint32(data) >> 24) & 0xFF)
	x := int32((uint32(data) >> 12) & 0xFFF)
	y := int32(uint32(data) & 0xFFF)
	color := s.text.color
	scale := s.text.getScale()

	// Chicago12x12 is stored in 16x16 tiles.
	tileSize := 16

	s.drawCFFMagnified(uint32(fontPtr), char, x, y, color, tileSize, scale)
	if s.Services != nil {
		if win := s.Services.GetActiveWindow(); win != nil {
			win.Dirty = true
		}
	}
	s.sciResult = 0
}
