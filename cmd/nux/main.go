package main

import (
	"flag"
	"fmt"
	"os"

	"vapor.solarvoid.com/russell/nuxvm/pkg/vm"
)

var (
	debugFlag = flag.Bool("debug", false, "Enable step-by-step debugging")
	traceFlag = flag.Bool("trace", false, "Show execution trace")
)

func main() {
	flag.Parse()

	if len(flag.Args()) < 1 {
		fmt.Println("Usage: nux [options] <program.nux>")
		fmt.Println("\nOptions:")
		flag.PrintDefaults()
		os.Exit(1)
	}

	filename := flag.Args()[0]
	program, err := os.ReadFile(filename)
	if err != nil {
		fmt.Fprintf(os.Stderr, "Error reading file: %v\n", err)
		os.Exit(1)
	}

	machine := vm.NewVM(program)

	if *debugFlag {
		runDebug(machine)
	} else if *traceFlag {
		runTrace(machine)
	} else {
		if err := machine.Run(); err != nil {
			fmt.Fprintf(os.Stderr, "---Runtime error---\n")
			fmt.Fprintf(os.Stderr, "Error: %v\n", err)
			fmt.Fprintf(os.Stderr, "%s\n", machine.DebugInfo())
			os.Exit(1)
		}
	}
}

func runDebug(machine *vm.VM) {
	fmt.Println("=== NUX Debugger ===")
	fmt.Println("Press Enter to step, 'q' to quit, 'c' to continue")
	fmt.Println()

	for {
		fmt.Printf("PC: %d, Stack: %v\n", machine.PC(), machine.Stack())
		fmt.Print("> ")

		var input string
		fmt.Scanln(&input)

		if input == "q" {
			break
		}

		if input == "c" {
			if err := machine.Run(); err != nil {
				fmt.Fprintf(os.Stderr, "Error: %v\n", err)
			}
			break
		}

		cont, err := machine.Step()
		if err != nil {
			fmt.Fprintf(os.Stderr, "Error: %v\n", err)
			break
		}
		if !cont {
			fmt.Println("Program halted")
			break
		}
	}

	fmt.Printf("\nFinal stack: %v\n", machine.Stack())
}

func runTrace(machine *vm.VM) {
	fmt.Println("=== Execution Trace ===")
	fmt.Println()

	for {
		pc := machine.PC()
		stack := machine.Stack()
		fmt.Printf("PC=%d Stack=%v\n", pc, stack)

		cont, err := machine.Step()
		if err != nil {
			fmt.Fprintf(os.Stderr, "Error at PC=%d: %v\n", pc, err)
			os.Exit(1)
		}
		if !cont {
			break
		}
	}

	fmt.Printf("\nFinal stack: %v\n", machine.Stack())
}
