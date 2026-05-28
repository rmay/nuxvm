package luxrepl

import (
	"reflect"
	"strings"
	"testing"
)

func TestREPL_BasicEval(t *testing.T) {
	var output strings.Builder
	r := New(func(s string) {
		output.WriteString(s)
	})

	r.Eval("3 4 +")
	stack := r.Stack()
	expected := []int32{7}
	if !reflect.DeepEqual(stack, expected) {
		t.Errorf("expected stack %v, got %v", expected, stack)
	}
	if !strings.Contains(output.String(), "[7]") {
		t.Errorf("output should show [7], got %q", output.String())
	}
}

func TestREPL_WordDefinition(t *testing.T) {
	var output strings.Builder
	r := New(func(s string) {
		output.WriteString(s)
	})

	r.Eval("@SQUARE DUP * ;")
	defs := r.Definitions()
	if len(defs) != 1 || defs[0] != "SQUARE" {
		t.Errorf("expected definitions ['SQUARE'], got %v", defs)
	}
	if !strings.Contains(r.History(), "@SQUARE DUP * ;") {
		t.Errorf("expected history to contain definition, got %q", r.History())
	}

	r.Eval("5 SQUARE")
	stack := r.Stack()
	expected := []int32{25}
	if !reflect.DeepEqual(stack, expected) {
		t.Errorf("expected stack %v, got %v", expected, stack)
	}
}

func TestREPL_Commands(t *testing.T) {
	var output strings.Builder
	r := New(func(s string) {
		output.WriteString(s)
	})

	// Setup stack and definitions
	r.Eval("10 20 30")
	r.Eval("@FOO 1 ;")
	output.Reset()

	// Test .s / stack
	r.Eval(".s")
	if !strings.Contains(output.String(), "[10 20 30]") {
		t.Errorf("stack output missing values, got %q", output.String())
	}
	output.Reset()

	// Test drop
	r.Eval("drop")
	expected := []int32{10, 20}
	if !reflect.DeepEqual(r.Stack(), expected) {
		t.Errorf("expected stack %v after drop, got %v", expected, r.Stack())
	}
	output.Reset()

	// Test clearstack
	r.Eval("cs")
	if len(r.Stack()) != 0 {
		t.Errorf("stack should be empty after cs, got %v", r.Stack())
	}
	output.Reset()

	// Test history
	r.Eval("history")
	if !strings.Contains(output.String(), "@FOO 1 ;") {
		t.Errorf("history output missing FOO, got %q", output.String())
	}
	output.Reset()

	// Test words
	r.Eval("words")
	if !strings.Contains(output.String(), "FOO") {
		t.Errorf("words output missing FOO, got %q", output.String())
	}
	output.Reset()

	// Test clear
	r.Eval("clear")
	if r.History() != "" || len(r.Definitions()) != 0 {
		t.Errorf("clear should wipe history and definitions")
	}
}

func TestREPL_IsExitCommand(t *testing.T) {
	r := New(nil)
	exits := []string{"exit", "quit", "q", " exit ", " q"}
	for _, cmd := range exits {
		if !r.IsExitCommand(cmd) {
			t.Errorf("expected %q to be exit command", cmd)
		}
	}
	notExits := []string{"help", "clear", "abc"}
	for _, cmd := range notExits {
		if r.IsExitCommand(cmd) {
			t.Errorf("expected %q NOT to be exit command", cmd)
		}
	}
}

func TestREPL_Errors(t *testing.T) {
	var output strings.Builder
	r := New(func(s string) {
		output.WriteString(s)
	})

	// Compile error
	r.Eval("invalid_word")
	if !strings.Contains(output.String(), "Compile error") {
		t.Errorf("expected compile error, got %q", output.String())
	}
	output.Reset()

	// Runtime error (e.g. stack underflow)
	r.Eval("DROP")
	if !strings.Contains(output.String(), "Runtime error") {
		t.Errorf("expected runtime error, got %q", output.String())
	}
	output.Reset()

	// Malformed word definition
	r.Eval("@FOO 1")
	if !strings.Contains(output.String(), "Error: Word definition must end with ';'") {
		t.Errorf("expected word definition error, got %q", output.String())
	}
}

func TestREPL_Help(t *testing.T) {
	var output strings.Builder
	r := New(func(s string) {
		output.WriteString(s)
	})
	r.Eval("help")
	if !strings.Contains(output.String(), "LUX REPL Commands") {
		t.Errorf("expected help text, got %q", output.String())
	}
}
