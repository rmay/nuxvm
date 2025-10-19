package main

import (
	"fmt"

	"vapor.solarvoid.com/russell/nuxvm/pkg/vm"
)

// Helper function aliases for convenience
func push(value int32) []byte {
	return vm.PushInstruction(value)
}

func jz(addr int32) []byte {
	return vm.JzInstruction(addr)
}

func jmp(addr int32) []byte {
	return vm.JmpInstruction(addr)
}

func enc(value int32) []byte {
	return vm.EncodeInt32(value)
}

// Example 1: GCD
func ex1_GCD() {
	fmt.Println("╔══ EXAMPLE 1: GCD (48, 18) ══╗")
	prog := []byte{}
	prog = append(prog, push(48)...)
	prog = append(prog, push(18)...)
	loop := vm.UserMemoryOffset + int32(len(prog))
	prog = append(prog, vm.OpDup) // DUP
	endPH := len(prog)
	prog = append(prog, jz(0)...)
	prog = append(prog, vm.OpRoll, vm.OpRoll, vm.OpMod) // ROLL, ROLL, MOD
	prog = append(prog, vm.OpRot, vm.OpPop)             // ROT, POP
	prog = append(prog, jmp(loop)...)
	// Patch the JZ target
	endAddr := vm.UserMemoryOffset + int32(len(prog))
	copy(prog[endPH+1:], enc(endAddr))
	prog = append(prog, vm.OpPop)
	prog = append(prog, vm.OutNumber()...)
	prog = append(prog, vm.OpHalt) // POP, OUT, HALT

	fmt.Print("Result: ")
	vm.NewVM(prog).Run()
	fmt.Print(" (Expected: 6)\n\n")
}

// Example 2: Even/Odd Check
func ex2_EvenOdd() {
	fmt.Println("╔══ EXAMPLE 2: Even/Odd Check (42) ══╗")
	prog := []byte{}
	prog = append(prog, push(42)...)
	prog = append(prog, push(2)...)
	prog = append(prog, vm.OpMod) // MOD
	oddPH := len(prog)
	prog = append(prog, jz(0)...) // if 0, even
	prog = append(prog, push(1)...)
	prog = append(prog, vm.OpOut, vm.OpHalt) // OUT, HALT
	evenAddr := vm.UserMemoryOffset + int32(len(prog))
	copy(prog[oddPH+1:], enc(evenAddr))
	prog = append(prog, push(0)...)
	prog = append(prog, vm.OutNumber()...)
	prog = append(prog, vm.OpHalt) // OUT, HALT

	fmt.Print("Result: ")
	vm.NewVM(prog).Run()
	fmt.Print(" (0=even, 1=odd; Expected: 0)\n\n")
}

// Example 3: Absolute Value
func ex3_Absolute() {
	fmt.Println("╔══ EXAMPLE 3: Absolute Value (-25) ══╗")
	prog := []byte{}
	prog = append(prog, push(-25)...)
	prog = append(prog, vm.OpDup) // DUP
	prog = append(prog, push(0)...)
	prog = append(prog, vm.OpLt) // LT (is negative?)
	negPH := len(prog)
	prog = append(prog, jz(0)...) // if not negative, skip
	prog = append(prog, vm.OpNeg) // NEG
	endAddr := int32(len(prog))
	copy(prog[negPH+1:], enc(endAddr))
	prog = append(prog, vm.OutNumber()...)
	prog = append(prog, vm.OpHalt) // OUT, HALT

	fmt.Print("Result: ")
	vm.NewVM(prog).Run()
	fmt.Print(" (Expected: 25)\n\n")
}

// Example 4: Min of Two Numbers
func ex4_Min() {
	fmt.Println("╔══ EXAMPLE 4: Min(34, 21) ══╗")
	prog := []byte{}
	prog = append(prog, push(34)...)
	prog = append(prog, push(21)...)
	// Stack: [34, 21]
	prog = append(prog, vm.OpRoll, vm.OpRoll) // ROLL, ROLL -> [34, 21, 34, 21]
	prog = append(prog, vm.OpGt)              // GT (34 > 21?) -> [34, 21, 1]
	elsePH := len(prog)
	prog = append(prog, jz(0)...) // if false (a not > b), a is min
	// a > b, so b is min
	prog = append(prog, vm.OpSwap, vm.OpPop) // SWAP, POP -> [21]
	endPH := len(prog)
	prog = append(prog, jmp(0)...)
	// a <= b, so a is min
	elseAddr := vm.UserMemoryOffset + int32(len(prog))
	copy(prog[elsePH+1:], enc(elseAddr))
	prog = append(prog, vm.OpPop) // POP -> [34]
	endAddr := vm.UserMemoryOffset + int32(len(prog))
	copy(prog[endPH+1:], enc(endAddr))
	prog = append(prog, vm.OutNumber()...)
	prog = append(prog, vm.OpHalt) // OUT, HALT

	fmt.Print("Result: ")
	vm.NewVM(prog).Run()
	fmt.Print(" (Expected: 21)\n\n")
}

// Example 5: Max of Two Numbers
func ex5_Max() {
	fmt.Println("╔══ EXAMPLE 5: Max(15, 28) ══╗")
	prog := []byte{}
	prog = append(prog, push(15)...)
	prog = append(prog, push(28)...)
	// Stack: [15, 28]
	prog = append(prog, vm.OpRoll, vm.OpRoll) // ROLL, ROLL -> [15, 28, 15, 28]
	prog = append(prog, vm.OpLt)              // LT (15 < 28?) -> [15, 28, 1]
	elsePH := len(prog)
	prog = append(prog, jz(0)...) // if false (a not < b), a is max
	// a < b, so b is max
	prog = append(prog, vm.OpSwap, vm.OpPop) // SWAP, POP -> [28]
	endPH := len(prog)
	prog = append(prog, jmp(0)...)
	// a >= b, so a is max
	elseAddr := vm.UserMemoryOffset + int32(len(prog))
	copy(prog[elsePH+1:], enc(elseAddr))
	prog = append(prog, vm.OpPop) // POP -> [15]
	endAddr := vm.UserMemoryOffset + int32(len(prog))
	copy(prog[endPH+1:], enc(endAddr))
	prog = append(prog, vm.OutNumber()...)
	prog = append(prog, vm.OpHalt) // OUT, HALT

	fmt.Print("Result: ")
	vm.NewVM(prog).Run()
	fmt.Print(" (Expected: 28)\n\n")
}

// Example 6: Square a Number
func ex6_Square() {
	fmt.Println("╔══ EXAMPLE 6: Square of 12 ══╗")
	prog := []byte{}
	prog = append(prog, push(12)...)
	prog = append(prog, vm.OpDup) // DUP
	prog = append(prog, vm.OpMul) // MUL
	prog = append(prog, vm.OutNumber()...)
	prog = append(prog, vm.OpHalt) // OUT, HALT

	fmt.Print("Result: ")
	vm.NewVM(prog).Run()
	fmt.Print(" (Expected: 144)\n\n")
}

// Example 7: Simple Counter
func ex7_CountDown() {
	fmt.Println("╔══ EXAMPLE 7: Count Down from 10 ══╗")
	prog := []byte{}
	prog = append(prog, push(10)...)
	loop := vm.UserMemoryOffset + int32(len(prog))
	prog = append(prog, vm.OpDup)             // DUP
	prog = append(prog, vm.OutNumber()...)    // OUT (print current value)
	prog = append(prog, push(32)...)          // Push space character (ASCII 32)
	prog = append(prog, vm.OutCharacter()...) // Output space
	prog = append(prog, vm.OpDec)             // DEC
	prog = append(prog, vm.OpDup)             // DUP
	endPH := len(prog)
	prog = append(prog, jz(0)...) // if 0, exit
	prog = append(prog, jmp(loop)...)
	endAddr := vm.UserMemoryOffset + int32(len(prog))
	copy(prog[endPH+1:], enc(endAddr))
	prog = append(prog, vm.OpPop, vm.OpHalt) // POP, HALT

	fmt.Print("Result: ")
	vm.NewVM(prog).Run()
	fmt.Print("\n(Expected: 10 9 8 7 6 5 4 3 2 1)\n\n")
}

func main() {
	fmt.Println("\n╔══════════════════════════════════════════════════════╗")
	fmt.Println("║      Stack VM Examples Using main.go                 ║")
	fmt.Print("╚══════════════════════════════════════════════════════╝\n\n")

	ex1_GCD()
	ex2_EvenOdd()
	ex3_Absolute()
	ex4_Min()
	ex5_Max()
	ex6_Square()
	ex7_CountDown()

	fmt.Println("╔═════════════════════════════════════════════════════════════╗")
	fmt.Println("║                  All Examples Complete!                     ║")
	fmt.Print("╚═════════════════════════════════════════════════════════════╝\n\n")
}
