package main

import (
	"bufio"
	"fmt"
	"os"
	"strings"

	"github.com/rmay/nuxvm/pkg/lux"
	"github.com/rmay/nuxvm/pkg/vm"
)

type REPL struct {
	history     string
	scanner     *bufio.Scanner
	stack       []int32  // Persistent stack across commands
	definitions []string // Track defined words
}

func NewREPL() *REPL {
	return &REPL{
		history:     "",
		scanner:     bufio.NewScanner(os.Stdin),
		stack:       []int32{},
		definitions: []string{},
	}
}

func (r *REPL) Run() {
	r.printBanner()

	for {
		fmt.Print("lux> ")

		if !r.scanner.Scan() {
			break
		}

		line := strings.TrimSpace(r.scanner.Text())

		if line == "" {
			continue
		}

		if r.handleCommand(line) {
			continue
		}

		r.evaluate(line)
	}
}

func (r *REPL) printBanner() {
	fmt.Println("╔═══════════════════════════════╗")
	fmt.Println("║       LUX REPL 300K           ║")
	fmt.Println("║  Stack-based Language REPL    ║")
	fmt.Println("╚═══════════════════════════════╝")
	fmt.Println()
	fmt.Println("Type 'help' for commands, 'exit' to quit")
	fmt.Println()
}

func (r *REPL) handleCommand(line string) bool {
	switch line {
	case "exit", "quit", "q":
		fmt.Println("Goodbye!")
		os.Exit(0)
		return true

	case "help", "?":
		r.printHelp()
		return true

	case "clear", "reset":
		r.history = ""
		r.definitions = []string{}
		fmt.Println("History cleared")
		return true

	case "clearstack", "cs":
		r.stack = []int32{}
		fmt.Println("Stack cleared")
		return true

	case "stack", ".s":
		if len(r.stack) == 0 {
			fmt.Println("  Stack: []")
		} else {
			fmt.Printf("  \nStack: %v\n", r.stack)
		}
		return true

	case "drop":
		if len(r.stack) > 0 {
			r.stack = r.stack[:len(r.stack)-1]
			if len(r.stack) > 0 {
				fmt.Printf("  Stack: %v\n", r.stack)
			} else {
				fmt.Println("  Stack: []")
			}
		} else {
			fmt.Println("Stack is empty")
		}
		return true

	case "words":
		if len(r.definitions) == 0 {
			fmt.Println("No words defined")
		} else {
			fmt.Printf("Defined words: %s\n", strings.Join(r.definitions, ", "))
		}
		return true

	case "history":
		if r.history == "" {
			fmt.Println("No history")
		} else {
			fmt.Println(r.history)
		}
		return true
	}

	return false
}

func (r *REPL) evaluate(line string) {
	// Handle word definitions
	if strings.HasPrefix(line, "@") {
		if !strings.Contains(line, ";") {
			fmt.Println("Error: Word definition must end with ';'")
			fmt.Println("Example: @square dup * ;")
			return
		}
		r.history += line + "\n"

		// Extract word name
		parts := strings.Fields(line[1:])
		if len(parts) >= 1 {
			wordName := parts[0]
			r.definitions = append(r.definitions, wordName)
			fmt.Printf("Defined word '%s'\n", wordName)
		}
		return
	}

	// Build source with current stack state + new line
	source := r.history

	// Restore stack by pushing all current values
	for _, val := range r.stack {
		source += fmt.Sprintf("%d ", val)
	}

	// Add the new line
	source += line

	// Compile and run
	bytecode, err := lux.Compile(source)
	if err != nil {
		fmt.Printf("Compile error: %v\n", err)
		return
	}

	// Execute
	machine := vm.NewVM(bytecode, false)
	if err := machine.Run(); err != nil {
		fmt.Printf("Runtime error: %v\n", err)
		return
	}

	// Save the resulting stack
	r.stack = machine.Stack()

	// Show stack
	if len(r.stack) > 0 {
		fmt.Printf("  Stack: %v\n", r.stack)
	} else {
		fmt.Println("  Stack: []")
	}
}

func (r *REPL) printHelp() {
	fmt.Println("\n═══ LUX REPL Commands ═══")
	fmt.Println("  help, ?          - Show this help")
	fmt.Println("  exit, quit, q    - Exit REPL")
	fmt.Println("  clear, reset     - Clear word definitions")
	fmt.Println("  clearstack, cs   - Clear the stack")
	fmt.Println("  stack, .s        - Show current stack")
	fmt.Println("  drop             - Drop top stack value")
	fmt.Println("  words            - List defined words")
	fmt.Println("  history          - Show definition history")
	fmt.Println()
	fmt.Println("═══ Examples ═══")
	fmt.Println("  Build up stack:")
	fmt.Println("    lux> 5")
	fmt.Println("    lux> 10")
	fmt.Println("    lux> +")
	fmt.Println()
	fmt.Println("  Numbers:")
	fmt.Println("    lux> 42")
	fmt.Println("    lux> 0xFF")
	fmt.Println()
	fmt.Println("  Arithmetic:")
	fmt.Println("    lux> 5 10 +")
	fmt.Println("    lux> 7 6 *")
	fmt.Println()
	fmt.Println("  Stack operations:")
	fmt.Println("    lux> 5 dup +")
	fmt.Println("    lux> 1 2 3 swap")
	fmt.Println()
	fmt.Println("  Output:")
	fmt.Println("    lux> 42 .        (print number)")
	fmt.Println("    lux> 72 emit     (print 'H')")
	fmt.Println()
	fmt.Println("  Define words:")
	fmt.Println("    lux> @square dup * ;")
	fmt.Println("    lux> 5 square")
	fmt.Println()
}

func main() {
	repl := NewREPL()
	repl.Run()
}
