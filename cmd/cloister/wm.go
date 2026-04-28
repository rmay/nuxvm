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
	HitZoneScrollUp
	HitZoneScrollDown
	HitZoneScrollTrack // vertical track (between arrows)
	HitZoneScrollLeft
	HitZoneScrollRight
	HitZoneScrollTrackH // horizontal track
	HitZoneGrowBox
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
		needsResize := img == nil || img.Bounds().Dx() != int(win.Port.PortRect.Width()) || img.Bounds().Dy() != int(win.Port.PortRect.Height())

		if needsResize {
			wm.images[win.ID] = ebiten.NewImage(int(win.Port.PortRect.Width()), int(win.Port.PortRect.Height()))
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
// TopBarH is the global menu bar height (24px) that's already consumed.
// windows must be in ascending Z-order (back to front); hit test walks in reverse.
func (wm *WindowManager) HitTest(x, y, TopBarH int, windows []*system.WindowRecord) HitResult {
	// Iterate in reverse Z-order (highest ZOrder = Topmost = first to hit)
	for i := len(windows) - 1; i >= 0; i-- {
		win := windows[i]
		if !win.Visible {
			continue
		}

		// Check if (x, y) is inside the structural region (full window frame)
		if x < int(win.StrucRgn.Left) || x >= int(win.StrucRgn.Right) || y < int(win.StrucRgn.Top) || y >= int(win.StrucRgn.Bottom) {
			continue
		}

		// We hit this window. Determine which zone.
		
		// Title bar check
		if y < int(win.ContRgn.Top) {
			// Chrome button hit-tests (close, prev, next) — all share Y and radius.
			btnCenterY := int(win.StrucRgn.Top) + WinCloseBtnY + system.WinBorderWidth
			dy := y - btnCenterY
			if dy*dy <= WinCloseBtnR*WinCloseBtnR {
				for _, b := range [...]struct {
					cx   int
					zone HitZone
				}{
					{int(win.StrucRgn.Left) + WinCloseBtnX + system.WinBorderWidth, HitZoneCloseButton},
					{int(win.StrucRgn.Left) + WinPrevBtnX + system.WinBorderWidth, HitZonePrevButton},
					{int(win.StrucRgn.Left) + WinNextBtnX + system.WinBorderWidth, HitZoneNextButton},
				} {
					dx := x - b.cx
					if dx*dx+dy*dy <= WinCloseBtnR*WinCloseBtnR {
						return HitResult{WinID: win.ID, Zone: b.zone}
					}
				}
			}
			return HitResult{WinID: win.ID, Zone: HitZoneTitleBar}
		}

		// Window-local coordinates inside the content area
		localX := x - int(win.ContRgn.Left)
		localY := y - int(win.ContRgn.Top)
		contentW := int(win.ContRgn.Width())
		contentH := int(win.ContRgn.Height())

		// Bottom-Right grow box (visual only in v1)
		if localX >= contentW-system.WinScrollbarSize && localY >= contentH-system.WinScrollbarSize {
			return HitResult{WinID: win.ID, Zone: HitZoneGrowBox}
		}

		// Vertical scrollbar (Right gutter, excluding the grow corner)
		if localX >= contentW-system.WinScrollbarSize {
			if localY < WinScrollArrowH {
				return HitResult{WinID: win.ID, Zone: HitZoneScrollUp}
			}
			if localY >= contentH-system.WinScrollbarSize-WinScrollArrowH {
				return HitResult{WinID: win.ID, Zone: HitZoneScrollDown}
			}
			return HitResult{WinID: win.ID, Zone: HitZoneScrollTrack}
		}

		// Horizontal scrollbar (Bottom gutter, excluding the grow corner)
		if localY >= contentH-system.WinScrollbarSize {
			if localX < WinScrollArrowH {
				return HitResult{WinID: win.ID, Zone: HitZoneScrollLeft}
			}
			if localX >= contentW-system.WinScrollbarSize-WinScrollArrowH {
				return HitResult{WinID: win.ID, Zone: HitZoneScrollRight}
			}
			return HitResult{WinID: win.ID, Zone: HitZoneScrollTrackH}
		}

		return HitResult{WinID: win.ID, Zone: HitZoneContent, LocalX: localX, LocalY: localY}
	}

	return HitResult{Zone: HitZoneNone}
}
