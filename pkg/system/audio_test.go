package system

import (
	"encoding/binary"
	"testing"
	"time"

	"github.com/rmay/nuxvm/pkg/vm"
)

func TestAudioSoundHandlerVFS(t *testing.T) {
	machine := NewMachine(nil, vm.HeadlessBaseAddress, 32*1024)
	sys := machine.System

	sm := NewServiceManager()
	sys.Services = sm
	sm.StartSoundServer()

	var playedSoundID int32
	sm.SoundHandler = func(soundID int32) {
		playedSoundID = soundID
	}

	fd, err := sys.vfs.Open(sys, "/sys/audio")
	if err != nil {
		t.Fatalf("Open /sys/audio failed: %v", err)
	}

	buf := make([]byte, 4)
	binary.LittleEndian.PutUint32(buf, 440)
	n, err := sys.vfs.Write(fd, buf)
	if err != nil {
		t.Fatalf("Write to /sys/audio failed: %v", err)
	}
	if n != 4 {
		t.Errorf("Expected to write 4 bytes, wrote %d", n)
	}

	// Give it a moment to process the message
	for i := 0; i < 100; i++ {
		if playedSoundID == 440 {
			break
		}
		time.Sleep(1 * time.Millisecond)
	}

	if playedSoundID != 440 {
		t.Errorf("Expected sound ID 440, got %d", playedSoundID)
	}
}
