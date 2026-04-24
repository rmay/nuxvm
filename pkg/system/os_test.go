package system

import (
	"fmt"
	"strconv"
	"strings"
	"testing"

	"github.com/rmay/nuxvm/pkg/lux"
	"github.com/rmay/nuxvm/pkg/vm"
)

func TestOSKeyboard(t *testing.T) {
	// Simple keyboard handler in LUX
	// We need an infinite loop with YIELD so the VM is "running"
	// but yielding to the host between instructions.
	source := fmt.Sprintf(`
		@on-key
			%d LOADI dup 0 > [ . ] [ drop ] ?:
		;
		[ on-key ] %d STOREI
		0 ( Initial value for while loop )
		[ 1 ] [ YIELD ] |:
	`, vm.DeviceMemoryOffset+0x0040+12, vm.DeviceMemoryOffset+0x0040)
	program, err := lux.Compile(source)
	if err != nil {
		t.Fatalf("Failed to compile test program: %v", err)
	}

	machine := NewMachine(program, 0)
	var output strings.Builder
	machine.CPU.OutputHandler = func(value int32, format int32) {
		if format == 1 {
			output.WriteByte(byte(value))
		} else {
			output.WriteString(strings.TrimSpace(strconv.FormatInt(int64(value), 10)))
		}
	}

	// Run initial setup (setting vectors, etc.)
	_, err = machine.Tick()
	if err != nil {
		t.Fatalf("Error during setup: %v", err)
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
	_, err = machine.Tick()
	if err != nil {
		t.Fatalf("Tick failed: %v", err)
	}

	got := output.String()
	if !strings.Contains(got, "65") {
		t.Errorf("Unexpected output: %q, expected it to contain '65'", got)
	}
}

