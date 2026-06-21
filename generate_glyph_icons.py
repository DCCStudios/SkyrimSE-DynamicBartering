"""Post-process AI-generated button/key glyph icons into clean transparent UI assets.

The image generator bakes a "transparency" checkerboard into the pixels (two light
greys ~235 and ~254) instead of producing a real alpha channel. This script:
  1. Flood-fills that checkerboard background inward from the image borders so only
     the connected background is removed (interior highlights are preserved).
  2. Extends the cut a few pixels into the light fringe to kill the halo ring.
  3. Crops to the glyph, adds a small transparent margin, and normalizes every glyph
     to a uniform height (keeping aspect) so they line up in the hint bar.
  4. Writes results to assets/icons/glyphs and copies them to the deployed PrismaUI
     icons folder.

Run: python generate_glyph_icons.py
"""
import os
import glob
import numpy as np
from PIL import Image

SRC = r"C:\Users\rober\.cursor\projects\e-Skyrim-Animation-SKSE\assets"
PLUGIN_DIR = os.path.dirname(os.path.abspath(__file__))
DST = os.path.join(PLUGIN_DIR, "assets", "icons", "glyphs")
PRISMA_DST = r"f:\Modlists\BottleRim2_0\mods\Silver Tongue - Dynamic Bartering\PrismaUI\views\DynamicBartering\icons\glyphs"

TARGET_HEIGHT = 128  # uniform glyph height (px); width keeps aspect ratio

GLYPHS = [
    "glyph_xbox_a", "glyph_xbox_b", "glyph_xbox_x", "glyph_xbox_lb", "glyph_xbox_rb",
    "glyph_ps_cross", "glyph_ps_circle", "glyph_ps_square", "glyph_ps_l1", "glyph_ps_r1",
    "glyph_dpad", "glyph_key_e", "glyph_key_r", "glyph_key_tab", "glyph_key_arrows",
]


def key_checkerboard(im):
    """Return RGBA image with the connected checkerboard background made transparent."""
    arr = np.asarray(im.convert("RGB")).astype(np.int16)
    r, g, b = arr[..., 0], arr[..., 1], arr[..., 2]
    L = np.maximum(np.maximum(r, g), b)
    mn = np.minimum(np.minimum(r, g), b)
    sat = L - mn

    # Background candidate: light and nearly grey (the checker is ~235 / ~254 grey)
    bg_cand = (L >= 210) & (sat <= 20)

    h, w = bg_cand.shape
    reached = np.zeros((h, w), bool)
    # Seed from any border pixel that is background-like
    reached[0, :] |= bg_cand[0, :]
    reached[-1, :] |= bg_cand[-1, :]
    reached[:, 0] |= bg_cand[:, 0]
    reached[:, -1] |= bg_cand[:, -1]

    # Iterative 4-connected dilation restricted to background candidates
    prev = -1
    while True:
        cur = int(reached.sum())
        if cur == prev:
            break
        prev = cur
        up = np.zeros_like(reached); up[:-1, :] = reached[1:, :]
        dn = np.zeros_like(reached); dn[1:, :] = reached[:-1, :]
        lf = np.zeros_like(reached); lf[:, :-1] = reached[:, 1:]
        rt = np.zeros_like(reached); rt[:, 1:] = reached[:, :-1]
        reached = (reached | up | dn | lf | rt) & bg_cand

    # Extend the cut into the light halo fringe adjacent to the background
    light_fringe = (L >= 195) & (sat <= 26)
    for _ in range(3):
        up = np.zeros_like(reached); up[:-1, :] = reached[1:, :]
        dn = np.zeros_like(reached); dn[1:, :] = reached[:-1, :]
        lf = np.zeros_like(reached); lf[:, :-1] = reached[:, 1:]
        rt = np.zeros_like(reached); rt[:, 1:] = reached[:, :-1]
        grow = (up | dn | lf | rt) & light_fringe & (~reached)
        if not grow.any():
            break
        reached |= grow

    alpha = np.where(reached, 0, 255).astype(np.uint8)
    rgba = np.dstack([arr.astype(np.uint8), alpha])
    return Image.fromarray(rgba, "RGBA")


def crop_and_normalize(im):
    """Crop to non-transparent content, add margin, scale to uniform height."""
    bbox = im.getbbox()
    if bbox:
        im = im.crop(bbox)
    w, h = im.size
    margin = max(4, int(0.04 * max(w, h)))
    padded = Image.new("RGBA", (w + 2 * margin, h + 2 * margin), (0, 0, 0, 0))
    padded.paste(im, (margin, margin))
    w, h = padded.size
    scale = TARGET_HEIGHT / float(h)
    new_w = max(1, int(round(w * scale)))
    return padded.resize((new_w, TARGET_HEIGHT), Image.LANCZOS)


def main():
    os.makedirs(DST, exist_ok=True)
    deployed = os.path.isdir(os.path.dirname(PRISMA_DST))
    if deployed:
        os.makedirs(PRISMA_DST, exist_ok=True)

    for name in GLYPHS:
        src = os.path.join(SRC, name + ".png")
        if not os.path.isfile(src):
            print(f"  MISSING: {src}")
            continue
        im = Image.open(src)
        keyed = key_checkerboard(im)
        final = crop_and_normalize(keyed)
        out = os.path.join(DST, name + ".png")
        final.save(out)
        print(f"  {name}: {final.size[0]}x{final.size[1]} -> {out}")
        if deployed:
            final.save(os.path.join(PRISMA_DST, name + ".png"))

    print("Done.")


if __name__ == "__main__":
    main()
