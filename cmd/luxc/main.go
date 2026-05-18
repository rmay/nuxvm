package main

import (
	"encoding/binary"
	"flag"
	"fmt"
	"os"
	"strconv"
	"strings"

	"github.com/rmay/nuxvm/pkg/lux"
	"github.com/rmay/nuxvm/pkg/vm"
)

func main() {
	trace := flag.Bool("trace", false, "enable compilation tracing")
	outFlag := flag.String("o", "", "output path (default: <input>.bin, stripping a trailing .lux)")
	targetFlag := flag.String("target", "graphical", "compilation target (graphical or headless)")
	dumpAtFlag := flag.String("dumpAt", "", "absolute PC (decimal or 0x-prefixed hex) — disassemble bytes around this address and exit without writing output")
	dumpRangeFlag := flag.Int("dumpRange", 64, "bytes before/after dumpAt to show")
	flag.Parse()

	if len(flag.Args()) < 1 {
		fmt.Fprintln(os.Stderr, "Usage: luxc [-trace] [-o out.bin] [-target graphical|headless] [-dumpAt 0xADDR] <file.lux>")
		os.Exit(1)
	}

	baseAddr := vm.GraphicalBaseAddress
	if *targetFlag == "headless" {
		baseAddr = vm.HeadlessBaseAddress
	}

	inPath := flag.Args()[0]

	source, err := os.ReadFile(inPath)
	if err != nil {
		fmt.Fprintf(os.Stderr, "luxc: read %s: %v\n", inPath, err)
		os.Exit(1)
	}

	bytecode, err := lux.Compile(string(source), int32(baseAddr), *trace)
	if err != nil {
		fmt.Fprintf(os.Stderr, "luxc: %v\n", err)
		os.Exit(1)
	}

	if *dumpAtFlag != "" {
		pc, err := parseAddress(*dumpAtFlag)
		if err != nil {
			fmt.Fprintf(os.Stderr, "luxc: bad -dumpAt %q: %v\n", *dumpAtFlag, err)
			os.Exit(1)
		}
		dumpAround(bytecode, int32(baseAddr), pc, *dumpRangeFlag)
		return
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

func parseAddress(s string) (int64, error) {
	s = strings.TrimSpace(s)
	if strings.HasPrefix(s, "0x") || strings.HasPrefix(s, "0X") {
		return strconv.ParseInt(s[2:], 16, 64)
	}
	return strconv.ParseInt(s, 10, 64)
}

// dumpAround prints a hex+disassembly window of `bytecode` centered on absolute
// address `pc`. Bytes whose opcode lookup fails are flagged so an unknown-opcode
// crash site is obvious. Multi-byte instructions (PUSH/JMP/JZ/...) are decoded
// from their starting offset; the window starts at pc-radius and *resyncs* on
// each emitted instruction so a stray padding byte doesn't produce nonsense.
func dumpAround(bytecode []byte, baseAddr int32, pc int64, radius int) {
	codeLen := len(bytecode)
	startAbs := pc - int64(radius)
	endAbs := pc + int64(radius)
	startOff := int(startAbs - int64(baseAddr))
	endOff := int(endAbs - int64(baseAddr))
	if startOff < 0 {
		startOff = 0
	}
	if endOff > codeLen {
		endOff = codeLen
	}

	fmt.Printf("Bytecode dump: total length %d (0x%X) bytes, baseAddr 0x%X\n", codeLen, codeLen, baseAddr)
	fmt.Printf("Target PC 0x%X (offset %d). Window: offset %d..%d (abs 0x%X..0x%X)\n\n",
		pc, int(pc-int64(baseAddr)), startOff, endOff, baseAddr+int32(startOff), baseAddr+int32(endOff))

	fmt.Println("Linear hex (16 bytes/line):")
	for i := startOff; i < endOff; i += 16 {
		end := i + 16
		if end > endOff {
			end = endOff
		}
		fmt.Printf("  0x%08X: ", baseAddr+int32(i))
		for j := i; j < end; j++ {
			mark := " "
			if int64(baseAddr+int32(j)) == pc {
				mark = "*"
			}
			fmt.Printf("%s%02X", mark, bytecode[j])
		}
		fmt.Println()
	}

	fmt.Println("\nDisassembly (best-effort, starting from window start):")
	off := startOff
	for off < endOff {
		abs := int64(baseAddr) + int64(off)
		op := bytecode[off]
		name := vm.OpcodeName(op)
		marker := "  "
		if abs == pc {
			marker = ">>"
		}
		size := opcodeSize(op)
		if off+size > codeLen {
			size = codeLen - off
		}
		switch size {
		case 1:
			fmt.Printf("  %s 0x%08X: %02X            %s\n", marker, abs, op, name)
		case 5:
			imm := int32(binary.BigEndian.Uint32(bytecode[off+1 : off+5]))
			fmt.Printf("  %s 0x%08X: %02X %02X%02X%02X%02X  %s 0x%X (%d)\n",
				marker, abs, op, bytecode[off+1], bytecode[off+2], bytecode[off+3], bytecode[off+4],
				name, uint32(imm), imm)
		default:
			fmt.Printf("  %s 0x%08X: %02X            %s (size=%d)\n", marker, abs, op, name, size)
		}
		if size <= 0 {
			size = 1
		}
		off += size
	}
}

// opcodeSize returns the encoded byte length of an opcode. 5 for opcodes with
// a 4-byte immediate (PUSH/JMP/JZ/JNZ/CALL/LOAD/STORE), 1 otherwise. Unknown
// opcodes return 1 so the disassembler can resync byte-by-byte.
func opcodeSize(op byte) int {
	switch op {
	case vm.OpPush, vm.OpJmp, vm.OpJz, vm.OpJnz, vm.OpCall, vm.OpLoad, vm.OpStore:
		return 5
	}
	return 1
}
