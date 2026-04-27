package system

// System Call Interface (SCI) command handlers
// These implement the Lux SCI word interface for interacting with OS services

func (s *System) handleSCICommand() {
	cmd := s.sciCommand
	arg1 := s.sciArg1
	arg2 := s.sciArg2

	// Clear result
	s.sciResult = 0

	switch cmd {
	// Window Management
	case SCICreateWin:
		s.handleSCICreateWin(arg1, arg2)
	case SCICloseWin:
		s.handleSCICloseWin(arg1)
	case SCIMoveWin:
		s.handleSCIMoveWin(arg1, arg2)
	case SCIDrawRect:
		s.handleSCIDrawRect(arg1, arg2)
	case SCIDrawText:
		s.handleSCIDrawText(arg1, arg2)
	case SCISetPixel:
		s.handleSCISetPixel(arg1, arg2)
	case SCIGetWinSize:
		s.handleSCIGetWinSize(arg1)
	case SCIFocusWin:
		s.handleSCIFocusWin(arg1)

	// Input
	case SCIPollEvent:
		s.handleSCIPollEvent()

	// File I/O
	case SCIOpenFile:
		s.handleSCIOpenFile(arg1)
	case SCIReadFile:
		s.handleSCIReadFile(arg1, arg2)
	case SCIWriteFile:
		s.handleSCIWriteFile(arg1, arg2)
	case SCICloseFile:
		s.handleSCICloseFile(arg1)

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
	}
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
			win.FrameBuf[offset] = byte((color >> 16) & 0xFF)     // R
			win.FrameBuf[offset+1] = byte((color >> 8) & 0xFF)    // G
			win.FrameBuf[offset+2] = byte(color & 0xFF)            // B
			win.FrameBuf[offset+3] = 255                           // A
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
	// This would be implemented with actual file reading
	s.sciResult = 0
}

// handleSCIWriteFile(fileHandle, length) -> bytesWritten
func (s *System) handleSCIWriteFile(fileHandle int32, length int32) {
	// This would be implemented with actual file writing
	s.sciResult = 0
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
	// Yielding is handled at the CPU level; this is a no-op at the System level
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
	s.sciResult = 0
}

// drawCharToWindow renders a character into a window's framebuffer at the current cursor position
func (s *System) drawCharToWindow(win *WindowRecord, c byte) {
	// ...
	glyph := Font[c]
	x := int(s.text.cursorX) * 6
	y := int(s.text.cursorY) * 8

	winW := win.ContRgn.Width()
	winH := win.ContRgn.Height()

	// Bounds check: stay within window framebuffer
	if x+6 >= int(winW) || y+8 >= int(winH) {
		s.advanceCursorInWindow(win)
		return
	}

	// Draw character glyph into window framebuffer (6px wide x 8px tall, black text)
	for row := 0; row < 8; row++ {
		bits := glyph[row]
		if bits == 0 {
			continue
		}
		for col := 0; col < 8; col++ {
			if (bits & (1 << col)) == 0 {
				continue
			}
			px := x + col
			py := y + row
			if px < int(winW) && py < int(winH) {
				offset := (py*int(winW) + px) * 4
				if offset+4 <= len(win.FrameBuf) {
					win.FrameBuf[offset] = 0     // R
					win.FrameBuf[offset+1] = 0   // G
					win.FrameBuf[offset+2] = 0   // B
					win.FrameBuf[offset+3] = 255 // A
				}
			}
		}
	}

	s.advanceCursorInWindow(win)
}

// advanceCursorInWindow moves cursor one cell right, wrapping at window edge
func (s *System) advanceCursorInWindow(win *WindowRecord) {
	winW := win.ContRgn.Width()
	charsPerRow := int(winW) / 6
	if charsPerRow < 1 {
		charsPerRow = 1
	}
	s.text.cursorX++
	if int(s.text.cursorX) >= charsPerRow {
		s.text.cursorX = 0
		s.text.cursorY++
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
