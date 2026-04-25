package vm

import (
	"testing"
)

func TestVectorAssignment(t *testing.T) {
	vm := NewVM([]byte{})

	// Test direct SetVector/GetVector access (public API)
	vectorAddr := uint32(0x6500)
	vm.SetVector(4, vectorAddr)

	if vm.GetVector(4) != vectorAddr {
		t.Errorf("Expected vector 4 to be 0x%X, got 0x%X", vectorAddr, vm.GetVector(4))
	}

	// Test out-of-bounds vector access
	vm.SetVector(16, 0x5000) // Should be ignored
	if vm.GetVector(16) != 0 {
		t.Errorf("Expected out-of-bounds vector access to return 0")
	}

	vm.SetVector(-1, 0x5000) // Should be ignored
	if vm.GetVector(-1) != 0 {
		t.Errorf("Expected negative vector index to return 0")
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

func TestTriggerVectorPushesReturnAddr(t *testing.T) {
	// Vector triggers should behave like a CALL: push the interrupted PC so the
	// handler's RET resumes where the VM was. Without this, vector handlers
	// (compiled with implicit RET) underflow the return stack.
	vm := NewVM([]byte{OpHalt})
	vm.vectors[4] = vm.UserMemoryStart()
	vm.pc = 0x1234
	prevDepth := len(vm.returnStack)

	if err := vm.TriggerVector(4); err != nil {
		t.Fatalf("TriggerVector failed: %v", err)
	}
	if len(vm.returnStack) != prevDepth+1 {
		t.Fatalf("expected return stack depth %d, got %d", prevDepth+1, len(vm.returnStack))
	}
	if got := vm.returnStack[len(vm.returnStack)-1]; got != 0x1234 {
		t.Errorf("expected pushed return addr 0x1234, got 0x%X", got)
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
	vm.SetVector(1, vectorAddr)

	// Read the vector address back using GetVector
	val := vm.GetVector(1)
	if val != vectorAddr {
		t.Errorf("Expected vector address 0x%X, got 0x%X", vectorAddr, val)
	}

	// Test reading unset vector
	if vm.GetVector(0) != 0 {
		t.Errorf("Expected unset vector to be 0")
	}
}

