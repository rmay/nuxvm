package system

import (
	"encoding/binary"
	"fmt"
	"io"
	"os"
	"strings"
	"sync"

	"github.com/rmay/nuxvm/pkg/lux"
)

// VFSFile is the interface that all virtual files must implement.
type VFSFile interface {
	io.ReadWriteCloser
}

// VFS represents the Virtual File System state for a single NuxVM instance.
type VFS struct {
	mu     sync.RWMutex
	fds    map[int32]VFSFile
	nextFD int32

	// Proxies for inter-VM communication
	proxies       map[string]VFSFile
	parentProxies map[string]VFSFile
}

func NewVFS() *VFS {
	return &VFS{
		fds:           make(map[int32]VFSFile),
		nextFD:        100,
		proxies:       make(map[string]VFSFile),
		parentProxies: make(map[string]VFSFile),
	}
}

func (v *VFS) Open(s *System, path string) (int32, error) {
	fmt.Fprintf(os.Stderr, "VFS: Open called for %q\n", path)
	v.mu.Lock()
	defer v.mu.Unlock()

	var file VFSFile
	switch {
	case path == "/sys/draw":
		file = &drawFile{s: s}
	case path == "/sys/kbd":
		file = &kbdFile{s: s}
	case path == "/sys/mouse":
		file = &mouseFile{s: s}
	case path == "/sys/debug":
		file = &debugFile{}
	case strings.HasPrefix(path, "/sys/vm/"):
		parts := strings.Split(strings.TrimPrefix(path, "/sys/vm/"), "/")
		if parts[0] == "new" {
			file = &vmFile{s: s, kind: "new"}
		} else {
			var id int32
			fmt.Sscanf(parts[0], "%d", &id)
			kind := "ctl"
			if len(parts) > 1 {
				kind = parts[1]
			}
			file = &vmFile{s: s, id: id, kind: kind}
		}
	case strings.HasPrefix(path, "/dev/"):
		// Check for proxies registered by parent
		name := strings.TrimPrefix(path, "/dev/")
		if proxy, ok := v.proxies[name]; ok {
			fmt.Fprintf(os.Stderr, "VFS: Found proxy for /dev/%s\n", name)
			file = proxy
		} else {
			fmt.Fprintf(os.Stderr, "VFS: No proxy for /dev/%s, using inline fallback to /sys/\n", name)
			// Fallback: map /dev/* to /sys/* for standalone apps running as root
			switch name {
			case "draw":
				file = &drawFile{s: s}
			case "kbd":
				file = &kbdFile{s: s}
			case "mouse":
				file = &mouseFile{s: s}
			default:
				fmt.Fprintf(os.Stderr, "VFS: Fallback failed, device not found: %s\n", path)
				return -1, fmt.Errorf("device not found: %s", path)
			}
		}
	default:
		fmt.Fprintf(os.Stderr, "VFS: File not found: %s\n", path)
		return -1, fmt.Errorf("file not found: %s", path)
	}

	fd := v.nextFD
	v.nextFD++
	v.fds[fd] = file
	fmt.Fprintf(os.Stderr, "VFS: Open %q success -> fd %d\n", path, fd)
	return fd, nil
}

func (v *VFS) Close(fd int32) error {
	v.mu.Lock()
	defer v.mu.Unlock()

	file, ok := v.fds[fd]
	if !ok {
		return fmt.Errorf("invalid file descriptor: %d", fd)
	}

	delete(v.fds, fd)
	return file.Close()
}

func (v *VFS) Read(fd int32, p []byte) (int, error) {
	v.mu.RLock()
	file, ok := v.fds[fd]
	v.mu.RUnlock()

	if !ok {
		return -1, fmt.Errorf("invalid file descriptor: %d", fd)
	}

	return file.Read(p)
}

func (v *VFS) Write(fd int32, p []byte) (int, error) {
	v.mu.RLock()
	file, ok := v.fds[fd]
	v.mu.RUnlock()

	if !ok {
		return -1, fmt.Errorf("invalid file descriptor: %d", fd)
	}

	return file.Write(p)
}

// drawFile accepts a command stream for graphics operations.
type drawFile struct {
	s *System
}

func (f *drawFile) Read(p []byte) (n int, err error) {
	return 0, io.EOF
}

func (f *drawFile) Write(p []byte) (int, error) {
	// Debug print
	fmt.Fprintf(os.Stderr, "VFS: drawFile Write len=%d\n", len(p))
	i := 0
	for i < len(p) {
		cmd := p[i]
		i++
		switch cmd {
		case 0: // FillRect
			if i+12 > len(p) {
				return i - 1, io.ErrShortWrite
			}
			x := int32(int16(binary.LittleEndian.Uint16(p[i : i+2])))
			y := int32(int16(binary.LittleEndian.Uint16(p[i+2 : i+4])))
			w := int32(int16(binary.LittleEndian.Uint16(p[i+4 : i+6])))
			h := int32(int16(binary.LittleEndian.Uint16(p[i+6 : i+8])))
			color := binary.LittleEndian.Uint32(p[i+8 : i+12])
			fmt.Fprintf(os.Stderr, "  VFS: FillRect %d,%d %dx%d col=%X\n", x, y, w, h, color)
			f.s.fillRect(x, y, w, h, color)
			i += 12
		case 1: // DrawChar
			if i+9 > len(p) {
				return i - 1, io.ErrShortWrite
			}
			x := int32(int16(binary.LittleEndian.Uint16(p[i : i+2])))
			y := int32(int16(binary.LittleEndian.Uint16(p[i+2 : i+4])))
			char := p[i+4]
			color := binary.LittleEndian.Uint32(p[i+5 : i+9])
			fmt.Fprintf(os.Stderr, "  VFS: DrawChar %d,%d char=%c col=%X\n", x, y, char, color)
			f.s.drawCharVFS(x, y, char, color)
			i += 9
		default:
			return i - 1, fmt.Errorf("unknown draw command: %d", cmd)
		}
	}
	return len(p), nil
}


func (f *drawFile) Close() error { return nil }

type kbdFile struct{ s *System }

func (f *kbdFile) Write(p []byte) (n int, err error) { return 0, io.ErrShortWrite }
func (f *kbdFile) Close() error                    { return nil }

type mouseFile struct{ s *System }

func (f *mouseFile) Write(p []byte) (n int, err error) { return 0, io.ErrShortWrite }
func (f *mouseFile) Close() error                    { return nil }

// vmFile handles child VM management via /sys/vm/
type vmFile struct {
	s    *System
	id   int32
	kind string // "new", "ctl", "draw", etc.
	lastCreatedID int32 // For /sys/vm/new
}

func (f *vmFile) Read(p []byte) (n int, err error) {
	if f.kind == "new" {
		if len(p) < 4 {
			return 0, io.ErrShortBuffer
		}
		binary.LittleEndian.PutUint32(p[0:4], uint32(f.lastCreatedID))
		f.lastCreatedID = 0 // Reset after read
		return 4, nil
	}
	if f.kind == "draw" || f.kind == "mouse" || f.kind == "kbd" {
		child := f.s.childMachines[f.id]
		if child == nil {
			return 0, fmt.Errorf("vm %d not found", f.id)
		}
		return child.System.vfs.parentProxies[f.kind].Read(p)
	}
	return 0, io.EOF
}

func (f *vmFile) Write(p []byte) (n int, err error) {
	if f.kind == "new" {
		programPath := strings.TrimSpace(string(p))
		bytecode, err := lux.LoadProgram(programPath)
		if err != nil {
			return 0, err
		}

		id := f.s.nextMachineID
		f.s.nextMachineID++

		child := NewMachine(bytecode, 32*1024*1024)
		f.s.childMachines[id] = child

		// Create proxy pairs
		parentDraw, childDraw := newBufferFilePair()
		parentMouse, childMouse := newBufferFilePair()
		parentKbd, childKbd := newBufferFilePair()

		child.System.vfs.proxies["draw"] = childDraw
		child.System.vfs.proxies["mouse"] = childMouse
		child.System.vfs.proxies["kbd"] = childKbd

		child.System.vfs.parentProxies["draw"] = parentDraw
		child.System.vfs.parentProxies["mouse"] = parentMouse
		child.System.vfs.parentProxies["kbd"] = parentKbd

		f.lastCreatedID = id
		return len(p), nil
	}

	if f.kind == "draw" || f.kind == "mouse" || f.kind == "kbd" {
		child := f.s.childMachines[f.id]
		if child == nil {
			return 0, fmt.Errorf("vm %d not found", f.id)
		}
		return child.System.vfs.parentProxies[f.kind].Write(p)
	}

	return 0, io.ErrShortWrite
}

func (f *vmFile) Close() error { return nil }

// bufferFile implements VFSFile using a non-blocking byte buffer
type bufferFile struct {
	mu  sync.Mutex
	buf []byte
}

func (f *bufferFile) Read(p []byte) (int, error) {
	f.mu.Lock()
	defer f.mu.Unlock()
	if len(f.buf) == 0 {
		return 0, nil
	}
	n := copy(p, f.buf)
	f.buf = f.buf[n:]
	return n, nil
}

func (f *bufferFile) Write(p []byte) (int, error) {
	f.mu.Lock()
	defer f.mu.Unlock()
	f.buf = append(f.buf, p...)
	return len(p), nil
}

func (f *bufferFile) Close() error { return nil }

func newBufferFilePair() (VFSFile, VFSFile) {
	// A pair of buffers for bidirectional communication
	// Parent writes to B1, Child reads from B1
	// Child writes to B2, Parent reads from B2
	b1 := &bufferFile{}
	b2 := &bufferFile{}
	return &proxyPair{readBuf: b2, writeBuf: b1}, &proxyPair{readBuf: b1, writeBuf: b2}
}

type proxyPair struct {
	readBuf  *bufferFile
	writeBuf *bufferFile
}

func (p *proxyPair) Read(b []byte) (int, error)  { return p.readBuf.Read(b) }
func (p *proxyPair) Write(b []byte) (int, error) { return p.writeBuf.Write(b) }
func (p *proxyPair) Close() error               { return nil }

// debugFile is a simple mock file that prints to host stdout.
type debugFile struct{}

func (f *debugFile) Read(p []byte) (n int, err error) {
	return 0, io.EOF
}

func (f *debugFile) Write(p []byte) (n int, err error) {
	fmt.Print(string(p))
	return len(p), nil
}

func (f *debugFile) Close() error {
	return nil
}
