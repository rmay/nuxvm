package main

import (
	"encoding/binary"
	"fmt"

	"github.com/rmay/nuxvm/pkg/system"
	"github.com/rmay/nuxvm/pkg/vm"
)

func push(value int32) []byte { return vm.PushInstruction(value) }
func load(addr uint32) []byte { return vm.LoadInstruction(int32(addr)) }
func store(addr uint32) []byte { return vm.StoreInstruction(int32(addr)) }

// Example 1: Write a pixel value to the video framebuffer and read it back.
// The framebuffer is 8KB starting at 0x4000.
// Each 4-byte word holds one pixel (0x00RRGGBB).
func ex1_VideoFramebuffer() {
	fmt.Println("╔══ EXAMPLE 1: Video Framebuffer Write/Read ══╗")

	pixelAddr := uint32(vm.VideoFramebufferStart)
	pixelValue := int32(0xFF0000) // Red channel

	prog := []byte{}
	prog = append(prog, push(pixelValue)...)
	prog = append(prog, store(pixelAddr)...)
	prog = append(prog, load(pixelAddr)...)
	prog = append(prog, vm.OutNumber()...)
	prog = append(prog, vm.OpHalt)

	machine := system.NewMachine(prog)
	if err := machine.CPU.Run(); err != nil {
		fmt.Printf("  Error: %v\n", err)
		return
	}
	fmt.Printf("  Pixel at [0x%04X]: ", pixelAddr)
	fmt.Printf(" (Expected: %d)\n\n", pixelValue)
}

// Example 2: Fill the first row of the framebuffer (8 pixels × 4 bytes = 32 bytes).
// Prints each stored value to confirm the fill.
func ex2_FillFramebufferRow() {
	fmt.Println("╔══ EXAMPLE 2: Fill Framebuffer Row (8 pixels) ══╗")

	const pixels = 8
	color := int32(0x00FF00) // Green

	prog := []byte{}
	for i := 0; i < pixels; i++ {
		addr := uint32(vm.VideoFramebufferStart) + uint32(i*4)
		prog = append(prog, push(color)...)
		prog = append(prog, store(addr)...)
	}
	// Read back all 8 pixels
	for i := 0; i < pixels; i++ {
		addr := uint32(vm.VideoFramebufferStart) + uint32(i*4)
		prog = append(prog, load(addr)...)
		prog = append(prog, vm.OutNumber()...)
		prog = append(prog, push(32)...)     // space
		prog = append(prog, vm.OutCharacter()...)
	}
	prog = append(prog, vm.OpHalt)

	machine := system.NewMachine(prog)
	fmt.Print("  Pixels: ")
	if err := machine.CPU.Run(); err != nil {
		fmt.Printf("  Error: %v\n", err)
		return
	}
	fmt.Printf("\n  (Expected: %d × 8)\n\n", color)
}

// Example 3: Read keyboard status register.
// Returns 1 if a key is pressed (simulated), 0 otherwise.
func ex3_KeyboardStatus() {
	fmt.Println("╔══ EXAMPLE 3: Keyboard Status Read ══╗")

	prog := []byte{}
	prog = append(prog, load(vm.ControllerStatusAddr)...)
	prog = append(prog, vm.OutNumber()...)
	prog = append(prog, vm.OpHalt)

	machine := system.NewMachine(prog)
	machine.PushKey(1) // Simulate key pressed
	fmt.Print("  Key pressed: ")
	if err := machine.CPU.Run(); err != nil {
		fmt.Printf("  Error: %v\n", err)
		return
	}
	fmt.Print(" (1 = key held, 0 = no key; simulated via Machine.PushKey)\n\n")
}

// Example 4: Send a command to the audio control register.
// Writes a frequency value; reads it back to confirm.
func ex4_AudioControl() {
	fmt.Println("╔══ EXAMPLE 4: Audio Control Write ══╗")

	const noteA4 = int32(440) // 440 Hz = A4

	prog := []byte{}
	prog = append(prog, push(noteA4)...)
	prog = append(prog, store(vm.AudioControlAddr)...)
	// Read back the value written to memory
	prog = append(prog, load(vm.AudioControlAddr)...)
	prog = append(prog, vm.OutNumber()...)
	prog = append(prog, vm.OpHalt)

	machine := system.NewMachine(prog)
	fmt.Print("  Audio command: ")
	if err := machine.CPU.Run(); err != nil {
		fmt.Printf("  Error: %v\n", err)
		return
	}
	fmt.Printf(" Hz (Expected: %d)\n\n", noteA4)
}

// Example 5: Scan the framebuffer for a non-zero pixel written by the program.
// Writes a pattern, then walks the buffer to find the first written word.
func ex5_FramebufferScan() {
	fmt.Println("╔══ EXAMPLE 5: Framebuffer Scan ══╗")

	// Write a sentinel at pixel 5 (offset 20 bytes)
	sentinel := int32(0xDEAD)
	sentinelAddr := uint32(vm.VideoFramebufferStart) + 20

	prog := []byte{}
	prog = append(prog, push(sentinel)...)
	prog = append(prog, store(sentinelAddr)...)

	// Scan pixels 0–7 and print non-zero ones
	for i := 0; i < 8; i++ {
		addr := uint32(vm.VideoFramebufferStart) + uint32(i*4)
		prog = append(prog, load(addr)...)
		prog = append(prog, vm.OutNumber()...)
		prog = append(prog, push(32)...)
		prog = append(prog, vm.OutCharacter()...)
	}
	prog = append(prog, vm.OpHalt)

	machine := system.NewMachine(prog)
	fmt.Print("  Pixels [0–7]: ")
	if err := machine.CPU.Run(); err != nil {
		fmt.Printf("  Error: %v\n", err)
		return
	}
	fmt.Printf("\n  (Expected: 0 0 0 0 0 %d 0 0)\n\n", sentinel)
}

// Example 6: Device memory map summary — print the base addresses of each device.
func ex6_DeviceMemoryMap() {
	fmt.Println("╔══ EXAMPLE 6: Device Memory Map ══╗")
	fmt.Printf("  Reserved memory:     0x%04X–0x%04X (%d bytes)\n",
		0, vm.ReservedMemorySize-1, vm.ReservedMemorySize)
	fmt.Printf("  Video framebuffer:   0x%04X–0x%04X (%d bytes)\n",
		vm.VideoFramebufferStart, vm.VideoFramebufferEnd-1, vm.VideoBufferSize)
	fmt.Printf("  Controller status:   0x%04X\n", vm.ControllerStatusAddr)
	fmt.Printf("  Audio control:       0x%04X\n", vm.AudioControlAddr)
	fmt.Printf("  User memory start:   0x%04X\n\n", vm.UserMemoryOffset)
	}

// Example 7: Verify that writing outside the device region (to user memory) works normally.
func ex7_NormalMemoryUnaffected() {
	fmt.Println("╔══ EXAMPLE 7: Normal Memory Write (outside device region) ══╗")

	userAddr := uint32(vm.UserMemoryOffset) + 200
	value := int32(12345)

	prog := make([]byte, 300) // enough room for user addr + 200 bytes
	instructions := []byte{}
	instructions = append(instructions, push(value)...)
	instructions = append(instructions, store(userAddr)...)
	instructions = append(instructions, load(userAddr)...)
	instructions = append(instructions, vm.OutNumber()...)
	instructions = append(instructions, vm.OpHalt)
	copy(prog, instructions)

	machine := system.NewMachine(prog)
	fmt.Print("  Value at user mem+200: ")
	if err := machine.CPU.Run(); err != nil {
		fmt.Printf("  Error: %v\n", err)
		return
	}
	fmt.Printf(" (Expected: %d)\n\n", value)
}

// Example 8: Read keyboard status in a tight poll loop (simulated; exits after 3 checks).
func ex8_KeyboardPoll() {
	fmt.Println("╔══ EXAMPLE 8: Keyboard Poll (3 iterations) ══╗")

	// Counter in reserved memory at address 0
	counterAddr := uint32(0)

	prog := []byte{}

	// initialise counter = 3
	prog = append(prog, push(3)...)
	prog = append(prog, store(counterAddr)...)

	// loop: read keyboard, print status, decrement counter, repeat if > 0
	loopStart := vm.UserMemoryOffset + int32(len(prog))

	prog = append(prog, load(vm.ControllerStatusAddr)...)
	prog = append(prog, vm.OutNumber()...)
	prog = append(prog, push(32)...)
	prog = append(prog, vm.OutCharacter()...)

	prog = append(prog, load(counterAddr)...)
	prog = append(prog, vm.OpDec)
	prog = append(prog, vm.OpDup)
	prog = append(prog, store(counterAddr)...)

	// exit when counter reaches 0
	exitPH := len(prog)
	prog = append(prog, vm.JzInstruction(0)...)
	prog = append(prog, vm.JmpInstruction(loopStart)...)

	exitAddr := vm.UserMemoryOffset + int32(len(prog))
	binary.BigEndian.PutUint32(prog[exitPH+1:], uint32(exitAddr))

	prog = append(prog, vm.OpHalt)

	machine := system.NewMachine(prog)
	machine.PushButton(1) // Simulate key pressed
	fmt.Print("  Keyboard reads: ")
	if err := machine.CPU.Run(); err != nil {
		fmt.Printf("  Error: %v\n", err)
		return
	}
	fmt.Print("\n  (Expected: 1 1 1 — key simulated via Machine.PushButton)\n\n")
}

func main() {
	fmt.Println("\n╔══════════════════════════════════════════════════════════╗")
	fmt.Println("║           Device I/O Examples — NUXVM                   ║")
	fmt.Print("╚══════════════════════════════════════════════════════════╝\n\n")

	ex6_DeviceMemoryMap()
	ex1_VideoFramebuffer()
	ex2_FillFramebufferRow()
	ex3_KeyboardStatus()
	ex4_AudioControl()
	ex5_FramebufferScan()
	ex7_NormalMemoryUnaffected()
	ex8_KeyboardPoll()

	fmt.Println("╔══════════════════════════════════════════════════════════╗")
	fmt.Println("║                   All Examples Complete!                 ║")
	fmt.Print("╚══════════════════════════════════════════════════════════╝\n\n")
}
