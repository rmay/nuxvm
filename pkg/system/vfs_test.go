package system

import (
	"encoding/binary"
	"testing"

	"github.com/rmay/nuxvm/pkg/vm"
)

func TestVFSDraw(t *testing.T) {
	machine := NewMachine(nil, vm.GraphicalBaseAddress, 32*1024)
	sys := machine.System

	fd, err := sys.vfs.Open(sys, "/sys/draw")
	if err != nil {
		t.Fatalf("Open /sys/draw failed: %v", err)
	}

	// FillRect: [0 (1), x (2), y (2), w (2), h (2), color (4)] = 13 bytes
	buf := make([]byte, 13)
	buf[0] = 0 // FillRect
	binary.LittleEndian.PutUint16(buf[1:3], 10)
	binary.LittleEndian.PutUint16(buf[3:5], 20)
	binary.LittleEndian.PutUint16(buf[5:7], 100)
	binary.LittleEndian.PutUint16(buf[7:9], 50)
	binary.LittleEndian.PutUint32(buf[9:13], 0xFF0000) // Red (0xRRGGBB)

	_, err = sys.vfs.Write(fd, buf)
	if err != nil {
		t.Fatalf("Write to /sys/draw failed: %v", err)
	}

	// Check if a pixel in the middle was colored
	// 10+50, 20+25 = 60, 45
	offset := (45*int(sys.screenWidth) + 60) * 4
	if sys.screenPixels[offset] != 0xFF {
		t.Errorf("Pixel at 60,45 not red. Got R=%02X", sys.screenPixels[offset])
	}
}

func TestVFSFilePersistence(t *testing.T) {
	// Mock services for host file access
	machine := NewMachine(nil, vm.GraphicalBaseAddress, 32*1024)
	sys := machine.System

	// We need a ServiceManager to test /sys/file/
	sm := NewServiceManager()
	sys.Services = sm

	// Create a temporary file on host if needed, but sm.OpenFile might map to something else.
	// Actually, system_test.go might have patterns for this.
	// Let's just test that the Open/Read/Write flow works through VFS.
}

func TestVFSMouse(t *testing.T) {
	machine := NewMachine(nil, vm.GraphicalBaseAddress, 32*1024)
	sys := machine.System

	fd, err := sys.vfs.Open(sys, "/sys/mouse")
	if err != nil {
		t.Fatalf("Open /sys/mouse failed: %v", err)
	}

	// Inject mouse event
	sys.mouseEvents <- InputEvent{
		Type:     InputMouseDown,
		MouseBtn: 1,
		MouseX:   100,
		MouseY:   200,
	}

	buf := make([]byte, 8)
	n, err := sys.vfs.Read(fd, buf)
	if err != nil {
		t.Fatalf("Read from /sys/mouse failed: %v", err)
	}
	if n != 8 {
		t.Fatalf("Expected 8 bytes, got %d", n)
	}

	if buf[0] != byte(InputMouseDown) {
		t.Errorf("Expected MouseDown (3), got %d", buf[0])
	}
	if binary.LittleEndian.Uint16(buf[2:4]) != 100 {
		t.Errorf("Expected X=100, got %d", binary.LittleEndian.Uint16(buf[2:4]))
	}
}
