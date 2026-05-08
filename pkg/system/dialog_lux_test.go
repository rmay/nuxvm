package system

import (
	"os"
	"testing"
	"github.com/rmay/nuxvm/pkg/lux"
)

func repoRoot(t *testing.T) string {
	cwd, err := os.Getwd()
	if err != nil {
		t.Fatalf("getwd: %v", err)
	}
	// find repo root
	for cwd != "/" {
		if _, err := os.Stat(cwd + "/go.mod"); err == nil {
			return cwd
		}
		cwd = cwd + "/.."
	}
	return cwd
}

func TestDialogAddItem(t *testing.T) {
	origDir, _ := os.Getwd()
	t.Cleanup(func() { os.Chdir(origDir) })

	root := repoRoot(t)
	if err := os.Chdir(root); err != nil {
		t.Fatalf("chdir to repo root: %v", err)
	}

	source := `
INCLUDE "lib/dialog.lux"
IMPORT DIALOG

@test-start
    init-locals
	DIALOG::clear-items
	F"Item 1" 6 DIALOG::add-item
	F"Item 2" 6 DIALOG::add-item
	DIALOG::DIALOG_LIST_BASE 4 + LOADI ( count )
;
test-start
`
	bytecode, err := lux.Compile(source)
	if err != nil {
		t.Fatalf("compile test: %v", err)
	}

	machine := NewMachine(bytecode, 16*1024*1024)
	if _, err := machine.Tick(); err != nil {
		t.Fatalf("run failed: %v", err)
	}

	stack := machine.CPU.Stack()
	if len(stack) != 1 || stack[0] != 2 {
		t.Errorf("expected count 2, got %v", stack)
	}
}
