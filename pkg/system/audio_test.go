package system

import (
	"testing"
	"github.com/rmay/nuxvm/pkg/vm"
)

func TestAudioSoundHandler(t *testing.T) {
	// Program that writes to AudioControlAddr
	program := []byte{}
	program = append(program, vm.PushInstruction(440)...) // Sound ID 440
	program = append(program, vm.StoreInstruction(vm.AudioControlAddr)...)
	program = append(program, vm.OpHalt)

	machine := NewMachine(program, 0)
	
	var playedSoundID int32
	machine.System.SoundHandler = func(soundID int32) {
		playedSoundID = soundID
	}

	if err := machine.CPU.Run(); err != nil {
		t.Fatalf("Run failed: %v", err)
	}

	if playedSoundID != 440 {
		t.Errorf("Expected sound ID 440, got %d", playedSoundID)
	}
}
