package lux

import (
	"bytes"
	"github.com/rmay/nuxvm/pkg/vm"
	"io"
	"os"
	"testing"
)

func TestDebugTimes(t *testing.T) {
	source := "[ 42 ] 1 #: HALT"
	bytecode, err := Compile(source, int32(vm.HeadlessBaseAddress))
	if err != nil {
		t.Fatalf("Compile error: %v", err)
	}

	oldStderr := os.Stderr
	r, w, _ := os.Pipe()
	os.Stderr = w

	machine := vm.NewVM(bytecode, vm.HeadlessBaseAddress)
	runErr := machine.Run()

	w.Close()
	var buf bytes.Buffer
	_, _ = io.Copy(&buf, r)
	os.Stderr = oldStderr

	if runErr != nil {
		t.Errorf("Runtime error: %v", runErr)
	}

	traceOutput := buf.String()
	t.Logf("VM Execution Trace:\n%s", traceOutput)
	t.Logf("Final Stack State: %v", machine.Stack())

	stack := machine.Stack()
	if len(stack) != 1 || stack[0] != 42 {
		t.Errorf("Expected [42], got %v", stack)
	}
}
