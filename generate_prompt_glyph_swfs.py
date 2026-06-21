"""
Generate tiny single-glyph SWFs for the Barter Cart prompt icon.

Each SWF's main timeline shows exactly one keybind glyph bitmap (the metallic
"B" key, the Xbox "Y", or the PlayStation triangle) at a small display size.
The plugin loads the device-appropriate one into the injected prompt clip via
MovieClip.loadMovie(), so the prompt shows the real bitmap icon instead of a
text key-cap. loadMovie of a nested SWF is the most reliable image path in
Skyrim's Scaleform (the engine loads all its menus as SWFs), and the plugin
keeps a gold text key-cap as a graceful fallback if the load ever fails.

Reuses the SWF tag builders from generate_cart_swf.py so the bit packing stays
identical to the rest of the project's generated assets.

Run: python generate_prompt_glyph_swfs.py
"""
import os
import struct

import generate_cart_swf as gc

TWIPS = 20
DISPLAY_H = 20  # px height the glyph renders at inside the prompt

# instance/file basename -> source glyph PNG
GLYPHS = {
    "glyph_prompt_b":   "glyph_key_b.png",       # keyboard / mouse
    "glyph_prompt_y":   "glyph_xbox_y.png",      # Xbox gamepad
    "glyph_prompt_tri": "glyph_ps_triangle.png", # PlayStation gamepad
}

OUT_DIRS = [
    r"E:\Skyrim Animation\SKSE\DynamicBarteringSKSE\assets\interface\DynamicBartering",
    r"F:\Modlists\BottleRim2_0\mods\Silver Tongue - Dynamic Bartering\Interface\DynamicBartering",
]


def build_glyph_swf(png_path):
    from PIL import Image
    with Image.open(png_path) as im:
        gw, gh = im.size

    disp_h = DISPLAY_H
    disp_w = max(1, int(round(DISPLAY_H * gw / float(gh))))

    BMP_ID = 1
    SHAPE_ID = 2
    SPRITE_ID = 3

    tags = bytearray()
    tags += gc.make_file_attributes()
    tags += gc.make_set_background(0, 0, 0)
    tags += gc.make_define_bits_lossless2(BMP_ID, png_path)
    tags += gc.make_define_shape_bitmap(SHAPE_ID, BMP_ID, gw, gh, display_w=disp_w, display_h=disp_h)

    # Wrap the bitmap-fill shape in a MovieClip and place the CLIP on the root
    # timeline. This matches the proven BarterOffer.swf pattern (its glyph bitmaps
    # always live inside sprites); placing a raw bitmap-fill shape directly on the
    # loaded root appears to upset Scaleform's advance/render.
    spr = bytearray()
    spr += gc.make_place_object2(SHAPE_ID, 1, 0, 0)
    spr += gc.make_show_frame()
    spr += gc.make_end_tag()
    tags += gc.make_define_sprite(SPRITE_ID, bytes(spr))

    tags += gc.make_place_object2(SPRITE_ID, 1, 0, 0)
    tags += gc.make_show_frame()
    tags += gc.make_end_tag()

    rect = gc.make_rect(0, disp_w * TWIPS, 0, disp_h * TWIPS)
    header = bytearray()
    header += rect
    header += struct.pack('<H', 60 << 8)  # 60fps
    header += struct.pack('<H', 1)         # frame count

    body = bytes(header) + bytes(tags)
    sig = b'FWS'
    version = struct.pack('B', 8)
    file_len = struct.pack('<I', len(body) + 8)
    return sig + version + file_len + body, (disp_w, disp_h)


def main():
    glyphs_dir = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                              "assets", "icons", "glyphs")
    for out_dir in OUT_DIRS:
        if not os.path.isdir(os.path.dirname(out_dir)):
            print(f"  (skip, parent missing) {out_dir}")
            continue
        os.makedirs(out_dir, exist_ok=True)

    for base, png in GLYPHS.items():
        src = os.path.join(glyphs_dir, png)
        if not os.path.isfile(src):
            print(f"  MISSING: {src}")
            continue
        data, (dw, dh) = build_glyph_swf(src)
        for out_dir in OUT_DIRS:
            if not os.path.isdir(out_dir):
                continue
            out_path = os.path.join(out_dir, base + ".swf")
            with open(out_path, 'wb') as f:
                f.write(data)
            print(f"  {base}.swf ({dw}x{dh}, {len(data)} bytes) -> {out_path}")

    print("Done.")


if __name__ == '__main__':
    main()
