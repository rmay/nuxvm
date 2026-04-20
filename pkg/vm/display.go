package vm

import (
	"encoding/binary"
	"strings"
)

// FrameWidth and FrameHeight define the logical pixel dimensions of the framebuffer.
// 64 × 32 = 2048 pixels × 4 bytes each = 8192 bytes (VideoBufferSize).
const (
	FrameWidth  = 64
	FrameHeight = 32
)

// RenderFramebuffer returns the video framebuffer as a terminal-safe grid.
// Uses ASCII border characters and ANSI background colour for pixels so the
// output is correctly aligned on any terminal regardless of font or locale.
// Each 4-byte word is one pixel; nonzero = lit (yellow), zero = dark.
func RenderFramebuffer(memory []byte) string {
	const (
		lit  = "\033[43m  \033[0m" // yellow background, 2 spaces
		dark = "  "
	)
	rule := "+" + strings.Repeat("--", FrameWidth) + "+"
	var sb strings.Builder
	sb.WriteString(rule + "\n")
	for row := 0; row < FrameHeight; row++ {
		sb.WriteString("|")
		for col := 0; col < FrameWidth; col++ {
			offset := VideoFramebufferStart + (row*FrameWidth+col)*4
			if offset+4 <= len(memory) &&
				binary.BigEndian.Uint32(memory[offset:offset+4]) != 0 {
				sb.WriteString(lit)
			} else {
				sb.WriteString(dark)
			}
		}
		sb.WriteString("|\n")
	}
	sb.WriteString(rule)
	return sb.String()
}
