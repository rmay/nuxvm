package lux

import (
	"fmt"
	"os"
	"strings"
)

// LoadProgram reads a program from path. If path ends in ".lux" the source is
// compiled in-process and the bytecode is returned. Any other extension is
// treated as pre-compiled bytecode and returned verbatim. Errors bubble up
// with a short, actionable message — callers can print and exit.
func LoadProgram(path string) ([]byte, error) {
	data, err := os.ReadFile(path)
	if err != nil {
		return nil, fmt.Errorf("read %s: %w", path, err)
	}
	if strings.HasSuffix(strings.ToLower(path), ".lux") {
		bytecode, err := Compile(string(data))
		if err != nil {
			return nil, fmt.Errorf("compile %s: %w", path, err)
		}
		return bytecode, nil
	}
	return data, nil
}
