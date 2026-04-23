package system

import (
	"os"
	"strconv"
	"strings"
	"testing"
)

func TestOSKeyboard(t *testing.T) {
	program, err := os.ReadFile("../../examples/keyboard.bin")
	if err != nil {
		t.Fatalf("Failed to read keyboard.bin: %v", err)
	}

	machine := NewMachine(program)
	var output strings.Builder
	machine.CPU.OutputHandler = func(value int32, format int32) {
		if format == 1 {
			output.WriteByte(byte(value))
		} else {
			output.WriteString(strings.TrimSpace(strconv.FormatInt(int64(value), 10)))
		}
	}

	// Run initial setup (setting vectors, etc.)
	// We need to run it until it yields.
	for i := 0; i < 1000; i++ {
		if machine.CPU.Yielded() {
			break
		}
		_, err := machine.CPU.Step()
		if err != nil {
			t.Fatalf("Error during setup: %v", err)
		}
	}

	if !machine.CPU.Yielded() {
		t.Fatal("Program did not yield after setup")
	}

	output.Reset()
	
	// Simulate key press 'A' (65)
	err = machine.PushKey(65)
	if err != nil {
		t.Fatalf("PushKey failed: %v", err)
	}

	// Run after vector trigger
	// machine.Tick() clears yield and runs until next yield
	_, err = machine.Tick()
	if err != nil {
		t.Fatalf("Tick failed: %v", err)
	}

	got := output.String()
	if !strings.Contains(got, "Key pressed:") || !strings.Contains(got, "65") {
		t.Errorf("Unexpected output: %q", got)
	}
}
