package main

import (
	"flag"
	"fmt"
	"os"

	"vapor.solarvoid.com/russell/nuxvm/pkg/lux"
)

func main() {
	flag.Parse()

	if len(flag.Args()) < 1 {
		fmt.Println("Usage: luxc <file.lux>")
		os.Exit(1)
	}

	// Read source
	source, _ := os.ReadFile(flag.Args()[0])

	// Compile to bytecode
	bytecode, err := lux.Compile(string(source))
	if err != nil {
		fmt.Fprintf(os.Stderr, "Error: %v\n", err)
		os.Exit(1)
	}

	// Write bytecode
	outFile := flag.Args()[0][:len(flag.Args()[0])-4] + ".bin"
	os.WriteFile(outFile, bytecode, 0644)

	fmt.Printf("Compiled: %s\n", outFile)
}
