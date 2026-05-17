package system

import (
	"encoding/binary"
	"fmt"
	"io"
	"os"
	"strings"
	"sync"

	"github.com/rmay/nuxvm/pkg/lux"
	"github.com/rmay/nuxvm/pkg/vm"
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

	// Mount table for per-process namespace
	mounts map[string]VFSFile
}

func NewVFS() *VFS {
	return &VFS{
		fds:    make(map[int32]VFSFile),
		nextFD: 100,
		mounts: make(map[string]VFSFile),
	}
}

func (v *VFS) Open(s *System, path string) (int32, error) {
	fmt.Fprintf(os.Stderr, "VFS: Open called for %q\n", path)
	
	file, err := v.openFile(s, path)
	if err != nil {
		return -1, err
	}

	v.mu.Lock()
	defer v.mu.Unlock()
	fd := v.nextFD
	v.nextFD++
	v.fds[fd] = file
	fmt.Fprintf(os.Stderr, "VFS: Open %q success -> fd %d\n", path, fd)
	return fd, nil
}

func (v *VFS) openFile(s *System, path string) (VFSFile, error) {
	v.mu.RLock()
	// 1. Check mount table first
	if file, ok := v.mounts[path]; ok {
		v.mu.RUnlock()
		return file, nil
	}
	v.mu.RUnlock()

	var file VFSFile
	switch {
	case path == "/sys/draw":
		file = &drawFile{s: s}
	case path == "/sys/kbd":
		file = &kbdFile{s: s}
	case path == "/sys/mouse":
		file = &mouseFile{s: s}
	case path == "/sys/audio":
		file = &audioFile{s: s}
	case path == "/sys/debug":
		file = &debugFile{}
	case path == "/sys/font/widths":
		file = &fontWidthsFile{s: s}
	case path == "/sys/chan/new":
		c1, c2 := newChannelPair()
		s.lastChan = c2
		file = c1
	case path == "/sys/chan/peer":
		if s.lastChan == nil {
			return nil, fmt.Errorf("no peer channel available")
		}
		file = s.lastChan
		s.lastChan = nil
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
	case strings.HasPrefix(path, "/sys/file/"):
		filePath := strings.TrimPrefix(path, "/sys/file/")
		if s.Services == nil {
			return nil, fmt.Errorf("services not available")
		}
		handle, err := s.Services.OpenFile(filePath)
		if err != nil {
			return nil, err
		}
		file = &hostFile{s: s, handle: handle}
	case strings.HasPrefix(path, "/dev/"):
		// Fallback: map /dev/* to /sys/* for standalone apps
		name := strings.TrimPrefix(path, "/dev/")
		return v.openFile(s, "/sys/"+name)
	default:
		fmt.Fprintf(os.Stderr, "VFS: File not found: %s\n", path)
		return nil, fmt.Errorf("file not found: %s", path)
	}

	return file, nil
}

func (v *VFS) Bind(s *System, fd int32, path string) error {
	v.mu.RLock()
	file, ok := v.fds[fd]
	v.mu.RUnlock()

	if !ok {
		return fmt.Errorf("invalid file descriptor: %d", fd)
	}

	return v.BindFile(s, file, path)
}

func (v *VFS) BindFile(s *System, file VFSFile, path string) error {
	// Handle binding into child namespaces: /sys/vm/[id]/ns/[path]
	if strings.HasPrefix(path, "/sys/vm/") {
		parts := strings.Split(strings.TrimPrefix(path, "/sys/vm/"), "/")
		if len(parts) >= 3 && parts[1] == "ns" {
			var id int32
			fmt.Sscanf(parts[0], "%d", &id)
			child := s.childMachines[id]
			if child == nil {
				return fmt.Errorf("vm %d not found", id)
			}
			childPath := "/" + strings.Join(parts[2:], "/")
			return child.System.vfs.BindFile(child.System, file, childPath)
		}
	}

	v.mu.Lock()
	v.mounts[path] = file
	v.mu.Unlock()
	return nil
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
			f.s.fillRect(x, y, w, h, color)
			i += 12
		case 1: // DrawChar
			if i+10 > len(p) {
				return i - 1, io.ErrShortWrite
			}
			x := int32(int16(binary.LittleEndian.Uint16(p[i : i+2])))
			y := int32(int16(binary.LittleEndian.Uint16(p[i+2 : i+4])))
			char := p[i+4]
			color := binary.LittleEndian.Uint32(p[i+5 : i+9])
			scale := p[i+9]
			if scale == 0 {
				scale = f.s.text.fontSize
			}
			f.s.drawCharVFS(x, y, char, color, scale)
			i += 10
		case 2: // DrawString
			if i+11 > len(p) {
				return i - 1, io.ErrShortWrite
			}
			x := int32(int16(binary.LittleEndian.Uint16(p[i : i+2])))
			y := int32(int16(binary.LittleEndian.Uint16(p[i+2 : i+4])))
			color := binary.LittleEndian.Uint32(p[i+4 : i+8])
			scale := p[i+8]
			if scale == 0 {
				scale = f.s.text.fontSize
			}
			strLen := int(binary.LittleEndian.Uint16(p[i+9 : i+11]))
			i += 11
			if i+strLen > len(p) {
				return i - 11, io.ErrShortWrite
			}
			cx := x
			sc := float64(scale)
			if scale >= 6 {
				div := 12.0
				if !f.s.text.useBasicFont {
					div = 16.0
				}
				sc = float64(scale) / div
			} else if sc <= 0 {
				sc = 1.0
			}

			face := getGoFace(scale)
			for j := 0; j < strLen; j++ {
				char := p[i+j]
				f.s.drawCharVFS(cx, y, char, color, scale)
				var charWidth int32
				if f.s.text.useBasicFont {
					charWidth = int32(float64(BasicFontWidth) * sc)
				} else {
					adv, ok := face.GlyphAdvance(rune(char))
					if ok {
						charWidth = int32(adv.Ceil())
					} else {
						charWidth = 4
					}
				}
				cx += charWidth
			}
			i += strLen
		case 3: // DrawRect
			if i+12 > len(p) {
				return i - 1, io.ErrShortWrite
			}
			x := int32(int16(binary.LittleEndian.Uint16(p[i : i+2])))
			y := int32(int16(binary.LittleEndian.Uint16(p[i+2 : i+4])))
			w := int32(int16(binary.LittleEndian.Uint16(p[i+4 : i+6])))
			h := int32(int16(binary.LittleEndian.Uint16(p[i+6 : i+8])))
			color := binary.LittleEndian.Uint32(p[i+8 : i+12])
			f.s.drawRect(x, y, w, h, color)
			i += 12
		case 4: // SetFontSize
			if i+1 > len(p) {
				return i - 1, io.ErrShortWrite
			}
			f.s.text.fontSize = p[i]
			i += 1
		case 5: // SetFont: 0 = Chicago CFF (default), 1 = 7x13 basicfont
			if i+1 > len(p) {
				return i - 1, io.ErrShortWrite
			}
			f.s.text.useBasicFont = p[i] != 0
			i += 1
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

// audioFile handles audio playback via /sys/audio
type audioFile struct{ s *System }

func (f *audioFile) Read(p []byte) (n int, err error) { return 0, io.EOF }
func (f *audioFile) Write(p []byte) (int, error) {
	if len(p) < 4 {
		return 0, io.ErrShortWrite
	}
	soundID := int32(binary.LittleEndian.Uint32(p[0:4]))
	if f.s.Services != nil {
		f.s.Services.PlaySound(soundID)
	}
	return 4, nil
}
func (f *audioFile) Close() error { return nil }

// hostFile handles standard file I/O via /sys/file/
type hostFile struct {
	s      *System
	handle int32
}

func (f *hostFile) Read(p []byte) (int, error) {
	if f.s.Services == nil {
		return 0, io.EOF
	}
	data, err := f.s.Services.ReadFile(f.handle, int32(len(p)))
	if err != nil {
		return 0, err
	}
	copy(p, data)
	return len(data), nil
}

func (f *hostFile) Write(p []byte) (int, error) {
	if f.s.Services == nil {
		return 0, io.ErrShortWrite
	}
	n, err := f.s.Services.WriteFile(f.handle, p)
	if err != nil {
		return 0, err
	}
	return int(n), nil
}

func (f *hostFile) Close() error {
	if f.s.Services != nil {
		return f.s.Services.CloseFile(f.handle)
	}
	return nil
}

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
	return 0, io.EOF
}

func (f *vmFile) Write(p []byte) (n int, err error) {
	if f.kind == "new" {
		programPath := strings.TrimSpace(string(p))
		bytecode, err := lux.LoadProgram(programPath, int32(vm.HeadlessBaseAddress))
		if err != nil {
			return 0, err
		}

		id := f.s.nextMachineID
		f.s.nextMachineID++

		child := NewMachine(bytecode, vm.HeadlessBaseAddress, 32*1024*1024)
		f.s.childMachines[id] = child

		f.lastCreatedID = id
		return len(p), nil
	}

	return 0, io.ErrShortWrite
}

func (f *vmFile) Close() error { return nil }

// channelFile implements a bidirectional, message-oriented VFS pipe.
type channelFile struct {
	readChan  chan []byte
	writeChan chan []byte
	closed    chan struct{}
	mu        sync.Mutex
}

func (f *channelFile) Read(p []byte) (int, error) {
	select {
	case msg := <-f.readChan:
		n := copy(p, msg)
		return n, nil
	case <-f.closed:
		return 0, io.EOF
	}
}

func (f *channelFile) Write(p []byte) (int, error) {
	// Preserve message boundaries by sending a copy of the buffer.
	msg := make([]byte, len(p))
	copy(msg, p)

	select {
	case f.writeChan <- msg:
		return len(p), nil
	case <-f.closed:
		return 0, io.ErrClosedPipe
	}
}

func (f *channelFile) Close() error {
	f.mu.Lock()
	defer f.mu.Unlock()
	select {
	case <-f.closed:
		// already closed
	default:
		close(f.closed)
	}
	return nil
}

func newChannelPair() (VFSFile, VFSFile) {
	c1 := make(chan []byte, 64)
	c2 := make(chan []byte, 64)
	closed := make(chan struct{})

	endpoint1 := &channelFile{
		readChan:  c1,
		writeChan: c2,
		closed:    closed,
	}
	endpoint2 := &channelFile{
		readChan:  c2,
		writeChan: c1,
		closed:    closed,
	}
	return endpoint1, endpoint2
}

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

// fontWidthsFile returns the character widths of the current Go font.
type fontWidthsFile struct {
	s *System
}

func (f *fontWidthsFile) Read(p []byte) (n int, err error) {
	face := getGoFace(f.s.text.fontSize)
	widths := make([]byte, 256)
	for i := 0; i < 256; i++ {
		adv, ok := face.GlyphAdvance(rune(i))
		if ok {
			widths[i] = uint8(adv.Ceil())
		} else {
			widths[i] = 0
		}
	}
	n = copy(p, widths)
	return n, io.EOF
}

func (f *fontWidthsFile) Write(p []byte) (n int, err error) { return 0, io.ErrShortWrite }
func (f *fontWidthsFile) Close() error                     { return nil }
