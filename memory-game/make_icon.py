from PIL import Image

# 10x10 1-bit style icon: black background, white "4 arrow" cross shape
# (matches the game's own on-device visual language)
img = Image.new("1", (10, 10), 0)  # 0 = black
px = img.load()

# "Simon says" style: 4 separate quadrant tiles with a cross-shaped gap,
# clearly recognizable as a memory/pattern game even at 10x10.
white_pixels = []
for x in range(10):
    for y in range(10):
        in_gap_col = x in (4, 5)
        in_gap_row = y in (4, 5)
        on_border = x == 0 or x == 9 or y == 0 or y == 9
        if not in_gap_col and not in_gap_row and not on_border:
            white_pixels.append((x, y))

for (x, y) in white_pixels:
    px[x, y] = 1

img.save("icon.png")
print("icon.png written", img.size, img.mode)
