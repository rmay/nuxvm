package system

import (
	"fmt"
)

// StartWindowManager starts the window management service goroutine.
// It handles window creation, destruction, movement, and drawing.
func (sm *ServiceManager) StartWindowManager() {
	go func() {
		for msg := range sm.windowChan {
			reply := sm.handleWindowMessage(msg)
			sm.windowReply <- reply
		}
	}()
}

func (sm *ServiceManager) handleWindowMessage(msg WindowMsg) WindowReply {
	sm.windowMu.Lock()
	defer sm.windowMu.Unlock()

	switch msg.Command {
	case "create":
		winID := sm.nextWinID
		sm.nextWinID++
		name := msg.Data["name"].(string)
		width := msg.Data["width"].(int32)
		height := msg.Data["height"].(int32)

		win := &WindowRecord{
			ID:      winID,
			Name:    name,
			Visible: true,
			ZOrder:  len(sm.windows),
			ScrollY: 0,
			ContRgn: rect{
				WinBorderWidth,
				TopBarHeight + WinChromeHeight + WinBorderWidth,
				width - WinBorderWidth,
				TopBarHeight + height - WinBorderWidth,
			},
			StrucRgn: rect{
				0,
				TopBarHeight,
				width,
				TopBarHeight + height,
			},
			FrameBuf: make([]byte, (width-2*WinBorderWidth)*(height-WinChromeHeight-2*WinBorderWidth)*4),
		}
		win.Port = GrafPort{
			PortRect: rect{0, 0, win.ContRgn.Width(), win.ContRgn.Height()},
			ClipRgn:  rect{0, 0, win.ContRgn.Width(), win.ContRgn.Height()},
		}
		sm.windows[winID] = win
		if sm.activeWinID == 0 {
			sm.activeWinID = winID
		}

		return WindowReply{
			Result:  int32(winID),
			Success: true,
		}

	case "close":
		winID := msg.WinID
		delete(sm.windows, winID)
		if sm.activeWinID == winID {
			sm.activeWinID = sm.pickBestActive()
		}
		return WindowReply{Success: true}

	case "move":
		win := sm.windows[msg.WinID]
		if win != nil {
			x := msg.Data["x"].(int32)
			y := msg.Data["y"].(int32)
			w := win.ContRgn.Width()
			h := win.ContRgn.Height()
			win.ContRgn = rect{
				x + WinBorderWidth,
				y + TopBarHeight + WinChromeHeight + WinBorderWidth,
				x + w + WinBorderWidth,
				y + TopBarHeight + WinChromeHeight + WinBorderWidth + h,
			}
			win.StrucRgn = rect{
				x,
				y + TopBarHeight,
				x + win.ContRgn.Width() + 2*WinBorderWidth,
				y + TopBarHeight + win.ContRgn.Height() + WinChromeHeight + 2*WinBorderWidth,
			}
		}
		return WindowReply{Success: true}

	case "set_scroll":
		win := sm.windows[msg.WinID]
		if win != nil {
			win.ScrollY = msg.Data["scrollY"].(int32)
		}
		return WindowReply{Success: true}

	case "focus":
		sm.activeWinID = msg.WinID
		if win := sm.windows[msg.WinID]; win != nil {
			win.ZOrder = sm.maxZOrder() + 1
		}
		return WindowReply{Success: true}

	case "raise":
		if win := sm.windows[msg.WinID]; win != nil {
			win.ZOrder = sm.maxZOrder() + 1
			sm.activeWinID = msg.WinID
		}
		return WindowReply{Success: true}

	case "get_size":
		win := sm.windows[msg.WinID]
		if win != nil {
			return WindowReply{
				Success: true,
				Data: map[string]interface{}{
					"width":  win.ContRgn.Width(),
					"height": win.ContRgn.Height(),
				},
			}
		}
		return WindowReply{Success: false}

	case "get_framebuffer":
		win := sm.windows[msg.WinID]
		if win != nil {
			return WindowReply{
				Success: true,
				Data: map[string]interface{}{
					"framebuffer": win.FrameBuf,
				},
			}
		}
		return WindowReply{Success: false}

	default:
		return WindowReply{Success: false}
	}
}

// StartInputManager starts the input management service goroutine.
// It collects and dispatches input events to applications.
func (sm *ServiceManager) StartInputManager() {
	go func() {
		for msg := range sm.inputChan {
			reply := sm.handleInputMessage(msg)
			sm.inputReply <- reply
		}
	}()
}

func (sm *ServiceManager) handleInputMessage(msg InputMsg) InputReply {
	switch msg.Command {
	case "poll_event":
		event := sm.PollEvent()
		return InputReply{
			Event:   event,
			Success: event != nil,
		}

	case "queue_event":
		event := msg.Data["event"].(*InputEvent)
		// Non-blocking send to input queue
		select {
		case sm.inputQueue <- event:
			return InputReply{Success: true}
		default:
			// Queue full, drop event
			return InputReply{Success: false}
		}

	default:
		return InputReply{Success: false}
	}
}

// StartSoundServer starts the sound management service goroutine.
// It handles audio playback requests from applications.
func (sm *ServiceManager) StartSoundServer() {
	go func() {
		for msg := range sm.soundChan {
			reply := sm.handleSoundMessage(msg)
			sm.soundReply <- reply
		}
	}()
}

func (sm *ServiceManager) handleSoundMessage(msg SoundMsg) SoundReply {
	switch msg.Command {
	case "play_sound":
		// TODO: Delegate to actual audio system
		return SoundReply{Success: true}

	case "stop_sound":
		// TODO: Delegate to actual audio system
		return SoundReply{Success: true}

	default:
		return SoundReply{Success: false, Error: "unknown command"}
	}
}

// StartFileSystemManager starts the file system management service goroutine.
// It handles file I/O operations within the sandbox.
func (sm *ServiceManager) StartFileSystemManager() {
	go func() {
		for msg := range sm.fileChan {
			reply := sm.handleFileMessage(msg)
			sm.fileReply <- reply
		}
	}()
}

func (sm *ServiceManager) handleFileMessage(msg FileMsg) FileReply {
	switch msg.Op {
	case FileOpOpen:
		handle := sm.nextFileHandle
		sm.nextFileHandle++
		sm.openFiles[handle] = &OSFile{
			Handle: handle,
			Path:   msg.Path,
		}
		return FileReply{
			Success: true,
			Handle:  handle,
		}

	case FileOpClose:
		delete(sm.openFiles, msg.Handle)
		return FileReply{Success: true}

	case FileOpRead:
		file, ok := sm.openFiles[msg.Handle]
		if !ok {
			return FileReply{Success: false, Error: "invalid file handle"}
		}
		// TODO: Implement actual file reading
		_ = file
		return FileReply{Success: true}

	case FileOpWrite:
		file, ok := sm.openFiles[msg.Handle]
		if !ok {
			return FileReply{Success: false, Error: "invalid file handle"}
		}
		// TODO: Implement actual file writing
		_ = file
		return FileReply{Success: true}

	case FileOpSeek:
		file, ok := sm.openFiles[msg.Handle]
		if !ok {
			return FileReply{Success: false, Error: "invalid file handle"}
		}
		// TODO: Implement actual file seeking
		_ = file
		return FileReply{Success: true}

	case FileOpStat:
		// TODO: Implement actual file stat
		return FileReply{Success: true}

	case FileOpDelete:
		// TODO: Implement actual file deletion
		return FileReply{Success: true}

	default:
		return FileReply{Success: false, Error: fmt.Sprintf("unknown operation: %v", msg.Op)}
	}
}

// StartAllServices starts all OS service goroutines.
func (sm *ServiceManager) StartAllServices() {
	sm.StartWindowManager()
	sm.StartInputManager()
	sm.StartSoundServer()
	sm.StartFileSystemManager()
}

// Helper methods for window management (called while lock is already held)

// pickBestActive selects the window with the highest ZOrder to be the new active window.
// Returns 0 if there are no windows.
func (sm *ServiceManager) pickBestActive() WindowID {
	if len(sm.windows) == 0 {
		return 0
	}
	var best *WindowRecord
	for _, w := range sm.windows {
		if best == nil || w.ZOrder > best.ZOrder {
			best = w
		}
	}
	if best == nil {
		return 0
	}
	return best.ID
}
