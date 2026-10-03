"""Build a synthetic web root matching process_assets.py output, for headless testing."""
import json
import math
import os
import random
import sys

from PIL import Image, ImageDraw

root = sys.argv[1]
random.seed(7)


def numbered(i, w, h, alpha=False):
    hue = (i * 37) % 360
    img = Image.new("RGBA", (w, h), (0, 0, 0, 0 if alpha else 255))
    d = ImageDraw.Draw(img)
    c = tuple(int(127 + 127 * math.sin(math.radians(hue + k * 120))) for k in range(3))
    if alpha:
        d.ellipse([w * 0.1, h * 0.1, w * 0.9, h * 0.9], fill=c + (255,))
    else:
        d.rectangle([0, 0, w, h], fill=c + (255,))
        for k in range(0, w, max(8, w // 8)):
            d.line([k, 0, k, h], fill=(255, 255, 255, 255), width=2)
    d.rectangle([w * 0.3, h * 0.4, w * 0.7, h * 0.6], fill=(0, 0, 0, 255))
    # Orientation marker: white band along the TOP edge and a dot top-left, so screenshots show flips.
    d.rectangle([0, 0, w, h * 0.12], fill=(255, 255, 255, 255))
    d.ellipse([w * 0.05, h * 0.18, w * 0.25, h * 0.18 + w * 0.2], fill=(255, 255, 0, 255))
    d.text((w * 0.35, h * 0.45), str(i), fill=(255, 255, 255, 255))
    return img


for folder, alpha in (("images", False), ("transparentimages", True)):
    n = 40
    manifest = {}
    for sub, scale in (("", 1.0), ("halfres/", 0.5), ("quarterres/", 0.25)):
        os.makedirs(os.path.join(root, folder, sub), exist_ok=True)
    for i in range(1, n + 1):
        aspect = random.choice([0.67, 1.0, 1.5, 0.7, 1.78])
        w, h = (1200, int(1200 / aspect)) if aspect >= 1 else (int(1200 * aspect), 1200)
        img = numbered(i, w, h, alpha)
        for sub, scale in (("", 1.0), ("halfres/", 0.5), ("quarterres/", 0.25)):
            out = img.resize((max(1, int(w * scale)), max(1, int(h * scale))))
            out.save(os.path.join(root, folder, sub, f"{i}.webp"), "WEBP", quality=80)
        manifest[f"{i}.webp"] = f"original_{folder}_{i}.png"
    json.dump({"totalImages": n, "formats": ["full", "halfres", "quarterres"]},
              open(os.path.join(root, folder, "config.json"), "w"))
    json.dump(manifest, open(os.path.join(root, folder, "manifest.json"), "w"))

textures = ["Bricks003_1K-JPG", "OfficeCeiling001_1K-PNG", "PavingStones079_1K-PNG", "PavingStones127_1K-PNG",
            "Tiles058_1K-PNG", "Tiles084_1K-PNG", "Tiles086_1K-PNG", "WoodFloor071_1K-PNG"]
for t in textures:
    ext = "jpg" if t.lower().endswith("-jpg") else "png"
    d = os.path.join(root, "textures", t)
    os.makedirs(d, exist_ok=True)
    size = 512
    color = Image.new("RGB", (size, size))
    normal = Image.new("RGB", (size, size))
    cp, np_ = color.load(), normal.load()
    base = [(150, 70, 50), (200, 200, 190), (120, 120, 110), (90, 90, 95), (180, 180, 170), (60, 90, 120),
            (200, 170, 120), (140, 90, 50)][textures.index(t)]
    for y in range(size):
        for x in range(size):
            mortar = (y % 64 < 4) or ((x + (32 if (y // 64) % 2 else 0)) % 128 < 4)
            cp[x, y] = (200, 200, 200) if mortar else base
            nx = 128 + (60 if (x % 128) < 4 else 0)
            ny = 128 + (60 if (y % 64) < 4 else 0)
            np_[x, y] = (nx, ny, 255)
    color.save(os.path.join(d, f"{t}_Color.{ext}"))
    normal.save(os.path.join(d, f"{t}_NormalGL.{ext}"))
print("webroot ready")
