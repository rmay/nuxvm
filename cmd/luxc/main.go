package main

import (
	"flag"
	"fmt"
	"os"
	"strings"

	"github.com/rmay/nuxvm/pkg/lux"
)

func main() {
	trace := flag.Bool("trace", false, "enable compilation tracing")
	outFlag := flag.String("o", "", "output path (default: <input>.bin, stripping a trailing .lux)")
	flag.Parse()

	if len(flag.Args()) < 1 {
		fmt.Fprintln(os.Stderr, "Usage: luxc [-trace] [-o out.bin] <file.lux>")
		os.Exit(1)
	}
	inPath := flag.Args()[0]

	source, err := os.ReadFile(inPath)
	if err != nil {
		fmt.Fprintf(os.Stderr, "luxc: read %s: %v\n", inPath, err)
		os.Exit(1)
	}

	bytecode, err := lux.Compile(string(source), *trace)
	if err != nil {
		fmt.Fprintf(os.Stderr, "luxc: %v\n", err)
		os.Exit(1)
	}

	outPath := *outFlag
	if outPath == "" {
		outPath = defaultOutput(inPath)
	}
	if err := os.WriteFile(outPath, bytecode, 0644); err != nil {
		fmt.Fprintf(os.Stderr, "luxc: write %s: %v\n", outPath, err)
		os.Exit(1)
	}
	fmt.Printf("Compiled: %s\n", outPath)
}

func defaultOutput(inPath string) string {
	if strings.HasSuffix(strings.ToLower(inPath), ".lux") {
		return inPath[:len(inPath)-4] + ".bin"
	}
	return inPath + ".bin"
}
