# NUX

A simple 32-opcode stack-based virtual machine written in Go.

## Features

- 32-bit signed integer operations
- 8192-element stack with 4096-elements reserved memory space
- Big-endian bytecode
- Jump instructions and subroutines
- Memory load/store operations
- Comprehensive test suite

## Installation

```bash
cd nux
go mod download
go build ./cmd/nux
```

## Usage

### Run a program
```bash
./nux program.nux
```

### Debug mode (step-by-step execution)
```bash
./nux -debug program.nux
```

### Trace mode (show all steps)
```bash
./nux -trace program.nux
```

### Run examples
```bash
cd examples
go run examples.go
```

### Run tests
```bash
cd pkg/vm
go test -v
```

## Architecture

- **Stack**: 1024 x 32-bit integers
- **Memory**: Byte-addressable (program + data)
- **PC**: 32-bit program counter
- **Encoding**: Big-endian

## Quick Example

```go
import "gvapor.solarvoid.com/russell/nuxvm"

// Create a simple program: 5 + 3
program := []byte{}
program = append(program, vm.PushInstruction(5)...)
program = append(program, vm.PushInstruction(3)...)
program = append(program, vm.OpAdd)
program = append(program, vm.OpOut)
program = append(program, vm.OpHalt)

// Run it
machine := vm.NewVM(program)
machine.Run() // Outputs: 8
```

## Project Structure

```
nux/
├── cmd/nux/          # CLI application
├── pkg/vm/           # VM implementation
├── examples/         # Example programs
└── docs/             # Documentation
```

## Documentation

See [examples/README.md](examples/README.md) for detailed examples and tutorials.

## License

MIT
