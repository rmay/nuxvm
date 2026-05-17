package lux

import (
	"bytes"
	"os"
	"path/filepath"
	"testing"

	"github.com/rmay/nuxvm/pkg/vm"
)

// TestLoadProgramCompilesLux verifies that .lux inputs are compiled on the fly
// and yield the same bytecode as calling Compile directly.
func TestLoadProgramCompilesLux(t *testing.T) {
	dir := t.TempDir()
	src := "1 2 + HALT"
	path := filepath.Join(dir, "tiny.lux")
	if err := os.WriteFile(path, []byte(src), 0644); err != nil {
		t.Fatal(err)
	}

	loaded, err := LoadProgram(path, int32(vm.HeadlessBaseAddress))
	if err != nil {
		t.Fatalf("LoadProgram: %v", err)
	}
	expected, err := Compile(src, int32(vm.HeadlessBaseAddress))
	if err != nil {
		t.Fatalf("Compile: %v", err)
	}
	if !bytes.Equal(loaded, expected) {
		t.Errorf("LoadProgram output %v did not match Compile output %v", loaded, expected)
	}
}

// TestLoadProgramPassesThroughBin verifies that non-.lux files are returned
// verbatim — LoadProgram is a runtime entry point for both source and bytecode.
func TestLoadProgramPassesThroughBin(t *testing.T) {
	dir := t.TempDir()
	raw := []byte{0x01, 0x02, 0x03, 0x04}
	path := filepath.Join(dir, "raw.bin")
	if err := os.WriteFile(path, raw, 0644); err != nil {
		t.Fatal(err)
	}
	got, err := LoadProgram(path, int32(vm.HeadlessBaseAddress))
	if err != nil {
		t.Fatalf("LoadProgram: %v", err)
	}
	if !bytes.Equal(got, raw) {
		t.Errorf("bin passthrough changed bytes: got %v, want %v", got, raw)
	}
}

// TestLoadProgramMissingFile returns a clear error.
func TestLoadProgramMissingFile(t *testing.T) {
	if _, err := LoadProgram("/this/path/should/not/exist.lux", int32(vm.HeadlessBaseAddress)); err == nil {
		t.Error("expected error for missing file")
	}
}
