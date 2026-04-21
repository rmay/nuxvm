package vm

import (
	"testing"
)

func TestVectorAssignment(t *testing.T) {
	vm := NewVM([]byte{})
	
	// Test setting Keyboard Vector (Port 0x3040, Index 4)
	vectorAddr := uint32(0x6500)
	// Writing to the base of the KeyboardPort (0x3040) should set vector 4.
	err := vm.handleDeviceWrite(KeyboardPort, int32(vectorAddr)) 
	if err != nil {
		t.Fatalf("handleDeviceWrite for vector assignment failed: %v", err)
	}
	
	if vm.vectors[4] != vectorAddr {
		t.Errorf("Expected vector 4 to be 0x%04X, got 0x%04X", vectorAddr, vm.vectors[4])
	}

	// Test writing to non-vector address within the same port block (should be handled by other device logic or error)
	// e.g., KeyboardStatusAddr which is KeyboardPort + 4
	err = vm.handleDeviceWrite(KeyboardStatusAddr, 123) // This should *not* set a vector
	if err == nil {
		t.Error("Expected error for non-vector address within port block")
	}
	if vm.vectors[4] == uint32(123) { // Ensure vector wasn't overwritten
		t.Errorf("Vector 4 should not have been set by writing to KeyboardStatusAddr")
	}
}

func TestTriggerVector(t *testing.T) {
	// Program that just halts
	program := []byte{OpHalt}
	vm := NewVM(program)
	
	// Set vector 0 (System) to UserMemoryOffset (start of program)
	expectedPC := vm.UserMemoryStart() 
	vm.vectors[0] = expectedPC // Set vector 0 to start of program
	
	// Halt the VM first to simulate an interrupted state
	vm.running = false 
	vm.pc = 0 // Reset PC to ensure it changes
	
	err := vm.TriggerVector(0)
	if err != nil {
		t.Fatalf("TriggerVector failed: %v", err)
	}
	
	if !vm.running {
		t.Error("Expected VM to be running after TriggerVector")
	}
	if vm.pc != expectedPC {
		t.Errorf("Expected PC to be 0x%X, got 0x%X", expectedPC, vm.pc)
	}
}

func TestTriggerVectorEdgeCases(t *testing.T) {
	vm := NewVM([]byte{OpHalt}) // Minimal program
	
	// Test invalid vector index
	err := vm.TriggerVector(16)
	if err == nil {
		t.Error("Expected error for invalid vector index (16)")
	}
	err = vm.TriggerVector(-1)
	if err == nil {
		t.Error("Expected error for invalid vector index (-1)")
	}

	// Test unset vector (address 0)
	// TriggerVector(0) should do nothing if vm.vectors[0] is 0
	// Test TriggerVector(0) to ensure it doesn't crash and doesn't change PC if unset.
	initialPC := vm.pc
	if err := vm.TriggerVector(0); err != nil {
		t.Fatalf("TriggerVector with unset vector failed: %v", err)
	}
	if vm.pc != initialPC {
		t.Errorf("Expected PC to remain %d when vector is unset, got %d", initialPC, vm.pc)
	}

	// Test vector address out of bounds
	vm.vectors[1] = uint32(len(vm.memory) + 10) // Set vector to an invalid address
	err = vm.TriggerVector(1)
	if err == nil {
		t.Error("Expected error for vector address out of bounds")
	}
}

func TestDeviceReadVector(t *testing.T) {
	vm := NewVM([]byte{})
	// Set a vector address
	vectorAddr := uint32(0x5000)
	vm.vectors[1] = vectorAddr // Setting vector for ConsolePort (index 1)

	// Read the vector address from the ConsolePort base address
	val, err := vm.handleDeviceRead(ConsolePort)
	if err != nil {
		t.Fatalf("handleDeviceRead for vector failed: %v", err)
	}
	if uint32(val) != vectorAddr {
		t.Errorf("Expected vector address 0x%X, got 0x%X", vectorAddr, val)
	}
}
