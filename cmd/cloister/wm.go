package main

import (
	"github.com/hajimehoshi/ebiten/v2"
	"github.com/rmay/nuxvm/pkg/system"
)

// Window chrome geometry constants
const (
	WinCloseBtnX      = 10 // close button center X offset from window Left
	WinPrevBtnX       = 24 // previous-window button center X offset
	WinNextBtnX       = 38 // next-window button center X offset
	WinCloseBtnY      = 10 // chrome-button center Y offset from chrome Top
	WinCloseBtnR      = 5  // chrome-button hit-test radius
	WinCloseBtnSize   = 8  // chrome-button drawn size (square)
	WinScrollArrowH   = 15 // height of the up/down arrow buttons
	WinScrollLineStep = 16 // pixels per arrow click
)

type HitZone int

const (
	HitZoneNone HitZone = iota
	HitZoneTitleBar
	HitZoneCloseButton
	HitZonePrevButton
	HitZoneNextButton
	HitZoneContent
	HitZoneGrowBox
	HitZoneOSScrollUp
	HitZoneOSScrollDown
	HitZoneOSScrollTrack
	HitZoneOSScrollLeft
	HitZoneOSScrollRight
	HitZoneOSScrollTrackH
	HitZoneOSCorner
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
}

func NewWindowManager(sm *system.ServiceManager) *WindowManager {
	return &WindowManager{
		sm:     sm,
		images: make(map[system.WindowID]*ebiten.Image),
	}
}

// SyncImages synchronizes the image cache with current window state:
// - GC entries for closed windows
// - Allocate/resize images for new/resized windows
// - Upload dirty FrameBufs via WritePixels
func (wm *WindowManager) SyncImages(windows []*system.WindowRecord) {
	// Build set of current window IDs
	currentIDs := make(map[system.WindowID]bool)
	for _, win := range windows {
		currentIDs[win.ID] = true
	}

	// GC stale images for closed windows
	for id := range wm.images {
		if !currentIDs[id] {
			delete(wm.images, id)
		}
	}

	// Process each visible window
	for _, win := range windows {
		if !win.Visible {
			continue
		}

		// Check if image exists and size matches
		img := wm.images[win.ID]
		needsResize := img == nil || img.Bounds().Dx() != int(win.Port.PortRect.Width()) || img.Bounds().Dy() != int(win.Port.PortRect.Height())

		if needsResize {
			wm.images[win.ID] = ebiten.NewImage(int(win.Port.PortRect.Width()), int(win.Port.PortRect.Height()))
			win.Dirty = true
		}

		// Upload FrameBuf if dirty
		if win.Dirty {
			if img := wm.images[win.ID]; img != nil && len(win.FrameBuf) > 0 {
				img.WritePixels(win.FrameBuf)
				win.Dirty = false
			}
		}
	}
}

// MarkDirty marks a window's image as needing FrameBuf re-upload.
func (wm *WindowManager) MarkDirty(id system.WindowID) {
	if win := wm.sm.GetWindowByID(id); win != nil {
		win.Dirty = true
	}
}

// ContentImage returns the cached ebiten.Image for a window's content area.
func (wm *WindowManager) ContentImage(id system.WindowID) *ebiten.Image {
	return wm.images[id]
}

// HitTest determines which window zone (if any) contains the screen point (x, y).
// TopBarH is the global menu bar height (24px) that's already consumed.
// viewW, viewH are the logical viewport dimensions (host window).
// osW, osH are the logical OS dimensions.
// windows must be in ascending Z-order (back to front); hit test walks in reverse.
func (wm *WindowManager) HitTest(x, y, TopBarH, viewW, viewH, osW, osH int, windows []*system.WindowRecord, scrollX, scrollY float64) HitResult {
	// 1. Check Desktop/Master Scrollbars (Physical edges)
	sbSize := system.WinScrollbarSize
	hasV := osH > viewH
	hasH := osW > viewW

	// Corner Check (if both bars exist)
	if hasV && hasH {
		if x >= viewW-sbSize && y >= viewH-sbSize {
			return HitResult{Zone: HitZoneOSCorner}
		}
	}

	// Vertical Master Scrollbar
	if hasV {
		vbX := viewW - sbSize
		// If horizontal bar is missing, vertical goes all the way to bottom
		limitY := viewH
		if hasH {
			limitY = viewH - sbSize
		}

		if x >= vbX && y >= TopBarH && x < viewW && y < limitY {
			if y < TopBarH+WinScrollArrowH {
				return HitResult{Zone: HitZoneOSScrollUp}
			}
			if y >= limitY-WinScrollArrowH {
				return HitResult{Zone: HitZoneOSScrollDown}
			}
			return HitResult{Zone: HitZoneOSScrollTrack}
		}
	}

	// Horizontal Master Scrollbar
	if hasH {
		hbY := viewH - sbSize
		// If vertical bar is missing, horizontal goes all the way to right
		limitX := viewW
		if hasV {
			limitX = viewW - sbSize
		}

		if y >= hbY && y < viewH && x < limitX {
			if x < WinScrollArrowH {
				return HitResult{Zone: HitZoneOSScrollLeft}
			}
			if x >= limitX-WinScrollArrowH {
				return HitResult{Zone: HitZoneOSScrollRight}
			}
			return HitResult{Zone: HitZoneOSScrollTrackH}
		}
	}

	// 2. Adjust mouse for desktop scrolling
	osX := x + int(scrollX)
	osY := y + int(scrollY)

	// Iterate in reverse Z-order (highest ZOrder = Topmost = first to hit)
	for i := len(windows) - 1; i >= 0; i-- {
		win := windows[i]
		if !win.Visible {
			continue
		}

		// Check if (osX, osY) is inside the structural region (full window frame)
		if osX < int(win.StrucRgn.Left) || osX >= int(win.StrucRgn.Right) || osY < int(win.StrucRgn.Top) || osY >= int(win.StrucRgn.Bottom) {
			continue
		}

		// We hit this window. Determine which zone.
		localX := osX - int(win.StrucRgn.Left)
		localY := osY - int(win.StrucRgn.Top)

		// Virtual Chrome check (Title bar and buttons drawn by Lux)
		if osY < int(win.StrucRgn.Top)+24 {
			// Close button is at x=8, y=4, size=12x12
			if localX >= 8 && localX <= 20 && localY >= 4 && localY <= 16 {
				return HitResult{WinID: win.ID, Zone: HitZoneCloseButton, LocalX: localX, LocalY: localY}
			}
			// Rest of title bar (including menu bar) is draggable
			return HitResult{WinID: win.ID, Zone: HitZoneTitleBar, LocalX: localX, LocalY: localY}
		}

		// Content area or scrollbars (now handled by Lux app)
		contentX := osX - int(win.ContRgn.Left)
		contentY := osY - int(win.ContRgn.Top)
		contentW := int(win.ContRgn.Width())
		contentH := int(win.ContRgn.Height())

		// Bottom-Right grow box (visual only in v1)
		if contentX >= contentW-system.WinScrollbarSize && contentY >= contentH-system.WinScrollbarSize {
			return HitResult{WinID: win.ID, Zone: HitZoneGrowBox}
		}

		return HitResult{WinID: win.ID, Zone: HitZoneContent, LocalX: contentX, LocalY: contentY}
	}

	return HitResult{Zone: HitZoneNone}
}
