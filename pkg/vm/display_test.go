package vm

import (
	"strings"
	"testing"
)

func TestRenderFramebuffer(t *testing.T) {
	// Allocate memory enough to hold the framebuffer
	mem := make([]byte, VideoFramebufferEnd)

	// Set a pixel to non-zero
	// Pixel (0, 0)
	pixelOffset := VideoFramebufferStart
	mem[pixelOffset+3] = 1 // big-endian, last byte

	output := RenderFramebuffer(mem)

	// Check if output contains the border
	if !strings.Contains(output, "+") {
		t.Error("Expected output to contain '+' for borders")
	}
	if !strings.Contains(output, "|") {
		t.Error("Expected output to contain '|' for borders")
	}

	// Check if output contains the lit pixel ANSI code
	lit := "\033[43m  \033[0m"
	if !strings.Contains(output, lit) {
		t.Error("Expected output to contain lit pixel")
	}
}
