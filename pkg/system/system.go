package system

import (
	"encoding/binary"
	"fmt"
	"os"
	"strings"
	"time"

	"github.com/rmay/nuxvm/pkg/vm"
)

// System implements the vm.Bus interface and provides concrete hardware devices.
type System struct {
	// Reference to VM memory for file operations
	memory []byte

	// Hardware state
	screenPixels []byte
	rngState     uint32
	
	// Controller/Mouse state
	controllerButton uint32
	controllerKey    int32
	mouseX           int32
	mouseY           int32
	mouseButton      uint32

	// File state
	fileNamePtr    uint32
	fileBufferPtr  uint32
	fileLength     uint32
	lastFileResult int32

	// Handlers for host integration
	SoundHandler func(soundID int32)
}

func NewSystem() *System {
	return &System{
		screenPixels: make([]byte, vm.VideoBufferSize),
		rngState:     uint32(time.Now().UnixNano()),
	}
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

// handleFileCommand executes a file operation based on the command and length.
func (s *System) handleFileCommand(cmd uint32, length uint32) {
	filename := s.getCString(s.fileNamePtr)
	if filename == "" {
		s.lastFileResult = -1
		return
	}

	// Security: for now, restrict to local directory and no parent-dir hopping
	if strings.Contains(filename, "..") || strings.HasPrefix(filename, "/") {
		s.lastFileResult = -1
		return
	}

	switch cmd {
	case 1: // Read
		data, err := os.ReadFile(filename)
		if err != nil {
			s.lastFileResult = -1
			return
		}
		toRead := uint32(len(data))
		if length > 0 && length < toRead {
			toRead = length
		}
		if s.fileBufferPtr > 0 && int(s.fileBufferPtr+toRead) <= len(s.memory) {
			copy(s.memory[s.fileBufferPtr:], data[:toRead])
			s.lastFileResult = int32(toRead)
		} else {
			s.lastFileResult = -1
		}

	case 2: // Write
		if s.fileBufferPtr == 0 || int(s.fileBufferPtr+length) > len(s.memory) {
			s.lastFileResult = -1
			return
		}
		data := s.memory[s.fileBufferPtr : s.fileBufferPtr+length]
		err := os.WriteFile(filename, data, 0644)
		if err != nil {
			s.lastFileResult = -1
			return
		}
		s.lastFileResult = int32(length)

	case 3: // Stat
		info, err := os.Stat(filename)
		if err != nil {
			s.lastFileResult = -1
			return
		}
		s.lastFileResult = int32(info.Size())

	case 4: // Delete
		err := os.Remove(filename)
		if err != nil {
			s.lastFileResult = -1
			return
		}
		s.lastFileResult = 0

	default:
		s.lastFileResult = -1
	}
}

// Read implements vm.Bus.Read
func (s *System) Read(address uint32) (int32, error) {
	// Screen (Framebuffer)
	if address >= vm.VideoFramebufferStart && address < vm.VideoFramebufferEnd {
		offset := address - vm.VideoFramebufferStart
		if offset+4 > uint32(len(s.screenPixels)) {
			return 0, fmt.Errorf("framebuffer read out of bounds")
		}
		return int32(binary.BigEndian.Uint32(s.screenPixels[offset : offset+4])), nil
	}

	// Controller registers:
	if address == vm.ControllerStatusAddr {
		var val int32 = 0
		if s.controllerKey != 0 || s.controllerButton != 0 {
			val = 1
		}
		return val, nil
	}
	if address == vm.ControllerButtonAddr {
		return int32(s.controllerButton), nil
	}
	if address == vm.ControllerKeyAddr {
		return s.controllerKey, nil
	}

	// Mouse registers:
	if address == vm.MousePort+4 { // Mouse X
		return s.mouseX, nil
	}
	if address == vm.MousePort+8 { // Mouse Y
		return s.mouseY, nil
	}
	if address == vm.MousePort+12 { // Mouse Buttons
		return int32(s.mouseButton), nil
	}

	// File registers:
	if address == vm.FilePort+4 { // FileNamePtr
		return int32(s.fileNamePtr), nil
	}
	if address == vm.FilePort+8 { // FileBufferPtr
		return int32(s.fileBufferPtr), nil
	}
	if address == vm.FilePort+12 { // Length / Result
		return s.lastFileResult, nil
	}

	// Audio Control read: (stubs for now, can be expanded to return last played)
	if address == vm.AudioControlAddr {
		return 0, nil
	}

	// RNG register read: apply Xorshift32
	if address == vm.RNGDataAddr {
		x := s.rngState
		x ^= x << 13
		x ^= x >> 17
		x ^= x << 5
		s.rngState = x
		return int32(x), nil
	}

	// DateTime register read:
	if address == vm.DateTimeAddr { // 0x3074: Unix timestamp
		return int32(time.Now().Unix()), nil
	}
	if address == vm.DateTimePort+8 { // 0x3078: Packed Date (Year << 16 | Month << 8 | Day)
		now := time.Now()
		val := (int32(now.Year()) << 16) | (int32(now.Month()) << 8) | int32(now.Day())
		return val, nil
	}
	if address == vm.DateTimePort+12 { // 0x307C: Packed Time (Hour << 16 | Minute << 8 | Second)
		now := time.Now()
		val := (int32(now.Hour()) << 16) | (int32(now.Minute()) << 8) | int32(now.Second())
		return val, nil
	}

	return 0, fmt.Errorf("system: unhandled read at 0x%04X", address)
}

// Write implements vm.Bus.Write
func (s *System) Write(address uint32, value int32) error {
	// Screen (Framebuffer)
	if address >= vm.VideoFramebufferStart && address < vm.VideoFramebufferEnd {
		offset := address - vm.VideoFramebufferStart
		if offset+4 > uint32(len(s.screenPixels)) {
			return fmt.Errorf("framebuffer write out of bounds")
		}
		binary.BigEndian.PutUint32(s.screenPixels[offset:offset+4], uint32(value))
		return nil
	}

	// File registers:
	if address == vm.FilePort+4 {
		s.fileNamePtr = uint32(value)
		return nil
	}
	if address == vm.FilePort+8 {
		s.fileBufferPtr = uint32(value)
		return nil
	}
	if address == vm.FilePort+12 {
		// Command in upper 8 bits, Length in lower 24 bits
		cmd := uint32(value) >> 24
		length := uint32(value) & 0xFFFFFF
		s.handleFileCommand(cmd, length)
		return nil
	}

	// Controller/Mouse/DateTime (read-only)
	if (address >= vm.ControllerPort && address < vm.ControllerPort+0x10) ||
		(address >= vm.MousePort && address < vm.MousePort+0x10) ||
		(address >= vm.DateTimePort && address < vm.DateTimePort+0x10) {
		return fmt.Errorf("system: address 0x%04X is read-only", address)
	}

	// Audio Control write
	if address == vm.AudioControlAddr {
		if s.SoundHandler != nil {
			s.SoundHandler(value)
		}
		return nil
	}

	// RNG register write: seed the state.
	if address == vm.RNGDataAddr {
		if value == 0 {
			s.rngState = 1
		} else {
			s.rngState = uint32(value)
		}
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

func (s *System) Framebuffer() []byte {
	return s.screenPixels
}

func (s *System) DebugInfo() string {
	return fmt.Sprintf("Controller: K=%d B=0x%X\nMouse: %d,%d B=0x%X\nFile: Ptr=0x%X Buf=0x%X Res=%d\nRNG: 0x%08X",
		s.controllerKey, s.controllerButton,
		s.mouseX, s.mouseY, s.mouseButton,
		s.fileNamePtr, s.fileBufferPtr, s.lastFileResult,
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
		{"AUD_CTRL", 0},
		{"CTRL_BTN", int32(s.controllerButton)},
		{"CTRL_KEY", s.controllerKey},
		{"MSE_X", s.mouseX},
		{"MSE_Y", s.mouseY},
		{"MSE_BTN", int32(s.mouseButton)},
		{"FILE_PTR", int32(s.fileNamePtr)},
		{"FILE_BUF", int32(s.fileBufferPtr)},
		{"FILE_RES", s.lastFileResult},
		{"RNG_DATA", int32(s.rngState)},
	}
}

