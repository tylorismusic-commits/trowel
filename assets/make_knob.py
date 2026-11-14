#!/usr/bin/env python3
"""Generate an original Neve-inspired knob body as a transparent PNG.

The image is deliberately pointerless: the UI draws the white pointer on top
so it can rotate while the lighting and knurling stay fixed.  Run this from the
project root:  python3 assets/make_knob.py
"""

from PIL import Image, ImageDraw, ImageFilter, ImageChops
import math
import os
import random

S = 1024
CX = CY = S // 2
BODY_R = 492
FACE_R = 392
CAP_R = 96


def radial_gradient(size, inner, outer, center=(0.35, 0.3), radius=1.0):
    img = Image.new("RGBA", (size, size), (0, 0, 0, 0))
    px = img.load()
    cx = size * center[0]
    cy = size * center[1]
    maxd = math.hypot(max(cx, size - cx), max(cy, size - cy)) * radius
    for y in range(size):
        for x in range(size):
            d = math.hypot(x - cx, y - cy) / maxd
            d = min(1.0, d)
            r = int(inner[0] + (outer[0] - inner[0]) * d)
            g = int(inner[1] + (outer[1] - inner[1]) * d)
            b = int(inner[2] + (outer[2] - inner[2]) * d)
            a = int(inner[3] + (outer[3] - inner[3]) * d)
            px[x, y] = (r, g, b, a)
    return img



def overlay(base, layer, mask):
    """Alpha-composite layer onto base, clipped by mask."""
    layer = layer.copy()
    alpha = layer.getchannel("A")
    layer.putalpha(ImageChops.multiply(alpha, mask))
    base.alpha_composite(layer)

def circle_mask(size, radius, center=None):
    m = Image.new("L", (size, size), 0)
    d = ImageDraw.Draw(m)
    c = center or (size // 2, size // 2)
    d.ellipse((c[0] - radius, c[1] - radius, c[0] + radius, c[1] + radius),
              fill=255)
    return m


def main():
    out_dir = os.path.dirname(os.path.abspath(__file__))
    img = Image.new("RGBA", (S, S), (0, 0, 0, 0))

    # --- outer bakelite body -------------------------------------------------
    body = radial_gradient(S, (72, 70, 66, 255), (7, 7, 8, 255), (0.34, 0.28))
    body_mask = circle_mask(S, BODY_R)
    body = body.copy()
    body.putalpha(body_mask)
    img.alpha_composite(body)

    # --- knurled rim ---------------------------------------------------------
    rim = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    d = ImageDraw.Draw(rim)
    ridges = 42
    for i in range(ridges):
        a0 = (i / ridges) * 2 * math.pi
        a1 = ((i + 0.45) / ridges) * 2 * math.pi
        r0, r1 = FACE_R + 18, BODY_R - 3
        p0 = (CX + math.cos(a0) * r0, CY + math.sin(a0) * r0)
        p1 = (CX + math.cos(a0) * r1, CY + math.sin(a0) * r1)
        p2 = (CX + math.cos(a1) * r1, CY + math.sin(a1) * r1)
        p3 = (CX + math.cos(a1) * r0, CY + math.sin(a1) * r0)
        shade = 58 if i % 2 == 0 else 16
        d.polygon([p0, p1, p2, p3], fill=(shade, shade, shade + 2, 255))
    rim = rim.filter(ImageFilter.GaussianBlur(0.8))
    rim_mask = Image.new("L", (S, S), 0)
    rm = ImageDraw.Draw(rim_mask)
    rm.ellipse((CX - BODY_R, CY - BODY_R, CX + BODY_R, CY + BODY_R), fill=255)
    rm.ellipse((CX - FACE_R - 18, CY - FACE_R - 18,
                CX + FACE_R + 18, CY + FACE_R + 18), fill=0)
    overlay(img, rim, rim_mask)

    # --- silver bevel ring ---------------------------------------------------
    bevel = radial_gradient(S, (210, 210, 205, 255), (48, 48, 50, 255),
                            (0.35, 0.25))
    bevel_mask = Image.new("L", (S, S), 0)
    bm = ImageDraw.Draw(bevel_mask)
    bm.ellipse((CX - FACE_R - 22, CY - FACE_R - 22,
                CX + FACE_R + 22, CY + FACE_R + 22), fill=255)
    bm.ellipse((CX - FACE_R + 4, CY - FACE_R + 4,
                CX + FACE_R - 4, CY + FACE_R - 4), fill=0)
    overlay(img, bevel, bevel_mask)

    # --- inner face ----------------------------------------------------------
    face = radial_gradient(S, (58, 56, 53, 255), (6, 6, 7, 255), (0.33, 0.25))
    face_mask = circle_mask(S, FACE_R)
    face = face.copy()
    face.putalpha(face_mask)
    img.alpha_composite(face)

    # subtle bakelite grain
    rnd = random.Random(7)
    grain = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    gp = grain.load()
    for y in range(0, S, 2):
        for x in range(0, S, 2):
            v = rnd.randint(0, 10)
            gp[x, y] = (v, v, v, 12)
    overlay(img, grain, face_mask)

    # specular highlight
    spec = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    sd = ImageDraw.Draw(spec)
    sd.ellipse((CX - 300, CY - 330, CX + 70, CY - 30), fill=(255, 255, 245, 30))
    spec = spec.filter(ImageFilter.GaussianBlur(46))
    overlay(img, spec, face_mask)

    # inner rim light
    rimlight = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    rl = ImageDraw.Draw(rimlight)
    rl.arc((CX - FACE_R + 2, CY - FACE_R + 2, CX + FACE_R - 2,
            CY + FACE_R - 2), 195, 330, fill=(220, 215, 200, 70), width=5)
    rimlight = rimlight.filter(ImageFilter.GaussianBlur(2))
    overlay(img, rimlight, face_mask)

    # --- centre cap ----------------------------------------------------------
    cap = radial_gradient(S, (235, 232, 222, 255), (48, 46, 44, 255),
                          (0.36, 0.28))
    cap_mask = circle_mask(S, CAP_R)
    cap = cap.copy()
    cap.putalpha(cap_mask)
    img.alpha_composite(cap)
    cd = ImageDraw.Draw(img)
    cd.ellipse((CX - CAP_R, CY - CAP_R, CX + CAP_R, CY + CAP_R),
               outline=(24, 24, 24, 180), width=5)
    cd.ellipse((CX - CAP_R + 9, CY - CAP_R + 9, CX + CAP_R - 9, CY + CAP_R - 9),
               outline=(255, 255, 248, 55), width=2)

    # center recess
    cd.ellipse((CX - 26, CY - 26, CX + 26, CY + 26), fill=(18, 18, 19, 255))
    cd.ellipse((CX - 26, CY - 26, CX + 26, CY + 26),
               outline=(160, 158, 148, 120), width=2)

    # --- outer edge highlight / shadow rim ----------------------------------
    edge = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    ed = ImageDraw.Draw(edge)
    ed.arc((CX - BODY_R + 2, CY - BODY_R + 2, CX + BODY_R - 2,
            CY + BODY_R - 2), 185, 335, fill=(230, 226, 210, 90), width=7)
    edge = edge.filter(ImageFilter.GaussianBlur(2))
    overlay(img, edge, body_mask)

    # downscale for the plugin
    small = img.resize((512, 512), Image.LANCZOS)
    small.save(os.path.join(out_dir, "knob.png"))
    img.save(os.path.join(out_dir, "knob-master.png"))

    # preview composite
    preview = Image.new("RGBA", (520, 200), (32, 48, 62, 255))
    for i, (size, ang) in enumerate([(160, -120), (100, 0), (64, 70)]):
        k = small.resize((size, size), Image.LANCZOS)
        x = 30 + i * 170
        y = 20
        preview.alpha_composite(k, (x, y))
        d = ImageDraw.Draw(preview)
        cx = x + size / 2
        cy = y + size / 2
        a = math.radians(ang)
        d.line((cx + math.cos(a) * size * 0.12,
                cy + math.sin(a) * size * 0.12,
                cx + math.cos(a) * size * 0.41,
                cy + math.sin(a) * size * 0.41),
               fill=(245, 238, 214, 255), width=max(2, size // 50))
    preview.convert("RGB").save(os.path.join(out_dir, "knob-preview.png"))
    print("wrote assets/knob.png, assets/knob-master.png, assets/knob-preview.png")


if __name__ == "__main__":
    main()
