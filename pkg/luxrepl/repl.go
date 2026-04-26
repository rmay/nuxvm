// Package luxrepl is the embeddable Lux REPL. It maintains a persistent
// stack and dictionary across Eval calls and emits output through a
// caller-provided sink, so it can be wrapped by a stdin/stdout CLI
// (cmd/luxrepl) or by a graphical Shell window inside Cloister.
package luxrepl

import (
	"fmt"
	"strings"

	"github.com/rmay/nuxvm/pkg/lux"
	"github.com/rmay/nuxvm/pkg/vm"
)

// REPL is one logical session — its own stack, dictionary, and output sink.
type REPL struct {
	history     string
	stack       []int32
	definitions []string
	output      func(string)
}

// New constructs a REPL whose output is sent to sink. Sink may be called with
// either single-line or multi-line strings; the caller is responsible for
// splitting on '\n' if it cares about line boundaries.
func New(sink func(string)) *REPL {
	if sink == nil {
		sink = func(string) {}
	}
	return &REPL{output: sink}
}

// Stack returns a copy of the current data stack.
func (r *REPL) Stack() []int32 {
	out := make([]int32, len(r.stack))
	copy(out, r.stack)
	return out
}

// Definitions returns the list of word names the user has defined this session.
func (r *REPL) Definitions() []string {
	out := make([]string, len(r.definitions))
	copy(out, r.definitions)
	return out
}

// History returns the accumulated source of all word definitions, used to
// reconstruct dictionary state when compiling the next user line.
func (r *REPL) History() string { return r.history }

// Eval handles one line of REPL input. Returns true if a meta-command (help,
// stack, exit, etc.) was matched; false means it was forwarded to the
// compiler. Either way the output sink is informed.
func (r *REPL) Eval(line string) {
	line = strings.TrimSpace(line)
	if line == "" {
		return
	}
	if r.handleCommand(line) {
		return
	}
	r.evaluate(line)
}

// IsExitCommand returns true if the line is a request to terminate the REPL.
// CLI wrappers use this to exit; graphical wrappers can ignore it.
func (r *REPL) IsExitCommand(line string) bool {
	switch strings.TrimSpace(line) {
	case "exit", "quit", "q":
		return true
	}
	return false
}

func (r *REPL) emit(s string) { r.output(s) }

func (r *REPL) handleCommand(line string) bool {
	switch line {
	case "help", "?":
		r.printHelp()
		return true
	case "clear", "reset":
		r.history = ""
		r.definitions = nil
		r.emit("History cleared\n")
		return true
	case "clearstack", "cs":
		r.stack = nil
		r.emit("Stack cleared\n")
		return true
	case "stack", ".s":
		if len(r.stack) == 0 {
			r.emit("  Stack: []\n")
		} else {
			r.emit(fmt.Sprintf("  Stack: %v\n", r.stack))
		}
		return true
	case "drop":
		if len(r.stack) > 0 {
			r.stack = r.stack[:len(r.stack)-1]
		}
		if len(r.stack) > 0 {
			r.emit(fmt.Sprintf("  Stack: %v\n", r.stack))
		} else {
			r.emit("  Stack: []\n")
		}
		return true
	case "words":
		if len(r.definitions) == 0 {
			r.emit("No words defined\n")
		} else {
			r.emit("Defined words: " + strings.Join(r.definitions, ", ") + "\n")
		}
		return true
	case "history":
		if r.history == "" {
			r.emit("No history\n")
		} else {
			r.emit(r.history + "\n")
		}
		return true
	}
	return false
}

func (r *REPL) evaluate(line string) {
	if strings.HasPrefix(line, "@") {
		if !strings.Contains(line, ";") {
			r.emit("Error: Word definition must end with ';'\n")
			return
		}
		r.history += line + "\n"
		parts := strings.Fields(line[1:])
		if len(parts) >= 1 {
			r.definitions = append(r.definitions, parts[0])
			r.emit(fmt.Sprintf("Defined word '%s'\n", parts[0]))
		}
		return
	}

	source := r.history
	for _, val := range r.stack {
		source += fmt.Sprintf("%d ", val)
	}
	source += line

	bytecode, err := lux.Compile(source)
	if err != nil {
		r.emit(fmt.Sprintf("Compile error: %v\n", err))
		return
	}
	machine := vm.NewVM(bytecode, false)
	if err := machine.Run(); err != nil {
		r.emit(fmt.Sprintf("Runtime error: %v\n", err))
		return
	}
	r.stack = machine.Stack()
	if len(r.stack) > 0 {
		r.emit(fmt.Sprintf("  Stack: %v\n", r.stack))
	} else {
		r.emit("  Stack: []\n")
	}
}

func (r *REPL) printHelp() {
	r.emit("\n=== LUX REPL Commands ===\n")
	r.emit("  help, ?          - Show this help\n")
	r.emit("  exit, quit, q    - Exit REPL\n")
	r.emit("  clear, reset     - Clear word definitions\n")
	r.emit("  clearstack, cs   - Clear the stack\n")
	r.emit("  stack, .s        - Show current stack\n")
	r.emit("  drop             - Drop top stack value\n")
	r.emit("  words            - List defined words\n")
	r.emit("  history          - Show definition history\n")
}
