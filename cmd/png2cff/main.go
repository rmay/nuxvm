package main

import (
	"fmt"
	"image"
	"image/color"
	_ "image/png"
	"os"
	"path/filepath"
	"strings"
)

func main() {
	if err := run(os.Args[1:]); err != nil {
		fmt.Fprintln(os.Stderr, "png2cff:", err)
		os.Exit(1)
	}
}

func run(args []string) error {
	if len(args) < 1 || len(args) > 2 {
		return fmt.Errorf("usage: png2cff <input.png> [output.cff]")
	}
	inPath := args[0]

	f, err := os.Open(inPath)
	if err != nil {
		return err
	}
	defer f.Close()

	img, _, err := image.Decode(f)
	if err != nil {
		return fmt.Errorf("decode %s: %w", inPath, err)
	}

	b := img.Bounds()
	w, h := b.Dx(), b.Dy()
	if w%8 != 0 || h%8 != 0 {
		return fmt.Errorf("source dimensions must be a multiple of 8, got %dx%d", w, h)
	}

	var outPath string
	if len(args) == 2 {
		outPath = args[1]
	} else {
		dir := filepath.Dir(inPath)
		base := filepath.Base(inPath)
		base = strings.TrimSuffix(base, filepath.Ext(base))
		outPath = filepath.Join(dir, fmt.Sprintf("%s%dx%d.cff", base, w, h))
	}

	tilesX := w / 8
	tilesY := h / 8
	tileCount := tilesX * tilesY
	buf := make([]byte, 0, tileCount*8)

	for ty := 0; ty < tilesY; ty++ {
		for tx := 0; tx < tilesX; tx++ {
			ox := b.Min.X + tx*8
			oy := b.Min.Y + ty*8
			for row := 0; row < 8; row++ {
				var rowByte byte
				for col := 0; col < 8; col++ {
					if pixelOn(img.At(ox+col, oy+row)) {
						rowByte |= 1 << (7 - col)
					}
				}
				buf = append(buf, rowByte)
			}
		}
	}

	if err := os.WriteFile(outPath, buf, 0644); err != nil {
		return err
	}

	fmt.Printf("wrote %s (%d tiles, %dx%d)\n", outPath, tileCount, w, h)
	return nil
}

// pixelOn reports whether a source pixel should become a 1-bit in the CFF
// output. The pixel is composited over white (so transparent areas read as
// background), then thresholded against Rec. 601 luma at 50%.
func pixelOn(c color.Color) bool {
	// RGBA returns alpha-premultiplied values in 0..0xFFFF.
	r, g, b, a := c.RGBA()
	inv := 0xFFFF - a
	rr := r + inv
	gg := g + inv
	bb := b + inv
	y := (299*rr + 587*gg + 114*bb) / 1000
	return y < 0x8000
}
