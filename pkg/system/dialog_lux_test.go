package system

import (
	"github.com/rmay/nuxvm/pkg/lux"
	"os"
	"testing"
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
IMPORT WINDOW

@test-start
    init-locals
	DIALOG::clear-items
	T"Item 1" 6 DIALOG::add-item
	T"Item 2" 6 DIALOG::add-item
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

func TestDialogSelectAndOpen(t *testing.T) {
	origDir, _ := os.Getwd()
	t.Cleanup(func() { os.Chdir(origDir) })

	root := repoRoot(t)
	if err := os.Chdir(root); err != nil {
		t.Fatalf("chdir to repo root: %v", err)
	}

	// Setup a dummy file
	tempDir, err := os.MkdirTemp("", "nuxvm-test")
	if err != nil {
		t.Fatal(err)
	}
	defer os.RemoveAll(tempDir)
	if err := os.WriteFile(tempDir+"/test.txt", []byte("hello"), 0644); err != nil {
		t.Fatal(err)
	}

	source := `
INCLUDE "lib/dialog.lux"
IMPORT DIALOG
IMPORT FILE-DIALOG
IMPORT WINDOW

@CALLBACK_FLAG 0x863FFF ;
@RESULT_BUF    0x864000 ;

@on-open
    1 CALLBACK_FLAG STOREI
;

@test-start
    init-locals
    0 CALLBACK_FLAG STOREI
    T"` + tempDir + `" T".txt" RESULT_BUF [ on-open ] FILE-DIALOG::show
    
    ( Force DRAW to set w and h )
    FILE-DIALOG::DRAW-FILE-DIALOG
    
    ( Select first item )
    0 FILE-DIALOG::FILE_LIST_BASE 8 + STOREI
    
    ( Simulate OK click )
    1 0x1005C STOREI ( down )
    DIALOG::w@ 50 + 160 - 0x10054 STOREI ( x )
    DIALOG::h@ 30 + 0x10058 STOREI ( y )
    
    FILE-DIALOG::ON-FILE-MOUSE
    
    CALLBACK_FLAG LOADI
;
test-start
`
	bytecode, err := lux.Compile(source)
	if err != nil {
		t.Fatalf("compile test: %v", err)
	}

	machine := NewMachine(bytecode, 16*1024*1024)
	machine.System.SetSandboxRoot("/")
	if _, err := machine.Tick(); err != nil {
		t.Fatalf("run failed: %v", err)
	}

	stack := machine.CPU.Stack()
	if len(stack) == 0 || stack[len(stack)-1] != 1 {
		t.Errorf("expected callback flag 1 at top of stack, got %v", stack)
	}
}

func TestDialogCancelButton(t *testing.T) {
	origDir, _ := os.Getwd()
	t.Cleanup(func() { os.Chdir(origDir) })

	root := repoRoot(t)
	if err := os.Chdir(root); err != nil {
		t.Fatalf("chdir to repo root: %v", err)
	}

	source := `
INCLUDE "lib/dialog.lux"
IMPORT DIALOG
IMPORT FILE-DIALOG
IMPORT WINDOW

@CALLBACK_FLAG 0x863FFF ;
@RESULT_BUF    0x864000 ;

@on-open
    1 CALLBACK_FLAG STOREI
;

@test-start
    init-locals
    0 CALLBACK_FLAG STOREI
    T"." T".txt" RESULT_BUF [ on-open ] FILE-DIALOG::show
    
    FILE-DIALOG::DRAW-FILE-DIALOG
    
    ( Simulate Cancel click )
    1 0x1005C STOREI ( down )
    DIALOG::w@ 50 + 60 - 0x10054 STOREI ( x )
    DIALOG::h@ 30 + 0x10058 STOREI ( y )
    
    FILE-DIALOG::ON-FILE-MOUSE
    
    CALLBACK_FLAG LOADI
;
test-start
`
	bytecode, err := lux.Compile(source)
	if err != nil {
		t.Fatalf("compile test: %v", err)
	}

	machine := NewMachine(bytecode, 16*1024*1024)
	machine.System.SetSandboxRoot("/")
	if _, err := machine.Tick(); err != nil {
		t.Fatalf("run failed: %v", err)
	}

	stack := machine.CPU.Stack()
	if len(stack) == 0 || stack[len(stack)-1] != 1 {
		t.Errorf("expected callback flag 1 at top of stack, got %v", stack)
	}
}

func TestDialogEscapeKey(t *testing.T) {
	origDir, _ := os.Getwd()
	t.Cleanup(func() { os.Chdir(origDir) })

	root := repoRoot(t)
	if err := os.Chdir(root); err != nil {
		t.Fatalf("chdir to repo root: %v", err)
	}

	source := `
INCLUDE "lib/dialog.lux"
IMPORT DIALOG
IMPORT FILE-DIALOG
IMPORT WINDOW

@CALLBACK_FLAG 0x863FFF ;
@RESULT_BUF    0x864000 ;

@on-open
    1 CALLBACK_FLAG STOREI
;

@test-start
    init-locals
    0 CALLBACK_FLAG STOREI
    T"." T".txt" RESULT_BUF [ on-open ] FILE-DIALOG::show
    
    27 0x1004C STOREI ( Escape key )
    FILE-DIALOG::ON-FILE-KEY
    
    CALLBACK_FLAG LOADI
;
test-start
`
	bytecode, err := lux.Compile(source)
	if err != nil {
		t.Fatalf("compile test: %v", err)
	}

	machine := NewMachine(bytecode, 16*1024*1024)
	machine.System.SetSandboxRoot("/")
	if _, err := machine.Tick(); err != nil {
		t.Fatalf("run failed: %v", err)
	}

	stack := machine.CPU.Stack()
	if len(stack) == 0 || stack[len(stack)-1] != 1 {
		t.Errorf("expected callback flag 1 at top of stack, got %v", stack)
	}
}
