"""Generate icmg.ico (multi-resolution) from logo design.
Draws Icemage mark directly via PIL — no SVG renderer needed.
Sizes: 256, 128, 64, 48, 32, 16.
"""
from PIL import Image, ImageDraw
import math, os

OUT = os.path.join(os.path.dirname(__file__), "icmg.ico")

# Palette (linear gradient stops 7DD3FC -> 6366F1 -> 4338CA approximated as midpoint stroke).
STROKE = (99, 102, 241, 255)        # indigo-500 mid
STROKE_LIGHT = (125, 211, 252, 255) # cyan-300
NODE_FILL = (15, 23, 42, 255)       # slate-900
CORE_FILL = (99, 102, 241, 255)
CORE_HI = (224, 242, 254, 255)
TRANSPARENT = (0, 0, 0, 0)

def render(size):
    """Render at `size` x `size`. Coords scale from 32x32 reference."""
    s = size / 32.0
    img = Image.new("RGBA", (size, size), TRANSPARENT)
    d = ImageDraw.Draw(img)

    def sx(x): return int(round(x * s))
    cx, cy = sx(16), sx(16)
    r_outer = sx(14)
    sw = max(1, int(round(1.4 * s)))   # stroke width
    nr = max(1, int(round(1.3 * s)))   # node radius
    nrL = max(1, int(round(1.5 * s)))  # large node radius

    # Outer ring
    d.ellipse([cx - r_outer, cy - r_outer, cx + r_outer, cy + r_outer],
              outline=STROKE, width=max(1, int(round(1.5 * s))))

    # Graph spokes (M-shape: 6 nodes around hub)
    nodes = [
        (16, 7,  nrL),  # top
        (9,  12, nr),
        (23, 12, nr),
        (9,  20, nr),
        (23, 20, nr),
        (16, 25, nrL),  # bottom
    ]
    for nx, ny, _ in nodes:
        d.line([cx, cy, sx(nx), sx(ny)], fill=STROKE, width=sw)
    # M cross-bars
    if size >= 32:
        d.line([sx(9), sx(12), sx(16), sx(7), sx(23), sx(12)], fill=STROKE_LIGHT, width=max(1, sw - 1))
        d.line([sx(9), sx(20), sx(16), sx(25), sx(23), sx(20)], fill=STROKE_LIGHT, width=max(1, sw - 1))

    # Nodes
    for nx, ny, r in nodes:
        x, y = sx(nx), sx(ny)
        d.ellipse([x - r, y - r, x + r, y + r], fill=NODE_FILL,
                  outline=STROKE, width=max(1, int(round(1.0 * s))))

    # Core diamond
    if size >= 24:
        cd = max(2, int(round(3 * s)))
        d.polygon([(cx, cy - cd), (cx + cd, cy), (cx, cy + cd), (cx - cd, cy)],
                  fill=CORE_FILL)
        cd2 = max(1, int(round(1.5 * s)))
        d.polygon([(cx, cy - cd2), (cx + cd2, cy), (cx, cy + cd2), (cx - cd2, cy)],
                  fill=CORE_HI)
    else:
        # Tiny — single pixel core
        d.ellipse([cx - 1, cy - 1, cx + 1, cy + 1], fill=CORE_FILL)

    return img

sizes = [256, 128, 64, 48, 32, 16]
images = [render(s) for s in sizes]
images[0].save(OUT, format="ICO", sizes=[(s, s) for s in sizes], append_images=images[1:])
print(f"wrote {OUT} ({len(sizes)} sizes)")
