package main

import (
	"github.com/hajimehoshi/ebiten/v2"
	"github.com/rmay/nuxvm/pkg/system"
)

// Window chrome geometry constants
const (
	WinChromeHeight = 20   // title bar height per window
	WinBorderWidth  = 1    // 1px border all sides
	WinCloseBtnX    = 10   // close button center X offset from window left
	WinCloseBtnY    = 10   // close button center Y offset from chrome top
	WinCloseBtnR    = 5    // close button hit-test radius
	WinCloseBtnSize = 8    // close button drawn size (square)
)

type HitZone int

const (
	HitZoneNone HitZone = iota
	HitZoneTitleBar
	HitZoneCloseButton
	HitZoneContent
)

type HitResult struct {
	WinID  system.WindowID
	Zone   HitZone
	LocalX int // window-local X (valid only when Zone == HitZoneContent)
	LocalY int // window-local Y
}

type WindowManager struct {
	sm     *system.ServiceManager
	images map[system.WindowID]*ebiten.Image
	dirty  map[system.WindowID]bool
}

func NewWindowManager(sm *system.ServiceManager) *WindowManager {
	return &WindowManager{
		sm:     sm,
		images: make(map[system.WindowID]*ebiten.Image),
		dirty:  make(map[system.WindowID]bool),
	}
}

// SyncImages synchronizes the image cache with current window state:
// - GC entries for closed windows
// - Allocate/resize images for new/resized windows
// - Upload dirty FrameBufs via WritePixels
func (wm *WindowManager) SyncImages(windows []*system.Window) {
	// Build set of current window IDs
	currentIDs := make(map[system.WindowID]bool)
	for _, win := range windows {
		currentIDs[win.ID] = true
	}

	// GC stale images for closed windows
	for id := range wm.images {
		if !currentIDs[id] {
			delete(wm.images, id)
			delete(wm.dirty, id)
		}
	}

	// Process each visible window
	for _, win := range windows {
		if !win.Visible {
			continue
		}

		// Check if image exists and size matches
		img := wm.images[win.ID]
		needsResize := img == nil || img.Bounds().Dx() != int(win.Width) || img.Bounds().Dy() != int(win.Height)

		if needsResize {
			wm.images[win.ID] = ebiten.NewImage(int(win.Width), int(win.Height))
			wm.dirty[win.ID] = true
		}

		// Upload FrameBuf if dirty
		if wm.dirty[win.ID] {
			if img := wm.images[win.ID]; img != nil && len(win.FrameBuf) > 0 {
				img.WritePixels(win.FrameBuf)
				wm.dirty[win.ID] = false
			}
		}
	}
}

// MarkDirty marks a window's image as needing FrameBuf re-upload.
func (wm *WindowManager) MarkDirty(id system.WindowID) {
	wm.dirty[id] = true
}

// ContentImage returns the cached ebiten.Image for a window's content area.
func (wm *WindowManager) ContentImage(id system.WindowID) *ebiten.Image {
	return wm.images[id]
}

// HitTest determines which window zone (if any) contains the screen point (x, y).
// topBarH is the global menu bar height (24px) that's already consumed.
// windows must be in ascending Z-order (back to front); hit test walks in reverse.
func (wm *WindowManager) HitTest(x, y, topBarH int, windows []*system.Window) HitResult {
	// Iterate in reverse Z-order (highest ZOrder = topmost = first to hit)
	for i := len(windows) - 1; i >= 0; i-- {
		win := windows[i]
		if !win.Visible {
			continue
		}

		// Compute full frame rectangle (includes chrome and border)
		frameX := int(win.X) - WinBorderWidth
		frameY := int(win.Y) + topBarH - WinChromeHeight - WinBorderWidth
		frameW := int(win.Width) + 2*WinBorderWidth
		frameH := int(win.Height) + WinChromeHeight + 2*WinBorderWidth

		// Check if (x, y) is inside the frame
		if x < frameX || x >= frameX+frameW || y < frameY || y >= frameY+frameH {
			continue
		}

		// We hit this window. Determine which zone.
		// Close button hit-test
		btnCenterX := int(win.X) + WinCloseBtnX
		btnCenterY := int(win.Y) + topBarH - WinChromeHeight + WinCloseBtnY
		dx := x - btnCenterX
		dy := y - btnCenterY
		if dx*dx+dy*dy <= WinCloseBtnR*WinCloseBtnR {
			return HitResult{WinID: win.ID, Zone: HitZoneCloseButton}
		}

		// Title bar check
		if y >= int(win.Y)+topBarH-WinChromeHeight && y < int(win.Y)+topBarH {
			return HitResult{WinID: win.ID, Zone: HitZoneTitleBar}
		}

		// Content area
		localX := x - int(win.X)
		localY := y - (int(win.Y) + topBarH + WinChromeHeight)
		return HitResult{WinID: win.ID, Zone: HitZoneContent, LocalX: localX, LocalY: localY}
	}

	return HitResult{Zone: HitZoneNone}
}
