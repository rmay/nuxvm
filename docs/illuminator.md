# Illuminator

Illuminator is a font exploration and inspection tool for Cloister Font Format (`.cff`) files. It provides a multi-pane interface for previewing, zooming, and selecting glyphs.

## Interface Layout

The application is divided into three functional areas:

### 1. Preview Pane (Top)
Displays a sample sentence ("The Quick Brown Fox Jumped Over The Lazy Dog.") using the currently loaded font. This allows you to see the font's appearance and kerning (if applicable) in a standard context.

### 2. Zoom Pane (Bottom Left)
Shows a highly magnified (8x) view of the currently selected glyph. This is useful for inspecting pixel-level details and alignment.

### 3. Picker Pane (Bottom Right)
A 16x16 grid of all 256 characters available in the font. The currently selected character is highlighted.

## Controls

### Mouse
- **Left Click (Picker)**: Select a glyph to view it in the Zoom pane.

### Keyboard
- **Arrow Keys**: Navigate the selection grid in the Picker pane.
- **Cmd+O**: Open a file dialog to load a different `.cff` font file.
- **Cmd+Q**: Quit the application.

## Formats
Illuminator specifically supports `.cff` files, which are binary formats containing glyph bitmaps (typically 12x12 or 16x16 pixels) and width tables.
