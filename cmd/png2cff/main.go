package main

import (
	"flag"
	"fmt"
	"image"
	"image/color"
	"image/png"
	"os"
)

func main() {
	inputPNG := flag.String("png", "", "Input PNG spritesheet (required)")
	outputCFF := flag.String("out", "cloister.cff", "Output .cff file")
	columns := flag.Int("cols", 16, "Number of columns in the spritesheet (default 16)")
	firstIdx := flag.Int("first", 0, "Codepoint of the first cell in the PNG (default 0)")
	cellW := flag.Int("cellw", 8, "Cell width in pixels (must be >=1)")
	cellH := flag.Int("cellh", 8, "Cell height in pixels (must be multiple of 8)")
	threshold := flag.Uint("thresh", 200, "Threshold 0-255 (darker = on; lower = more aggressive)")
	autoTrim := flag.Bool("trim", true, "Auto-trim right empty columns for proportional width")
	flag.Parse()

	if *inputPNG == "" {
		fmt.Println("Usage: go run cffpack.go -png font.png [options]")
		flag.PrintDefaults()
		os.Exit(1)
	}

	if *cellH%8 != 0 {
		fmt.Printf("Error: cell height must be a multiple of 8 (got %d)\n", *cellH)
		os.Exit(1)
	}

	// Open PNG
	file, err := os.Open(*inputPNG)
	if err != nil {
		fmt.Printf("Error opening PNG: %v\n", err)
		os.Exit(1)
	}
	defer file.Close()

	img, err := png.Decode(file)
	if err != nil {
		fmt.Printf("Error decoding PNG: %v\n", err)
		os.Exit(1)
	}

	bounds := img.Bounds()
	
	// Prepare CFF data
	widthTable := make([]byte, 256)
	var glyphData []byte

	// Calculate how many cells are in the PNG
	pngCols := bounds.Dx() / *cellW
	pngRows := bounds.Dy() / *cellH
	numCells := pngCols * pngRows
	if numCells > 256-*firstIdx {
		numCells = 256 - *firstIdx
	}

	for i := 0; i < numCells; i++ {
		codepoint := *firstIdx + i
		if codepoint >= 256 {
			break
		}

		row := i / *columns
		col := i % *columns

		x0 := col * *cellW
		y0 := row * *cellH

		// Extract glyph and convert to 1-bit + auto-trim width
		actualWidth := *cellW
		if *autoTrim {
			actualWidth = computeTrimmedWidth(img, x0, y0, *cellW, *cellH, uint8(*threshold))
		}

		widthTable[codepoint] = byte(actualWidth)

		// Generate tile data for this glyph
		glyphBytes := glyphToTiles(img, x0, y0, actualWidth, *cellH, uint8(*threshold))
		glyphData = append(glyphData, glyphBytes...)
	}

	// Write .cff file
	outFile, err := os.Create(*outputCFF)
	if err != nil {
		fmt.Printf("Error creating output file: %v\n", err)
		os.Exit(1)
	}
	defer outFile.Close()

	// Header: 256 width bytes
	_, _ = outFile.Write(widthTable)
	// Glyph data
	_, _ = outFile.Write(glyphData)

	fmt.Printf("✅ Successfully created %s\n", *outputCFF)
	fmt.Printf("   Grid: %dx%d cells (%d×%d px)\n", *columns, 256/(*columns), *cellW, *cellH)
	fmt.Printf("   Total glyph data: %d bytes\n", len(glyphData))
}

// computeTrimmedWidth returns the rightmost used column + 1 (0 if empty glyph)
func computeTrimmedWidth(img image.Image, x0, y0, cellW, cellH int, thresh uint8) int {
	maxX := -1
	for x := 0; x < cellW; x++ {
		for y := 0; y < cellH; y++ {
			if isOn(img, x0+x, y0+y, thresh) {
				if x > maxX {
					maxX = x
				}
				break // no need to check further rows in this column
			}
		}
	}
	if maxX == -1 {
		return 0
	}
	return maxX + 1
}

// glyphToTiles converts a sub-rectangle to CFF tile data (vertical tile order, MSB-first)
func glyphToTiles(img image.Image, x0, y0, width, height int, thresh uint8) []byte {
	if width == 0 || height == 0 {
		return nil
	}

	numHTiles := (width + 7) / 8
	numVTiles := height / 8

	var data []byte

	for tx := 0; tx < numHTiles; tx++ { // tile column
		for ty := 0; ty < numVTiles; ty++ { // tile row
			for rowInTile := 0; rowInTile < 8; rowInTile++ { // each of the 8 rows in this tile
				py := ty*8 + rowInTile
				b := byte(0)
				for bit := 0; bit < 8; bit++ { // bits left-to-right (MSB = left)
					px := tx*8 + bit
					if px < width && isOn(img, x0+px, y0+py, thresh) {
						b |= 1 << (7 - bit) // MSB-first
					}
				}
				data = append(data, b)
			}
		}
	}
	return data
}

// isOn returns true if the pixel is "on" (dark enough)
func isOn(img image.Image, x, y int, thresh uint8) bool {
	// Works with any image type
	c := color.GrayModel.Convert(img.At(x, y)).(color.Gray)
	return c.Y < thresh // darker = on
}
