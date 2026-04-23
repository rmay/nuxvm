package system

import (
	"testing"
	"github.com/rmay/nuxvm/pkg/vm"
)

func TestMachinePushKeyAndMouse(t *testing.T) {
	program := []byte{vm.OpHalt}
	machine := NewMachine(program)
	
	// Set vector 4 (Controller) and vector 5 (Mouse)
	controllerHandlerAddr := uint32(0x1000)
	mouseHandlerAddr := uint32(0x2000)
	machine.CPU.WriteVector(4, controllerHandlerAddr)
	machine.CPU.WriteVector(5, mouseHandlerAddr)
	
	machine.CPU.Halt()
	
	// Test PushKey
	err := machine.PushKey(65) // 'A'
	if err != nil {
		t.Fatalf("PushKey failed: %v", err)
	}
	// Check internal system state
	if machine.System.controllerKey != 65 {
		t.Errorf("Expected controllerKey 65, got %d", machine.System.controllerKey)
	}
	if machine.CPU.PC() != controllerHandlerAddr {
		t.Errorf("Expected PC to jump to controller handler 0x%X, got 0x%X", controllerHandlerAddr, machine.CPU.PC())
	}
	if !machine.CPU.Running() {
		t.Error("Expected VM to be running after PushKey")
	}
	
	machine.CPU.Halt()
	
	// Test MoveMouse
	err = machine.MoveMouse(100, 200)
	if err != nil {
		t.Fatalf("MoveMouse failed: %v", err)
	}
	if machine.System.mouseX != 100 || machine.System.mouseY != 200 {
		t.Errorf("Expected mouse X/Y 100/200, got %d/%d", machine.System.mouseX, machine.System.mouseY)
	}
	if machine.CPU.PC() != mouseHandlerAddr {
		t.Errorf("Expected PC to jump to mouse handler 0x%X, got 0x%X", mouseHandlerAddr, machine.CPU.PC())
	}
}

func TestMachineVBlank(t *testing.T) {
	// Program that just yields
	program := []byte{vm.OpYield, vm.OpHalt}
	machine := NewMachine(program)
	
	// Set vector 2 (Screen)
	screenHandlerAddr := uint32(0x1000)
	machine.CPU.WriteVector(2, screenHandlerAddr)
	
	// Start the machine, it should yield immediately
	running, err := machine.Tick()
	if err != nil {
		t.Fatalf("Initial Tick failed: %v", err)
	}
	if !running {
		t.Fatal("Expected machine to be running after yield")
	}

	// Trigger VBlank
	err = machine.VBlank()
	if err != nil {
		t.Fatalf("VBlank failed: %v", err)
	}
	
	if machine.CPU.PC() != screenHandlerAddr {
		t.Errorf("Expected PC to jump to screen handler 0x%X, got 0x%X", screenHandlerAddr, machine.CPU.PC())
	}
}

