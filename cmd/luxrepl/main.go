package main

import (
	"bufio"
	"fmt"
	"os"
	"strings"

	"github.com/rmay/nuxvm/pkg/luxrepl"
)

func main() {
	printBanner()

	scanner := bufio.NewScanner(os.Stdin)
	repl := luxrepl.New(func(s string) { fmt.Print(s) })

	for {
		fmt.Print("lux> ")
		if !scanner.Scan() {
			break
		}
		line := strings.TrimSpace(scanner.Text())
		if repl.IsExitCommand(line) {
			fmt.Println("Goodbye!")
			return
		}
		repl.Eval(line)
	}
}

func printBanner() {
	fmt.Println("╔═══════════════════════════════╗")
	fmt.Println("║       LUX REPL 280K           ║")
	fmt.Println("║  Stack-based Language REPL    ║")
	fmt.Println("╚═══════════════════════════════╝")
	fmt.Println()
	fmt.Println("Type 'help' for commands, 'exit' to quit")
	fmt.Println()
}
