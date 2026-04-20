package vm

import (
	"testing"
)

func TestAudioSoundHandler(t *testing.T) {
	var capturedSoundID int32 = -1
	program := []byte{}
	// Push a sound ID
	program = append(program, pushInstruction(2)...) 
	// Store to audio control address
	program = append(program, StoreInstruction(AudioControlAddr)...)
	program = append(program, OpHalt)

	vm := createVMWithProgram(program)
	vm.SoundHandler = func(soundID int32) {
		capturedSoundID = soundID
	}

	if err := vm.Run(); err != nil {
		t.Fatalf("Run failed: %v", err)
	}

	if capturedSoundID != 2 {
		t.Errorf("Expected SoundHandler to receive soundID 2, got %d", capturedSoundID)
	}
}
