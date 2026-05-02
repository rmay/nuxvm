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

// Window chrome geometry constants (moved from wm.go)
const (
	TopBarHeight      = 24
	WinChromeHeight   = 20 // title bar height per window
	WinMenuBarHeight  = 18 // menu bar height (if present)
	WinBorderWidth    = 1  // 1px border all sides
	WindowInset       = 4  // standard inset for windows from edges
	WinScrollbarSize  = 15 // vertical and horizontal scrollbar thickness
)

type rect struct {
	Left, Top, Right, Bottom int32
}

func (r rect) Width() int32  { return r.Right - r.Left }
func (r rect) Height() int32 { return r.Bottom - r.Top }

type GrafPort struct {
	PortRect rect
	VisRgn   rect // for simplicity, using rect as region
	ClipRgn  rect
}

type WindowRecord struct {
	ID            WindowID
	Name          string
	Port          GrafPort
	StrucRgn      rect // full window (includes chrome)
	ContRgn       rect // content area
	UpdateRgn     rect // area needing redraw
	Visible       bool
	ZOrder        int
	ScrollX       int32
	ScrollY       int32
	ContentWidth  int32
	ContentHeight int32
	FrameBuf      []byte
	MenuTablePtr  uint32 // pointer to menu table in VM memory (0 = no menu)
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
	windows    map[WindowID]*WindowRecord
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

	screenWidth, screenHeight int32
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
		windows: make(map[WindowID]*WindowRecord),
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

func (sm *ServiceManager) GetActiveWindow() *WindowRecord {
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

// SetWindowMenu sets the menu table pointer for a window and adjusts its content rect.
// tablePtr=0 removes the menu.
func (sm *ServiceManager) SetWindowMenu(winID WindowID, tablePtr uint32) {
	sm.windowMu.Lock()
	defer sm.windowMu.Unlock()
	win := sm.windows[winID]
	if win == nil {
		return
	}

	win.MenuTablePtr = tablePtr

	// Adjust content rect if menu state changed
	// (V2: menus are in the chrome, so we don't shift content anymore)
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

func (sm *ServiceManager) ListWindowsSorted() []*WindowRecord {
	sm.windowMu.RLock()
	defer sm.windowMu.RUnlock()
	windows := make([]*WindowRecord, 0, len(sm.windows))
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
		if win.Port.PortRect.Width() == w && win.Port.PortRect.Height() == h {
			return
		}
		win.Port.PortRect = rect{0, 0, w, h}
		win.Port.ClipRgn = win.Port.PortRect
		// Regions are global; for now assume it stays where it is
		win.ContRgn = rect{win.ContRgn.Left, win.ContRgn.Top, win.ContRgn.Left + w, win.ContRgn.Top + h}
		win.StrucRgn = rect{
			win.ContRgn.Left - WinBorderWidth,
			win.ContRgn.Top - WinChromeHeight - WinBorderWidth,
			win.ContRgn.Right + WinBorderWidth,
			win.ContRgn.Bottom + WinBorderWidth,
		}
		win.FrameBuf = make([]byte, w*h*4)
	}
}

func (sm *ServiceManager) SetScreenSize(w, h int32) {
	sm.windowMu.Lock()
	sm.screenWidth = w
	sm.screenHeight = h
	sm.windowMu.Unlock()
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

// SetRenderTarget directly sets the active window ID without changing ZOrder
// or moving user focus. Used by host-side multi-VM scheduling to redirect
// screen/text writes to a specific VM's owned window during its tick. The
// scheduler must restore the previous value afterwards.
func (sm *ServiceManager) SetRenderTarget(id WindowID) {
	sm.windowMu.Lock()
	sm.activeWinID = id
	sm.windowMu.Unlock()
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
	wins := make([]*WindowRecord, 0, len(sm.windows))
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
		w := win.ContRgn.Width()
		h := win.ContRgn.Height()

		// Clamp window structure to screen boundaries.
		// Allow some overlap on Left/Right/Bottom, but keep title bar on screen.
		minY := int32(0)
		maxY := sm.screenHeight - TopBarHeight - WinChromeHeight
		if y < minY {
			y = minY
		}
		if y > maxY {
			y = maxY
		}

		// Keep at least some of the window on screen horizontally
		minX := -w + 20
		maxX := sm.screenWidth - 20
		if x < minX {
			x = minX
		}
		if x > maxX {
			x = maxX
		}

		win.ContRgn = rect{x + WinBorderWidth, y + WinChromeHeight + TopBarHeight + WinBorderWidth, x + w + WinBorderWidth, y + WinChromeHeight + TopBarHeight + h + WinBorderWidth}
		win.StrucRgn = rect{
			x,
			y + TopBarHeight,
			x + w + 2*WinBorderWidth,
			y + h + WinChromeHeight + TopBarHeight + 2*WinBorderWidth,
		}
	}
}

// GetWindowByID returns the window with the given ID, or nil if not found.
func (sm *ServiceManager) GetWindowByID(id WindowID) *WindowRecord {
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

// ClearPanes drops all panes. The Update loop's auto-recovery code will
// re-layout to LayoutSingle(activeWindow) on the next frame. Useful when a
// pane's window was just closed and the layout should fall back to whatever
// the new active window is.
func (sm *ServiceManager) ClearPanes() {
	sm.windowMu.Lock()
	defer sm.windowMu.Unlock()
	sm.panes = sm.panes[:0]
}

// LayoutSingle sets up a single full-screen pane for the given window ID.
// contentX, contentY, contentW, contentH describe the area available for windows (below menubar, etc).
func (sm *ServiceManager) LayoutSingle(winID WindowID, contentX, contentY, contentW, contentH int32) {
	sm.windowMu.Lock()
	defer sm.windowMu.Unlock()
	sm.panes = []Pane{{WinID: winID, X: contentX, Y: contentY, W: contentW, H: contentH}}
	sm.applyLayout()
}

// LayoutSplit splits the content area into two panes side-by-side (Left/Right) or Top/Bottom.
// vertical=true means Left/Right split, vertical=false means Top/Bottom.
func (sm *ServiceManager) LayoutSplit(LeftID, RightID WindowID, contentX, contentY, contentW, contentH int32, vertical bool) {
	sm.windowMu.Lock()
	defer sm.windowMu.Unlock()
	var panes []Pane
	if vertical {
		// Left/Right split
		half := contentW / 2
		panes = []Pane{
			{WinID: LeftID, X: contentX, Y: contentY, W: half, H: contentH},
			{WinID: RightID, X: contentX + half, Y: contentY, W: contentW - half, H: contentH},
		}
	} else {
		// Top/Bottom split
		half := contentH / 2
		panes = []Pane{
			{WinID: LeftID, X: contentX, Y: contentY, W: contentW, H: half},
			{WinID: RightID, X: contentX, Y: contentY + half, W: contentW, H: contentH - half},
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
			oldW := win.ContRgn.Width()
			oldH := win.ContRgn.Height()

			// pane.X, pane.Y are relative to the desktop area (below menubar)
			// and they represent the Top-Left of the window structure (including chrome).
			win.ContRgn = rect{
				pane.X + WinBorderWidth,
				pane.Y + TopBarHeight + WinChromeHeight + WinBorderWidth,
				pane.X + pane.W - WinBorderWidth,
				pane.Y + pane.H + TopBarHeight - WinBorderWidth,
			}
			win.StrucRgn = rect{
				pane.X,
				pane.Y + TopBarHeight,
				pane.X + pane.W,
				pane.Y + pane.H + TopBarHeight,
			}
			win.Port.PortRect = rect{0, 0, win.ContRgn.Width(), win.ContRgn.Height()}
			win.Port.ClipRgn = win.Port.PortRect

			newW := win.ContRgn.Width()
			newH := win.ContRgn.Height()

			// Reallocate framebuffer if size changed
			if oldW != newW || oldH != newH {
				win.FrameBuf = make([]byte, newW*newH*4)
				// Emit resize event
				select {
				case sm.inputQueue <- &InputEvent{
					Type:    InputResize,
					WinID:   pane.WinID,
					ResizeW: newW,
					ResizeH: newH,
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
