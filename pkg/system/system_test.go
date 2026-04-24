package system

import (
	"os"
	"path/filepath"
	"strings"
	"testing"

	"github.com/rmay/nuxvm/pkg/vm"
)

// fileTestRig sets up a System sandboxed to a temporary directory with a VM
// memory slice big enough for name and buffer pointers at fixed offsets.
type fileTestRig struct {
	sys     *System
	mem     []byte
	tempDir string
	nameAddr uint32
	bufAddr  uint32
}

func newFileTestRig(t *testing.T) *fileTestRig {
	t.Helper()
	tempDir := t.TempDir()
	origDir, _ := os.Getwd()
	t.Cleanup(func() { os.Chdir(origDir) })
	if err := os.Chdir(tempDir); err != nil {
		t.Fatalf("chdir: %v", err)
	}
	sys := NewSystem()
	mem := make([]byte, vm.UserMemoryOffset+4096)
	sys.SetMemory(mem)
	return &fileTestRig{
		sys:      sys,
		mem:      mem,
		tempDir:  tempDir,
		nameAddr: uint32(vm.UserMemoryOffset) + 100,
		bufAddr:  uint32(vm.UserMemoryOffset) + 400,
	}
}

// setName writes a null-terminated filename into VM memory and points the
// File device at it.
func (r *fileTestRig) setName(name string) {
	// Wipe the old name region first so a shorter name doesn't leave tail
	// bytes from a previous test step.
	for i := r.nameAddr; i < r.nameAddr+256; i++ {
		r.mem[i] = 0
	}
	copy(r.mem[r.nameAddr:], []byte(name+"\x00"))
	r.sys.Write(vm.FilePort+4, int32(r.nameAddr))
	r.sys.Write(vm.FilePort+8, int32(r.bufAddr))
}

// cmd packs (command, flags, length) the way the +12 register expects.
func cmd(command, flags, length uint32) int32 {
	return int32((command << 24) | ((flags & 0xFF) << 16) | (length & 0xFFFF))
}

// result reads FilePort+12 and returns the last op's result.
func (r *fileTestRig) result() int32 {
	v, _ := r.sys.Read(vm.FilePort + 12)
	return v
}

func TestDateTime(t *testing.T) {
	sys := NewSystem()
	
	// Test Unix timestamp
	ts, err := sys.Read(vm.DateTimeAddr)
	if err != nil {
		t.Fatalf("Read DateTimeAddr failed: %v", err)
	}
	if ts <= 0 {
		t.Errorf("Expected positive timestamp, got %d", ts)
	}

	// Test Packed Date
	date, err := sys.Read(vm.DateTimePort + 8)
	if err != nil {
		t.Fatalf("Read Packed Date failed: %v", err)
	}
	year := date >> 16
	if year < 2024 {
		t.Errorf("Expected year >= 2024, got %d", year)
	}

	// Test Read-only
	err = sys.Write(vm.DateTimeAddr, 123)
	if err == nil {
		t.Error("Expected error when writing to DateTimeAddr")
	}
}

func TestFileReadWrite(t *testing.T) {
	tempDir := t.TempDir()
	origDir, _ := os.Getwd()
	os.Chdir(tempDir)
	defer os.Chdir(origDir)

	sys := NewSystem()
	mem := make([]byte, vm.UserMemoryOffset+1024) 
	sys.SetMemory(mem)

	filename := "testfile.txt"
	content := "Hello CLOISTER!"
	
	// Use addresses relative to UserMemoryOffset for safety
	nameAddr := uint32(vm.UserMemoryOffset) + 100
	bufAddr := uint32(vm.UserMemoryOffset) + 200

	// Setup filename in VM memory
	copy(mem[nameAddr:], []byte(filename+"\x00"))
	sys.Write(vm.FilePort+4, int32(nameAddr)) // FileNamePtr

	// Setup data in VM memory
	copy(mem[bufAddr:], []byte(content))
	sys.Write(vm.FilePort+8, int32(bufAddr)) // FileBufferPtr

	// 1. Test Write
	// Command 2 (Write), Length 13
	cmd := (uint32(2) << 24) | uint32(len(content))
	err := sys.Write(vm.FilePort+12, int32(cmd))
	if err != nil {
		t.Fatalf("File Write command failed: %v", err)
	}
	
	res, _ := sys.Read(vm.FilePort+12)
	if res != int32(len(content)) {
		t.Errorf("Expected write result %d, got %d", len(content), res)
	}

	// Verify file was written
	if _, err := os.Stat(filename); os.IsNotExist(err) {
		t.Fatalf("File was not created in %s", tempDir)
	}

	// 2. Test Stat
	cmdStat := (uint32(3) << 24)
	sys.Write(vm.FilePort+12, int32(cmdStat))
	resStat, _ := sys.Read(vm.FilePort+12)
	if resStat != int32(len(content)) {
		t.Errorf("Expected stat result %d, got %d", len(content), resStat)
	}

	// 3. Test Read
	// Clear memory first
	for i := bufAddr; i < bufAddr+100; i++ { mem[i] = 0 }
	
	cmdRead := (uint32(1) << 24) | uint32(len(content))
	sys.Write(vm.FilePort+12, int32(cmdRead))
	resRead, _ := sys.Read(vm.FilePort+12)
	if resRead != int32(len(content)) {
		t.Errorf("Expected read result %d, got %d", len(content), resRead)
	}
	
	readContent := string(mem[bufAddr : bufAddr+uint32(len(content))])
	if readContent != content {
		t.Errorf("Expected read content %q, got %q", content, readContent)
	}

	// 4. Test Delete
	cmdDel := (uint32(4) << 24)
	sys.Write(vm.FilePort+12, int32(cmdDel))
	resDel, _ := sys.Read(vm.FilePort+12)
	if resDel != 0 {
		t.Errorf("Expected delete result 0, got %d", resDel)
	}
	
	if _, err := os.Stat(filename); !os.IsNotExist(err) {
		t.Errorf("File still exists after delete")
	}
}

// TestFileSandboxEscape verifies that the File device rejects any path that
// would step outside the sandbox root: absolute paths, ".." traversal, and
// symlinks whose target lives outside the root.
func TestFileSandboxEscape(t *testing.T) {
	// Seed a file outside the rig's tempdir so escape attempts have
	// something to "find".
	outsideDir := t.TempDir()
	outsideFile := filepath.Join(outsideDir, "secret.txt")
	if err := os.WriteFile(outsideFile, []byte("do not read"), 0644); err != nil {
		t.Fatalf("seed: %v", err)
	}

	r := newFileTestRig(t)

	// A symlink inside the sandbox that points outside. resolvePath must
	// refuse this at EvalSymlinks time.
	linkPath := filepath.Join(r.tempDir, "escape")
	if err := os.Symlink(outsideDir, linkPath); err != nil {
		t.Fatalf("symlink: %v", err)
	}

	cases := []struct {
		name, path string
	}{
		{"absolute", outsideFile},
		{"parent traversal", "../" + filepath.Base(outsideDir) + "/secret.txt"},
		{"symlink escape", "escape/secret.txt"},
	}
	for _, tc := range cases {
		t.Run(tc.name, func(t *testing.T) {
			r.setName(tc.path)
			r.sys.Write(vm.FilePort+12, cmd(fileCmdRead, 0, 16))
			if got := r.result(); got != -1 {
				t.Errorf("expected -1 for escape %q, got %d", tc.path, got)
			}
			r.sys.Write(vm.FilePort+12, cmd(fileCmdStat, 0, 4))
			if got := r.result(); got != -1 {
				t.Errorf("expected -1 stat for escape %q, got %d", tc.path, got)
			}
			r.sys.Write(vm.FilePort+12, cmd(fileCmdDelete, 0, 0))
			if got := r.result(); got != -1 {
				t.Errorf("expected -1 delete for escape %q, got %d", tc.path, got)
			}
		})
	}

	// The outside file must still be intact.
	if _, err := os.Stat(outsideFile); err != nil {
		t.Fatalf("outside file disturbed: %v", err)
	}
}

// TestFileAppend verifies the append flag concatenates into an existing file
// instead of truncating.
func TestFileAppend(t *testing.T) {
	r := newFileTestRig(t)
	const name = "log.txt"
	r.setName(name)

	copy(r.mem[r.bufAddr:], []byte("one\n"))
	r.sys.Write(vm.FilePort+12, cmd(fileCmdWrite, 0, 4))
	if got := r.result(); got != 4 {
		t.Fatalf("initial write got %d, want 4", got)
	}

	// Re-set the name to close the writer and reset state. Then do an
	// append-mode write — this must add to the existing bytes, not truncate.
	r.setName(name)
	copy(r.mem[r.bufAddr:], []byte("two\n"))
	r.sys.Write(vm.FilePort+12, cmd(fileCmdWrite, 1, 4))
	if got := r.result(); got != 4 {
		t.Fatalf("append write got %d, want 4", got)
	}

	got, err := os.ReadFile(filepath.Join(r.tempDir, name))
	if err != nil {
		t.Fatalf("read back: %v", err)
	}
	if string(got) != "one\ntwo\n" {
		t.Errorf("append concat: got %q, want %q", got, "one\ntwo\n")
	}
}

// TestFileSequentialRead verifies the read cursor advances across calls so a
// large file can be streamed in chunks.
func TestFileSequentialRead(t *testing.T) {
	r := newFileTestRig(t)
	const name = "chunks.bin"
	payload := "ABCDEFGHIJ" // 10 bytes
	if err := os.WriteFile(filepath.Join(r.tempDir, name), []byte(payload), 0644); err != nil {
		t.Fatalf("seed: %v", err)
	}
	r.setName(name)

	// Read 4 bytes, then 4 bytes, then the remaining 2, then 0 (EOF).
	expect := []struct {
		want int32
		text string
	}{
		{4, "ABCD"},
		{4, "EFGH"},
		{2, "IJ"},
		{0, ""},
	}
	for i, step := range expect {
		// Scribble buffer so we can tell exactly how many bytes land.
		for j := r.bufAddr; j < r.bufAddr+16; j++ {
			r.mem[j] = 0
		}
		r.sys.Write(vm.FilePort+12, cmd(fileCmdRead, 0, 4))
		got := r.result()
		if got != step.want {
			t.Fatalf("step %d: got %d bytes, want %d", i, got, step.want)
		}
		if got > 0 {
			slice := string(r.mem[r.bufAddr : r.bufAddr+uint32(got)])
			if slice != step.text {
				t.Errorf("step %d: got %q, want %q", i, slice, step.text)
			}
		}
	}

	// SEEK rewinds; the next read should start from the beginning again.
	r.sys.Write(vm.FilePort+12, cmd(fileCmdSeek, 0, 0))
	if got := r.result(); got != 0 {
		t.Errorf("seek result: got %d, want 0", got)
	}
	r.sys.Write(vm.FilePort+12, cmd(fileCmdRead, 0, 4))
	if got := r.result(); got != 4 || string(r.mem[r.bufAddr:r.bufAddr+4]) != "ABCD" {
		t.Errorf("post-seek read: got %d/%q", got, string(r.mem[r.bufAddr:r.bufAddr+4]))
	}
}

// TestFileDirectoryListing verifies that pointing name at a directory and
// issuing READ emits one Varvara-style entry line per call.
func TestFileDirectoryListing(t *testing.T) {
	r := newFileTestRig(t)
	// Seed two files and a subdirectory.
	if err := os.WriteFile(filepath.Join(r.tempDir, "a.txt"), []byte("hello"), 0644); err != nil {
		t.Fatalf("seed a: %v", err)
	}
	if err := os.WriteFile(filepath.Join(r.tempDir, "b.bin"), []byte("hi"), 0644); err != nil {
		t.Fatalf("seed b: %v", err)
	}
	if err := os.Mkdir(filepath.Join(r.tempDir, "sub"), 0755); err != nil {
		t.Fatalf("seed sub: %v", err)
	}

	r.setName(".")
	var lines []string
	for i := 0; i < 10; i++ {
		for j := r.bufAddr; j < r.bufAddr+64; j++ {
			r.mem[j] = 0
		}
		r.sys.Write(vm.FilePort+12, cmd(fileCmdRead, 0, 64))
		n := r.result()
		if n == 0 {
			break
		}
		if n < 0 {
			t.Fatalf("list read error: %d", n)
		}
		lines = append(lines, string(r.mem[r.bufAddr:r.bufAddr+uint32(n)]))
	}
	if len(lines) != 3 {
		t.Fatalf("expected 3 entries, got %d: %v", len(lines), lines)
	}

	// Each entry is "<4-char detail> <name>\n". Order is directory-dependent;
	// just check presence and shape.
	joined := strings.Join(lines, "")
	if !strings.Contains(joined, "0005 a.txt\n") {
		t.Errorf("missing 'a.txt' size-5 entry; got:\n%s", joined)
	}
	if !strings.Contains(joined, "0002 b.bin\n") {
		t.Errorf("missing 'b.bin' size-2 entry; got:\n%s", joined)
	}
	if !strings.Contains(joined, "---- sub\n") {
		t.Errorf("missing 'sub' directory marker; got:\n%s", joined)
	}
}

// TestFileStatDetail verifies the 4-char detail string is written into the
// buffer when STAT is called with enough room.
func TestFileStatDetail(t *testing.T) {
	r := newFileTestRig(t)
	const name = "size.bin"
	if err := os.WriteFile(filepath.Join(r.tempDir, name), []byte("abcd"), 0644); err != nil {
		t.Fatalf("seed: %v", err)
	}
	r.setName(name)

	for i := r.bufAddr; i < r.bufAddr+8; i++ {
		r.mem[i] = 0xEE
	}
	r.sys.Write(vm.FilePort+12, cmd(fileCmdStat, 0, 4))
	if got := r.result(); got != 4 {
		t.Fatalf("stat size: got %d, want 4", got)
	}
	detail := string(r.mem[r.bufAddr : r.bufAddr+4])
	if detail != "0004" {
		t.Errorf("stat detail: got %q, want %q", detail, "0004")
	}
}
