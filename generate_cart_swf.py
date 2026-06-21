"""
Generate BarterCart.swf - A lightweight non-modal overlay for the Barter Cart system.

Creates a valid SWF with:
- Upper-right cart panel: dark bordered panel listing cart items, subtotals, and net
- Cursor-anchored "[B/Y] Barter" prompt sprite
- Rectangular hold meter bar (filled via _xscale from C++)
- ActionScript functions called from C++: clearCart, addCartItem, setCartTotals,
  setHoldProgress, setPromptVisible

Stage: 1280x720 (same as BarterOffer.swf), 60fps.
"""

import struct
import os
import zlib

TWIPS = 20
WIDTH = 1280
HEIGHT = 720

# Character ID allocation for BarterCart.swf (own ID space, separate SWF)
CHAR_PANEL_BG = 1
CHAR_PANEL_BORDER = 2
CHAR_TITLE_TEXT = 3
CHAR_ITEM_LIST_TEXT = 4
CHAR_TOTALS_TEXT = 5
CHAR_PROMPT_BG = 6
CHAR_PROMPT_TEXT = 7
CHAR_METER_BG = 8
CHAR_METER_FILL = 9
CHAR_METER_MC = 10
CHAR_PROMPT_MC = 11
CHAR_EMPTY_TEXT = 12

# Font IDs (imported from gfxfontlib.swf)
FONT_MEDIUM_ID = 50
FONT_BOLD_ID = 51

# Glyph bitmap/shape/sprite IDs start at 60
GLYPH_START_ID = 60


# ===========================================================================
# Low-level SWF helpers (same as generate_swf.py)
# ===========================================================================

class BitWriter:
    def __init__(self):
        self.bytes = bytearray()
        self.current = 0
        self.pos = 7

    def write_ub(self, val, bits):
        for i in range(bits - 1, -1, -1):
            bit = (val >> i) & 1
            self.current |= (bit << self.pos)
            self.pos -= 1
            if self.pos < 0:
                self.bytes.append(self.current)
                self.current = 0
                self.pos = 7

    def write_sb(self, val, bits):
        if val < 0:
            self.write_ub(val & ((1 << bits) - 1), bits)
        else:
            self.write_ub(val, bits)

    def flush(self):
        if self.pos < 7:
            self.bytes.append(self.current)
            self.current = 0
            self.pos = 7
        return bytes(self.bytes)


def make_rect(xmin, xmax, ymin, ymax):
    bw = BitWriter()
    mx = max(abs(v) for v in (xmin, xmax, ymin, ymax))
    nbits = mx.bit_length() + 1 if mx > 0 else 1
    bw.write_ub(nbits, 5)
    bw.write_sb(xmin, nbits)
    bw.write_sb(xmax, nbits)
    bw.write_sb(ymin, nbits)
    bw.write_sb(ymax, nbits)
    return bw.flush()


def make_tag(tag_type, data=b''):
    length = len(data)
    if length < 0x3F:
        return struct.pack('<H', (tag_type << 6) | length) + data
    else:
        return struct.pack('<H', (tag_type << 6) | 0x3F) + struct.pack('<I', length) + data


def make_file_attributes():
    return make_tag(69, struct.pack('<I', 0))


def make_set_background(r, g, b):
    return make_tag(9, bytes([r, g, b]))


def make_show_frame():
    return make_tag(1)


def make_end_tag():
    return make_tag(0)


def make_import_assets2(url, assets):
    data = bytearray()
    data += url.encode('ascii') + b'\x00'
    data += struct.pack('<B', 1)
    data += struct.pack('<B', 0)
    data += struct.pack('<H', len(assets))
    for char_id, name in assets:
        data += struct.pack('<H', char_id)
        data += name.encode('ascii') + b'\x00'
    return make_tag(71, bytes(data))


def make_define_edit_text(char_id, bounds, var_name, initial_text="",
                          font_height=240, color=(255, 255, 255, 255),
                          max_length=0, read_only=True, multiline=False,
                          word_wrap=False, html=False, has_border=False,
                          font_id=50, align=0):
    data = bytearray()
    data += struct.pack('<H', char_id)
    data += make_rect(bounds[0], bounds[1], bounds[2], bounds[3])

    has_text = len(initial_text) > 0
    has_max_length = max_length > 0
    has_font = font_id > 0
    has_layout = True

    flags = 0
    if has_font:       flags |= 0x0001
    if has_max_length: flags |= 0x0002
    flags |= 0x0004
    if read_only:      flags |= 0x0008
    if multiline:      flags |= 0x0020
    if word_wrap:      flags |= 0x0040
    if has_text:       flags |= 0x0080
    flags |= 0x0100   # UseOutlines
    if html:           flags |= 0x0200
    if has_border:     flags |= 0x0800
    flags |= 0x1000   # NoSelect
    if has_layout:     flags |= 0x2000

    data += struct.pack('<H', flags)

    if has_font:
        data += struct.pack('<H', font_id)
        data += struct.pack('<H', font_height)

    data += bytes(color)

    if has_max_length:
        data += struct.pack('<H', max_length)

    if has_layout:
        data += struct.pack('<B', align)
        data += struct.pack('<H', 0)   # leftMargin
        data += struct.pack('<H', 0)   # rightMargin
        data += struct.pack('<H', 0)   # indent
        data += struct.pack('<h', 40)  # leading

    data += var_name.encode('ascii') + b'\x00'

    if has_text:
        data += initial_text.encode('ascii') + b'\x00'

    return make_tag(37, bytes(data))


def make_define_shape(char_id, fill_color, bounds, line_color=None, line_width=0):
    xmin = bounds[0] * TWIPS
    xmax = bounds[1] * TWIPS
    ymin = bounds[2] * TWIPS
    ymax = bounds[3] * TWIPS

    data = bytearray()
    data += struct.pack('<H', char_id)
    data += make_rect(xmin, xmax, ymin, ymax)

    shape_bw = BitWriter()
    shape_bw.bytes += bytes([1])
    shape_bw.bytes += bytes([0x00])
    shape_bw.bytes += bytes(fill_color)

    has_line = line_color is not None and line_width > 0
    if has_line:
        shape_bw.bytes += bytes([1])
        shape_bw.bytes += struct.pack('<H', line_width)
        shape_bw.bytes += bytes(line_color)
    else:
        shape_bw.bytes += bytes([0])

    num_fill_bits = 1
    num_line_bits = 1 if has_line else 0
    shape_bw.write_ub(num_fill_bits, 4)
    shape_bw.write_ub(num_line_bits, 4)

    shape_bw.write_ub(0, 1)
    shape_bw.write_ub(0, 1)
    shape_bw.write_ub(has_line, 1)
    shape_bw.write_ub(1, 1)
    shape_bw.write_ub(0, 1)
    shape_bw.write_ub(1, 1)

    move_x, move_y = xmin, ymin
    mv_max = max(abs(move_x), abs(move_y))
    move_bits = mv_max.bit_length() + 1 if mv_max > 0 else 5
    shape_bw.write_ub(move_bits, 5)
    shape_bw.write_sb(move_x, move_bits)
    shape_bw.write_sb(move_y, move_bits)

    shape_bw.write_ub(1, num_fill_bits)
    if has_line:
        shape_bw.write_ub(1, num_line_bits)

    w = xmax - xmin
    h = ymax - ymin

    def write_straight_edge(bw, dx, dy):
        bw.write_ub(1, 1)
        bw.write_ub(1, 1)
        edge_max = max(abs(dx), abs(dy))
        edge_nbits = max(edge_max.bit_length() + 1, 2)
        bw.write_ub(edge_nbits - 2, 4)
        if dx != 0 and dy != 0:
            bw.write_ub(1, 1)
            bw.write_sb(dx, edge_nbits)
            bw.write_sb(dy, edge_nbits)
        elif dx == 0:
            bw.write_ub(0, 1)
            bw.write_ub(1, 1)
            bw.write_sb(dy, edge_nbits)
        else:
            bw.write_ub(0, 1)
            bw.write_ub(0, 1)
            bw.write_sb(dx, edge_nbits)

    write_straight_edge(shape_bw, w, 0)
    write_straight_edge(shape_bw, 0, h)
    write_straight_edge(shape_bw, -w, 0)
    write_straight_edge(shape_bw, 0, -h)

    shape_bw.write_ub(0, 1)
    shape_bw.write_ub(0, 5)

    data += shape_bw.flush()
    return make_tag(32, bytes(data))


def make_define_sprite(char_id, tags_data):
    data = bytearray()
    data += struct.pack('<H', char_id)
    data += struct.pack('<H', 1)
    data += tags_data
    return make_tag(39, bytes(data))


def make_export_assets(items):
    data = bytearray()
    data += struct.pack('<H', len(items))
    for char_id, name in items:
        data += struct.pack('<H', char_id)
        data += name.encode('ascii') + b'\x00'
    return make_tag(56, bytes(data))


def make_place_object2(char_id, depth, x=0, y=0, name=""):
    data = bytearray()
    has_name = len(name) > 0
    flags = 0
    if has_name: flags |= 0x20
    flags |= 0x04
    flags |= 0x02

    data += struct.pack('<B', flags)
    data += struct.pack('<H', depth)
    data += struct.pack('<H', char_id)

    bw = BitWriter()
    bw.write_ub(0, 1)
    bw.write_ub(0, 1)
    tx = x * TWIPS
    ty = y * TWIPS
    mx = max(abs(tx), abs(ty))
    nbits = mx.bit_length() + 1 if mx > 0 else 5
    bw.write_ub(nbits, 5)
    bw.write_sb(tx, nbits)
    bw.write_sb(ty, nbits)
    data += bw.flush()

    if has_name:
        data += name.encode('ascii') + b'\x00'

    return make_tag(26, bytes(data))


def make_define_bits_lossless2(char_id, png_path):
    from PIL import Image

    img = Image.open(png_path).convert('RGBA')
    width, height = img.size

    raw_pixels = bytearray()
    for y in range(height):
        for x in range(width):
            r, g, b, a = img.getpixel((x, y))
            if a < 255:
                r = (r * a) // 255
                g = (g * a) // 255
                b = (b * a) // 255
            raw_pixels += bytes([a, r, g, b])

    compressed = zlib.compress(bytes(raw_pixels), 9)

    data = bytearray()
    data += struct.pack('<H', char_id)
    data += struct.pack('<B', 5)
    data += struct.pack('<H', width)
    data += struct.pack('<H', height)
    data += compressed

    return make_tag(36, bytes(data))


def make_define_shape_bitmap(char_id, bitmap_id, width, height, display_w=None, display_h=None):
    dw = display_w if display_w else width
    dh = display_h if display_h else height

    xmin = 0
    xmax = dw * TWIPS
    ymin = 0
    ymax = dh * TWIPS

    data = bytearray()
    data += struct.pack('<H', char_id)
    data += make_rect(xmin, xmax, ymin, ymax)

    shape_bw = BitWriter()
    shape_bw.bytes += bytes([1])
    shape_bw.bytes += bytes([0x41])
    shape_bw.bytes += struct.pack('<H', bitmap_id)

    scale_x_fixed = int((dw / width) * TWIPS * 65536)
    scale_y_fixed = int((dh / height) * TWIPS * 65536)
    scale_bits = max(max(scale_x_fixed, scale_y_fixed).bit_length() + 1, 5)
    mat_bw = BitWriter()
    mat_bw.write_ub(1, 1)
    mat_bw.write_ub(scale_bits, 5)
    mat_bw.write_sb(scale_x_fixed, scale_bits)
    mat_bw.write_sb(scale_y_fixed, scale_bits)
    mat_bw.write_ub(0, 1)
    mat_bw.write_ub(0, 1)
    shape_bw.bytes += mat_bw.flush()

    shape_bw.bytes += bytes([0])

    num_fill_bits = 1
    num_line_bits = 0
    shape_bw.write_ub(num_fill_bits, 4)
    shape_bw.write_ub(num_line_bits, 4)

    shape_bw.write_ub(0, 1)
    shape_bw.write_ub(0, 1)
    shape_bw.write_ub(0, 1)
    shape_bw.write_ub(1, 1)
    shape_bw.write_ub(0, 1)
    shape_bw.write_ub(1, 1)

    move_bits = 5
    shape_bw.write_ub(move_bits, 5)
    shape_bw.write_sb(0, move_bits)
    shape_bw.write_sb(0, move_bits)

    shape_bw.write_ub(1, num_fill_bits)

    w = xmax
    h = ymax

    def write_straight_edge(bw, dx, dy):
        bw.write_ub(1, 1)
        bw.write_ub(1, 1)
        edge_max = max(abs(dx), abs(dy))
        edge_nbits = max(edge_max.bit_length() + 1, 2)
        bw.write_ub(edge_nbits - 2, 4)
        if dx != 0 and dy != 0:
            bw.write_ub(1, 1)
            bw.write_sb(dx, edge_nbits)
            bw.write_sb(dy, edge_nbits)
        elif dx == 0:
            bw.write_ub(0, 1)
            bw.write_ub(1, 1)
            bw.write_sb(dy, edge_nbits)
        else:
            bw.write_ub(0, 1)
            bw.write_ub(0, 1)
            bw.write_sb(dx, edge_nbits)

    write_straight_edge(shape_bw, w, 0)
    write_straight_edge(shape_bw, 0, h)
    write_straight_edge(shape_bw, -w, 0)
    write_straight_edge(shape_bw, 0, -h)

    shape_bw.write_ub(0, 1)
    shape_bw.write_ub(0, 5)

    data += shape_bw.flush()
    return make_tag(32, bytes(data))


def make_doaction(bytecode):
    return make_tag(12, bytecode)


# ===========================================================================
# ActionScript bytecode helpers
# ===========================================================================

def as2_push_string(s):
    encoded = s.encode('ascii') + b'\x00'
    return b'\x96' + struct.pack('<H', len(encoded) + 1) + b'\x00' + encoded


def as2_push_int(n):
    return b'\x96' + struct.pack('<H', 5) + b'\x07' + struct.pack('<i', n)


def as2_push_float(f):
    return b'\x96' + struct.pack('<H', 5) + b'\x01' + struct.pack('<f', f)


def as2_push_bool(b):
    return b'\x96' + struct.pack('<H', 2) + b'\x05' + (b'\x01' if b else b'\x00')


def as2_set_variable():
    return b'\x1D'


def as2_get_variable():
    return b'\x1C'


def as2_push_register(reg):
    return b'\x96' + struct.pack('<H', 2) + b'\x04' + bytes([reg])


def as2_pop():
    return b'\x17'


def as2_stop():
    return b'\x07'


def as2_define_function2(name, params, register_count, body_bytes):
    name_bytes = name.encode('ascii') + b'\x00'
    num_params = len(params)
    flags = 0x04

    param_data = b''
    for reg, pname in params:
        param_data += struct.pack('<B', reg)
        param_data += pname.encode('ascii') + b'\x00'

    body_len = len(body_bytes)

    data = name_bytes
    data += struct.pack('<H', num_params)
    data += struct.pack('<B', register_count)
    data += struct.pack('<H', flags)
    data += param_data
    data += struct.pack('<H', body_len)

    return b'\x8E' + struct.pack('<H', len(data)) + data + body_bytes


# ===========================================================================
# Glyph generation (programmatic PIL)
# ===========================================================================

def generate_glyphs():
    """Generate the Y, Triangle, and B key glyph PNGs programmatically."""
    from PIL import Image, ImageDraw, ImageFont

    glyphs_dir = os.path.join(os.path.dirname(os.path.abspath(__file__)), "assets", "icons", "glyphs")
    os.makedirs(glyphs_dir, exist_ok=True)

    size = 128

    # Xbox Y button (green Y on dark circle)
    img = Image.new("RGBA", (size, size), (0, 0, 0, 0))
    draw = ImageDraw.Draw(img)
    draw.ellipse([4, 4, size - 5, size - 5], fill=(40, 40, 40, 220), outline=(80, 180, 80, 255), width=3)
    try:
        font = ImageFont.truetype("arial.ttf", 72)
    except:
        font = ImageFont.load_default()
    draw.text((size // 2, size // 2), "Y", fill=(80, 200, 80, 255), font=font, anchor="mm")
    img.save(os.path.join(glyphs_dir, "glyph_xbox_y.png"))
    print(f"  Generated glyph_xbox_y.png")

    # PS Triangle (green triangle on dark circle)
    img = Image.new("RGBA", (size, size), (0, 0, 0, 0))
    draw = ImageDraw.Draw(img)
    draw.ellipse([4, 4, size - 5, size - 5], fill=(40, 40, 40, 220), outline=(80, 180, 80, 255), width=3)
    cx, cy = size // 2, size // 2
    tri_h = 44
    tri_w = 50
    points = [(cx, cy - tri_h // 2), (cx - tri_w // 2, cy + tri_h // 2), (cx + tri_w // 2, cy + tri_h // 2)]
    draw.polygon(points, outline=(80, 200, 80, 255), fill=None)
    draw.polygon(points, outline=(80, 200, 80, 255))
    img.save(os.path.join(glyphs_dir, "glyph_ps_triangle.png"))
    print(f"  Generated glyph_ps_triangle.png")

    # Keyboard B key (white B on dark rounded rect)
    img = Image.new("RGBA", (size, size), (0, 0, 0, 0))
    draw = ImageDraw.Draw(img)
    draw.rounded_rectangle([4, 4, size - 5, size - 5], radius=12, fill=(40, 40, 40, 220), outline=(180, 180, 180, 255), width=2)
    draw.text((size // 2, size // 2), "B", fill=(240, 240, 240, 255), font=font, anchor="mm")
    img.save(os.path.join(glyphs_dir, "glyph_key_b.png"))
    print(f"  Generated glyph_key_b.png")

    return glyphs_dir


# ===========================================================================
# ActionScript for the cart overlay
# ===========================================================================

def build_actionscript():
    """Frame-1 AS2 bytecode for the cart overlay.

    Minimal: just enable GFx extensions and stop. All display logic (htmlText,
    _visible, _x/_y, _xscale) is driven from C++ via SetVariable — the proven
    pattern used by BarterOffer.swf.
    """
    bc = bytearray()

    # _global.gfxExtensions = true
    bc += as2_push_string("_global")
    bc += as2_get_variable()
    bc += as2_push_string("gfxExtensions")
    bc += as2_push_bool(True)
    bc += b'\x4F'  # SetMember

    bc += as2_stop()
    return bytes(bc)


# ===========================================================================
# SWF assembly
# ===========================================================================

def build_swf():
    tags = bytearray()
    tags += make_file_attributes()
    tags += make_set_background(0, 0, 0)

    # Font import
    tags += make_import_assets2("gfxfontlib.swf", [
        (FONT_MEDIUM_ID, "$EverywhereMediumFont"),
        (FONT_BOLD_ID, "$EverywhereBoldFont"),
    ])

    # Cart panel dimensions (upper-right corner)
    panel_w = 260
    panel_h = 300
    panel_x = WIDTH - panel_w - 30  # 30px from right edge
    panel_y = 40                     # 40px from top

    # Panel background (dark, semi-transparent)
    tags += make_define_shape(CHAR_PANEL_BG,
        (20, 20, 20, 200),  # RGBA
        (0, panel_w, 0, panel_h))

    # Panel border
    tags += make_define_shape(CHAR_PANEL_BORDER,
        (0, 0, 0, 0),  # transparent fill
        (0, panel_w, 0, panel_h),
        line_color=(180, 150, 80, 255), line_width=40)  # gold border

    # Title text: "BARTER CART"
    title_bounds = (0, panel_w * TWIPS, 0, 24 * TWIPS)
    tags += make_define_edit_text(CHAR_TITLE_TEXT, title_bounds, "CartTitle",
        initial_text="", font_height=200, color=(220, 190, 100, 255),
        font_id=FONT_BOLD_ID, align=2, html=True, multiline=False)

    # Item list text (multi-line, word-wrap, html for formatting)
    list_bounds = (0, (panel_w - 20) * TWIPS, 0, 180 * TWIPS)
    tags += make_define_edit_text(CHAR_ITEM_LIST_TEXT, list_bounds, "ItemListText",
        initial_text="", font_height=160, color=(220, 220, 220, 255),
        font_id=FONT_MEDIUM_ID, align=0, html=True, multiline=True, word_wrap=True)

    # Totals text
    totals_bounds = (0, (panel_w - 20) * TWIPS, 0, 60 * TWIPS)
    tags += make_define_edit_text(CHAR_TOTALS_TEXT, totals_bounds, "TotalsText",
        initial_text="", font_height=160, color=(200, 180, 100, 255),
        font_id=FONT_MEDIUM_ID, align=0, html=True, multiline=True, word_wrap=True)

    # Empty cart text
    empty_bounds = (0, (panel_w - 20) * TWIPS, 0, 30 * TWIPS)
    tags += make_define_edit_text(CHAR_EMPTY_TEXT, empty_bounds, "EmptyText",
        initial_text="", font_height=160, color=(150, 150, 150, 255),
        font_id=FONT_MEDIUM_ID, align=2, html=True, multiline=False)

    # --- Hold Meter ---
    meter_w = 80
    meter_h = 8

    # Meter background
    tags += make_define_shape(CHAR_METER_BG,
        (30, 30, 30, 180),
        (0, meter_w, 0, meter_h),
        line_color=(120, 100, 60, 255), line_width=20)

    # Meter fill (gold bar)
    tags += make_define_shape(CHAR_METER_FILL,
        (200, 170, 60, 255),
        (0, meter_w, 0, meter_h))

    # Meter MovieClip (background + fill inside, fill named "MeterFill")
    meter_spr = bytearray()
    meter_spr += make_place_object2(CHAR_METER_BG, 1, 0, 0)
    meter_spr += make_place_object2(CHAR_METER_FILL, 2, 0, 0, "MeterFill")
    meter_spr += make_show_frame()
    meter_spr += make_end_tag()
    tags += make_define_sprite(CHAR_METER_MC, bytes(meter_spr))

    # --- Glyph embedding (MUST be defined before the prompt sprite references them) ---
    glyphs_dir = os.path.join(os.path.dirname(os.path.abspath(__file__)), "assets", "icons", "glyphs")
    glyph_defs = [
        ("pg_xb_y", "glyph_xbox_y.png"),
        ("pg_ps_tri", "glyph_ps_triangle.png"),
        ("pg_kbd_b", "glyph_key_b.png"),
    ]
    GLYPH_DISPLAY_H = 18
    glyph_next_id = GLYPH_START_ID
    glyph_sprites = {}  # instance_name -> (sprite_char_id, display_w)

    from PIL import Image as _GlyphImage

    for inst_name, fn in glyph_defs:
        path = os.path.join(glyphs_dir, fn)
        if not os.path.isfile(path):
            print(f"  WARNING: Glyph not found: {path}")
            continue
        with _GlyphImage.open(path) as gi:
            gw, gh = gi.size
        disp_h = GLYPH_DISPLAY_H
        disp_w = max(1, int(round(GLYPH_DISPLAY_H * gw / float(gh))))
        bmp_id = glyph_next_id; glyph_next_id += 1
        shp_id = glyph_next_id; glyph_next_id += 1
        spr_id = glyph_next_id; glyph_next_id += 1
        tags += make_define_bits_lossless2(bmp_id, path)
        tags += make_define_shape_bitmap(shp_id, bmp_id, gw, gh, display_w=disp_w, display_h=disp_h)
        spr = bytearray()
        spr += make_place_object2(shp_id, 1, 0, 0)
        spr += make_show_frame()
        spr += make_end_tag()
        tags += make_define_sprite(spr_id, bytes(spr))
        glyph_sprites[inst_name] = (spr_id, disp_w)
        print(f"  Embedded glyph {inst_name}: {gw}x{gh} -> display {disp_w}x{disp_h}")

    # --- Prompt sprite: [glyph] Barter near the cursor / item ---
    prompt_w = 96
    prompt_h = 26

    # Prompt background (dark with thin gold trim)
    tags += make_define_shape(CHAR_PROMPT_BG,
        (18, 16, 12, 210),
        (0, prompt_w, 0, prompt_h),
        line_color=(150, 130, 60, 230), line_width=20)

    # Prompt label "Barter" (text set from C++ with font markup)
    prompt_bounds = (0, (prompt_w - 26) * TWIPS, 0, (prompt_h - 6) * TWIPS)
    tags += make_define_edit_text(CHAR_PROMPT_TEXT, prompt_bounds, "PromptLabel",
        initial_text="", font_height=170, color=(230, 215, 150, 255),
        font_id=FONT_MEDIUM_ID, align=0, html=True, multiline=False)

    # Prompt MovieClip: bg + all glyphs (C++ shows the active one) + label
    prompt_spr = bytearray()
    pdepth = 1
    prompt_spr += make_place_object2(CHAR_PROMPT_BG, pdepth, 0, 0); pdepth += 1
    # Place each glyph at the left, same spot; C++ toggles _visible to show the active device's glyph
    for inst_name, (spr_id, disp_w) in glyph_sprites.items():
        prompt_spr += make_place_object2(spr_id, pdepth, 6, 4, inst_name); pdepth += 1
    # Label sits to the right of the glyph
    prompt_spr += make_place_object2(CHAR_PROMPT_TEXT, pdepth, 30, 4, "PromptLabel"); pdepth += 1
    prompt_spr += make_show_frame()
    prompt_spr += make_end_tag()
    tags += make_define_sprite(CHAR_PROMPT_MC, bytes(prompt_spr))

    # ===================================================================
    # STAGE PLACEMENT
    # ===================================================================
    depth = 1

    # Cart panel
    tags += make_place_object2(CHAR_PANEL_BG, depth, panel_x, panel_y, "panelBG"); depth += 1
    tags += make_place_object2(CHAR_PANEL_BORDER, depth, panel_x, panel_y, "panelBorder"); depth += 1

    # Title
    tags += make_place_object2(CHAR_TITLE_TEXT, depth, panel_x + 10, panel_y + 8, "CartTitle"); depth += 1

    # Item list
    tags += make_place_object2(CHAR_ITEM_LIST_TEXT, depth, panel_x + 10, panel_y + 36, "ItemListText"); depth += 1

    # Empty text
    tags += make_place_object2(CHAR_EMPTY_TEXT, depth, panel_x + 10, panel_y + 120, "EmptyText"); depth += 1

    # Totals
    tags += make_place_object2(CHAR_TOTALS_TEXT, depth, panel_x + 10, panel_y + 220, "TotalsText"); depth += 1

    # Prompt (C++ repositions via _x/_y to follow cursor or anchor near item)
    tags += make_place_object2(CHAR_PROMPT_MC, depth, 400, 360, "PromptMC"); depth += 1

    # Hold meter (C++ repositions to sit just under the prompt)
    tags += make_place_object2(CHAR_METER_MC, depth, 400, 388, "MeterMC"); depth += 1

    # ===================================================================
    # ACTIONSCRIPT
    # ===================================================================
    bytecode = build_actionscript()
    tags += make_doaction(bytecode)

    tags += make_show_frame()
    tags += make_end_tag()

    # --- SWF header ---
    rect = make_rect(0, WIDTH * TWIPS, 0, HEIGHT * TWIPS)
    header = bytearray()
    header += rect
    header += struct.pack('<H', 60 << 8)  # 60fps
    header += struct.pack('<H', 1)         # frame count

    body = bytes(header) + bytes(tags)
    sig = b'FWS'
    version = struct.pack('B', 8)
    file_len = struct.pack('<I', len(body) + 8)

    return sig + version + file_len + body


def main():
    print("Generating cart glyphs...")
    generate_glyphs()

    print("Building BarterCart.swf...")
    swf_data = build_swf()

    out_dir = r"E:\Skyrim Animation\SKSE\DynamicBarteringSKSE\assets\interface\DynamicBartering"
    os.makedirs(out_dir, exist_ok=True)

    out_path = os.path.join(out_dir, "BarterCart.swf")
    with open(out_path, 'wb') as f:
        f.write(swf_data)
    print(f"Generated {out_path} ({len(swf_data)} bytes)")


if __name__ == '__main__':
    main()
