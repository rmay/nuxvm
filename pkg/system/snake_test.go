package system

import (
	"encoding/binary"
	"os"
	"testing"

	"github.com/rmay/nuxvm/pkg/lux"
	"github.com/rmay/nuxvm/pkg/vm"
)

func TestSnakeStartButton(t *testing.T) {
	origDir, _ := os.Getwd()
	os.Chdir("../..")
	defer os.Chdir(origDir)

	// 1. Load and compile Snake.lux
	program, err := lux.LoadProgram("apps/Snake.lux", int32(vm.GraphicalBaseAddress))
	if err != nil {
		t.Fatalf("Failed to load Snake.lux: %v", err)
	}

	// 2. Setup Machine with enough memory (16MB)
	machine := NewMachine(program, vm.GraphicalBaseAddress, 16*1024*1024)
	sys := machine.System

	// Set a standard resolution
	sys.SetResolution(320, 240)

	// 3. Run until it opens files and enters main loop
	maxTicks := 1000000
	yielded := false
	for i := 0; i < maxTicks; i++ {
		_, err := machine.Tick()
		if err != nil {
			t.Fatalf("Tick error at %d: %v", i, err)
		}
		// Check GAME_STATE at 0x80002C (Big Endian)
		state := binary.BigEndian.Uint32(sys.memory[0x80002C : 0x80002C+4])
		if i > 1000 && state == 0 {
			yielded = true
			break
		}
	}

	if !yielded {
		state := binary.BigEndian.Uint32(sys.memory[0x80002C : 0x80002C+4])
		t.Fatalf("Snake did not reach start screen state (0). Current state: %d", state)
	}

	// 4. Calculate expected Start Button coordinates for 320x240
	// box-x = (320 - 200) / 2 = 60
	// box-y = (240 - 150) / 2 = 45
	// Button rectangle: 100x30 at box-x + 50, box-y + 80
	// Button bounds: [110, 210] x [125, 155]

	// 5. Inject MouseDown event on the button
	sys.mouseEvents <- InputEvent{
		Type:     InputMouseDown,
		MouseBtn: 1,
		MouseX:   150, // Center of button (110 + 50)
		MouseY:   140, // Center of button (125 + 15)
	}

	// 6. Tick enough to process the mouse event
	for i := 0; i < 100000; i++ {
		_, err := machine.Tick()
		if err != nil {
			t.Fatalf("Tick error during mouse handling: %v", err)
		}
		state := binary.BigEndian.Uint32(sys.memory[0x80002C : 0x80002C+4])
		if state == 1 {
			// Success!
			return
		}
		if state == 2 {
			t.Fatalf("Snake transitioned to Game Over (2) instead of Playing (1)")
		}
	}

	// If we get here, it didn't transition
	state := binary.BigEndian.Uint32(sys.memory[0x80002C : 0x80002C+4])
	t.Errorf("Snake failed to transition to state 1 (Playing) after click. Current state: %d", state)
}

func TestSnakeKeyboardDown(t *testing.T) {
	origDir, _ := os.Getwd()
	os.Chdir("../..")
	defer os.Chdir(origDir)

	program, err := lux.LoadProgram("apps/Snake.lux", int32(vm.GraphicalBaseAddress))
	if err != nil {
		t.Fatalf("Failed to load Snake.lux: %v", err)
	}

	machine := NewMachine(program, vm.GraphicalBaseAddress, 16*1024*1024)
	sys := machine.System
	sys.SetResolution(320, 240)

	// Run until start screen
	for i := 0; i < 1000000; i++ {
		machine.Tick()
		state := binary.BigEndian.Uint32(sys.memory[0x80002C : 0x80002C+4])
		if i > 1000 && state == 0 {
			break
		}
	}

	// Click Start to enter playing state
	sys.mouseEvents <- InputEvent{Type: InputMouseDown, MouseBtn: 1, MouseX: 150, MouseY: 140}
	for i := 0; i < 100000; i++ {
		machine.Tick()
		state := binary.BigEndian.Uint32(sys.memory[0x80002C : 0x80002C+4])
		if state == 1 {
			break
		}
	}

	// Verify initial dir=1 (right)
	dir := binary.BigEndian.Uint32(sys.memory[0x800008 : 0x800008+4])
	if dir != 1 {
		t.Fatalf("Expected initial dir=1 (Right), got %d", dir)
	}

	// Inject arrow-Down (cloister translates ebiten.KeyArrowDown → 18) via the
	// same path cloister uses: QueueKeyDown + DrainInputEvents.
	machine.QueueKeyDown(18)
	machine.DrainInputEvents()

	// Tick until handle-kbd processes the event and sets next-dir=2
	got := uint32(0)
	for i := 0; i < 100000; i++ {
		machine.Tick()
		got = binary.BigEndian.Uint32(sys.memory[0x80000C : 0x80000C+4])
		if got == 2 {
			return // success
		}
	}
	t.Errorf("Expected next-dir=2 (Down) after 's' keypress, got %d", got)
}

// TestSnakeMovesContinuously simulates the user's reported scenario: after
// clicking Start, the snake should keep moving every `speed` frames without
// any further input. If the snake "stops" we want to catch it here.
func TestSnakeMovesContinuously(t *testing.T) {
	origDir, _ := os.Getwd()
	os.Chdir("../..")
	defer os.Chdir(origDir)

	program, err := lux.LoadProgram("apps/Snake.lux", int32(vm.GraphicalBaseAddress))
	if err != nil {
		t.Fatalf("Failed to load Snake.lux: %v", err)
	}

	machine := NewMachine(program, vm.GraphicalBaseAddress, 16*1024*1024)
	sys := machine.System
	sys.SetResolution(800, 600)

	// Wait for start screen.
	for i := 0; i < 1000000; i++ {
		machine.Tick()
		st := binary.BigEndian.Uint32(sys.memory[0x80002C : 0x80002C+4])
		if i > 1000 && st == 0 {
			break
		}
	}

	// Click START button. For 800x600: box at (300,225), button at (350-450, 305-335).
	sys.mouseEvents <- InputEvent{Type: InputMouseDown, MouseBtn: 1, MouseX: 400, MouseY: 320}
	for i := 0; i < 100000; i++ {
		machine.Tick()
		st := binary.BigEndian.Uint32(sys.memory[0x80002C : 0x80002C+4])
		if st == 1 {
			break
		}
	}
	st := binary.BigEndian.Uint32(sys.memory[0x80002C : 0x80002C+4])
	if st != 1 {
		t.Fatalf("Did not reach playing state; got state=%d", st)
	}

	readSeg := func(idx uint32) (x, y int32) {
		base := uint32(0x801000) + idx*8
		x = int32(binary.BigEndian.Uint32(sys.memory[base : base+4]))
		y = int32(binary.BigEndian.Uint32(sys.memory[base+4 : base+8]))
		return
	}
	headIdx := func() uint32 {
		return binary.BigEndian.Uint32(sys.memory[0x800000 : 0x800000+4])
	}

	// Run a few ticks and confirm head-idx auto-advances.
	startHead := headIdx()
	for i := 0; i < 60; i++ { // ~4 moves at speed=15
		machine.Tick()
	}
	endHead := headIdx()
	if endHead == startHead {
		t.Errorf("Snake did not auto-move in 60 ticks: head-idx still %d", endHead)
	}

	// Press Down before the snake hits the right wall, then verify y increases.
	machine.QueueKeyDown(18)
	machine.DrainInputEvents()

	_, beforeY := readSeg(headIdx())
	for i := 0; i < 60; i++ { // a few more moves
		machine.Tick()
	}
	_, afterY := readSeg(headIdx())
	state := binary.BigEndian.Uint32(sys.memory[0x80002C : 0x80002C+4])
	if afterY <= beforeY {
		t.Errorf("Snake did not move down: before y=%d after y=%d state=%d",
			beforeY, afterY, state)
	}
}
