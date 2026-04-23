package system

import (
	"os"
	"testing"
	"github.com/rmay/nuxvm/pkg/vm"
)

func TestDateTime(t *testing.T) {
	sys := NewSystem()
	
	// Test Unix timestamp
	ts, err := sys.Read(vm.DateTimeAddr)
	if err != nil {
		t.Fatalf("Read DateTimeAddr failed: %v", err)
	}
	if ts <= 0 {
		t.Errorf("Expected positive timestamp, got %d", ts)
	}

	// Test Packed Date
	date, err := sys.Read(vm.DateTimePort + 8)
	if err != nil {
		t.Fatalf("Read Packed Date failed: %v", err)
	}
	year := date >> 16
	if year < 2024 {
		t.Errorf("Expected year >= 2024, got %d", year)
	}

	// Test Read-only
	err = sys.Write(vm.DateTimeAddr, 123)
	if err == nil {
		t.Error("Expected error when writing to DateTimeAddr")
	}
}

func TestFileReadWrite(t *testing.T) {
	sys := NewSystem()
	mem := make([]byte, 1024)
	sys.SetMemory(mem)

	filename := "testfile.txt"
	content := "Hello NUX OS!"
	
	// Setup filename in VM memory
	copy(mem[100:], []byte(filename+"\x00"))
	sys.Write(vm.FilePort+4, 100) // FileNamePtr

	// Setup data in VM memory
	copy(mem[200:], []byte(content))
	sys.Write(vm.FilePort+8, 200) // FileBufferPtr

	// 1. Test Write
	// Command 2 (Write), Length 13
	cmd := (uint32(2) << 24) | uint32(len(content))
	err := sys.Write(vm.FilePort+12, int32(cmd))
	if err != nil {
		t.Fatalf("File Write command failed: %v", err)
	}
	
	res, _ := sys.Read(vm.FilePort+12)
	if res != int32(len(content)) {
		t.Errorf("Expected write result %d, got %d", len(content), res)
	}

	// Verify file was written
	if _, err := os.Stat(filename); os.IsNotExist(err) {
		t.Fatalf("File was not created")
	}
	defer os.Remove(filename)

	// 2. Test Stat
	cmdStat := (uint32(3) << 24)
	sys.Write(vm.FilePort+12, int32(cmdStat))
	resStat, _ := sys.Read(vm.FilePort+12)
	if resStat != int32(len(content)) {
		t.Errorf("Expected stat result %d, got %d", len(content), resStat)
	}

	// 3. Test Read
	// Clear memory first
	for i := 200; i < 300; i++ { mem[i] = 0 }
	
	cmdRead := (uint32(1) << 24) | uint32(len(content))
	sys.Write(vm.FilePort+12, int32(cmdRead))
	resRead, _ := sys.Read(vm.FilePort+12)
	if resRead != int32(len(content)) {
		t.Errorf("Expected read result %d, got %d", len(content), resRead)
	}
	
	readContent := string(mem[200 : 200+len(content)])
	if readContent != content {
		t.Errorf("Expected read content %q, got %q", content, readContent)
	}

	// 4. Test Delete
	cmdDel := (uint32(4) << 24)
	sys.Write(vm.FilePort+12, int32(cmdDel))
	resDel, _ := sys.Read(vm.FilePort+12)
	if resDel != 0 {
		t.Errorf("Expected delete result 0, got %d", resDel)
	}
	
	if _, err := os.Stat(filename); !os.IsNotExist(err) {
		t.Errorf("File still exists after delete")
	}
}
