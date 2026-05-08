package vm

import (
	"bytes"
	"io"
	"os"
	"strings"
	"testing"
)

func TestTraceLimit(t *testing.T) {
	// Create a program that loops forever
	// 0: PUSH 0
	// 5: JMP 0
	program := []byte{
		OpPush, 0, 0, 0, 0,
		OpJmp, 0, 0, 0, 0,
	}

	vm := NewVM(program, true)
	
	// Capture stderr
	oldStderr := os.Stderr
	r, w, _ := os.Pipe()
	os.Stderr = w

	outChan := make(chan string)
	go func() {
		var buf bytes.Buffer
		io.Copy(&buf, r)
		outChan <- buf.String()
	}()

	// Step it 1100 times. Limit is 1000.
	for i := 0; i < 1100; i++ {
		_, err := vm.Step()
		if err != nil {
			w.Close()
			os.Stderr = oldStderr
			t.Fatalf("Step failed at %d: %v", i, err)
		}
	}

	w.Close()
	output := <-outChan
	os.Stderr = oldStderr

	// Check if limit message is present
	if !strings.Contains(output, "VM: Trace limit (1000) reached, tracing disabled") {
		t.Errorf("Expected trace limit message not found in output")
	}

	// Check if tracing was actually disabled
	if vm.trace {
		t.Errorf("vm.trace should be false after reaching limit")
	}

	// Count occurrences of "VM: PC="
	// Each traced instruction starts with "\nVM: PC=" (added in our implementation)
	count := strings.Count(output, "VM: PC=")
	if count != 1000 {
		t.Errorf("Expected 1000 traced instructions, got %d", count)
	}
}
