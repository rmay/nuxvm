package system

import (
	"encoding/binary"
	"strings"
	"github.com/rmay/nuxvm/pkg/vm"
)

// RenderFramebuffer returns a string representation of the framebuffer for terminal display.
func RenderFramebuffer(memory []byte) string {
	const (
		lit  = "██"
		dark = "  "
		rule = "+--------------------------------------------------------------------------------------------------------------------------------+\n"
	)
	var sb strings.Builder
	sb.WriteString(rule)
	for y := 0; y < int(vm.FrameHeight); y++ {
		sb.WriteString("|")
		for x := 0; x < int(vm.FrameWidth); x++ {
			offset := (y*int(vm.FrameWidth) + x) * 4
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
