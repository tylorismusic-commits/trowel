#!/usr/bin/env python3
"""Generate a subtle seamless brushed-metal tile for the UI."""
from PIL import Image, ImageFilter
import math
import random

W = H = 256
im = Image.new("L", (W, H))
px = im.load()
random.seed(11)
phases = [random.uniform(0.0, math.tau) for _ in range(6)]
for y in range(H):
    for x in range(W):
        v = 128.0
        v += 14.0 * math.sin(math.tau * 3.0 * y / H + phases[0])
        v += 7.0 * math.sin(math.tau * 7.0 * y / H + phases[1])
        v += 4.0 * math.sin(math.tau * 13.0 * y / H + phases[2])
        v += 3.0 * math.sin(math.tau * 5.0 * x / W + phases[3])
        v += 2.0 * math.sin(math.tau * 9.0 * x / W + phases[4]) * math.sin(
            math.tau * 3.0 * y / H + phases[5])
        v += random.randint(-3, 3)
        px[x, y] = max(0, min(255, int(v)))
im = im.filter(ImageFilter.GaussianBlur(0.5))
im.convert("RGB").save("assets/metal.png")
print("wrote assets/metal.png")
