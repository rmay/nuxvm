package system

import (
	"encoding/binary"
	"fmt"
	"os"
	"path/filepath"
	"strings"
	"time"

	"github.com/rmay/nuxvm/pkg/vm"
)

const topBarHeight = 24

// Device port addresses (moved from vm.go to own device semantics)
const (
	screenPort     = vm.DeviceMemoryOffset + 0x0020
	audioPort      = vm.DeviceMemoryOffset + 0x0030
	controllerPort = vm.DeviceMemoryOffset + 0x0040
	mousePort      = vm.DeviceMemoryOffset + 0x0050
	filePort       = vm.DeviceMemoryOffset + 0x0060
	dateTimePort   = vm.DeviceMemoryOffset + 0x0070
	rngPort        = vm.DeviceMemoryOffset + 0x0080
	textPort       = vm.DeviceMemoryOffset + 0x0090
	windowPort     = vm.DeviceMemoryOffset + 0x00B0
	sciPort        = vm.DeviceMemoryOffset + 0x00C0

	controllerStatusAddr = controllerPort + 4
	controllerButtonAddr = controllerPort + 8
	controllerKeyAddr    = controllerPort + 12
	audioControlAddr     = audioPort + 4
	rngDataAddr          = rngPort + 4
	dateTimeAddr         = dateTimePort + 4
	screenWidthAddr      = screenPort + 4
	screenHeightAddr     = screenPort + 8
	textAttrAddr         = textPort + 4
	textCursorAddr       = textPort + 8
	textCharAddr         = textPort + 12
	sciCommandAddr       = sciPort + 4
	sciArg1Addr          = sciPort + 8
	sciArg2Addr          = sciPort + 12

	// Vector indices (exported for machine.go use)
	ScreenVectorIdx     = (screenPort - vm.DeviceMemoryOffset) / 16     // 2
	AudioVectorIdx      = (audioPort - vm.DeviceMemoryOffset) / 16      // 3
	ControllerVectorIdx = (controllerPort - vm.DeviceMemoryOffset) / 16 // 4
	MouseVectorIdx      = (mousePort - vm.DeviceMemoryOffset) / 16      // 5
	SCIVectorIdx        = (sciPort - vm.DeviceMemoryOffset) / 16         // 6

	// SCI Command codes
	SCICreateWin      = 1
	SCICloseWin       = 2
	SCIMoveWin        = 3
	SCIDrawRect       = 4
	SCIDrawText       = 5
	SCISetPixel       = 6
	SCIGetWinSize     = 7
	SCIFocusWin       = 8
	SCIPollEvent      = 9
	SCIOpenFile       = 10
	SCIReadFile       = 11
	SCIWriteFile      = 12
	SCICloseFile      = 13
	SCIPlaySound      = 14
	SCIYield          = 15
	SCIGetPID         = 16
)

// fileState tracks an open file or directory for the File device.
// The cursor persists across READ/WRITE operations so large transfers can be
// chunked. Writing a new name pointer resets everything.
type fileState struct {
	name       string        // resolved absolute path, empty if none
	readFile   *os.File      // lazy-opened on first READ
	writeFile  *os.File      // lazy-opened on first WRITE
	dir        []os.DirEntry // populated on first READ of a directory
	dirIndex   int
	readCursor int64
	appendMode bool // set by the append flag on the first WRITE after a name!
}

func (f *fileState) close() {
	if f.readFile != nil {
		f.readFile.Close()
		f.readFile = nil
	}
	if f.writeFile != nil {
		f.writeFile.Close()
		f.writeFile = nil
	}
	f.dir = nil
	f.dirIndex = 0
	f.readCursor = 0
	f.appendMode = false
}

// System implements the vm.Bus interface and provides concrete hardware devices.
type System struct {
	// Reference to VM memory for file operations
	memory []byte

	// Hardware state
	screenPixels []byte
	screenWidth  int32
	screenHeight int32
	rngState     uint32

	// Controller/Mouse state
	controllerButton uint32
	controllerKey    int32
	mouseX           int32
	mouseY           int32
	mouseButton      uint32

	// File device state
	sandboxRoot    string // canonical path; all file ops must stay within
	fileNamePtr    uint32
	fileBufferPtr  uint32
	lastFileResult int32
	file           fileState

	// Text device state
	text textState

	// Window state
	windowScrollY int32

	// SCI (System Call Interface) state
	sciCommand int32
	sciArg1    int32
	sciArg2    int32
	sciResult  int32

	// Vector callbacks (wired by Machine layer)
	getVector func(index int) uint32
	setVector func(index int, addr uint32)

	// Handlers for host integration
	SoundHandler func(soundID int32)

	// OS Services (goroutine-based)
	Services *ServiceManager
}

func NewSystem() *System {
	s := &System{
		screenWidth:  800,
		screenHeight: 600,
		screenPixels: make([]byte, vm.VideoMaxBufferSize),
		rngState:     uint32(time.Now().UnixNano()),
		text: textState{
			scale: 2,
			color: 0xFFFFFF,
		},
		Services: NewServiceManager(),
	}
	// Default the sandbox root to the process cwd so tests and ad-hoc use
	// keep working. cmd/cloister overrides this explicitly at startup.
	if cwd, err := os.Getwd(); err == nil {
		_ = s.SetSandboxRoot(cwd)
	}
	return s
}

// SetVectorCallbacks wires vector register read/write to the CPU (used by Machine).
func (s *System) SetVectorCallbacks(get func(int) uint32, set func(int, uint32)) {
	s.getVector = get
	s.setVector = set
}

// TextScale returns the current text rendering scale.
func (s *System) TextScale() int {
	return int(s.text.scale)
}

// SetSandboxRoot pins the filesystem sandbox to dir. All subsequent file
// operations are resolved relative to this path and rejected if they escape.
// The stored root is canonical (symlinks resolved, absolute).
func (s *System) SetSandboxRoot(dir string) error {
	if dir == "" {
		return fmt.Errorf("sandbox root cannot be empty")
	}
	abs, err := filepath.Abs(dir)
	if err != nil {
		return fmt.Errorf("sandbox root abs: %w", err)
	}
	// EvalSymlinks only works on existing paths; fall back to Abs if the
	// root itself isn't resolvable (unusual, but don't block startup).
	if resolved, err := filepath.EvalSymlinks(abs); err == nil {
		abs = resolved
	}
	s.sandboxRoot = filepath.Clean(abs)
	s.file.close()
	return nil
}

// SandboxRoot returns the canonical filesystem root the File device is pinned
// to. Mainly useful for tests and diagnostics.
func (s *System) SandboxRoot() string {
	return s.sandboxRoot
}

// resolvePath turns a VM-supplied filename into a real path that is guaranteed
// to live inside sandboxRoot. Returns ("", error) on any attempt to escape.
func (s *System) resolvePath(name string) (string, error) {
	if name == "" {
		return "", fmt.Errorf("empty name")
	}
	if filepath.IsAbs(name) {
		return "", fmt.Errorf("absolute path not allowed: %q", name)
	}
	if s.sandboxRoot == "" {
		return "", fmt.Errorf("sandbox root not set")
	}
	joined := filepath.Clean(filepath.Join(s.sandboxRoot, name))
	if !withinRoot(joined, s.sandboxRoot) {
		return "", fmt.Errorf("path escapes sandbox: %q", name)
	}
	// If the path (or any ancestor) exists as a symlink, EvalSymlinks will
	// follow it and we need to re-check containment.
	if resolved, err := filepath.EvalSymlinks(joined); err == nil {
		if !withinRoot(resolved, s.sandboxRoot) {
			return "", fmt.Errorf("symlink escapes sandbox: %q", name)
		}
		return resolved, nil
	}
	return joined, nil
}

// withinRoot reports whether p is root itself or a descendant of root. The
// trailing-separator guard stops "/tmp/rootX" from passing against "/tmp/root".
func withinRoot(p, root string) bool {
	if p == root {
		return true
	}
	return strings.HasPrefix(p, root+string(filepath.Separator))
}

// getActiveFramebuffer returns the framebuffer of the active window.
// If the service manager is not initialized or has no active window, falls back to screenPixels.
func (s *System) getActiveFramebuffer() []byte {
	if s.Services != nil {
		fb := s.Services.GetActiveWindowFramebuf()
		if fb != nil {
			return fb
		}
	}
	return s.screenPixels
}

// getScreenWidth returns the width of the active window, or the global screen width if no service.
func (s *System) getScreenWidth() int32 {
	if s.Services != nil {
		if win := s.Services.GetActiveWindow(); win != nil {
			return win.Width
		}
	}
	return s.screenWidth
}

// getScreenHeight returns the height of the active window, or the global screen height if no service.
func (s *System) getScreenHeight() int32 {
	if s.Services != nil {
		if win := s.Services.GetActiveWindow(); win != nil {
			return win.Height
		}
	}
	return s.screenHeight
}

// setResolution updates both global resolution and active window size.
func (s *System) setResolution(w, h int32) {
	if w > 0 && h > 0 && w*h*4 <= int32(len(s.screenPixels)) {
		s.screenWidth = w
		s.screenHeight = h
		if s.Services != nil {
			s.Services.ResizeActiveWindow(w, h)
		}
	}
}

func (s *System) SetResolution(w, h int32) {
	s.setResolution(w, h)
}

func (s *System) ScreenWidth() int32 {
	return s.getScreenWidth()
}

func (s *System) ScreenHeight() int32 {
	return s.getScreenHeight()
}

// SetMemory provides the system with access to the VM's memory slice.
func (s *System) SetMemory(mem []byte) {
	s.memory = mem
}

// getCString reads a null-terminated string from VM memory starting at ptr.
func (s *System) getCString(ptr uint32) string {
	if ptr == 0 || int(ptr) >= len(s.memory) {
		return ""
	}
	var res []byte
	for i := int(ptr); i < len(s.memory) && s.memory[i] != 0; i++ {
		res = append(res, s.memory[i])
	}
	return string(res)
}

// File device command codes (high byte of the +12 register).
const (
	fileCmdRead   = 1
	fileCmdWrite  = 2
	fileCmdStat   = 3
	fileCmdDelete = 4
	fileCmdSeek   = 5
)

// handleFileCommand executes a File device operation. The 32-bit value written
// to FilePort+12 is split into:
//
//	bits 31..24: command
//	bits 23..16: flags (bit 0 = append)
//	bits 15..0:  length
func (s *System) handleFileCommand(cmd, flags, length uint32) {
	// STAT / DELETE / SEEK don't need an open handle or an in-range buffer.
	switch cmd {
	case fileCmdStat:
		s.fileStat(length)
		return
	case fileCmdDelete:
		s.fileDelete()
		return
	case fileCmdSeek:
		s.fileSeek()
		return
	}

	path, err := s.resolvePath(s.getCString(s.fileNamePtr))
	if err != nil {
		s.lastFileResult = -1
		return
	}

	switch cmd {
	case fileCmdRead:
		s.fileRead(path, length)
	case fileCmdWrite:
		s.fileWrite(path, flags, length)
	default:
		s.lastFileResult = -1
	}
}

// fileRead reads up to length bytes from the cursor into buffer. For a
// directory, emits one formatted entry per call.
func (s *System) fileRead(path string, length uint32) {
	if length == 0 {
		s.lastFileResult = 0
		return
	}
	if s.fileBufferPtr == 0 || uint64(s.fileBufferPtr)+uint64(length) > uint64(len(s.memory)) {
		s.lastFileResult = -1
		return
	}

	// If a writer is still open on this name, flush it so the reader sees
	// everything that was written.
	if s.file.writeFile != nil {
		s.file.writeFile.Close()
		s.file.writeFile = nil
	}

	// First op after a name! — decide whether this is a file or directory.
	if s.file.readFile == nil && s.file.dir == nil {
		info, err := os.Stat(path)
		if err != nil {
			s.lastFileResult = -1
			return
		}
		if info.IsDir() {
			entries, err := os.ReadDir(path)
			if err != nil {
				s.lastFileResult = -1
				return
			}
			s.file.dir = entries
			s.file.dirIndex = 0
		} else {
			f, err := os.Open(path)
			if err != nil {
				s.lastFileResult = -1
				return
			}
			s.file.readFile = f
		}
	}

	if s.file.dir != nil {
		s.lastFileResult = s.readDirEntry(length)
		return
	}

	buf := s.memory[s.fileBufferPtr : s.fileBufferPtr+length]
	n, err := s.file.readFile.ReadAt(buf, s.file.readCursor)
	s.file.readCursor += int64(n)
	if err != nil && n == 0 {
		// EOF with nothing read → 0. Any other error → -1.
		if strings.Contains(err.Error(), "EOF") {
			s.lastFileResult = 0
			return
		}
		s.lastFileResult = -1
		return
	}
	s.lastFileResult = int32(n)
}

// readDirEntry formats the next directory entry into the VM buffer. Returns
// bytes written, or 0 at end of listing.
func (s *System) readDirEntry(length uint32) int32 {
	if s.file.dirIndex >= len(s.file.dir) {
		return 0
	}
	entry := s.file.dir[s.file.dirIndex]
	s.file.dirIndex++

	detail := "----"
	if !entry.IsDir() {
		if info, err := entry.Info(); err == nil {
			size := info.Size()
			switch {
			case size > 0xFFFF:
				detail = "????"
			default:
				detail = fmt.Sprintf("%04x", size)
			}
		} else {
			detail = "!!!!"
		}
	}
	line := fmt.Sprintf("%s %s\n", detail, entry.Name())
	lineBytes := []byte(line)
	if uint32(len(lineBytes)) > length {
		lineBytes = lineBytes[:length]
	}
	copy(s.memory[s.fileBufferPtr:], lineBytes)
	return int32(len(lineBytes))
}

// fileWrite writes length bytes from buffer to the current file.
func (s *System) fileWrite(path string, flags, length uint32) {
	if length == 0 {
		s.lastFileResult = 0
		return
	}
	if s.fileBufferPtr == 0 || uint64(s.fileBufferPtr)+uint64(length) > uint64(len(s.memory)) {
		s.lastFileResult = -1
		return
	}

	if s.file.writeFile == nil {
		mode := os.O_WRONLY | os.O_CREATE | os.O_TRUNC
		if flags&1 != 0 {
			mode = os.O_WRONLY | os.O_CREATE | os.O_APPEND
			s.file.appendMode = true
		}
		f, err := os.OpenFile(path, mode, 0644)
		if err != nil {
			s.lastFileResult = -1
			return
		}
		s.file.writeFile = f
	}

	data := s.memory[s.fileBufferPtr : s.fileBufferPtr+length]
	n, err := s.file.writeFile.Write(data)
	if err != nil {
		s.lastFileResult = -1
		return
	}
	s.lastFileResult = int32(n)
}

// fileStat returns the size of the named file, or -1 on error. If buffer is
// non-zero, also writes a 4-char Varvara-style detail string there.
func (s *System) fileStat(length uint32) {
	path, err := s.resolvePath(s.getCString(s.fileNamePtr))
	if err != nil {
		s.lastFileResult = -1
		return
	}
	info, err := os.Stat(path)
	if err != nil {
		s.lastFileResult = -1
		return
	}

	if s.fileBufferPtr != 0 && length >= 4 && uint64(s.fileBufferPtr)+4 <= uint64(len(s.memory)) {
		detail := "----"
		if !info.IsDir() {
			size := info.Size()
			if size > 0xFFFF {
				detail = "????"
			} else {
				detail = fmt.Sprintf("%04x", size)
			}
		}
		copy(s.memory[s.fileBufferPtr:], []byte(detail))
	}
	s.lastFileResult = int32(info.Size())
}

// fileDelete removes the named file and resets state.
func (s *System) fileDelete() {
	path, err := s.resolvePath(s.getCString(s.fileNamePtr))
	if err != nil {
		s.lastFileResult = -1
		return
	}
	s.file.close()
	if err := os.Remove(path); err != nil {
		s.lastFileResult = -1
		return
	}
	s.file.name = ""
	s.lastFileResult = 0
}

// fileSeek rewinds the read/write cursor to 0 and reopens handles lazily.
func (s *System) fileSeek() {
	s.file.close()
	s.lastFileResult = 0
}

// Read implements vm.Bus.Read
func (s *System) Read(address uint32) (int32, error) {
	// Port vector registers (offset+0 of any 16-byte device block)
	if address >= vm.DeviceMemoryOffset && address < vm.DeviceMemoryOffset+vm.DeviceMemorySize {
		offset := address - vm.DeviceMemoryOffset
		if offset%16 == 0 && s.getVector != nil {
			index := int(offset / 16)
			return int32(s.getVector(index)), nil
		}
	}

	// Screen (Framebuffer)
	if address >= vm.VideoFramebufferStart && address < vm.VideoFramebufferEnd {
		offset := address - vm.VideoFramebufferStart
		// Read from active window's framebuffer
		fb := s.getActiveFramebuffer()
		if fb == nil || offset+4 > uint32(len(fb)) {
			return 0, fmt.Errorf("framebuffer read out of bounds")
		}
		return int32(binary.BigEndian.Uint32(fb[offset : offset+4])), nil
	}

	// Screen registers
	if address == screenWidthAddr {
		return s.screenWidth, nil
	}
	if address == screenHeightAddr {
		return s.screenHeight, nil
	}

	// Controller registers:
	if address == controllerStatusAddr {
		var val int32 = 0
		if s.controllerKey != 0 || s.controllerButton != 0 {
			val = 1
		}
		return val, nil
	}
	if address == controllerButtonAddr {
		return int32(s.controllerButton), nil
	}
	if address == controllerKeyAddr {
		return s.controllerKey, nil
	}

	// Mouse registers:
	if address == mousePort+4 { // Mouse X
		return s.mouseX, nil
	}
	if address == mousePort+8 { // Mouse Y
		return s.mouseY, nil
	}
	if address == mousePort+12 { // Mouse Buttons
		return int32(s.mouseButton), nil
	}

	// File registers:
	if address == filePort+4 { // FileNamePtr
		return int32(s.fileNamePtr), nil
	}
	if address == filePort+8 { // FileBufferPtr
		return int32(s.fileBufferPtr), nil
	}
	if address == filePort+12 { // Length / Result
		return s.lastFileResult, nil
	}

	// Audio Control read: (stubs for now, can be expanded to return last played)
	if address == audioControlAddr {
		return 0, nil
	}

	// RNG register read: apply Xorshift32
	if address == rngDataAddr {
		x := s.rngState
		x ^= x << 13
		x ^= x >> 17
		x ^= x << 5
		s.rngState = x
		return int32(x), nil
	}

	// Text device:
	if address == textAttrAddr {
		return int32(s.text.attrPacked()), nil
	}
	if address == textCursorAddr {
		return int32(s.text.cursorPacked()), nil
	}
	if address == textCharAddr {
		return int32(s.text.lastChar), nil
	}

	// DateTime register read:
	if address == dateTimeAddr { // 0x3074: Unix timestamp
		return int32(time.Now().Unix()), nil
	}
	if address == dateTimePort+8 { // 0x3078: Packed Date (Year << 16 | Month << 8 | Day)
		now := time.Now()
		val := (int32(now.Year()) << 16) | (int32(now.Month()) << 8) | int32(now.Day())
		return val, nil
	}
	if address == dateTimePort+12 { // 0x307C: Packed Time (Hour << 16 | Minute << 8 | Second)
		now := time.Now()
		val := (int32(now.Hour()) << 16) | (int32(now.Minute()) << 8) | int32(now.Second())
		return val, nil
	}

	// Window device:
	if address == windowPort+4 { // scroll-y
		return s.windowScrollY, nil
	}
	if address == windowPort+8 { // content-height
		return s.screenHeight - topBarHeight, nil
	}
	if address == windowPort+12 { // top-bar-height
		return topBarHeight, nil
	}

	// SCI (System Call Interface) device:
	if address == sciPort { // Vector address (result from last SCI call)
		return s.sciResult, nil
	}
	if address == sciCommandAddr {
		return s.sciCommand, nil
	}
	if address == sciArg1Addr {
		return s.sciArg1, nil
	}
	if address == sciArg2Addr {
		return s.sciArg2, nil
	}

	return 0, fmt.Errorf("system: unhandled read at 0x%04X", address)
}

// Write implements vm.Bus.Write
func (s *System) Write(address uint32, value int32) error {
	// Port vector registers (offset+0 of any 16-byte device block)
	if address >= vm.DeviceMemoryOffset && address < vm.DeviceMemoryOffset+vm.DeviceMemorySize {
		offset := address - vm.DeviceMemoryOffset
		if offset%16 == 0 && s.setVector != nil {
			index := int(offset / 16)
			s.setVector(index, uint32(value))
			return nil
		}
	}

	// Screen (Framebuffer)
	if address >= vm.VideoFramebufferStart && address < vm.VideoFramebufferEnd {
		offset := address - vm.VideoFramebufferStart
		// Write to active window's framebuffer
		fb := s.getActiveFramebuffer()
		if fb == nil || offset+4 > uint32(len(fb)) {
			return fmt.Errorf("framebuffer write out of bounds")
		}
		binary.BigEndian.PutUint32(fb[offset:offset+4], uint32(value))
		return nil
	}

	// Screen dimensions (allow writing to resize)
	if address == screenWidthAddr {
		s.setResolution(value, s.getScreenHeight())
		return nil
	}
	if address == screenHeightAddr {
		s.setResolution(s.getScreenWidth(), value)
		return nil
	}

	// File registers:
	if address == filePort+4 {
		// Writing a new filename pointer closes any open handle and clears
		// cursor state, mirroring Varvara's File/name semantics.
		s.fileNamePtr = uint32(value)
		s.file.close()
		return nil
	}
	if address == filePort+8 {
		s.fileBufferPtr = uint32(value)
		return nil
	}
	if address == filePort+12 {
		// cmd   : bits 31..24
		// flags : bits 23..16 (bit 0 = append)
		// length: bits 15..0
		cmd := uint32(value) >> 24
		flags := (uint32(value) >> 16) & 0xFF
		length := uint32(value) & 0xFFFF
		s.handleFileCommand(cmd, flags, length)
		return nil
	}

	// Text device:
	if address == textAttrAddr {
		s.text.setAttr(uint32(value))
		return nil
	}
	if address == textCursorAddr {
		s.text.setCursor(uint32(value))
		return nil
	}
	if address == textCharAddr {
		s.drawChar(byte(value & 0xFF))
		return nil
	}

	// Window device:
	if address == windowPort+4 { // scroll-y
		s.windowScrollY = value
		return nil
	}

	// Mouse and DateTime registers are read-only
	if address == mousePort+4 || address == mousePort+8 || address == mousePort+12 {
		return fmt.Errorf("system: mouse position/button registers are read-only")
	}
	if address == dateTimePort+4 || address == dateTimePort+8 || address == dateTimePort+12 {
		return fmt.Errorf("system: datetime registers are read-only")
	}

	// Audio Control write
	if address == audioControlAddr {
		if s.SoundHandler != nil {
			s.SoundHandler(value)
		}
		return nil
	}

	// RNG register write: seed the state.
	if address == rngDataAddr {
		if value == 0 {
			s.rngState = 1
		} else {
			s.rngState = uint32(value)
		}
		return nil
	}

	// SCI (System Call Interface) device:
	if address == sciCommandAddr {
		s.sciCommand = value
		return nil
	}
	if address == sciArg1Addr {
		s.sciArg1 = value
		return nil
	}
	if address == sciArg2Addr {
		s.sciArg2 = value
		// Trigger SCI command handler when arg2 is written
		s.handleSCICommand()
		return nil
	}

	return fmt.Errorf("system: unhandled write at 0x%04X", address)
}

// Host Methods to set state

func (s *System) SetKey(key int32) {
	s.controllerKey = key
}

func (s *System) SetButton(mask uint32) {
	s.controllerButton = mask
}

func (s *System) SetMouse(x, y int32, button uint32) {
	s.mouseX = x
	s.mouseY = y
	s.mouseButton = button
}

func (s *System) MouseButton() uint32 {
	return s.mouseButton
}

func (s *System) Framebuffer() []byte {
	fb := s.getActiveFramebuffer()
	w := s.getScreenWidth()
	h := s.getScreenHeight()
	size := w * h * 4
	if size > int32(len(fb)) {
		size = int32(len(fb))
	}
	return fb[:size]
}

func (s *System) DebugInfo() string {
	return fmt.Sprintf("Controller: K=%d B=0x%X\nMouse: %d,%d B=0x%X\nFile: Ptr=0x%X Buf=0x%X Res=%d Cur=%d\nSandbox: %s\nRNG: 0x%08X",
		s.controllerKey, s.controllerButton,
		s.mouseX, s.mouseY, s.mouseButton,
		s.fileNamePtr, s.fileBufferPtr, s.lastFileResult, s.file.readCursor,
		s.sandboxRoot,
		s.rngState)
}

func (s *System) MMIORegisters() []struct {
	Name  string
	Value int32
} {
	return []struct {
		Name  string
		Value int32
	}{
		{"SYS_CTRL", 0},
		{"CON_OUT", 0},
		{"SCR_VEC", 0},
		{"SCR_W", s.screenWidth},
		{"SCR_H", s.screenHeight},
		{"AUD_CTRL", 0},
		{"CTRL_BTN", int32(s.controllerButton)},
		{"CTRL_KEY", s.controllerKey},
		{"MSE_X", s.mouseX},
		{"MSE_Y", s.mouseY},
		{"MSE_BTN", int32(s.mouseButton)},
		{"FILE_PTR", int32(s.fileNamePtr)},
		{"FILE_BUF", int32(s.fileBufferPtr)},
		{"FILE_RES", s.lastFileResult},
		{"FILE_CUR", int32(s.file.readCursor)},
		{"TEXT_ATTR", int32(s.text.attrPacked())},
		{"TEXT_CUR", int32(s.text.cursorPacked())},
		{"TEXT_CHAR", int32(s.text.lastChar)},
		{"RNG_DATA", int32(s.rngState)},
	}
}

