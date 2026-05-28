package system

import (
	"os"
	"path/filepath"
	"reflect"
	"sort"
	"strings"
	"testing"
	"time"
)

func TestFileDialogSorting(t *testing.T) {
	entries := []fileEntry{
		{name: "b.txt", isDir: false},
		{name: "a.txt", isDir: false},
		{name: "..", isDir: true},
		{name: ".hidden_file", isDir: false},
		{name: "FolderB", isDir: true},
		{name: "folderA", isDir: true},
		{name: ".hidden_folder", isDir: true},
		{name: "A.txt", isDir: false},
		{name: "B.txt", isDir: false},
	}

	priority := func(e fileEntry) int {
		if e.name == ".." {
			return 0
		}
		isHidden := strings.HasPrefix(e.name, ".")
		if e.isDir {
			if isHidden {
				return 1
			}
			return 2
		}
		if isHidden {
			return 3
		}
		return 4
	}

	sort.Slice(entries, func(i, j int) bool {
		pi := priority(entries[i])
		pj := priority(entries[j])

		if pi != pj {
			return pi < pj
		}
		// Case-insensitive comparison
		li := strings.ToLower(entries[i].name)
		lj := strings.ToLower(entries[j].name)
		if li != lj {
			return li < lj
		}
		// Tie-breaker for stable-ish look if names are same but different case
		return entries[i].name < entries[j].name
	})

	expected := []fileEntry{
		{name: "..", isDir: true},             // Priority 0
		{name: ".hidden_folder", isDir: true}, // Priority 1
		{name: "folderA", isDir: true},         // Priority 2
		{name: "FolderB", isDir: true},         // Priority 2
		{name: ".hidden_file", isDir: false},  // Priority 3
		{name: "A.txt", isDir: false},         // Priority 4 (tie-break A < a)
		{name: "a.txt", isDir: false},         // Priority 4
		{name: "B.txt", isDir: false},         // Priority 4 (tie-break B < b)
		{name: "b.txt", isDir: false},         // Priority 4
	}

	if !reflect.DeepEqual(entries, expected) {
		t.Errorf("Sorting failed.\nGot: %v\nWant: %v", entries, expected)
	}
}

func TestFileDialogNavigation(t *testing.T) {
	// 1. Setup temp directory structure
	tmpDir, err := os.MkdirTemp("", "nuxvm-dialog-test")
	if err != nil {
		t.Fatal(err)
	}
	defer os.RemoveAll(tmpDir)

	err = os.Mkdir(filepath.Join(tmpDir, "subdir"), 0755)
	if err != nil {
		t.Fatal(err)
	}
	err = os.WriteFile(filepath.Join(tmpDir, "file1.txt"), []byte("hello"), 0644)
	if err != nil {
		t.Fatal(err)
	}
	err = os.WriteFile(filepath.Join(tmpDir, "subdir", "file2.txt"), []byte("world"), 0644)
	if err != nil {
		t.Fatal(err)
	}

	// 2. Setup System and ServiceManager
	sys := &System{}
	sm := NewServiceManager()
	sm.SetSandboxResolver(func(p string) (string, error) {
		return filepath.Join(tmpDir, p), nil
	})
	sys.Services = sm
	sys.screenWidth = 640
	sys.screenHeight = 480

	replyChan := make(chan string, 1)
	m := newFileDialogModal(sys, replyChan)

	// 3. Verify initial state (root)
	if len(m.entries) != 2 {
		t.Errorf("Expected 2 entries, got %d", len(m.entries))
	}
	foundSubdir := false
	for _, e := range m.entries {
		if e.name == "subdir" && e.isDir {
			foundSubdir = true
		}
	}
	if !foundSubdir {
		t.Error("subdir not found in root")
	}

	// 4. Navigate into subdir
	subdirIdx := -1
	for i, e := range m.entries {
		if e.name == "subdir" {
			subdirIdx = i
			break
		}
	}
	m.selected = subdirIdx
	active := m.Update(&InputEvent{Type: InputKeyDown, KeyCode: 13}) // Enter
	if !active {
		t.Error("Modal should still be active after navigating into dir")
	}
	if m.path != "subdir" {
		t.Errorf("Expected path 'subdir', got %q", m.path)
	}

	// Verify entries in subdir: .., file2.txt
	if len(m.entries) != 2 {
		t.Errorf("Expected 2 entries in subdir, got %d", len(m.entries))
	}
	if m.entries[0].name != ".." {
		t.Errorf("Expected first entry to be '..', got %q", m.entries[0].name)
	}

	// 5. Navigate up
	m.selected = 0 // ".."
	m.Update(&InputEvent{Type: InputKeyDown, KeyCode: 13})
	if m.path != "." {
		t.Errorf("Expected path '.', got %q", m.path)
	}

	// 6. Select a file
	fileIdx := -1
	for i, e := range m.entries {
		if e.name == "file1.txt" {
			fileIdx = i
			break
		}
	}
	m.selected = fileIdx
	active = m.Update(&InputEvent{Type: InputKeyDown, KeyCode: 13})
	if active {
		t.Error("Modal should be closed after selecting a file")
	}

	select {
	case res := <-replyChan:
		if res != "file1.txt" {
			t.Errorf("Expected 'file1.txt', got %q", res)
		}
	case <-time.After(100 * time.Millisecond):
		t.Error("Timed out waiting for reply")
	}
}

func TestFileDialogDoubleClick(t *testing.T) {
	// 1. Setup temp directory structure
	tmpDir, err := os.MkdirTemp("", "nuxvm-dialog-click-test")
	if err != nil {
		t.Fatal(err)
	}
	defer os.RemoveAll(tmpDir)

	err = os.Mkdir(filepath.Join(tmpDir, "subdir"), 0755)
	if err != nil {
		t.Fatal(err)
	}

	// 2. Setup System and ServiceManager
	sys := &System{}
	sm := NewServiceManager()
	sm.SetSandboxResolver(func(p string) (string, error) {
		return filepath.Join(tmpDir, p), nil
	})
	sys.Services = sm
	sys.screenWidth = 640
	sys.screenHeight = 480

	replyChan := make(chan string, 1)
	m := newFileDialogModal(sys, replyChan)

	// Find subdir index
	subdirIdx := -1
	for i, e := range m.entries {
		if e.name == "subdir" {
			subdirIdx = i
			break
		}
	}

	// Calculate mouse position for item
	winW, winH := sys.screenWidth, sys.screenHeight
	dw, dh := int32(400), int32(300)
	dx := (winW - dw) / 2
	dy := (winH - dh) / 2
	lbX, lbY := dx+20, dy+40
	itemH := int32(20)

	mouseY := lbY + int32(subdirIdx)*itemH + 5
	mouseX := lbX + 5

	// First click
	m.Update(&InputEvent{Type: InputMouseDown, MouseX: mouseX, MouseY: mouseY})
	if m.path != "." {
		t.Error("Should not have navigated yet")
	}
	if m.selected != subdirIdx {
		t.Errorf("Expected selected %d, got %d", subdirIdx, m.selected)
	}

	// Second click
	m.Update(&InputEvent{Type: InputMouseDown, MouseX: mouseX, MouseY: mouseY})
	if m.path != "subdir" {
		t.Error("Should have navigated after double click")
	}
}
