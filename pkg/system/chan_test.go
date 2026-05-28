package system

import (
	"testing"
)

func TestChannelVFSNonBlocking(t *testing.T) {
	s := NewSystem()
	vfs := s.vfs

	fd1, err := vfs.Open(s, "/sys/chan/new")
	if err != nil {
		t.Fatalf("Open /sys/chan/new failed: %v", err)
	}

	fd2, err := vfs.Open(s, "/sys/chan/peer")
	if err != nil {
		t.Fatalf("Open /sys/chan/peer failed: %v", err)
	}

	// Test non-blocking read from empty channel (fd1)
	buf := make([]byte, 10)
	n, err := vfs.Read(fd1, buf)
	if err != nil {
		t.Fatalf("Read from empty channel fd1 failed: %v", err)
	}
	if n != 0 {
		t.Errorf("Expected 0 bytes from empty channel fd1, got %d", n)
	}

	// Test non-blocking read from empty channel (fd2)
	n, err = vfs.Read(fd2, buf)
	if err != nil {
		t.Fatalf("Read from empty channel fd2 failed: %v", err)
	}
	if n != 0 {
		t.Errorf("Expected 0 bytes from empty channel fd2, got %d", n)
	}

	// Verify that writing to one end makes data available on the other
	testMsg := "hello"
	nw, err := vfs.Write(fd1, []byte(testMsg))
	if err != nil {
		t.Fatalf("Write to fd1 failed: %v", err)
	}
	if nw != len(testMsg) {
		t.Errorf("Expected %d bytes written, got %d", len(testMsg), nw)
	}

	n, err = vfs.Read(fd2, buf)
	if err != nil {
		t.Fatalf("Read from fd2 failed: %v", err)
	}
	if n != len(testMsg) {
		t.Errorf("Expected %d bytes read, got %d", len(testMsg), n)
	}
	if string(buf[:n]) != testMsg {
		t.Errorf("Expected msg %q, got %q", testMsg, string(buf[:n]))
	}
}

func TestChannelVFSFullNonBlocking(t *testing.T) {
	s := NewSystem()
	vfs := s.vfs

	fd1, err := vfs.Open(s, "/sys/chan/new")
	if err != nil {
		t.Fatalf("Open /sys/chan/new failed: %v", err)
	}

	// Channel capacity is 64. Fill it up.
	for i := 0; i < 64; i++ {
		n, err := vfs.Write(fd1, []byte("x"))
		if err != nil {
			t.Fatalf("Write %d failed: %v", i, err)
		}
		if n != 1 {
			t.Fatalf("Write %d only wrote %d bytes", i, n)
		}
	}

	// The 65th write should be non-blocking and return 0
	n, err := vfs.Write(fd1, []byte("too much"))
	if err != nil {
		t.Fatalf("65th write failed with error: %v", err)
	}
	if n != 0 {
		t.Errorf("Expected 0 bytes written to full channel, got %d", n)
	}
}
