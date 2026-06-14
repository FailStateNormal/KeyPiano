"""Generate PNG toolbar icons (record/play/stop) from the original SVG designs.

vcpkg's Qt port ships no qsvg plugin, so QIcon(":/icons/*.svg") renders blank.
PNG is robust and these icons are trivial shapes. Rendered at 4x then downsized
for anti-aliasing. Output: 64x64 PNG (Qt downscales to the ~24px toolbar size).
"""
from PIL import Image, ImageDraw

SS = 4            # supersample factor
SIZE = 64         # final px
S = SIZE * SS     # working canvas

def scale(coords):
    # SVG viewBox is 0..32; map to working canvas.
    f = S / 32.0
    return [c * f for c in coords]

def save(img, name):
    img = img.resize((SIZE, SIZE), Image.LANCZOS)
    img.save(f"{name}.png")
    print(f"wrote {name}.png")

# ── record: red circle + light-red inner dot ─────────────────────────────────
img = Image.new("RGBA", (S, S), (0, 0, 0, 0))
d = ImageDraw.Draw(img)
def circle(d, cx, cy, r, fill):
    x0, y0, x1, y1 = scale([cx - r, cy - r, cx + r, cy + r])
    d.ellipse([x0, y0, x1, y1], fill=fill)
circle(d, 16, 16, 13, (0xDC, 0x35, 0x45, 255))
circle(d, 16, 16, 6, (0xFF, 0x80, 0x80, 255))
save(img, "record")

# ── play: green circle + cream triangle ──────────────────────────────────────
img = Image.new("RGBA", (S, S), (0, 0, 0, 0))
d = ImageDraw.Draw(img)
circle(d, 16, 16, 14, (0x1A, 0x7A, 0x3A, 255))
d.polygon(scale([11, 8, 26, 16, 11, 24]), fill=(0xEE, 0xFF, 0xEE, 255))
save(img, "play")

# ── stop: dark circle + light rounded square ─────────────────────────────────
img = Image.new("RGBA", (S, S), (0, 0, 0, 0))
d = ImageDraw.Draw(img)
circle(d, 16, 16, 14, (0x44, 0x44, 0x44, 255))
x0, y0, x1, y1 = scale([9, 9, 23, 23])
d.rounded_rectangle([x0, y0, x1, y1], radius=2 * (S / 32.0), fill=(0xCC, 0xCC, 0xCC, 255))
save(img, "stop")
