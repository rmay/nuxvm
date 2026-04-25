package system

import (
	"fmt"
	"os"
	"sort"
	"sync"
)

// IPC Message Types and Service Definitions
// These are the core OS services that run as goroutines, communicating
// with the VM via channels.

// ============= Window Manager =============

type WindowID int32

type WindowMsg struct {
	Command string // "create", "close", "move", "draw_rect", "draw_text", "set_pixel", "focus", "get_size"
	WinID   WindowID
	Data    map[string]interface{}
}

type WindowReply struct {
	Result  int32
	Data    map[string]interface{}
	Success bool
}

type Window struct {
	ID       WindowID
	Name     string
	X, Y     int32
	Width    int32
	Height   int32
	Visible  bool
	ZOrder   int // higher = on top
	ScrollY  int32
	FrameBuf []byte
}

// ============= Input Manager =============

type InputEventType uint32

const (
	InputKeyDown InputEventType = iota
	InputKeyUp
	InputMouseMove
	InputMouseDown
	InputMouseUp
	InputResize
)

type InputEvent struct {
	Type      InputEventType
	KeyCode   int32 // for key events
	MouseX    int32 // for mouse events
	MouseY    int32
	MouseBtn  uint32
	Timestamp int64
	WinID     WindowID // for resize events
	ResizeW   int32    // for resize events
	ResizeH   int32
}

type InputMsg struct {
	Command string // "poll_event", "register_handler"
	Data    map[string]interface{}
}

type InputReply struct {
	Event   *InputEvent
	Success bool
}

// ============= Sound Server =============

type SoundMsg struct {
	Command string // "play_sound", "stop_sound"
	SoundID int32
	Data    map[string]interface{}
}

type SoundReply struct {
	Success bool
	Error   string
}

// ============= File System Manager =============

type FileOp string

const (
	FileOpOpen   FileOp = "open"
	FileOpClose  FileOp = "close"
	FileOpRead   FileOp = "read"
	FileOpWrite  FileOp = "write"
	FileOpSeek   FileOp = "seek"
	FileOpStat   FileOp = "stat"
	FileOpDelete FileOp = "delete"
)

type FileMsg struct {
	Op       FileOp
	Path     string
	Handle   int32
	Offset   int64
	Data     []byte
	Flags    uint32 // e.g., append mode
	Metadata map[string]interface{}
}

type FileReply struct {
	Success bool
	Handle  int32
	Data    []byte
	Error   string
	Info    map[string]interface{}
}

// ============= Layout System =============

type Pane struct {
	WinID  WindowID
	X, Y   int32
	W, H   int32
}

// ============= Service Manager =============

type ServiceManager struct {
	windowMu sync.RWMutex // protects windows map and activeWinID

	// Window management
	windowChan chan WindowMsg
	windowReply chan WindowReply
	windows    map[WindowID]*Window
	nextWinID  WindowID
	activeWinID WindowID

	// Layout management
	panes []Pane // list of visible window panes (typically 1 or 2)

	// Input management
	inputChan chan InputMsg
	inputReply chan InputReply
	inputQueue chan *InputEvent // buffered channel, lock-free

	// Sound management
	soundChan chan SoundMsg
	soundReply chan SoundReply

	// File system management
	fileChan chan FileMsg
	fileReply chan FileReply
	openFiles map[int32]*OSFile
	nextFileHandle int32

	// Sandbox enforcement (file device)
	sandboxResolver func(string) (string, error)
}

type OSFile struct {
	Handle int32
	Path   string
	// Will add actual file handle when we implement
}

func NewServiceManager() *ServiceManager {
	sm := &ServiceManager{
		windowChan: make(chan WindowMsg, 16),
		windowReply: make(chan WindowReply, 16),
		windows: make(map[WindowID]*Window),
		nextWinID: 1,
		activeWinID: 1,

		inputChan: make(chan InputMsg, 16),
		inputReply: make(chan InputReply, 16),
		inputQueue: make(chan *InputEvent, 64), // lock-free event queue

		soundChan: make(chan SoundMsg, 16),
		soundReply: make(chan SoundReply, 16),

		fileChan: make(chan FileMsg, 16),
		fileReply: make(chan FileReply, 16),
		openFiles: make(map[int32]*OSFile),
		nextFileHandle: 1,

		panes: make([]Pane, 0),
	}
	return sm
}

// ============= Service Channels =============

// ============= Thread-Safe Accessors =============

func (sm *ServiceManager) GetActiveWindow() *Window {
	sm.windowMu.RLock()
	defer sm.windowMu.RUnlock()
	return sm.windows[sm.activeWinID]
}

func (sm *ServiceManager) GetActiveWindowFramebuf() []byte {
	sm.windowMu.RLock()
	defer sm.windowMu.RUnlock()
	if win := sm.windows[sm.activeWinID]; win != nil {
		return win.FrameBuf
	}
	return nil
}

func (sm *ServiceManager) ActiveWindowName() string {
	sm.windowMu.RLock()
	defer sm.windowMu.RUnlock()
	if win := sm.windows[sm.activeWinID]; win != nil {
		return win.Name
	}
	return ""
}

func (sm *ServiceManager) ActiveWindowScrollY() int32 {
	sm.windowMu.RLock()
	defer sm.windowMu.RUnlock()
	if win := sm.windows[sm.activeWinID]; win != nil {
		return win.ScrollY
	}
	return 0
}

func (sm *ServiceManager) SetActiveWindowScrollY(y int32) {
	sm.windowMu.Lock()
	defer sm.windowMu.Unlock()
	if win := sm.windows[sm.activeWinID]; win != nil {
		win.ScrollY = y
	}
}

func (sm *ServiceManager) ListWindowsSorted() []*Window {
	sm.windowMu.RLock()
	defer sm.windowMu.RUnlock()
	windows := make([]*Window, 0, len(sm.windows))
	for _, win := range sm.windows {
		if win.Visible {
			windows = append(windows, win)
		}
	}
	// Sort by Z-order (simple bubble sort, assuming small window count)
	for i := 0; i < len(windows); i++ {
		for j := i + 1; j < len(windows); j++ {
			if windows[j].ZOrder < windows[i].ZOrder {
				windows[i], windows[j] = windows[j], windows[i]
			}
		}
	}
	return windows
}

func (sm *ServiceManager) ResizeActiveWindow(w, h int32) {
	sm.windowMu.Lock()
	defer sm.windowMu.Unlock()
	if win := sm.windows[sm.activeWinID]; win != nil {
		win.Width = w
		win.Height = h
		win.FrameBuf = make([]byte, w*h*4)
	}
}

// GetWindowChannel returns the window management request channel
func (sm *ServiceManager) GetWindowChannel() chan WindowMsg {
	return sm.windowChan
}

// GetInputChannel returns the input management request channel
func (sm *ServiceManager) GetInputChannel() chan InputMsg {
	return sm.inputChan
}

// GetSoundChannel returns the sound management request channel
func (sm *ServiceManager) GetSoundChannel() chan SoundMsg {
	return sm.soundChan
}

// GetFileChannel returns the file system management request channel
func (sm *ServiceManager) GetFileChannel() chan FileMsg {
	return sm.fileChan
}

// ============= Window Service Operations =============

func (sm *ServiceManager) CreateWindow(name string, width, height int32) (WindowID, error) {
	msg := WindowMsg{
		Command: "create",
		Data: map[string]interface{}{
			"name": name,
			"width": width,
			"height": height,
		},
	}
	sm.windowChan <- msg
	reply := <-sm.windowReply
	if reply.Success {
		return WindowID(reply.Result), nil
	}
	return 0, nil
}

func (sm *ServiceManager) CloseWindow(winID WindowID) error {
	msg := WindowMsg{
		Command: "close",
		WinID: winID,
	}
	sm.windowChan <- msg
	<-sm.windowReply
	return nil
}

func (sm *ServiceManager) MoveWindow(winID WindowID, x, y int32) error {
	msg := WindowMsg{
		Command: "move",
		WinID: winID,
		Data: map[string]interface{}{
			"x": x,
			"y": y,
		},
	}
	sm.windowChan <- msg
	<-sm.windowReply
	return nil
}

func (sm *ServiceManager) GetActiveWindowID() WindowID {
	sm.windowMu.RLock()
	defer sm.windowMu.RUnlock()
	return sm.activeWinID
}

// ============= Input Service Operations =============

func (sm *ServiceManager) PollEvent() *InputEvent {
	select {
	case evt := <-sm.inputQueue:
		return evt
	default:
		return nil
	}
}


// ============= Sound Service Operations =============

func (sm *ServiceManager) PlaySound(soundID int32) error {
	msg := SoundMsg{
		Command: "play_sound",
		SoundID: soundID,
	}
	sm.soundChan <- msg
	<-sm.soundReply
	return nil
}

// ============= File Service Operations =============

func (sm *ServiceManager) OpenFile(path string) (int32, error) {
	msg := FileMsg{
		Op: FileOpOpen,
		Path: path,
	}
	sm.fileChan <- msg
	reply := <-sm.fileReply
	if reply.Success {
		return reply.Handle, nil
	}
	return -1, nil
}

func (sm *ServiceManager) CloseFile(handle int32) error {
	msg := FileMsg{
		Op: FileOpClose,
		Handle: handle,
	}
	sm.fileChan <- msg
	<-sm.fileReply
	return nil
}

// ============= Input Integration =============

// QueueKeyDown queues a key down event (non-blocking)
func (sm *ServiceManager) QueueKeyDown(key int32) {
	event := &InputEvent{
		Type:      InputKeyDown,
		KeyCode:   key,
		Timestamp: 0,
	}
	// Direct queue, non-blocking
	select {
	case sm.inputQueue <- event:
	default:
		// Queue full, drop event
	}
}

// QueueKeyUp queues a key up event (non-blocking)
func (sm *ServiceManager) QueueKeyUp(key int32) {
	event := &InputEvent{
		Type:      InputKeyUp,
		KeyCode:   key,
		Timestamp: 0,
	}
	select {
	case sm.inputQueue <- event:
	default:
	}
}

// QueueMouseMove queues a mouse move event (non-blocking)
func (sm *ServiceManager) QueueMouseMove(x, y int32) {
	event := &InputEvent{
		Type:      InputMouseMove,
		MouseX:    x,
		MouseY:    y,
		Timestamp: 0,
	}
	select {
	case sm.inputQueue <- event:
	default:
	}
}

// QueueMouseButton queues a mouse button event (non-blocking)
func (sm *ServiceManager) QueueMouseButton(x, y int32, btn uint32, isDown bool) {
	typ := InputMouseUp
	if isDown {
		typ = InputMouseDown
	}
	event := &InputEvent{
		Type:      typ,
		MouseX:    x,
		MouseY:    y,
		MouseBtn:  btn,
		Timestamp: 0,
	}
	select {
	case sm.inputQueue <- event:
	default:
	}
}

// SetSandboxResolver sets the path resolver for sandbox-enforced file operations.
func (sm *ServiceManager) SetSandboxResolver(resolver func(string) (string, error)) {
	sm.sandboxResolver = resolver
}

// ListDirectory returns a sorted list of filenames in a directory, or an error
// if the path cannot be resolved within the sandbox.
func (sm *ServiceManager) ListDirectory(path string) ([]string, error) {
	if sm.sandboxResolver == nil {
		return nil, fmt.Errorf("sandbox resolver not configured")
	}
	resolved, err := sm.sandboxResolver(path)
	if err != nil {
		return nil, err
	}
	entries, err := os.ReadDir(resolved)
	if err != nil {
		return nil, err
	}
	names := make([]string, 0, len(entries))
	for _, entry := range entries {
		names = append(names, entry.Name())
	}
	return names, nil
}

// FocusWindow sets active window and bumps its ZOrder to front.
func (sm *ServiceManager) FocusWindow(id WindowID) {
	sm.windowMu.Lock()
	defer sm.windowMu.Unlock()
	if win := sm.windows[id]; win != nil {
		sm.activeWinID = id
		win.ZOrder = sm.maxZOrder() + 1
	}
}

// CycleWindows advances focus to the next window in ZOrder and returns its ID.
func (sm *ServiceManager) CycleWindows() WindowID {
	sm.windowMu.Lock()
	defer sm.windowMu.Unlock()
	if len(sm.windows) == 0 {
		return 0
	}
	wins := make([]*Window, 0, len(sm.windows))
	for _, w := range sm.windows {
		wins = append(wins, w)
	}
	sort.Slice(wins, func(i, j int) bool { return wins[i].ZOrder < wins[j].ZOrder })
	for i, w := range wins {
		if w.ID == sm.activeWinID {
			next := wins[(i+1)%len(wins)]
			sm.activeWinID = next.ID
			next.ZOrder = sm.maxZOrder() + 1
			return next.ID
		}
	}
	if len(wins) > 0 {
		sm.activeWinID = wins[0].ID
		return wins[0].ID
	}
	return 0
}

// DirectMoveWindow moves a window by ID without going through the channel goroutine.
// Used for high-frequency drag operations.
func (sm *ServiceManager) DirectMoveWindow(id WindowID, x, y int32) {
	sm.windowMu.Lock()
	defer sm.windowMu.Unlock()
	if win := sm.windows[id]; win != nil {
		win.X, win.Y = x, y
	}
}

// GetWindowByID returns the window with the given ID, or nil if not found.
func (sm *ServiceManager) GetWindowByID(id WindowID) *Window {
	sm.windowMu.RLock()
	defer sm.windowMu.RUnlock()
	return sm.windows[id]
}

// ============= Layout Management =============

// ListPanes returns a copy of the current pane list (must be called under lock).
func (sm *ServiceManager) ListPanes() []Pane {
	sm.windowMu.RLock()
	defer sm.windowMu.RUnlock()
	panes := make([]Pane, len(sm.panes))
	copy(panes, sm.panes)
	return panes
}

// LayoutSingle sets up a single full-screen pane for the given window ID.
// contentX, contentY, contentW, contentH describe the area available for windows (below menubar, etc).
func (sm *ServiceManager) LayoutSingle(winID WindowID, contentX, contentY, contentW, contentH int32) {
	sm.windowMu.Lock()
	defer sm.windowMu.Unlock()
	sm.panes = []Pane{{WinID: winID, X: contentX, Y: contentY, W: contentW, H: contentH}}
	sm.applyLayout()
}

// LayoutSplit splits the content area into two panes side-by-side (left/right) or top/bottom.
// vertical=true means left/right split, vertical=false means top/bottom.
func (sm *ServiceManager) LayoutSplit(leftID, rightID WindowID, contentX, contentY, contentW, contentH int32, vertical bool) {
	sm.windowMu.Lock()
	defer sm.windowMu.Unlock()
	var panes []Pane
	if vertical {
		// Left/right split
		half := contentW / 2
		panes = []Pane{
			{WinID: leftID, X: contentX, Y: contentY, W: half, H: contentH},
			{WinID: rightID, X: contentX + half, Y: contentY, W: contentW - half, H: contentH},
		}
	} else {
		// Top/bottom split
		half := contentH / 2
		panes = []Pane{
			{WinID: leftID, X: contentX, Y: contentY, W: contentW, H: half},
			{WinID: rightID, X: contentX, Y: contentY + half, W: contentW, H: contentH - half},
		}
	}
	sm.panes = panes
	sm.applyLayout()
}

// applyLayout updates each window in panes with its new position/size and emits resize events if needed.
// Must be called under windowMu lock.
func (sm *ServiceManager) applyLayout() {
	for _, pane := range sm.panes {
		if win := sm.windows[pane.WinID]; win != nil {
			oldW, oldH := win.Width, win.Height
			win.X, win.Y = pane.X, pane.Y
			win.Width, win.Height = pane.W, pane.H
			// Reallocate framebuffer if size changed
			if oldW != pane.W || oldH != pane.H {
				win.FrameBuf = make([]byte, pane.W*pane.H*4)
				// Emit resize event
				select {
				case sm.inputQueue <- &InputEvent{
					Type:    InputResize,
					WinID:   pane.WinID,
					ResizeW: pane.W,
					ResizeH: pane.H,
				}:
				default:
				}
			}
		}
	}
}

// maxZOrder returns the highest Z-order among all windows. Must be called under lock.
func (sm *ServiceManager) maxZOrder() int {
	max := 0
	for _, win := range sm.windows {
		if win.ZOrder > max {
			max = win.ZOrder
		}
	}
	return max
}
