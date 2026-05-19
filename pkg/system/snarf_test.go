package system

import (
	"testing"
)

// TestSnarfRoundTrip writes to /sys/snarf and reads it back through a
// second open, mirroring how Lux apps use the file.
func TestSnarfRoundTrip(t *testing.T) {
	s := NewSystem()

	fd, err := s.vfs.Open(s, "/sys/snarf")
	if err != nil {
		t.Fatalf("open write: %v", err)
	}
	want := []byte("hello, snarf")
	n, err := s.vfs.Write(fd, want)
	if err != nil || n != len(want) {
		t.Fatalf("write: n=%d err=%v", n, err)
	}
	if err := s.vfs.Close(fd); err != nil {
		t.Fatalf("close write fd: %v", err)
	}

	fd2, err := s.vfs.Open(s, "/sys/snarf")
	if err != nil {
		t.Fatalf("open read: %v", err)
	}
	buf := make([]byte, 64)
	n, err = s.vfs.Read(fd2, buf)
	if err != nil {
		t.Fatalf("read: %v", err)
	}
	if string(buf[:n]) != string(want) {
		t.Fatalf("got %q want %q", buf[:n], want)
	}
	_ = s.vfs.Close(fd2)
}

// TestSnarfSharedAcrossOpens verifies a write through one fd is visible
// to a subsequent open — proving the buffer is system-wide, not per-fd.
func TestSnarfSharedAcrossOpens(t *testing.T) {
	s := NewSystem()

	a, _ := s.vfs.Open(s, "/sys/snarf")
	s.vfs.Write(a, []byte("first"))
	s.vfs.Close(a)

	b, _ := s.vfs.Open(s, "/sys/snarf")
	s.vfs.Write(b, []byte("second"))
	s.vfs.Close(b)

	c, _ := s.vfs.Open(s, "/sys/snarf")
	buf := make([]byte, 64)
	n, _ := s.vfs.Read(c, buf)
	s.vfs.Close(c)

	if string(buf[:n]) != "second" {
		t.Fatalf("got %q want %q", buf[:n], "second")
	}
}
