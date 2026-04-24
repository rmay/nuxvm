from PIL import Image, ImageDraw, ImageFont

# Pix Chicago is an 8px tall font usually. Let's render at size 8.
# An 8x8 grid of 128 characters = 16 columns x 8 rows.
# Width = 16 * 8 = 128px, Height = 8 * 8 = 64px.
font_size = 8
font = ImageFont.truetype("pixChicago.ttf", font_size)

cell_w = 8
cell_h = 8
img = Image.new("1", (cell_w * 16, cell_h * 8), 0)
draw = ImageDraw.Draw(img)

for i in range(128):
    x = (i % 16) * cell_w
    y = (i // 16) * cell_h
    # Pix Chicago might have an internal offset. 
    # Usually we draw text at (x, y) but we might need to adjust baseline.
    # Let's try drawing at (x, y).
    draw.text((x, y - 2), chr(i), font=font, fill=1)

img.save("pkg/system/chicago.png")
print("Saved chicago.png")
