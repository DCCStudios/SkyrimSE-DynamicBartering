"""
Turn the ChatGPT-generated clean X (solid light background) into transparent
64x64 button-icon variants (btn_cancel_normal / btn_cancel_highlight) and a
clean icon_cancel.png, matching the existing button-icon pipeline.
"""
import os
import numpy as np
from PIL import Image, ImageFilter

ROOT = os.path.dirname(__file__)
SRC = os.path.normpath(os.path.join(
    ROOT, "..", "..", "..", "Users", "rober", ".cursor", "projects",
    "e-Skyrim-Animation-SKSE", "assets", "cancel_x_clean.png"))
# Fall back to the absolute path if the relative walk is off.
if not os.path.exists(SRC):
    SRC = r"C:\Users\rober\.cursor\projects\e-Skyrim-Animation-SKSE\assets\cancel_x_clean.png"

BTN_DIR = os.path.join(ROOT, "assets", "icons", "buttons")
ICONS_DIR = os.path.join(ROOT, "assets", "icons")

ICON_PINK = (221, 166, 166)        # dusty rose (matches reference)
HIGHLIGHT = (235, 150, 150)        # slightly more saturated for the lit state
NORMAL    = (165, 120, 120)        # dimmer resting state


def key_out_background(img):
    """Build an alpha mask: background (light, neutral) -> transparent,
    the colored X -> opaque. Returns an RGBA image."""
    rgb = np.asarray(img.convert("RGB")).astype(np.int16)
    # Background sampled from a corner.
    bg = rgb[2, 2].astype(np.int16)
    # Per-pixel max channel distance from the background color.
    dist = np.abs(rgb - bg[None, None, :]).max(axis=2).astype(np.float32)
    lo, hi = 30.0, 90.0
    alpha = np.clip((dist - lo) / (hi - lo), 0.0, 1.0)
    a = (alpha * 255).astype(np.uint8)
    out = np.zeros((rgb.shape[0], rgb.shape[1], 4), dtype=np.uint8)
    out[..., :3] = rgb.astype(np.uint8)
    out[..., 3] = a
    return Image.fromarray(out, "RGBA")


def autocrop(img):
    a = np.asarray(img)[..., 3]
    ys, xs = np.where(a > 10)
    if len(xs) == 0:
        return img
    x0, x1, y0, y1 = xs.min(), xs.max(), ys.min(), ys.max()
    return img.crop((x0, y0, x1 + 1, y1 + 1))


def to_square(img, margin_frac=0.10):
    w, h = img.size
    side = max(w, h)
    pad = int(side * margin_frac)
    canvas = Image.new("RGBA", (side + 2 * pad, side + 2 * pad), (0, 0, 0, 0))
    canvas.paste(img, ((canvas.width - w) // 2, (canvas.height - h) // 2), img)
    return canvas


def recolor(img, rgb):
    """Replace RGB of all visible pixels with `rgb`, keep the alpha shape."""
    arr = np.asarray(img).copy()
    arr[..., 0] = rgb[0]
    arr[..., 1] = rgb[1]
    arr[..., 2] = rgb[2]
    return Image.fromarray(arr, "RGBA")


def main():
    base = key_out_background(Image.open(SRC))
    base = autocrop(base)
    base = to_square(base, margin_frac=0.12)

    # 64x64 button variants (transparent, recolored), matching pipeline.
    sq = base.resize((64, 64), Image.LANCZOS)

    normal = recolor(sq, NORMAL)
    normal.save(os.path.join(BTN_DIR, "btn_cancel_normal.png"))

    hi = recolor(sq, HIGHLIGHT)
    glow = hi.filter(ImageFilter.GaussianBlur(radius=2))
    hi = Image.alpha_composite(glow, hi)
    hi.save(os.path.join(BTN_DIR, "btn_cancel_highlight.png"))

    # Replace the (clean) standalone cancel icon too, at a larger size.
    big = recolor(base, ICON_PINK).resize((512, 512), Image.LANCZOS)
    big.save(os.path.join(ICONS_DIR, "buttons", "icon_cancel.png"))

    print("btn_cancel_normal/highlight + icon_cancel.png written")


if __name__ == "__main__":
    main()
