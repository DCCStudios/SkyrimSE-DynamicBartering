"""
Generate BarterOffer.swf - A compact Skyrim-style barter offer popup.

Creates a valid SWF with:
- Compact dark popup panel (~340x300) centered on 1280x720 stage
- Text fields for merchant name, flavor, offer price, slider range, reactions
- 7 button MovieClips for offer/counter/result states
- Horizontal slider with diamond handle
- GameDelegate callbacks to communicate with C++ (OfferSubmit, CounterResponse, etc.)
- Functions callable from C++ (SetOfferData, ShowCounterOffer, ShowResult, adjustSlider, etc.)
- Button state management (offer, counter, result)
"""

import struct
import os

TWIPS = 20
WIDTH = 1280
HEIGHT = 720

# Character ID allocation
CHAR_MERCHANT_NAME = 1
CHAR_FLAVOR_TEXT = 2
CHAR_OFFER_LABEL = 3
CHAR_PRICE_TEXT = 4
CHAR_SLIDER_TEXT = 5
CHAR_CURRENT_PRICE = 6
CHAR_REACTION_TEXT = 7
CHAR_STATUS_TEXT = 8
CHAR_BUTTON_HINT = 9

CHAR_PANEL_BG = 10
CHAR_PANEL_BORDER = 11
CHAR_ORNAMENT = 12
CHAR_SLIDER_TRACK = 13
CHAR_SLIDER_HANDLE = 14
CHAR_SLIDER_MC = 15
CHAR_SLIDER_HITAREA = 52
CHAR_BTN_BG_NORMAL = 16
CHAR_BTN_BG_HIGHLIGHT = 17

CHAR_BTN_TEXT_SUBMIT = 20
CHAR_BTN_TEXT_INTIMIDATE = 21
CHAR_BTN_TEXT_CANCEL = 22
CHAR_BTN_TEXT_ACCEPT = 23
CHAR_BTN_TEXT_REOFFER = 24
CHAR_BTN_TEXT_WALKAWAY = 25
CHAR_BTN_TEXT_CONTINUE = 26

CHAR_BTN_SUBMIT = 30
CHAR_BTN_INTIMIDATE = 31
CHAR_BTN_CANCEL = 32
CHAR_BTN_ACCEPT = 33
CHAR_BTN_REOFFER = 34
CHAR_BTN_WALKAWAY = 35
CHAR_BTN_CONTINUE = 36

CHAR_ARROW_LEFT = 40
CHAR_ARROW_RIGHT = 41

CHAR_BMP_ORNAMENT = 42
CHAR_BMP_ARROW_LEFT = 43
CHAR_BMP_ARROW_RIGHT = 44
CHAR_BMP_GOLD_COIN = 45
CHAR_BMP_SLIDER_DIAMOND = 46
CHAR_BMP_ORNAMENT_SHAPE = 53
CHAR_BMP_COIN_SHAPE = 54
CHAR_BMP_ARROW_LEFT_SHAPE = 55
CHAR_BMP_ARROW_RIGHT_SHAPE = 56

CHAR_SLIDER_HANDLE_MC = 57
CHAR_BTN_BG_NORMAL_MC = 58
CHAR_BTN_BG_HIGHLIGHT_MC = 59

# Button icon bitmaps (highlight versions - full color, alpha-dimmed by button state)
CHAR_BTN_ICON_BMP_SUBMIT = 60
CHAR_BTN_ICON_BMP_INTIMIDATE = 61
CHAR_BTN_ICON_BMP_CANCEL = 62
CHAR_BTN_ICON_BMP_ACCEPT = 63
CHAR_BTN_ICON_BMP_REOFFER = 64
CHAR_BTN_ICON_BMP_WALKAWAY = 65
CHAR_BTN_ICON_BMP_CONTINUE = 66

# Button icon shapes (bitmap fill, rendered at 16x16 display from 64x64 source)
CHAR_BTN_ICON_SHAPE_SUBMIT = 70
CHAR_BTN_ICON_SHAPE_INTIMIDATE = 71
CHAR_BTN_ICON_SHAPE_CANCEL = 72
CHAR_BTN_ICON_SHAPE_ACCEPT = 73
CHAR_BTN_ICON_SHAPE_REOFFER = 74
CHAR_BTN_ICON_SHAPE_WALKAWAY = 75
CHAR_BTN_ICON_SHAPE_CONTINUE = 76

# New visual elements for enhanced UI
CHAR_ACCEPTANCE_TEXT = 80
CHAR_BASE_PRICE_TEXT = 81
CHAR_REL_EFFECT_TEXT = 82
CHAR_REL_BAR_BG = 83
CHAR_REL_BAR_FILL = 84
CHAR_REL_BAR_MC = 85
CHAR_SEPARATOR_LINE = 86
CHAR_CORNER_TL = 87
CHAR_CORNER_TR = 88
CHAR_CORNER_BL = 89
CHAR_CORNER_BR = 90

# Bitmap IDs for separator and corners
CHAR_BMP_SEPARATOR = 91
CHAR_BMP_CORNER_TL = 92
CHAR_BMP_CORNER_TR = 93
CHAR_BMP_CORNER_BL = 94
CHAR_BMP_CORNER_BR = 95
CHAR_DEAL_HISTORY_TEXT = 96

# Relationship meter extras (IDs >= 150 to stay clear of the dynamically-assigned
# keybind glyph IDs which start at 100).
CHAR_REL_BAR_FILL_RED = 150   # hostile (red) fill
CHAR_REL_BAR_FILL_GRN = 151   # friendly (green) fill
CHAR_REL_MARKER = 152         # moving diamond/tick marker
CHAR_COIN_SPRITE = 153        # coin MovieClip placed as "coinIcon"; C++ moves its _x
# Keybind hint labels (placed glyph shapes + these text fields form the hint row).
CHAR_HINT_LBL_1 = 154
CHAR_HINT_LBL_2 = 155
CHAR_HINT_LBL_3 = 156
CHAR_HINT_LBL_4 = 157
# Extra hint label for the gamepad-only "shoulder bumpers move by 5" cue (id chosen
# from the free 166-199 gap; button-bg char ids start at 200).
CHAR_HINT_LBL_5 = 166
# Relationship meter parts wrapped in MovieClips so _x/_xscale/_visible animate
# reliably (bare shapes don't honor these in this GFx build, like the slider handle).
CHAR_REL_BAR_FILL_MC = 157
CHAR_REL_BAR_FILL_RED_MC = 158
CHAR_REL_BAR_FILL_GRN_MC = 159
CHAR_REL_MARKER_MC = 160
# Relationship meter colored zones (hostile red -> neutral -> friendly green) drawn
# as the bar's inner background so the marker's position carries clear meaning.
CHAR_REL_ZONE_0 = 161
CHAR_REL_ZONE_1 = 162
CHAR_REL_ZONE_2 = 163
CHAR_REL_ZONE_3 = 164
CHAR_REL_ZONE_4 = 165

FONT_MEDIUM_ID = 50
FONT_BOLD_ID = 51


# ===========================================================================
# Low-level SWF helpers
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
    """ImportAssets2 tag (tag type 71).

    url: the SWF file to import from (e.g. "gfxfontlib.swf")
    assets: list of (char_id, name) tuples
    """
    data = bytearray()
    data += url.encode('ascii') + b'\x00'
    data += struct.pack('<B', 1)   # reserved
    data += struct.pack('<B', 0)   # reserved
    data += struct.pack('<H', len(assets))
    for char_id, name in assets:
        data += struct.pack('<H', char_id)
        data += name.encode('ascii') + b'\x00'
    return make_tag(71, bytes(data))


# ===========================================================================
# ActionScript 2 bytecode helpers
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


def as2_push_null():
    return b'\x96' + struct.pack('<H', 1) + b'\x02'


def as2_push_undefined():
    return b'\x96' + struct.pack('<H', 1) + b'\x03'


def as2_push_register(reg):
    return b'\x96' + struct.pack('<H', 2) + b'\x04' + bytes([reg])


def as2_set_variable():
    return b'\x1D'


def as2_get_variable():
    return b'\x1C'


def as2_call_function():
    return b'\x3D'


def as2_pop():
    return b'\x17'


def as2_stop():
    return b'\x07'


def as2_trace():
    return b'\x26'


def as2_define_function2(name, params, register_count, body_bytes):
    """DefineFunction2 (0x8E)."""
    name_bytes = name.encode('ascii') + b'\x00'
    num_params = len(params)
    flags = 0x04  # preloadThis

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
# SWF structure builders
# ===========================================================================

def make_define_edit_text(char_id, bounds, var_name, initial_text="",
                          font_height=240, color=(255, 255, 255, 255),
                          max_length=0, read_only=True, multiline=False,
                          word_wrap=False, html=False, has_border=False,
                          font_id=50, align=0):
    """DefineEditText tag (tag type 37)."""
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
    flags |= 0x0004                       # HasTextColor
    if read_only:      flags |= 0x0008
    if multiline:      flags |= 0x0020
    if word_wrap:      flags |= 0x0040
    if has_text:       flags |= 0x0080
    flags |= 0x0100                       # UseOutlines
    if html:           flags |= 0x0200
    if has_border:     flags |= 0x0800
    flags |= 0x1000                       # NoSelect
    if has_layout:     flags |= 0x2000

    data += struct.pack('<H', flags)

    if has_font:
        data += struct.pack('<H', font_id)
        data += struct.pack('<H', font_height)

    data += bytes(color)

    if has_max_length:
        data += struct.pack('<H', max_length)

    if has_layout:
        data += struct.pack('<B', align)   # 0=left, 1=right, 2=center, 3=justify
        data += struct.pack('<H', 0)       # leftMargin
        data += struct.pack('<H', 0)       # rightMargin
        data += struct.pack('<H', 0)       # indent
        data += struct.pack('<h', 40)      # leading

    data += var_name.encode('ascii') + b'\x00'

    if has_text:
        data += initial_text.encode('ascii') + b'\x00'

    return make_tag(37, bytes(data))


def make_define_shape(char_id, fill_color, bounds, line_color=None, line_width=0):
    """DefineShape3 (tag 32) - filled rectangle with optional border."""
    xmin = bounds[0] * TWIPS
    xmax = bounds[1] * TWIPS
    ymin = bounds[2] * TWIPS
    ymax = bounds[3] * TWIPS

    data = bytearray()
    data += struct.pack('<H', char_id)
    data += make_rect(xmin, xmax, ymin, ymax)

    shape_bw = BitWriter()

    # Fill styles
    shape_bw.bytes += bytes([1])     # 1 fill style
    shape_bw.bytes += bytes([0x00])  # solid type
    shape_bw.bytes += bytes(fill_color)

    # Line styles
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

    # StyleChangeRecord: moveTo top-left
    shape_bw.write_ub(0, 1)          # non-edge
    shape_bw.write_ub(0, 1)          # stateNewStyles
    shape_bw.write_ub(has_line, 1)   # stateLineStyle
    shape_bw.write_ub(1, 1)          # stateFillStyle1
    shape_bw.write_ub(0, 1)          # stateFillStyle0
    shape_bw.write_ub(1, 1)          # stateMoveTo

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
        bw.write_ub(1, 1)  # edge
        bw.write_ub(1, 1)  # straight
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

    # EndShapeRecord
    shape_bw.write_ub(0, 1)
    shape_bw.write_ub(0, 5)

    data += shape_bw.flush()
    return make_tag(32, bytes(data))


def make_define_shape_diamond(char_id, size, fill_color, line_color=None, line_width=0):
    """DefineShape3 - a diamond (rotated square) shape."""
    half = size * TWIPS
    xmin = -half
    xmax = half
    ymin = -half
    ymax = half

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

    # Move to top vertex (0, -half)
    shape_bw.write_ub(0, 1)
    shape_bw.write_ub(0, 1)
    shape_bw.write_ub(has_line, 1)
    shape_bw.write_ub(1, 1)
    shape_bw.write_ub(0, 1)
    shape_bw.write_ub(1, 1)

    mv_max = half
    move_bits = mv_max.bit_length() + 1 if mv_max > 0 else 5
    shape_bw.write_ub(move_bits, 5)
    shape_bw.write_sb(0, move_bits)       # x = 0
    shape_bw.write_sb(-half, move_bits)   # y = -half

    shape_bw.write_ub(1, num_fill_bits)
    if has_line:
        shape_bw.write_ub(1, num_line_bits)

    def write_general_edge(bw, dx, dy):
        bw.write_ub(1, 1)
        bw.write_ub(1, 1)
        edge_max = max(abs(dx), abs(dy))
        edge_nbits = max(edge_max.bit_length() + 1, 2)
        bw.write_ub(edge_nbits - 2, 4)
        bw.write_ub(1, 1)  # GeneralLine
        bw.write_sb(dx, edge_nbits)
        bw.write_sb(dy, edge_nbits)

    # top → right → bottom → left → top
    write_general_edge(shape_bw, half, half)     # top to right
    write_general_edge(shape_bw, -half, half)    # right to bottom
    write_general_edge(shape_bw, -half, -half)   # bottom to left
    write_general_edge(shape_bw, half, -half)    # left to top

    shape_bw.write_ub(0, 1)
    shape_bw.write_ub(0, 5)

    data += shape_bw.flush()
    return make_tag(32, bytes(data))


def make_define_shape_line(char_id, width_px, thickness_px, color):
    """DefineShape3 - a thin horizontal line (for slider track, ornaments)."""
    xmin = 0
    xmax = width_px * TWIPS
    ymin = 0
    ymax = thickness_px * TWIPS

    data = bytearray()
    data += struct.pack('<H', char_id)
    data += make_rect(xmin, xmax, ymin, ymax)

    shape_bw = BitWriter()

    shape_bw.bytes += bytes([1])
    shape_bw.bytes += bytes([0x00])
    shape_bw.bytes += bytes(color)
    shape_bw.bytes += bytes([0])  # no line styles

    shape_bw.write_ub(1, 4)  # numFillBits
    shape_bw.write_ub(0, 4)  # numLineBits

    # Move to top-left
    shape_bw.write_ub(0, 1)
    shape_bw.write_ub(0, 1)
    shape_bw.write_ub(0, 1)
    shape_bw.write_ub(1, 1)
    shape_bw.write_ub(0, 1)
    shape_bw.write_ub(1, 1)

    mv_max = max(abs(xmin), abs(ymin))
    move_bits = max(mv_max.bit_length() + 1, 5) if mv_max > 0 else 5
    shape_bw.write_ub(move_bits, 5)
    shape_bw.write_sb(xmin, move_bits)
    shape_bw.write_sb(ymin, move_bits)

    shape_bw.write_ub(1, 1)  # fillStyle1 = 1

    w = xmax - xmin
    h = ymax - ymin

    def write_edge(bw, dx, dy):
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

    write_edge(shape_bw, w, 0)
    write_edge(shape_bw, 0, h)
    write_edge(shape_bw, -w, 0)
    write_edge(shape_bw, 0, -h)

    shape_bw.write_ub(0, 1)
    shape_bw.write_ub(0, 5)

    data += shape_bw.flush()
    return make_tag(32, bytes(data))


def make_define_bits_lossless2(char_id, png_path):
    """DefineBitsLossless2 (tag 36) - embed an RGBA PNG as a bitmap character.

    Reads the PNG via Pillow, extracts raw RGBA pixels, and wraps them
    in the SWF tag format (format=5 = 32-bit ARGB, no palette).
    """
    import zlib
    from PIL import Image

    img = Image.open(png_path).convert('RGBA')
    width, height = img.size

    # SWF expects ARGB (pre-multiplied alpha) pixel data, row-padded to 4-byte boundary
    raw_pixels = bytearray()
    for y in range(height):
        for x in range(width):
            r, g, b, a = img.getpixel((x, y))
            # Pre-multiply alpha
            if a < 255:
                r = (r * a) // 255
                g = (g * a) // 255
                b = (b * a) // 255
            raw_pixels += bytes([a, r, g, b])

    compressed = zlib.compress(bytes(raw_pixels), 9)

    data = bytearray()
    data += struct.pack('<H', char_id)
    data += struct.pack('<B', 5)       # format 5 = 32-bit ARGB
    data += struct.pack('<H', width)
    data += struct.pack('<H', height)
    data += compressed

    return make_tag(36, bytes(data))


def make_define_shape_bitmap(char_id, bitmap_id, width, height, display_w=None, display_h=None):
    """DefineShape3 that fills a rectangle with a bitmap (for placing images on stage).
    
    If display_w/display_h are provided, the bitmap is scaled to fit that display size
    while the underlying bitmap remains at full resolution for quality.
    """
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

    # Fill style: clipped bitmap fill (type 0x41)
    shape_bw.bytes += bytes([1])       # 1 fill style
    shape_bw.bytes += bytes([0x41])    # clipped bitmap fill
    shape_bw.bytes += struct.pack('<H', bitmap_id)
    # Fill matrix: scale that maps bitmap pixels to display twips
    # Each bitmap pixel maps to (display_size / bitmap_size) * TWIPS twips
    scale_x_fixed = int((dw / width) * TWIPS * 65536)
    scale_y_fixed = int((dh / height) * TWIPS * 65536)
    scale_fixed = max(scale_x_fixed, scale_y_fixed)
    scale_bits = max(scale_fixed.bit_length() + 1, 5)  # +1 for sign
    mat_bw = BitWriter()
    mat_bw.write_ub(1, 1)            # hasScale
    mat_bw.write_ub(scale_bits, 5)   # nScaleBits
    mat_bw.write_sb(scale_x_fixed, scale_bits)  # scaleX
    mat_bw.write_sb(scale_y_fixed, scale_bits)  # scaleY
    mat_bw.write_ub(0, 1)            # hasRotate
    mat_bw.write_ub(0, 5)            # NTranslateBits = 0 (no translation)
    shape_bw.bytes += mat_bw.flush()

    # No line styles
    shape_bw.bytes += bytes([0])

    num_fill_bits = 1
    num_line_bits = 0
    shape_bw.write_ub(num_fill_bits, 4)
    shape_bw.write_ub(num_line_bits, 4)

    # StyleChangeRecord: moveTo top-left, set fill
    shape_bw.write_ub(0, 1)   # non-edge
    shape_bw.write_ub(0, 1)   # stateNewStyles
    shape_bw.write_ub(0, 1)   # stateLineStyle
    shape_bw.write_ub(1, 1)   # stateFillStyle1
    shape_bw.write_ub(0, 1)   # stateFillStyle0
    shape_bw.write_ub(1, 1)   # stateMoveTo
    shape_bw.write_ub(5, 5)   # moveBits
    shape_bw.write_sb(0, 5)   # moveX
    shape_bw.write_sb(0, 5)   # moveY
    shape_bw.write_ub(1, num_fill_bits)  # fillStyle1 = 1

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

    # EndShapeRecord
    shape_bw.write_ub(0, 1)
    shape_bw.write_ub(0, 5)

    data += shape_bw.flush()
    return make_tag(32, bytes(data))


def make_define_sprite(char_id, tags_data):
    """DefineSprite tag (tag type 39) - a MovieClip."""
    data = bytearray()
    data += struct.pack('<H', char_id)
    data += struct.pack('<H', 1)  # frame count
    data += tags_data
    return make_tag(39, bytes(data))


def make_export_assets(items):
    """ExportAssets (tag 56) - assign linkage names to characters.

    This is the AS2 'export for ActionScript' mechanism; it lets htmlText
    reference a MovieClip inline via <img src='linkageName'/>.
    items: list of (char_id, name) tuples.
    """
    data = bytearray()
    data += struct.pack('<H', len(items))
    for char_id, name in items:
        data += struct.pack('<H', char_id)
        data += name.encode('ascii') + b'\x00'
    return make_tag(56, bytes(data))


def make_place_object2(char_id, depth, x=0, y=0, name=""):
    """PlaceObject2 tag (tag type 26)."""
    data = bytearray()

    has_name = len(name) > 0
    flags = 0
    if has_name: flags |= 0x20
    flags |= 0x04  # hasMatrix
    flags |= 0x02  # hasCharacter

    data += struct.pack('<B', flags)
    data += struct.pack('<H', depth)
    data += struct.pack('<H', char_id)

    # Translate matrix
    bw = BitWriter()
    bw.write_ub(0, 1)  # hasScale
    bw.write_ub(0, 1)  # hasRotate
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


def make_place_object2_hidden(char_id, depth, x=0, y=0, name=""):
    """PlaceObject2 with alpha=0 (hidden by default)."""
    data = bytearray()

    has_name = len(name) > 0
    flags = 0
    if has_name: flags |= 0x20
    flags |= 0x08  # hasColorTransform
    flags |= 0x04  # hasMatrix
    flags |= 0x02  # hasCharacter

    data += struct.pack('<B', flags)
    data += struct.pack('<H', depth)
    data += struct.pack('<H', char_id)

    # Matrix
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

    # CxFormWithAlpha: multiply alpha by 0 (hidden), keep RGB at 1.0 (=256 in 8.8 fixed)
    cxbw = BitWriter()
    cxbw.write_ub(0, 1)  # HasAddTerms
    cxbw.write_ub(1, 1)  # HasMultTerms
    cxbw.write_ub(10, 4) # Nbits (10 needed: 256 overflows 9-bit signed [-256..255])
    cxbw.write_sb(256, 10)  # R mult = 1.0
    cxbw.write_sb(256, 10)  # G mult = 1.0
    cxbw.write_sb(256, 10)  # B mult = 1.0
    cxbw.write_sb(0, 10)    # A mult = 0 (hidden)
    data += cxbw.flush()

    if has_name:
        data += name.encode('ascii') + b'\x00'

    return make_tag(26, bytes(data))


def make_doaction(bytecode):
    """DoAction tag (tag type 12)."""
    return make_tag(12, bytecode + b'\x00')


# (Duplicate removed - using Pillow-based make_define_bits_lossless2 above)


# ===========================================================================
# ActionScript bytecode builder
# ===========================================================================

def build_actionscript():
    """Build complete ActionScript bytecode for the barter popup."""
    bc = bytearray()

    bc += as2_stop()

    # Enable GFx HTML extensions. WITHOUT this, Scaleform ignores inline <img>
    # tags in htmlText, so keybind glyphs never render. Vanilla Skyrim menus set
    # this in frame-1 (see decompiled InventoryScrollingList / FocusHandler).
    # SetVariable order matches the rest of this builder: push NAME then VALUE.
    bc += as2_push_string("_global.gfxExtensions")
    bc += as2_push_bool(True)
    bc += as2_set_variable()

    # --- State variables ---
    bc += as2_push_string("state")
    bc += as2_push_string("idle")
    bc += as2_set_variable()

    bc += as2_push_string("offeredPrice")
    bc += as2_push_int(0)
    bc += as2_set_variable()

    bc += as2_push_string("basePrice")
    bc += as2_push_int(0)
    bc += as2_set_variable()

    bc += as2_push_string("effectivePrice")
    bc += as2_push_int(0)
    bc += as2_set_variable()

    bc += as2_push_string("sliderMin")
    bc += as2_push_int(-30)
    bc += as2_set_variable()

    bc += as2_push_string("sliderMax")
    bc += as2_push_int(30)
    bc += as2_set_variable()

    bc += as2_push_string("sliderValue")
    bc += as2_push_int(0)
    bc += as2_set_variable()

    bc += as2_push_string("buttonState")
    bc += as2_push_string("offer")
    bc += as2_set_variable()

    bc += as2_push_string("focusIndex")
    bc += as2_push_int(0)
    bc += as2_set_variable()

    bc += as2_push_string("initialized")
    bc += as2_push_bool(True)
    bc += as2_set_variable()

    bc += as2_push_string("version")
    bc += as2_push_string("DynamicBartering 2.0.0")
    bc += as2_set_variable()

    # --- notifyHost ---
    notify_body = bytearray()
    notify_body += as2_push_register(3)
    notify_body += as2_push_register(2)
    notify_body += as2_push_int(2)
    notify_body += as2_push_string("call")
    notify_body += as2_push_string("gfx")
    notify_body += as2_get_variable()
    notify_body += as2_push_string("io")
    notify_body += b'\x4E'
    notify_body += as2_push_string("GameDelegate")
    notify_body += b'\x4E'
    notify_body += b'\x52'  # callMethod
    notify_body += as2_pop()
    notify_body += as2_push_bool(True)
    notify_body += b'\x3E'  # return

    bc += as2_push_string("notifyHost")
    bc += as2_define_function2("notifyHost", [(2, "name"), (3, "args")], 4, bytes(notify_body))
    bc += as2_set_variable()

    # --- SetOfferData ---
    offer_body = bytearray()
    offer_body += as2_push_string("state")
    offer_body += as2_push_string("offer")
    offer_body += as2_set_variable()

    offer_body += as2_push_string("basePrice")
    offer_body += as2_push_register(3)
    offer_body += as2_set_variable()
    offer_body += as2_push_string("effectivePrice")
    offer_body += as2_push_register(4)
    offer_body += as2_set_variable()
    offer_body += as2_push_string("offeredPrice")
    offer_body += as2_push_register(4)
    offer_body += as2_set_variable()

    offer_body += as2_push_string("sliderMin")
    offer_body += as2_push_register(8)
    offer_body += as2_set_variable()
    offer_body += as2_push_string("sliderMax")
    offer_body += as2_push_register(9)
    offer_body += as2_set_variable()
    # Start slider at effectivePrice (default/market price)
    offer_body += as2_push_string("sliderValue")
    offer_body += as2_push_register(4)
    offer_body += as2_set_variable()

    # Store acceptance chance for dynamic updates
    offer_body += as2_push_string("acceptanceChance")
    offer_body += as2_push_register(11)
    offer_body += as2_set_variable()

    # Set button state to offer
    offer_body += as2_push_string("offer")
    offer_body += as2_push_int(1)
    offer_body += as2_push_string("setButtonState")
    offer_body += as2_call_function()
    offer_body += as2_pop()

    # Update slider display
    offer_body += as2_push_int(0)
    offer_body += as2_push_string("updateSliderDisplay")
    offer_body += as2_call_function()
    offer_body += as2_pop()

    bc += as2_push_string("SetOfferData")
    bc += as2_define_function2("SetOfferData",
        [(2, "itemName"), (3, "basePrice"), (4, "effectivePrice"),
         (5, "merchantName"), (6, "personalityName"), (7, "relationship"),
         (8, "sliderMin"), (9, "sliderMax"), (10, "hasIntimidation"), (11, "acceptChance")], 12, bytes(offer_body))
    bc += as2_set_variable()

    # --- ShowCounterOffer ---
    counter_body = bytearray()
    counter_body += as2_push_string("state")
    counter_body += as2_push_string("counter")
    counter_body += as2_set_variable()

    counter_body += as2_push_string("counter")
    counter_body += as2_push_int(1)
    counter_body += as2_push_string("setButtonState")
    counter_body += as2_call_function()
    counter_body += as2_pop()

    bc += as2_push_string("ShowCounterOffer")
    bc += as2_define_function2("ShowCounterOffer",
        [(2, "amount"), (3, "patience")], 4, bytes(counter_body))
    bc += as2_set_variable()

    # --- ShowResult ---
    result_body = bytearray()
    result_body += as2_push_string("state")
    result_body += as2_push_string("result")
    result_body += as2_set_variable()

    result_body += as2_push_string("result")
    result_body += as2_push_int(1)
    result_body += as2_push_string("setButtonState")
    result_body += as2_call_function()
    result_body += as2_pop()

    bc += as2_push_string("ShowResult")
    bc += as2_define_function2("ShowResult",
        [(2, "accepted"), (3, "relDelta")], 4, bytes(result_body))
    bc += as2_set_variable()

    # --- updateSliderDisplay ---
    slider_disp_body = bytearray()

    # Safety clamp: sliderValue = max(sliderMin, min(sliderMax, sliderValue))
    # if sliderValue < sliderMin: sliderValue = sliderMin
    slider_disp_body += as2_push_string("sliderValue")
    slider_disp_body += as2_get_variable()
    slider_disp_body += as2_push_string("sliderMin")
    slider_disp_body += as2_get_variable()
    slider_disp_body += b'\x48'  # Less2: sliderValue < sliderMin?
    slider_disp_body += b'\x12'  # Not: sliderValue >= sliderMin?
    clamp_lo = as2_push_string("sliderValue") + as2_push_string("sliderMin") + as2_get_variable() + as2_set_variable()
    slider_disp_body += b'\x99' + struct.pack('<H', 2) + struct.pack('<h', len(clamp_lo))
    slider_disp_body += clamp_lo
    # if sliderValue > sliderMax: sliderValue = sliderMax
    slider_disp_body += as2_push_string("sliderMax")
    slider_disp_body += as2_get_variable()
    slider_disp_body += as2_push_string("sliderValue")
    slider_disp_body += as2_get_variable()
    slider_disp_body += b'\x48'  # Less2: sliderMax < sliderValue?
    slider_disp_body += b'\x12'  # Not: sliderValue <= sliderMax?
    clamp_hi = as2_push_string("sliderValue") + as2_push_string("sliderMax") + as2_get_variable() + as2_set_variable()
    slider_disp_body += b'\x99' + struct.pack('<H', 2) + struct.pack('<h', len(clamp_hi))
    slider_disp_body += clamp_hi

    # offeredPrice = sliderValue (direct gold value, no percentage conversion)
    slider_disp_body += as2_push_string("offeredPrice")
    slider_disp_body += as2_push_string("sliderValue")
    slider_disp_body += as2_get_variable()
    slider_disp_body += as2_push_int(1)
    slider_disp_body += as2_push_string("Math")
    slider_disp_body += as2_get_variable()
    slider_disp_body += as2_push_string("round")
    slider_disp_body += b'\x52'
    slider_disp_body += as2_set_variable()

    # sliderInt not needed anymore (offeredPrice is already integer gold)

    # Update PriceText: just show "X gold"
    slider_disp_body += as2_push_string("_root")
    slider_disp_body += as2_get_variable()
    slider_disp_body += as2_push_string("PriceText")
    slider_disp_body += b'\x4E'  # getMember → _root.PriceText (TextField object)
    slider_disp_body += as2_push_string("htmlText")
    slider_disp_body += as2_push_string("<font face='$EverywhereBoldFont' size='22' color='#DAA520'>")
    slider_disp_body += as2_push_string("offeredPrice")
    slider_disp_body += as2_get_variable()
    slider_disp_body += b'\x21'  # StringAdd
    slider_disp_body += as2_push_string(" gold</font>")
    slider_disp_body += b'\x21'  # StringAdd
    slider_disp_body += b'\x4F'  # setMember → _root.PriceText.htmlText = value

    # Update slider handle position via _root.sliderMC.handle._x
    slider_disp_body += as2_push_string("_root")
    slider_disp_body += as2_get_variable()
    slider_disp_body += as2_push_string("sliderMC")
    slider_disp_body += b'\x4E'
    slider_disp_body += as2_push_string("handle")
    slider_disp_body += b'\x4E'
    slider_disp_body += as2_push_string("_x")
    # Map sliderValue to pixel: (sliderValue - sliderMin) / (sliderMax - sliderMin) * trackWidth
    slider_disp_body += as2_push_string("sliderValue")
    slider_disp_body += as2_get_variable()
    slider_disp_body += as2_push_string("sliderMin")
    slider_disp_body += as2_get_variable()
    slider_disp_body += b'\x0B'  # subtract
    slider_disp_body += as2_push_string("sliderMax")
    slider_disp_body += as2_get_variable()
    slider_disp_body += as2_push_string("sliderMin")
    slider_disp_body += as2_get_variable()
    slider_disp_body += b'\x0B'  # subtract (max - min)
    slider_disp_body += b'\x0D'  # divide
    slider_disp_body += as2_push_int(200)
    slider_disp_body += b'\x0C'  # multiply → pixel pos
    slider_disp_body += b'\x4F'  # setMember

    bc += as2_push_string("updateSliderDisplay")
    bc += as2_define_function2("updateSliderDisplay", [], 2, bytes(slider_disp_body))
    bc += as2_set_variable()

    # --- adjustSlider ---
    adjust_body = bytearray()
    adjust_body += as2_push_string("sliderValue")
    adjust_body += as2_push_string("sliderValue")
    adjust_body += as2_get_variable()
    adjust_body += as2_push_register(2)
    adjust_body += b'\x0A'  # add
    adjust_body += as2_set_variable()
    # Clamp min
    adjust_body += as2_push_string("sliderValue")
    adjust_body += as2_get_variable()
    adjust_body += as2_push_string("sliderMin")
    adjust_body += as2_get_variable()
    adjust_body += b'\x48'  # Less2
    adjust_body += b'\x12'  # Not
    action_clamp_min = (as2_push_string("sliderValue") +
                        as2_push_string("sliderMin") + as2_get_variable() +
                        as2_set_variable())
    adjust_body += b'\x99' + struct.pack('<H', 2) + struct.pack('<h', len(action_clamp_min))
    adjust_body += action_clamp_min
    # Clamp max
    adjust_body += as2_push_string("sliderMax")
    adjust_body += as2_get_variable()
    adjust_body += as2_push_string("sliderValue")
    adjust_body += as2_get_variable()
    adjust_body += b'\x48'  # Less2
    adjust_body += b'\x12'  # Not
    action_clamp_max = (as2_push_string("sliderValue") +
                        as2_push_string("sliderMax") + as2_get_variable() +
                        as2_set_variable())
    adjust_body += b'\x99' + struct.pack('<H', 2) + struct.pack('<h', len(action_clamp_max))
    adjust_body += action_clamp_max
    # Update display
    adjust_body += as2_push_int(0)
    adjust_body += as2_push_string("updateSliderDisplay")
    adjust_body += as2_call_function()
    adjust_body += as2_pop()

    bc += as2_push_string("adjustSlider")
    bc += as2_define_function2("adjustSlider", [(2, "delta")], 3, bytes(adjust_body))
    bc += as2_set_variable()

    # --- setButtonState ---
    # Hides all buttons, shows relevant set, resets focus
    btn_state_body = bytearray()
    btn_state_body += as2_push_string("buttonState")
    btn_state_body += as2_push_register(2)
    btn_state_body += as2_set_variable()
    btn_state_body += as2_push_string("focusIndex")
    btn_state_body += as2_push_int(0)
    btn_state_body += as2_set_variable()

    # Hide all buttons: set _visible = false on each
    for btn_name in ["btn_submit", "btn_intimidate", "btn_cancel",
                     "btn_accept", "btn_reoffer", "btn_walkaway", "btn_continue"]:
        btn_state_body += as2_push_string("_root")
        btn_state_body += as2_get_variable()
        btn_state_body += as2_push_string(btn_name)
        btn_state_body += b'\x4E'
        btn_state_body += as2_push_string("_visible")
        btn_state_body += as2_push_bool(False)
        btn_state_body += b'\x4F'

    # Show based on state: check register 2 (state param)
    # if state == "offer": show submit, intimidate, cancel
    btn_state_body += as2_push_register(2)
    btn_state_body += as2_push_string("offer")
    btn_state_body += b'\x49'  # StrictEquals
    btn_state_body += b'\x12'  # Not
    offer_show = bytearray()
    for btn_name in ["btn_submit", "btn_intimidate", "btn_cancel"]:
        offer_show += as2_push_string("_root")
        offer_show += as2_get_variable()
        offer_show += as2_push_string(btn_name)
        offer_show += b'\x4E'
        offer_show += as2_push_string("_visible")
        offer_show += as2_push_bool(True)
        offer_show += b'\x4F'
    btn_state_body += b'\x99' + struct.pack('<H', 2) + struct.pack('<h', len(offer_show))
    btn_state_body += offer_show

    # if state == "counter": show accept, reoffer, walkaway
    btn_state_body += as2_push_register(2)
    btn_state_body += as2_push_string("counter")
    btn_state_body += b'\x49'
    btn_state_body += b'\x12'
    counter_show = bytearray()
    for btn_name in ["btn_accept", "btn_reoffer", "btn_walkaway"]:
        counter_show += as2_push_string("_root")
        counter_show += as2_get_variable()
        counter_show += as2_push_string(btn_name)
        counter_show += b'\x4E'
        counter_show += as2_push_string("_visible")
        counter_show += as2_push_bool(True)
        counter_show += b'\x4F'
    btn_state_body += b'\x99' + struct.pack('<H', 2) + struct.pack('<h', len(counter_show))
    btn_state_body += counter_show

    # if state == "result": show continue
    btn_state_body += as2_push_register(2)
    btn_state_body += as2_push_string("result")
    btn_state_body += b'\x49'
    btn_state_body += b'\x12'
    result_show = bytearray()
    result_show += as2_push_string("_root")
    result_show += as2_get_variable()
    result_show += as2_push_string("btn_continue")
    result_show += b'\x4E'
    result_show += as2_push_string("_visible")
    result_show += as2_push_bool(True)
    result_show += b'\x4F'
    btn_state_body += b'\x99' + struct.pack('<H', 2) + struct.pack('<h', len(result_show))
    btn_state_body += result_show

    # Call setFocusedButton(0)
    btn_state_body += as2_push_int(0)
    btn_state_body += as2_push_int(1)
    btn_state_body += as2_push_string("setFocusedButton")
    btn_state_body += as2_call_function()
    btn_state_body += as2_pop()

    bc += as2_push_string("setButtonState")
    bc += as2_define_function2("setButtonState", [(2, "state")], 3, bytes(btn_state_body))
    bc += as2_set_variable()

    # --- setFocusedButton ---
    focus_body = bytearray()
    focus_body += as2_push_string("focusIndex")
    focus_body += as2_push_register(2)
    focus_body += as2_set_variable()

    # Reset all buttons to normal state (bgNormal visible, bgHighlight hidden, full opacity)
    for btn_name in ["btn_submit", "btn_intimidate", "btn_cancel",
                     "btn_accept", "btn_reoffer", "btn_walkaway", "btn_continue"]:
        # btn._alpha = 100 (full opacity - don't dim buttons)
        focus_body += as2_push_string("_root")
        focus_body += as2_get_variable()
        focus_body += as2_push_string(btn_name)
        focus_body += b'\x4E'
        focus_body += as2_push_string("_alpha")
        focus_body += as2_push_int(100)
        focus_body += b'\x4F'
        # btn.bgNormal._visible = true
        focus_body += as2_push_string("_root")
        focus_body += as2_get_variable()
        focus_body += as2_push_string(btn_name)
        focus_body += b'\x4E'
        focus_body += as2_push_string("bgNormal")
        focus_body += b'\x4E'
        focus_body += as2_push_string("_visible")
        focus_body += as2_push_bool(True)
        focus_body += b'\x4F'
        # btn.bgHighlight._visible = false
        focus_body += as2_push_string("_root")
        focus_body += as2_get_variable()
        focus_body += as2_push_string(btn_name)
        focus_body += b'\x4E'
        focus_body += as2_push_string("bgHighlight")
        focus_body += b'\x4E'
        focus_body += as2_push_string("_visible")
        focus_body += as2_push_bool(False)
        focus_body += b'\x4F'

    # Highlight the focused button based on state and index
    offer_btns = ["btn_submit", "btn_intimidate", "btn_cancel"]
    counter_btns = ["btn_accept", "btn_reoffer", "btn_walkaway"]
    result_btns = ["btn_continue"]

    for state_name, btn_list in [("offer", offer_btns), ("counter", counter_btns), ("result", result_btns)]:
        focus_body += as2_push_string("buttonState")
        focus_body += as2_get_variable()
        focus_body += as2_push_string(state_name)
        focus_body += b'\x49'
        focus_body += b'\x12'  # Not (skip if not equal)

        highlight_block = bytearray()
        for i, btn_name in enumerate(btn_list):
            highlight_block += as2_push_register(2)  # focusIndex
            highlight_block += as2_push_int(i)
            highlight_block += b'\x49'  # StrictEquals
            highlight_block += b'\x12'  # Not
            # If focused: set alpha=100, bgNormal hidden, bgHighlight visible
            set_highlight = bytearray()
            # btn._alpha = 100
            set_highlight += as2_push_string("_root")
            set_highlight += as2_get_variable()
            set_highlight += as2_push_string(btn_name)
            set_highlight += b'\x4E'
            set_highlight += as2_push_string("_alpha")
            set_highlight += as2_push_int(100)
            set_highlight += b'\x4F'
            # btn.bgNormal._visible = false
            set_highlight += as2_push_string("_root")
            set_highlight += as2_get_variable()
            set_highlight += as2_push_string(btn_name)
            set_highlight += b'\x4E'
            set_highlight += as2_push_string("bgNormal")
            set_highlight += b'\x4E'
            set_highlight += as2_push_string("_visible")
            set_highlight += as2_push_bool(False)
            set_highlight += b'\x4F'
            # btn.bgHighlight._visible = true
            set_highlight += as2_push_string("_root")
            set_highlight += as2_get_variable()
            set_highlight += as2_push_string(btn_name)
            set_highlight += b'\x4E'
            set_highlight += as2_push_string("bgHighlight")
            set_highlight += b'\x4E'
            set_highlight += as2_push_string("_visible")
            set_highlight += as2_push_bool(True)
            set_highlight += b'\x4F'

            highlight_block += b'\x99' + struct.pack('<H', 2) + struct.pack('<h', len(set_highlight))
            highlight_block += set_highlight

        focus_body += b'\x99' + struct.pack('<H', 2) + struct.pack('<h', len(highlight_block))
        focus_body += highlight_block

    bc += as2_push_string("setFocusedButton")
    bc += as2_define_function2("setFocusedButton", [(2, "idx")], 3, bytes(focus_body))
    bc += as2_set_variable()

    # --- navigateButtons ---
    nav_body = bytearray()
    # Determine button count based on state
    # count = (buttonState == "result") ? 1 : 3
    # focusIndex = (focusIndex + direction + count) % count
    nav_body += as2_push_string("focusIndex")
    nav_body += as2_push_string("focusIndex")
    nav_body += as2_get_variable()
    nav_body += as2_push_register(2)  # direction
    nav_body += b'\x0A'  # add
    nav_body += as2_push_int(3)
    nav_body += b'\x0A'  # add +3 to keep positive
    nav_body += as2_push_int(3)
    nav_body += b'\x3F'  # modulo
    nav_body += as2_set_variable()

    nav_body += as2_push_string("focusIndex")
    nav_body += as2_get_variable()
    nav_body += as2_push_int(1)
    nav_body += as2_push_string("setFocusedButton")
    nav_body += as2_call_function()
    nav_body += as2_pop()

    bc += as2_push_string("navigateButtons")
    bc += as2_define_function2("navigateButtons", [(2, "direction")], 3, bytes(nav_body))
    bc += as2_set_variable()

    # --- onAcceptKey ---
    accept_key_body = bytearray()
    # if state == "offer" → SubmitOffer
    accept_key_body += as2_push_string("state")
    accept_key_body += as2_get_variable()
    accept_key_body += as2_push_string("offer")
    accept_key_body += b'\x49'
    accept_key_body += b'\x12'
    action_submit = (as2_push_int(0) + as2_push_string("SubmitOffer") +
                     as2_call_function() + as2_pop() + b'\x3E')
    accept_key_body += b'\x99' + struct.pack('<H', 2) + struct.pack('<h', len(action_submit))
    accept_key_body += action_submit
    # if state == "counter" → AcceptCounter
    accept_key_body += as2_push_string("state")
    accept_key_body += as2_get_variable()
    accept_key_body += as2_push_string("counter")
    accept_key_body += b'\x49'
    accept_key_body += b'\x12'
    action_accept = (as2_push_int(0) + as2_push_string("AcceptCounter") +
                     as2_call_function() + as2_pop() + b'\x3E')
    accept_key_body += b'\x99' + struct.pack('<H', 2) + struct.pack('<h', len(action_accept))
    accept_key_body += action_accept
    # default (result) → CloseMenu
    accept_key_body += as2_push_int(0) + as2_push_string("CloseMenu") + as2_call_function() + as2_pop()

    bc += as2_push_string("onAcceptKey")
    bc += as2_define_function2("onAcceptKey", [], 2, bytes(accept_key_body))
    bc += as2_set_variable()

    # --- handleInput ---
    input_body = bytearray()
    input_body += as2_push_register(2)
    input_body += as2_push_string("code")
    input_body += b'\x4E'
    input_body += b'\x87' + struct.pack('<H', 1) + struct.pack('<B', 3)  # storeRegister(3)
    input_body += as2_pop()

    def make_key_check(key_code, action_bytes):
        block = bytearray()
        block += as2_push_register(3)
        block += as2_push_int(key_code)
        block += b'\x49'  # StrictEquals
        block += b'\x12'  # Not
        block += b'\x99' + struct.pack('<H', 2) + struct.pack('<h', len(action_bytes))
        block += action_bytes
        return bytes(block)

    # Left (37) → adjustSlider(-1)
    action_left = (as2_push_int(-1) + as2_push_int(1) +
                   as2_push_string("adjustSlider") + as2_call_function() +
                   as2_pop() + as2_push_bool(True) + b'\x3E')
    input_body += make_key_check(37, action_left)

    # Right (39) → adjustSlider(1)
    action_right = (as2_push_int(1) + as2_push_int(1) +
                    as2_push_string("adjustSlider") + as2_call_function() +
                    as2_pop() + as2_push_bool(True) + b'\x3E')
    input_body += make_key_check(39, action_right)

    # E (69) → onAcceptKey
    action_e = (as2_push_int(0) + as2_push_string("onAcceptKey") +
                as2_call_function() + as2_pop() + as2_push_bool(True) + b'\x3E')
    input_body += make_key_check(69, action_e)

    # Enter (13) → onAcceptKey
    input_body += make_key_check(13, action_e)

    # Tab (9) → cancel/walkaway
    action_tab = (as2_push_int(0) + as2_push_string("onCancelKey") +
                  as2_call_function() + as2_pop() + as2_push_bool(True) + b'\x3E')
    input_body += make_key_check(9, action_tab)

    # Escape (27)
    input_body += make_key_check(27, action_tab)

    # I (73) → intimidate
    action_i = (as2_push_int(0) + as2_push_string("IntimidateAttempt") +
                as2_call_function() + as2_pop() + as2_push_bool(True) + b'\x3E')
    input_body += make_key_check(73, action_i)

    # R (82) → re-offer
    action_r = (as2_push_int(0) + as2_push_string("ReOffer") +
                as2_call_function() + as2_pop() + as2_push_bool(True) + b'\x3E')
    input_body += make_key_check(82, action_r)

    # Up (38) → navigateButtons(-1)
    action_up = (as2_push_int(-1) + as2_push_int(1) +
                 as2_push_string("navigateButtons") + as2_call_function() +
                 as2_pop() + as2_push_bool(True) + b'\x3E')
    input_body += make_key_check(38, action_up)

    # Down (40) → navigateButtons(1)
    action_down = (as2_push_int(1) + as2_push_int(1) +
                   as2_push_string("navigateButtons") + as2_call_function() +
                   as2_pop() + as2_push_bool(True) + b'\x3E')
    input_body += make_key_check(40, action_down)

    # Default: unhandled
    input_body += as2_push_bool(False)
    input_body += b'\x3E'

    bc += as2_push_string("handleInput")
    bc += as2_define_function2("handleInput",
        [(2, "details"), (3, "pathToFocus")], 4, bytes(input_body))
    bc += as2_set_variable()

    # --- onCancelKey ---
    cancel_body = bytearray()
    cancel_body += as2_push_string("state")
    cancel_body += as2_get_variable()
    cancel_body += as2_push_string("counter")
    cancel_body += b'\x49'
    cancel_body += b'\x12'
    action_reject = (as2_push_int(0) + as2_push_string("RejectCounter") +
                     as2_call_function() + as2_pop() + b'\x3E')
    cancel_body += b'\x99' + struct.pack('<H', 2) + struct.pack('<h', len(action_reject))
    cancel_body += action_reject
    cancel_body += as2_push_int(0) + as2_push_string("CloseMenu") + as2_call_function() + as2_pop()

    bc += as2_push_string("onCancelKey")
    bc += as2_define_function2("onCancelKey", [], 2, bytes(cancel_body))
    bc += as2_set_variable()

    # --- SubmitOffer ---
    submit_body = bytearray()
    # Use sliderValue (which IS the offered gold amount now)
    submit_body += as2_push_string("sliderValue")
    submit_body += as2_get_variable()
    submit_body += as2_push_int(1)
    submit_body += as2_push_string("Math")
    submit_body += as2_get_variable()
    submit_body += as2_push_string("round")
    submit_body += b'\x52'
    submit_body += as2_push_int(1)
    submit_body += b'\x42'  # initArray → [Math.round(sliderValue)]
    submit_body += as2_push_string("OfferSubmit")
    submit_body += as2_push_int(2)
    submit_body += as2_push_string("notifyHost")
    submit_body += as2_call_function()
    submit_body += as2_pop()

    bc += as2_push_string("SubmitOffer")
    bc += as2_define_function2("SubmitOffer", [], 2, bytes(submit_body))
    bc += as2_set_variable()

    # --- AcceptCounter ---
    accept_counter_body = bytearray()
    accept_counter_body += as2_push_int(0)
    accept_counter_body += as2_push_int(1)
    accept_counter_body += b'\x42'
    accept_counter_body += as2_push_string("CounterResponse")
    accept_counter_body += as2_push_int(2)
    accept_counter_body += as2_push_string("notifyHost")
    accept_counter_body += as2_call_function()
    accept_counter_body += as2_pop()

    bc += as2_push_string("AcceptCounter")
    bc += as2_define_function2("AcceptCounter", [], 2, bytes(accept_counter_body))
    bc += as2_set_variable()

    # --- RejectCounter ---
    reject_body = bytearray()
    reject_body += as2_push_int(2)
    reject_body += as2_push_int(1)
    reject_body += b'\x42'
    reject_body += as2_push_string("CounterResponse")
    reject_body += as2_push_int(2)
    reject_body += as2_push_string("notifyHost")
    reject_body += as2_call_function()
    reject_body += as2_pop()

    bc += as2_push_string("RejectCounter")
    bc += as2_define_function2("RejectCounter", [], 2, bytes(reject_body))
    bc += as2_set_variable()

    # --- ReOffer ---
    reoffer_body = bytearray()
    reoffer_body += as2_push_int(1)
    reoffer_body += as2_push_int(1)
    reoffer_body += b'\x42'
    reoffer_body += as2_push_string("CounterResponse")
    reoffer_body += as2_push_int(2)
    reoffer_body += as2_push_string("notifyHost")
    reoffer_body += as2_call_function()
    reoffer_body += as2_pop()

    bc += as2_push_string("ReOffer")
    bc += as2_define_function2("ReOffer", [], 2, bytes(reoffer_body))
    bc += as2_set_variable()

    # --- IntimidateAttempt ---
    intimidate_body = bytearray()
    intimidate_body += as2_push_null()
    intimidate_body += as2_push_string("IntimidateAttempt")
    intimidate_body += as2_push_int(2)
    intimidate_body += as2_push_string("notifyHost")
    intimidate_body += as2_call_function()
    intimidate_body += as2_pop()

    bc += as2_push_string("IntimidateAttempt")
    bc += as2_define_function2("IntimidateAttempt", [], 2, bytes(intimidate_body))
    bc += as2_set_variable()

    # --- CloseMenu ---
    close_body = bytearray()
    close_body += as2_push_null()
    close_body += as2_push_string("CloseMenu")
    close_body += as2_push_int(2)
    close_body += as2_push_string("notifyHost")
    close_body += as2_call_function()
    close_body += as2_pop()

    bc += as2_push_string("CloseMenu")
    bc += as2_define_function2("CloseMenu", [], 2, bytes(close_body))
    bc += as2_set_variable()

    # --- Button onPress/onRelease handlers ---
    btn_actions = {
        "btn_submit": "SubmitOffer",
        "btn_intimidate": "IntimidateAttempt",
        "btn_cancel": "CloseMenu",
        "btn_accept": "AcceptCounter",
        "btn_reoffer": "ReOffer",
        "btn_walkaway": "RejectCounter",
        "btn_continue": "CloseMenu",
    }
    for btn_name, func_name in btn_actions.items():
        handler = as2_push_int(0) + as2_push_string(func_name) + as2_call_function() + as2_pop()
        bc += as2_push_string("_root")
        bc += as2_get_variable()
        bc += as2_push_string(btn_name)
        bc += b'\x4E'
        bc += as2_push_string("onPress")
        bc += as2_define_function2("", [], 2, bytes(handler))
        bc += b'\x4F'

        bc += as2_push_string("_root")
        bc += as2_get_variable()
        bc += as2_push_string(btn_name)
        bc += b'\x4E'
        bc += as2_push_string("onRelease")
        bc += as2_define_function2("", [], 2, bytes(handler))
        bc += b'\x4F'

    # --- Button onRollOver/onRollOut for mouse hover highlight ---
    for btn_name in btn_actions.keys():
        # onRollOver: show highlight bg, hide normal bg (register 1 = this)
        rollover = bytearray()
        # this.bgNormal._visible = false
        rollover += as2_push_register(1)
        rollover += as2_push_string("bgNormal")
        rollover += b'\x4E'  # getMember → this.bgNormal
        rollover += as2_push_string("_visible")
        rollover += as2_push_bool(False)
        rollover += b'\x4F'  # setMember
        # this.bgHighlight._visible = true
        rollover += as2_push_register(1)
        rollover += as2_push_string("bgHighlight")
        rollover += b'\x4E'
        rollover += as2_push_string("_visible")
        rollover += as2_push_bool(True)
        rollover += b'\x4F'
        # this._alpha = 100
        rollover += as2_push_register(1)
        rollover += as2_push_string("_alpha")
        rollover += as2_push_int(100)
        rollover += b'\x4F'

        bc += as2_push_string("_root")
        bc += as2_get_variable()
        bc += as2_push_string(btn_name)
        bc += b'\x4E'
        bc += as2_push_string("onRollOver")
        bc += as2_define_function2("", [], 2, bytes(rollover))
        bc += b'\x4F'

        # onRollOut: show normal bg, hide highlight bg
        rollout = bytearray()
        # this.bgNormal._visible = true
        rollout += as2_push_register(1)
        rollout += as2_push_string("bgNormal")
        rollout += b'\x4E'
        rollout += as2_push_string("_visible")
        rollout += as2_push_bool(True)
        rollout += b'\x4F'
        # this.bgHighlight._visible = false
        rollout += as2_push_register(1)
        rollout += as2_push_string("bgHighlight")
        rollout += b'\x4E'
        rollout += as2_push_string("_visible")
        rollout += as2_push_bool(False)
        rollout += b'\x4F'

        bc += as2_push_string("_root")
        bc += as2_get_variable()
        bc += as2_push_string(btn_name)
        bc += b'\x4E'
        bc += as2_push_string("onRollOut")
        bc += as2_define_function2("", [], 2, bytes(rollout))
        bc += b'\x4F'

    # --- Slider mouse interaction ---
    # _root.dragging = false
    bc += as2_push_string("dragging")
    bc += as2_push_bool(False)
    bc += as2_set_variable()

    # sliderMC.onPress: calculate position from _xmouse, set sliderValue, update
    slider_press = bytearray()
    # localX = _root.sliderMC._xmouse
    slider_press += as2_push_string("localX")
    slider_press += as2_push_string("_root")
    slider_press += as2_get_variable()
    slider_press += as2_push_string("sliderMC")
    slider_press += b'\x4E'
    slider_press += as2_push_string("_xmouse")
    slider_press += b'\x4E'
    slider_press += as2_set_variable()
    # Clamp localX to [0, 200]
    slider_press += as2_push_string("localX")
    slider_press += as2_get_variable()
    slider_press += as2_push_int(0)
    slider_press += b'\x48'  # Less2
    slider_press += b'\x12'  # Not → true if localX >= 0
    clamp_low_p = as2_push_string("localX") + as2_push_int(0) + as2_set_variable()
    slider_press += b'\x99' + struct.pack('<H', 2) + struct.pack('<h', len(clamp_low_p))
    slider_press += clamp_low_p
    slider_press += as2_push_int(200)
    slider_press += as2_push_string("localX")
    slider_press += as2_get_variable()
    slider_press += b'\x48'  # Less2
    slider_press += b'\x12'  # Not → true if localX < 200
    clamp_high_p = as2_push_string("localX") + as2_push_int(200) + as2_set_variable()
    slider_press += b'\x99' + struct.pack('<H', 2) + struct.pack('<h', len(clamp_high_p))
    slider_press += clamp_high_p
    # sliderValue = sliderMin + (sliderMax - sliderMin) * (localX / 200)
    slider_press += as2_push_string("sliderValue")
    slider_press += as2_push_string("sliderMin")
    slider_press += as2_get_variable()
    slider_press += as2_push_string("sliderMax")
    slider_press += as2_get_variable()
    slider_press += as2_push_string("sliderMin")
    slider_press += as2_get_variable()
    slider_press += b'\x0B'  # subtract (max - min)
    slider_press += as2_push_string("localX")
    slider_press += as2_get_variable()
    slider_press += as2_push_int(200)
    slider_press += b'\x0D'  # divide
    slider_press += b'\x0C'  # multiply
    slider_press += b'\x0A'  # add (min + (max-min)*pct)
    slider_press += as2_set_variable()
    # Update display
    slider_press += as2_push_int(0)
    slider_press += as2_push_string("updateSliderDisplay")
    slider_press += as2_call_function()
    slider_press += as2_pop()
    # dragging = true
    slider_press += as2_push_string("dragging")
    slider_press += as2_push_bool(True)
    slider_press += as2_set_variable()

    bc += as2_push_string("_root")
    bc += as2_get_variable()
    bc += as2_push_string("sliderMC")
    bc += b'\x4E'
    bc += as2_push_string("onPress")
    bc += as2_define_function2("", [], 2, bytes(slider_press))
    bc += b'\x4F'

    # sliderMC.onRelease / onReleaseOutside: dragging = false
    slider_release = as2_push_string("dragging") + as2_push_bool(False) + as2_set_variable()
    for ev in ["onRelease", "onReleaseOutside"]:
        bc += as2_push_string("_root")
        bc += as2_get_variable()
        bc += as2_push_string("sliderMC")
        bc += b'\x4E'
        bc += as2_push_string(ev)
        bc += as2_define_function2("", [], 2, bytes(slider_release))
        bc += b'\x4F'

    # _root.onEnterFrame: if dragging, update slider from current mouse position
    # (onEnterFrame is more reliable than onMouseMove in Scaleform GFx)
    mouse_move = bytearray()
    # if (!dragging) return
    mouse_move += as2_push_string("dragging")
    mouse_move += as2_get_variable()
    mouse_move += b'\x12'  # Not
    skip_body = bytearray()
    # localX = _root.sliderMC._xmouse
    skip_body += as2_push_string("localX")
    skip_body += as2_push_string("_root")
    skip_body += as2_get_variable()
    skip_body += as2_push_string("sliderMC")
    skip_body += b'\x4E'
    skip_body += as2_push_string("_xmouse")
    skip_body += b'\x4E'
    skip_body += as2_set_variable()
    # Clamp localX to [0, 200]
    # if localX < 0: localX = 0
    skip_body += as2_push_string("localX")
    skip_body += as2_get_variable()
    skip_body += as2_push_int(0)
    skip_body += b'\x48'  # Less2: 0 < localX? NO → localX < 0
    skip_body += b'\x12'  # Not → true if localX >= 0
    clamp_low = as2_push_string("localX") + as2_push_int(0) + as2_set_variable()
    skip_body += b'\x99' + struct.pack('<H', 2) + struct.pack('<h', len(clamp_low))
    skip_body += clamp_low
    # if localX > 200: localX = 200
    skip_body += as2_push_int(200)
    skip_body += as2_push_string("localX")
    skip_body += as2_get_variable()
    skip_body += b'\x48'  # Less2: localX < 200? NO → localX >= 200
    skip_body += b'\x12'  # Not → true if localX < 200
    clamp_high = as2_push_string("localX") + as2_push_int(200) + as2_set_variable()
    skip_body += b'\x99' + struct.pack('<H', 2) + struct.pack('<h', len(clamp_high))
    skip_body += clamp_high
    # sliderValue = sliderMin + (sliderMax - sliderMin) * (localX / 200)
    skip_body += as2_push_string("sliderValue")
    skip_body += as2_push_string("sliderMin")
    skip_body += as2_get_variable()
    skip_body += as2_push_string("sliderMax")
    skip_body += as2_get_variable()
    skip_body += as2_push_string("sliderMin")
    skip_body += as2_get_variable()
    skip_body += b'\x0B'  # subtract
    skip_body += as2_push_string("localX")
    skip_body += as2_get_variable()
    skip_body += as2_push_int(200)
    skip_body += b'\x0D'  # divide
    skip_body += b'\x0C'  # multiply
    skip_body += b'\x0A'  # add
    skip_body += as2_set_variable()
    # update display
    skip_body += as2_push_int(0)
    skip_body += as2_push_string("updateSliderDisplay")
    skip_body += as2_call_function()
    skip_body += as2_pop()

    # If not dragging, jump over the skip_body
    mouse_move += b'\x99' + struct.pack('<H', 2) + struct.pack('<h', len(skip_body))
    mouse_move += skip_body

    bc += as2_push_string("_root")
    bc += as2_get_variable()
    bc += as2_push_string("onEnterFrame")
    bc += as2_define_function2("", [], 2, bytes(mouse_move))
    bc += b'\x4F'

    # --- Set button labels from AS using htmlText with font tags ---
    btn_labels = {
        "btn_submit": ("Submit Offer", "#DAA520"),
        "btn_intimidate": ("Intimidate", "#CC4444"),
        "btn_cancel": ("Cancel", "#A0A0A0"),
        "btn_accept": ("Accept", "#66CC66"),
        "btn_reoffer": ("Re-Offer", "#DAA520"),
        "btn_walkaway": ("Walk Away", "#A0A0A0"),
        "btn_continue": ("Continue", "#FFFFFF"),
    }
    for btn_name, (label, color) in btn_labels.items():
        html_str = "<p align='center'><font face='$EverywhereMediumFont' size='11' color='{}'>{}</font></p>".format(color, label)
        bc += as2_push_string("_root")
        bc += as2_get_variable()
        bc += as2_push_string(btn_name)
        bc += b'\x4E'  # getMember → _root.btn_name
        bc += as2_push_string("labelField")
        bc += b'\x4E'  # getMember → _root.btn_name.labelField
        bc += as2_push_string("htmlText")
        bc += as2_push_string(html_str)
        bc += b'\x4F'  # setMember → .htmlText = html_str

    # --- Enable useHandCursor on all buttons ---
    for btn_name in ["btn_submit", "btn_intimidate", "btn_cancel",
                     "btn_accept", "btn_reoffer", "btn_walkaway", "btn_continue"]:
        bc += as2_push_string("_root")
        bc += as2_get_variable()
        bc += as2_push_string(btn_name)
        bc += b'\x4E'
        bc += as2_push_string("useHandCursor")
        bc += as2_push_bool(True)
        bc += b'\x4F'

    # --- Focus capture ---
    bc += as2_push_int(0)
    bc += as2_push_string("_root")
    bc += as2_get_variable()
    bc += as2_push_int(2)
    bc += as2_push_string("setFocus")
    bc += as2_push_string("gfx")
    bc += as2_get_variable()
    bc += as2_push_string("managers")
    bc += b'\x4E'
    bc += as2_push_string("FocusHandler")
    bc += b'\x4E'
    bc += as2_push_string("instance")
    bc += b'\x4E'
    bc += b'\x52'
    bc += as2_pop()

    # --- Notify host that menu is ready ---
    bc += as2_push_null()
    bc += as2_push_string("DynBarter_MenuLoaded")
    bc += as2_push_int(2)
    bc += as2_push_string("notifyHost")
    bc += as2_call_function()
    bc += as2_pop()

    return bytes(bc)


# ===========================================================================
# SWF assembly
# ===========================================================================

def build_swf():
    """Build the complete SWF file."""
    tags = bytearray()

    tags += make_file_attributes()
    tags += make_set_background(0, 0, 0)

    # --- Font import ---
    tags += make_import_assets2("gfxfontlib.swf", [
        (FONT_MEDIUM_ID, "$EverywhereMediumFont"),
        (FONT_BOLD_ID, "$EverywhereBoldFont"),
    ])

    # --- Embed PNG bitmaps ---
    icons_dir = os.path.join(os.path.dirname(os.path.abspath(__file__)), "assets", "icons")
    tags += make_define_bits_lossless2(CHAR_BMP_ORNAMENT, os.path.join(icons_dir, "ornament_divider.png"))
    tags += make_define_bits_lossless2(CHAR_BMP_ARROW_LEFT, os.path.join(icons_dir, "arrow_left.png"))
    tags += make_define_bits_lossless2(CHAR_BMP_ARROW_RIGHT, os.path.join(icons_dir, "arrow_right.png"))
    tags += make_define_bits_lossless2(CHAR_BMP_GOLD_COIN, os.path.join(icons_dir, "gold_coin.png"))
    tags += make_define_bits_lossless2(CHAR_BMP_SLIDER_DIAMOND, os.path.join(icons_dir, "slider_diamond.png"))
    tags += make_define_bits_lossless2(CHAR_BMP_SEPARATOR, os.path.join(icons_dir, "separator.png"))
    tags += make_define_bits_lossless2(CHAR_BMP_CORNER_TL, os.path.join(icons_dir, "corner_tl.png"))
    tags += make_define_bits_lossless2(CHAR_BMP_CORNER_TR, os.path.join(icons_dir, "corner_tr.png"))
    tags += make_define_bits_lossless2(CHAR_BMP_CORNER_BL, os.path.join(icons_dir, "corner_bl.png"))
    tags += make_define_bits_lossless2(CHAR_BMP_CORNER_BR, os.path.join(icons_dir, "corner_br.png"))

    # Button icon bitmaps (64x64 source, display at 16x16)
    btn_icons_dir = os.path.join(icons_dir, "buttons")
    tags += make_define_bits_lossless2(CHAR_BTN_ICON_BMP_SUBMIT, os.path.join(btn_icons_dir, "btn_submit_highlight.png"))
    tags += make_define_bits_lossless2(CHAR_BTN_ICON_BMP_INTIMIDATE, os.path.join(btn_icons_dir, "btn_intimidate_highlight.png"))
    tags += make_define_bits_lossless2(CHAR_BTN_ICON_BMP_CANCEL, os.path.join(btn_icons_dir, "btn_cancel_highlight.png"))
    tags += make_define_bits_lossless2(CHAR_BTN_ICON_BMP_ACCEPT, os.path.join(btn_icons_dir, "btn_accept_highlight.png"))
    tags += make_define_bits_lossless2(CHAR_BTN_ICON_BMP_REOFFER, os.path.join(btn_icons_dir, "btn_reoffer_highlight.png"))
    tags += make_define_bits_lossless2(CHAR_BTN_ICON_BMP_WALKAWAY, os.path.join(btn_icons_dir, "btn_walkaway_highlight.png"))
    tags += make_define_bits_lossless2(CHAR_BTN_ICON_BMP_CONTINUE, os.path.join(btn_icons_dir, "btn_continue_highlight.png"))

    # Wrap bitmaps in shapes for placement
    tags += make_define_shape_bitmap(CHAR_BMP_ORNAMENT_SHAPE, CHAR_BMP_ORNAMENT, 512, 64, display_w=200, display_h=16)
    tags += make_define_shape_bitmap(CHAR_BMP_COIN_SHAPE, CHAR_BMP_GOLD_COIN, 128, 128, display_w=18, display_h=18)
    tags += make_define_shape_bitmap(CHAR_BMP_ARROW_LEFT_SHAPE, CHAR_BMP_ARROW_LEFT, 128, 128, display_w=14, display_h=14)
    tags += make_define_shape_bitmap(CHAR_BMP_ARROW_RIGHT_SHAPE, CHAR_BMP_ARROW_RIGHT, 128, 128, display_w=14, display_h=14)

    # Button icon shapes (64x64 rendered at 16x16 display)
    tags += make_define_shape_bitmap(CHAR_BTN_ICON_SHAPE_SUBMIT, CHAR_BTN_ICON_BMP_SUBMIT, 64, 64, display_w=22, display_h=22)
    tags += make_define_shape_bitmap(CHAR_BTN_ICON_SHAPE_INTIMIDATE, CHAR_BTN_ICON_BMP_INTIMIDATE, 64, 64, display_w=22, display_h=22)
    tags += make_define_shape_bitmap(CHAR_BTN_ICON_SHAPE_CANCEL, CHAR_BTN_ICON_BMP_CANCEL, 64, 64, display_w=22, display_h=22)
    tags += make_define_shape_bitmap(CHAR_BTN_ICON_SHAPE_ACCEPT, CHAR_BTN_ICON_BMP_ACCEPT, 64, 64, display_w=22, display_h=22)
    tags += make_define_shape_bitmap(CHAR_BTN_ICON_SHAPE_REOFFER, CHAR_BTN_ICON_BMP_REOFFER, 64, 64, display_w=22, display_h=22)
    tags += make_define_shape_bitmap(CHAR_BTN_ICON_SHAPE_WALKAWAY, CHAR_BTN_ICON_BMP_WALKAWAY, 64, 64, display_w=22, display_h=22)
    tags += make_define_shape_bitmap(CHAR_BTN_ICON_SHAPE_CONTINUE, CHAR_BTN_ICON_BMP_CONTINUE, 64, 64, display_w=22, display_h=22)

    # --- Panel background (near-black) ---
    panel_w = 340
    panel_h = 400
    tags += make_define_shape(
        char_id=CHAR_PANEL_BG,
        fill_color=(12, 12, 14, 230),
        bounds=(0, panel_w, 0, panel_h),
    )

    # --- Panel border (warm off-white) ---
    tags += make_define_shape(
        char_id=CHAR_PANEL_BORDER,
        fill_color=(0, 0, 0, 0),  # transparent fill
        bounds=(0, panel_w, 0, panel_h),
        line_color=(160, 150, 130, 200),
        line_width=30,  # 1.5px visible border frame
    )

    # --- Ornament (decorative line with diamond endpoints) ---
    ornament_w = 160
    tags += make_define_shape_line(
        char_id=CHAR_ORNAMENT,
        width_px=ornament_w,
        thickness_px=1,
        color=(200, 195, 180, 100),
    )

    # --- Slider track (thin horizontal line, 200px) ---
    tags += make_define_shape_line(
        char_id=CHAR_SLIDER_TRACK,
        width_px=200,
        thickness_px=1,
        color=(128, 128, 128, 200),
    )

    # --- Slider handle (high-res diamond bitmap, 128px source → 12px display) ---
    tags += make_define_shape_bitmap(CHAR_SLIDER_HANDLE, CHAR_BMP_SLIDER_DIAMOND, 128, 128, display_w=14, display_h=14)

    # --- Slider hit area (transparent rectangle for mouse click detection) ---
    tags += make_define_shape(
        char_id=CHAR_SLIDER_HITAREA,
        fill_color=(0, 0, 0, 1),  # nearly invisible but clickable
        bounds=(0, 200, 0, 24),
    )

    # Wrap slider handle shape in a MovieClip so it can be accessed by instance name
    handle_wrap = bytearray()
    handle_wrap += make_place_object2(CHAR_SLIDER_HANDLE, 1, -7, -6)
    handle_wrap += make_show_frame()
    handle_wrap += make_end_tag()
    tags += make_define_sprite(CHAR_SLIDER_HANDLE_MC, bytes(handle_wrap))

    # --- Slider MovieClip ---
    slider_inner = bytearray()
    slider_inner += make_place_object2(CHAR_SLIDER_HITAREA, 1, 0, -12, "hitArea")
    slider_inner += make_place_object2(CHAR_SLIDER_TRACK, 2, 0, 0, "track")
    slider_inner += make_place_object2(CHAR_SLIDER_HANDLE_MC, 3, 100, 0, "handle")
    slider_inner += make_show_frame()
    slider_inner += make_end_tag()
    tags += make_define_sprite(CHAR_SLIDER_MC, bytes(slider_inner))

    # --- Per-button bitmap backgrounds (individual color-coded borders) ---
    btn_w = 100
    btn_h = 28
    btn_bg_dir = os.path.join(os.path.dirname(__file__), "assets", "icons", "buttons")
    btn_names_ordered = ['submit', 'intimidate', 'cancel', 'accept', 'reoffer', 'walkaway', 'continue']

    # Bitmap IDs 200-213 for normal/highlight backgrounds (2 per button × 7 buttons)
    btn_bg_bmp_base = 200
    # Shape IDs for bitmap-backed shapes: reuse CHAR_BTN_BG_NORMAL (16) and CHAR_BTN_BG_HIGHLIGHT (17)
    # as the first pair, then use 210+ for the rest — actually we need unique shapes per button.
    # We'll use IDs 220-233 for per-button bg shapes (normal + highlight interleaved)
    btn_bg_shape_base = 220
    # Sprites wrapping each shape: IDs 240-253
    btn_bg_sprite_base = 240

    for idx, bname in enumerate(btn_names_ordered):
        norm_path = os.path.join(btn_bg_dir, f"bg_{bname}_normal.png")
        hl_path = os.path.join(btn_bg_dir, f"bg_{bname}_highlight.png")
        bmp_id_n = btn_bg_bmp_base + idx * 2
        bmp_id_h = btn_bg_bmp_base + idx * 2 + 1
        shp_id_n = btn_bg_shape_base + idx * 2
        shp_id_h = btn_bg_shape_base + idx * 2 + 1
        spr_id_n = btn_bg_sprite_base + idx * 2
        spr_id_h = btn_bg_sprite_base + idx * 2 + 1

        tags += make_define_bits_lossless2(bmp_id_n, norm_path)
        tags += make_define_shape_bitmap(shp_id_n, bmp_id_n, 400, 112, display_w=btn_w, display_h=btn_h)
        norm_wrap = bytearray()
        norm_wrap += make_place_object2(shp_id_n, 1, 0, 0)
        norm_wrap += make_show_frame()
        norm_wrap += make_end_tag()
        tags += make_define_sprite(spr_id_n, bytes(norm_wrap))

        tags += make_define_bits_lossless2(bmp_id_h, hl_path)
        tags += make_define_shape_bitmap(shp_id_h, bmp_id_h, 400, 112, display_w=btn_w, display_h=btn_h)
        hl_wrap = bytearray()
        hl_wrap += make_place_object2(shp_id_h, 1, 0, 0)
        hl_wrap += make_show_frame()
        hl_wrap += make_end_tag()
        tags += make_define_sprite(spr_id_h, bytes(hl_wrap))

    # --- Button text labels (DefineEditText - empty initial, set from AS) ---
    btn_text_w = btn_w - 30  # narrower: icon(20px) + gap(10px) = 30px used on left
    btn_text_defs = [
        (CHAR_BTN_TEXT_SUBMIT, (218, 165, 32, 255)),
        (CHAR_BTN_TEXT_INTIMIDATE, (204, 68, 68, 255)),
        (CHAR_BTN_TEXT_CANCEL, (160, 160, 160, 255)),
        (CHAR_BTN_TEXT_ACCEPT, (102, 204, 102, 255)),
        (CHAR_BTN_TEXT_REOFFER, (218, 165, 32, 255)),
        (CHAR_BTN_TEXT_WALKAWAY, (160, 160, 160, 255)),
        (CHAR_BTN_TEXT_CONTINUE, (255, 255, 255, 255)),
    ]
    for cid, rgba in btn_text_defs:
        tags += make_define_edit_text(
            char_id=cid,
            bounds=(0, btn_text_w * TWIPS, 0, btn_h * TWIPS),
            var_name="",
            initial_text="",
            font_height=200,
            font_id=FONT_MEDIUM_ID,
            html=True,
            read_only=True,
            color=rgba,
            align=2,
        )

    # --- Hold-to-fill overlay clips (Submit = gold, Intimidate = red) -------------
    # Semi-transparent rectangles the SKSE plugin scales horizontally (_xscale 0..100)
    # while the player holds the button, committing the action when full. Anchored at the
    # left edge (bounds start at x=0) so the fill grows left -> right.
    CHAR_BTN_FILL_SHAPE_SUBMIT  = 260
    CHAR_BTN_FILL_SHAPE_INTIM   = 261
    CHAR_BTN_FILL_SPRITE_SUBMIT = 262
    CHAR_BTN_FILL_SPRITE_INTIM  = 263
    tags += make_define_shape(CHAR_BTN_FILL_SHAPE_SUBMIT, (218, 165, 32, 120), (0, btn_w, 0, btn_h))
    tags += make_define_shape(CHAR_BTN_FILL_SHAPE_INTIM,  (204, 68, 68, 120),  (0, btn_w, 0, btn_h))
    for _fill_shape, _fill_sprite in [(CHAR_BTN_FILL_SHAPE_SUBMIT, CHAR_BTN_FILL_SPRITE_SUBMIT),
                                      (CHAR_BTN_FILL_SHAPE_INTIM,  CHAR_BTN_FILL_SPRITE_INTIM)]:
        _fwrap = bytearray()
        _fwrap += make_place_object2(_fill_shape, 1, 0, 0)
        _fwrap += make_show_frame()
        _fwrap += make_end_tag()
        tags += make_define_sprite(_fill_sprite, bytes(_fwrap))
    fill_sprite_for_idx = {0: CHAR_BTN_FILL_SPRITE_SUBMIT, 1: CHAR_BTN_FILL_SPRITE_INTIM}

    # --- Button MovieClip sprites (per-button bg normal + highlight + icon + text) ---
    btn_sprite_defs = [
        (CHAR_BTN_SUBMIT, CHAR_BTN_TEXT_SUBMIT, CHAR_BTN_ICON_SHAPE_SUBMIT, 0),
        (CHAR_BTN_INTIMIDATE, CHAR_BTN_TEXT_INTIMIDATE, CHAR_BTN_ICON_SHAPE_INTIMIDATE, 1),
        (CHAR_BTN_CANCEL, CHAR_BTN_TEXT_CANCEL, CHAR_BTN_ICON_SHAPE_CANCEL, 2),
        (CHAR_BTN_ACCEPT, CHAR_BTN_TEXT_ACCEPT, CHAR_BTN_ICON_SHAPE_ACCEPT, 3),
        (CHAR_BTN_REOFFER, CHAR_BTN_TEXT_REOFFER, CHAR_BTN_ICON_SHAPE_REOFFER, 4),
        (CHAR_BTN_WALKAWAY, CHAR_BTN_TEXT_WALKAWAY, CHAR_BTN_ICON_SHAPE_WALKAWAY, 5),
        (CHAR_BTN_CONTINUE, CHAR_BTN_TEXT_CONTINUE, CHAR_BTN_ICON_SHAPE_CONTINUE, 6),
    ]
    for sprite_id, text_id, icon_shape_id, btn_idx in btn_sprite_defs:
        spr_id_n = btn_bg_sprite_base + btn_idx * 2
        spr_id_h = btn_bg_sprite_base + btn_idx * 2 + 1
        inner = bytearray()
        inner += make_place_object2(spr_id_n, 1, 0, 0, "bgNormal")
        inner += make_place_object2_hidden(spr_id_h, 2, 0, 0, "bgHighlight")
        fill_sprite = fill_sprite_for_idx.get(btn_idx)
        if fill_sprite is not None:
            # Hold-to-fill bar sits ABOVE the backgrounds but BELOW the icon/text, starts
            # hidden, and the plugin animates its _xscale. Bump icon/label up a depth.
            inner += make_place_object2_hidden(fill_sprite, 3, 0, 0, "bgFill")
            # Icon at left side, vertically centered (22x22 icon in 28px tall button → y=3)
            inner += make_place_object2(icon_shape_id, 5, 5, 3, "icon")
            # Text label closer to icon, vertically centered (btn_h=28, font 10pt → y=5)
            inner += make_place_object2(text_id, 4, 28, 5, "labelField")
        else:
            # Icon at left side, vertically centered (22x22 icon in 28px tall button → y=3)
            inner += make_place_object2(icon_shape_id, 4, 5, 3, "icon")
            # Text label closer to icon, vertically centered (btn_h=28, font 10pt → y=5)
            inner += make_place_object2(text_id, 3, 28, 5, "labelField")
        inner += make_show_frame()
        inner += make_end_tag()
        tags += make_define_sprite(sprite_id, bytes(inner))


    # --- Text fields ---
    field_w = 300
    # MerchantName - Bold, size 16, white (wider field to prevent cutoff)
    tags += make_define_edit_text(
        char_id=CHAR_MERCHANT_NAME,
        bounds=(0, field_w * TWIPS, 0, 24 * TWIPS),
        var_name="",
        initial_text="",
        font_height=320,
        font_id=FONT_BOLD_ID,
        html=True,
        color=(255, 255, 255, 255),
        align=2,
    )
    # FlavorText - Medium, size 9, grey
    tags += make_define_edit_text(
        char_id=CHAR_FLAVOR_TEXT,
        bounds=(0, field_w * TWIPS, 0, 14 * TWIPS),
        var_name="",
        initial_text="",
        font_height=180,
        font_id=FONT_MEDIUM_ID,
        html=True,
        color=(153, 153, 153, 255),
        align=2,
    )
    # OfferLabel - Medium, size 8, muted
    tags += make_define_edit_text(
        char_id=CHAR_OFFER_LABEL,
        bounds=(0, field_w * TWIPS, 0, 20 * TWIPS),
        var_name="",
        initial_text="",
        font_height=240,
        font_id=FONT_MEDIUM_ID,
        html=True,
        color=(210, 180, 120, 255),
        align=2,
    )
    # PriceText - Bold, size 22, gold, center-aligned (visual center at stage x=640).
    # The coin is a separate MovieClip that C++ parks just left of the number using
    # this field's textWidth (see PositionCoin); no inline <img> is used.
    tags += make_define_edit_text(
        char_id=CHAR_PRICE_TEXT,
        bounds=(0, field_w * TWIPS, 0, 32 * TWIPS),
        var_name="",
        initial_text="",
        font_height=440,
        font_id=FONT_BOLD_ID,
        html=True,
        multiline=True,
        word_wrap=True,
        color=(218, 165, 32, 255),
        align=2,
    )
    # SliderText - Medium, size 8, grey
    tags += make_define_edit_text(
        char_id=CHAR_SLIDER_TEXT,
        bounds=(0, field_w * TWIPS, 0, 14 * TWIPS),
        var_name="",
        initial_text="",
        font_height=160,
        font_id=FONT_MEDIUM_ID,
        html=True,
        color=(200, 200, 200, 255),
        align=2,
    )
    # CurrentPrice - Medium, size 9, white
    tags += make_define_edit_text(
        char_id=CHAR_CURRENT_PRICE,
        bounds=(0, field_w * TWIPS, 0, 14 * TWIPS),
        var_name="",
        initial_text="",
        font_height=180,
        font_id=FONT_MEDIUM_ID,
        html=True,
        color=(255, 255, 255, 255),
        align=2,
    )
    # ReactionText - Medium, size 9, colored by sentiment (default white)
    tags += make_define_edit_text(
        char_id=CHAR_REACTION_TEXT,
        bounds=(0, field_w * TWIPS, 0, 14 * TWIPS),
        var_name="",
        initial_text="",
        font_height=180,
        font_id=FONT_MEDIUM_ID,
        html=True,
        color=(255, 255, 255, 255),
        align=2,
    )
    # StatusText - Medium, size 10, white (hidden by default)
    tags += make_define_edit_text(
        char_id=CHAR_STATUS_TEXT,
        bounds=(0, field_w * TWIPS, 0, 18 * TWIPS),
        var_name="",
        initial_text="",
        font_height=200,
        font_id=FONT_MEDIUM_ID,
        html=True,
        color=(255, 255, 255, 255),
        align=2,
    )
    # ButtonHintText - Medium, grey. Multiline + word-wrap are required for GFx to
    # render inline <img> glyphs; height is enlarged to fit the keybind glyphs.
    tags += make_define_edit_text(
        char_id=CHAR_BUTTON_HINT,
        bounds=(0, field_w * TWIPS, 0, 26 * TWIPS),
        var_name="",
        initial_text="",
        font_height=160,
        font_id=FONT_MEDIUM_ID,
        html=True,
        multiline=True,
        word_wrap=True,
        color=(154, 140, 120, 255),
        align=2,
    )

    # Hint-row labels. Each sits just right of a placed keybind glyph shape. Inline
    # <img> glyphs do NOT render in this SWF, so the hint row is built from placed
    # glyph shapes (toggled by device in C++) + these small left-aligned labels.
    for _hint_lbl_id in (CHAR_HINT_LBL_1, CHAR_HINT_LBL_2, CHAR_HINT_LBL_3, CHAR_HINT_LBL_4,
                         CHAR_HINT_LBL_5):
        tags += make_define_edit_text(
            char_id=_hint_lbl_id,
            bounds=(0, 78 * TWIPS, 0, 16 * TWIPS),
            var_name="",
            initial_text="",
            font_height=150,
            font_id=FONT_MEDIUM_ID,
            html=True,
            color=(154, 140, 120, 255),
            align=0,
        )

    # --- NEW: Enhanced visual elements ---

    # AcceptanceText - shows "Merchant will ACCEPT/REJECT" colored dynamically
    tags += make_define_edit_text(
        char_id=CHAR_ACCEPTANCE_TEXT,
        bounds=(0, field_w * TWIPS, 0, 16 * TWIPS),
        var_name="",
        initial_text="",
        font_height=180,
        font_id=FONT_MEDIUM_ID,
        html=True,
        color=(80, 176, 80, 255),
        align=2,
    )
    # BasePriceText - shows base/market price for comparison
    tags += make_define_edit_text(
        char_id=CHAR_BASE_PRICE_TEXT,
        bounds=(0, field_w * TWIPS, 0, 14 * TWIPS),
        var_name="",
        initial_text="",
        font_height=160,
        font_id=FONT_MEDIUM_ID,
        html=True,
        color=(160, 144, 128, 255),
        align=2,
    )
    # RelEffectText - shows "This offer will slightly improve/worsen your standing"
    tags += make_define_edit_text(
        char_id=CHAR_REL_EFFECT_TEXT,
        bounds=(0, field_w * TWIPS, 0, 14 * TWIPS),
        var_name="",
        initial_text="",
        font_height=140,
        font_id=FONT_MEDIUM_ID,
        html=True,
        color=(160, 144, 128, 255),
        align=2,
    )
    # DealHistoryText - previous deals synopsis
    tags += make_define_edit_text(
        char_id=CHAR_DEAL_HISTORY_TEXT,
        bounds=(0, field_w * TWIPS, 0, 14 * TWIPS),
        var_name="",
        initial_text="",
        font_height=140,
        font_id=FONT_MEDIUM_ID,
        html=True,
        color=(140, 120, 100, 255),
        align=2,
    )

    # Relationship bar background (dark, 120x6 px)
    rel_bar_w = 120
    rel_bar_h = 6
    tags += make_define_shape(
        char_id=CHAR_REL_BAR_BG,
        fill_color=(30, 28, 26, 220),
        bounds=(0, rel_bar_w, 0, rel_bar_h),
        line_color=(80, 70, 50, 200),
        line_width=10,
    )
    # Colored zone segments (each 24px of the 120px bar): hostile red on the left,
    # through neutral amber, to friendly green on the right. Static background that
    # gives the marker position meaning.
    rel_zone_w = rel_bar_w // 5  # 24px
    rel_zone_colors = [
        (190, 70, 64, 255),    # 0: hostile (red)
        (205, 120, 70, 255),   # 1: cool (red-orange)
        (200, 175, 90, 255),   # 2: neutral (amber)
        (150, 180, 85, 255),   # 3: warm (yellow-green)
        (90, 185, 90, 255),    # 4: friendly (green)
    ]
    rel_zone_ids = [CHAR_REL_ZONE_0, CHAR_REL_ZONE_1, CHAR_REL_ZONE_2, CHAR_REL_ZONE_3, CHAR_REL_ZONE_4]
    for zid, zcol in zip(rel_zone_ids, rel_zone_colors):
        tags += make_define_shape(
            char_id=zid,
            fill_color=zcol,
            bounds=(0, rel_zone_w, 0, rel_bar_h),
        )
    # Moving marker: a bright vertical tick taller than the bar so it reads as a
    # position indicator on the meter (distinct from the slider's diamond).
    tags += make_define_shape(
        char_id=CHAR_REL_MARKER,
        fill_color=(245, 240, 225, 255),
        bounds=(0, 5, 0, 16),
        line_color=(30, 26, 20, 255),
        line_width=20,
    )
    # Wrap each fill + the marker in its OWN MovieClip. GFx only reliably honors
    # _x / _xscale / _visible on MovieClip instances (this is why the slider handle
    # is wrapped); bare shapes leave the marker pinned and the fill colour unswapped.
    def _wrap_mc(shape_id, mc_id, ox=0, oy=0):
        body = bytearray()
        body += make_place_object2(shape_id, 1, ox, oy)
        body += make_show_frame()
        body += make_end_tag()
        return make_define_sprite(mc_id, bytes(body))

    # Marker shape (5px wide, 16px tall) centered on the MC origin so marker._x is
    # the marker's CENTER; y=-5 straddles the 6px bar.
    tags += _wrap_mc(CHAR_REL_MARKER, CHAR_REL_MARKER_MC, ox=-2, oy=-5)

    # Assemble the meter MovieClip: dark frame, the 5 colored zone segments laid
    # left-to-right, then the marker on top (highest depth = drawn last/on top).
    rel_bar_inner = bytearray()
    rel_bar_inner += make_place_object2(CHAR_REL_BAR_BG, 1, 0, 0, "bg")
    for i, zid in enumerate(rel_zone_ids):
        rel_bar_inner += make_place_object2(zid, 2 + i, i * rel_zone_w, 0, "zone{}".format(i))
    rel_bar_inner += make_place_object2(CHAR_REL_MARKER_MC, 8, 0, 0, "marker")
    rel_bar_inner += make_show_frame()
    rel_bar_inner += make_end_tag()
    tags += make_define_sprite(CHAR_REL_BAR_MC, bytes(rel_bar_inner))

    # Separator line (bitmap-based, 512x16 source → 240x2 display)
    tags += make_define_shape_bitmap(CHAR_SEPARATOR_LINE, CHAR_BMP_SEPARATOR, 512, 16, display_w=240, display_h=2)

    # Corner ornaments (bitmap-based, 128x128 source → 40x40 display for visibility)
    tags += make_define_shape_bitmap(CHAR_CORNER_TL, CHAR_BMP_CORNER_TL, 128, 128, display_w=40, display_h=40)
    tags += make_define_shape_bitmap(CHAR_CORNER_TR, CHAR_BMP_CORNER_TR, 128, 128, display_w=40, display_h=40)
    tags += make_define_shape_bitmap(CHAR_CORNER_BL, CHAR_BMP_CORNER_BL, 128, 128, display_w=40, display_h=40)
    tags += make_define_shape_bitmap(CHAR_CORNER_BR, CHAR_BMP_CORNER_BR, 128, 128, display_w=40, display_h=40)

    # ===================================================================
    # KEYBIND HINT GLYPHS (controller + keyboard)
    # ===================================================================
    # Each glyph is embedded as a bitmap -> bitmap-fill shape -> sprite, and the
    # sprite is exported with a linkage name so the hint text field can render it
    # inline via <img src='linkageName'/>. Sizes keep each glyph's aspect ratio at
    # a uniform display height so they line up in the hint bar.
    from PIL import Image as _GlyphImage
    glyphs_dir = os.path.join(icons_dir, "glyphs")
    glyph_defs = [
        ("g_xb_a", "glyph_xbox_a.png"),
        ("g_xb_b", "glyph_xbox_b.png"),
        ("g_xb_x", "glyph_xbox_x.png"),
        ("g_xb_lb", "glyph_xbox_lb.png"),
        ("g_xb_rb", "glyph_xbox_rb.png"),
        ("g_ps_cross", "glyph_ps_cross.png"),
        ("g_ps_circle", "glyph_ps_circle.png"),
        ("g_ps_square", "glyph_ps_square.png"),
        ("g_ps_l1", "glyph_ps_l1.png"),
        ("g_ps_r1", "glyph_ps_r1.png"),
        ("g_pad", "glyph_dpad.png"),
        ("g_kbd_e", "glyph_key_e.png"),
        ("g_kbd_r", "glyph_key_r.png"),
        ("g_kbd_tab", "glyph_key_tab.png"),
        ("g_kbd_arrows", "glyph_key_arrows.png"),
    ]
    GLYPH_DISPLAY_H = 16  # px tall in the hint bar
    glyph_next_id = 100   # existing char ids top out at 96
    glyph_exports = []
    glyph_shapes = {}     # linkage -> DefineShape id
    glyph_sprites = {}    # linkage -> DefineSprite (MovieClip) id; place THESE so _visible works
    for linkage, fn in glyph_defs:
        path = os.path.join(glyphs_dir, fn)
        with _GlyphImage.open(path) as gi:
            gw, gh = gi.size
        disp_h = GLYPH_DISPLAY_H
        disp_w = max(1, int(round(GLYPH_DISPLAY_H * gw / float(gh))))
        bmp_id = glyph_next_id; glyph_next_id += 1
        shp_id = glyph_next_id; glyph_next_id += 1
        spr_id = glyph_next_id; glyph_next_id += 1
        tags += make_define_bits_lossless2(bmp_id, path)
        tags += make_define_shape_bitmap(shp_id, bmp_id, gw, gh, display_w=disp_w, display_h=disp_h)
        glyph_shapes[linkage] = shp_id
        spr = bytearray()
        spr += make_place_object2(shp_id, 1, 0, 0)
        spr += make_show_frame()
        spr += make_end_tag()
        tags += make_define_sprite(spr_id, bytes(spr))
        glyph_sprites[linkage] = spr_id
        glyph_exports.append((spr_id, linkage))

    # Coin sprite exported as "g_coin" so the price text can render the septim
    # inline (<img src='g_coin'>), keeping it right next to the gold amount.
    coin_spr = bytearray()
    coin_spr += make_place_object2(CHAR_BMP_COIN_SHAPE, 1, 0, 0)
    coin_spr += make_show_frame()
    coin_spr += make_end_tag()
    tags += make_define_sprite(CHAR_COIN_SPRITE, bytes(coin_spr))
    glyph_exports.append((CHAR_COIN_SPRITE, "g_coin"))

    tags += make_export_assets(glyph_exports)

    # ===================================================================
    # STAGE PLACEMENT
    # ===================================================================
    # Panel centered on 1280x720 stage (expanded for richer content)
    panel_x = 470
    panel_y = 160

    tags += make_place_object2(CHAR_PANEL_BG, 1, panel_x, panel_y, "panelBG")
    tags += make_place_object2(CHAR_PANEL_BORDER, 2, panel_x, panel_y, "panelBorder")

    # Corner ornaments (flush with panel edges, 40x40). Named so the SKSE plugin can
    # recolor them per UI theme via a color-transform.
    tags += make_place_object2(CHAR_CORNER_TL, 40, panel_x - 4, panel_y - 4, "cornerTL")
    tags += make_place_object2(CHAR_CORNER_TR, 41, panel_x + panel_w - 36, panel_y - 4, "cornerTR")
    tags += make_place_object2(CHAR_CORNER_BL, 42, panel_x - 4, panel_y + panel_h - 36, "cornerBL")
    tags += make_place_object2(CHAR_CORNER_BR, 43, panel_x + panel_w - 36, panel_y + panel_h - 36, "cornerBR")

    # Text fields (left-aligned to panel_x + 20)
    tx = panel_x + 20

    # Layout Y offsets from panel_y:
    #   8: MerchantName (bold, white, 24px tall)
    #  32: FlavorText (grey, 14px)
    #  48: ─── ornament divider ───
    #  60: BasePriceText "Market Price: XX gold"
    #  76: OfferLabel "Your Offer"
    #  90: PriceText "XX gold (+X%)" big gold
    # 118: SliderText "-X% / +X%"
    # 132: Slider
    # 150: AcceptanceText "Merchant will likely ACCEPT"
    # 168: ─── separator ───
    # 174: ReactionText "Fair merchant | Relationship:" + bar
    # 188: Relationship bar
    # 204: RelEffectText "This offer will..."
    # 218: ─── separator ───
    # 226: Buttons row (24px)
    # 260: ButtonHintText

    tags += make_place_object2(CHAR_MERCHANT_NAME, 10, tx, panel_y + 8, "MerchantName")
    tags += make_place_object2(CHAR_FLAVOR_TEXT, 11, tx, panel_y + 32, "FlavorText")

    # Ornament divider
    ornament_x = 640 - 100
    ornament_y = panel_y + 48
    tags += make_place_object2(CHAR_BMP_ORNAMENT_SHAPE, 3, ornament_x, ornament_y, "ornament")

    # Price comparison section
    tags += make_place_object2(CHAR_BASE_PRICE_TEXT, 27, tx, panel_y + 60, "BasePriceText")
    tags += make_place_object2(CHAR_OFFER_LABEL, 12, tx, panel_y + 74, "OfferLabel")

    # Big price text + standalone coin glyph. GFx silently drops inline <img> here,
    # so the coin is the coin MovieClip (CHAR_COIN_SPRITE) that C++ repositions each
    # frame to hug the (center-aligned) number via PriceText.textWidth. It MUST be a
    # MovieClip (not a bare shape) or _x is ignored — same lesson as the slider handle.
    tags += make_place_object2(CHAR_COIN_SPRITE, 5, tx + 6, panel_y + 96, "coinIcon")
    tags += make_place_object2(CHAR_PRICE_TEXT, 13, tx, panel_y + 92, "PriceText")

    # Slider section
    tags += make_place_object2(CHAR_SLIDER_TEXT, 14, tx, panel_y + 120, "SliderText")
    slider_x = 640 - 100
    slider_y = panel_y + 138
    tags += make_place_object2(CHAR_SLIDER_MC, 4, slider_x, slider_y, "sliderMC")
    tags += make_place_object2(CHAR_BMP_ARROW_LEFT_SHAPE, 6, slider_x - 18, slider_y - 7, "arrowLeft")
    tags += make_place_object2(CHAR_BMP_ARROW_RIGHT_SHAPE, 7, slider_x + 203, slider_y - 7, "arrowRight")

    # Acceptance indicator (shifted down to clear Adjust keybind hint above slider)
    tags += make_place_object2(CHAR_ACCEPTANCE_TEXT, 28, tx, panel_y + 176, "AcceptanceText")

    # Separator before relationship section
    sep_x = panel_x + 30
    tags += make_place_object2(CHAR_SEPARATOR_LINE, 44, sep_x, panel_y + 194)

    # Relationship section
    tags += make_place_object2(CHAR_REACTION_TEXT, 16, tx, panel_y + 200, "ReactionText")
    # Relationship bar (centered)
    rel_bar_x = 640 - 60
    tags += make_place_object2(CHAR_REL_BAR_MC, 29, rel_bar_x, panel_y + 216, "relBarMC")
    # Effect preview
    tags += make_place_object2(CHAR_REL_EFFECT_TEXT, 30, tx, panel_y + 228, "RelEffectText")

    # Deal history synopsis
    tags += make_place_object2(CHAR_DEAL_HISTORY_TEXT, 31, tx, panel_y + 244, "DealHistoryText")

    # Separator before buttons
    tags += make_place_object2(CHAR_SEPARATOR_LINE, 45, sep_x, panel_y + 260)

    # StatusText (item name - overlay position)
    tags += make_place_object2(CHAR_STATUS_TEXT, 17, tx, panel_y + 70, "StatusText")

    # Buttons - offer state (centered: 3*100 + 2*8 = 316px, panel_w=340, margin=(340-316)/2=12)
    btn_y = panel_y + 270
    btn_gap = 8
    btn_start_x = panel_x + 12
    tags += make_place_object2(CHAR_BTN_SUBMIT, 20, btn_start_x, btn_y, "btn_submit")
    tags += make_place_object2(CHAR_BTN_INTIMIDATE, 21, btn_start_x + btn_w + btn_gap, btn_y, "btn_intimidate")
    tags += make_place_object2(CHAR_BTN_CANCEL, 22, btn_start_x + 2 * (btn_w + btn_gap), btn_y, "btn_cancel")
    # Buttons - counter state (hidden by default)
    tags += make_place_object2_hidden(CHAR_BTN_ACCEPT, 23, btn_start_x, btn_y, "btn_accept")
    tags += make_place_object2_hidden(CHAR_BTN_REOFFER, 24, btn_start_x + btn_w + btn_gap, btn_y, "btn_reoffer")
    tags += make_place_object2_hidden(CHAR_BTN_WALKAWAY, 25, btn_start_x + 2 * (btn_w + btn_gap), btn_y, "btn_walkaway")
    # Buttons - result state (hidden by default)
    tags += make_place_object2_hidden(CHAR_BTN_CONTINUE, 26, 640 - btn_w // 2, btn_y, "btn_continue")

    # Button hints at bottom. ButtonHintText is kept (hidden/empty) as a fallback;
    # the visible hint row is built from placed glyph shapes + labels below.
    tags += make_place_object2(CHAR_BUTTON_HINT, 18, tx, panel_y + 302, "ButtonHintText")

    # --- Keybind hints (placed glyph shapes + labels) -------------------
    # 4 hint slots:
    #   Slot 0 (Confirm)    → under Submit button
    #   Slot 1 (Intimidate) → under Intimidate button
    #   Slot 2 (Cancel)     → under Cancel button
    #   Slot 3 (Adjust)     → under gold slider (different Y)
    glyph_w = 16
    depth = 50

    # Button hint row: Y just below button row
    btn_hint_y = btn_y + btn_h + 4       # glyph Y
    btn_hint_lbl_y = btn_y + btn_h + 3   # label Y

    # Slot X positions centered under each button
    slot0_x = btn_start_x + btn_w // 2 - 20   # under Submit
    slot1_x = btn_start_x + (btn_w + btn_gap) + btn_w // 2 - 20  # under Intimidate
    slot2_x = btn_start_x + 2 * (btn_w + btn_gap) + btn_w // 2 - 20  # under Cancel

    # Adjust slot: centered under slider
    adjust_hint_y = slider_y + 20         # below slider track
    adjust_hint_lbl_y = slider_y + 19
    slot3_x = 640 - 20                    # centered under slider

    KBD_X_SHIFT = -4       # shift single-char keyboard glyphs left slightly
    KBD_X_SHIFT_WIDE = -8  # shift wider glyphs (Tab, Arrows) further left

    def place_hint_at(linkage, x, y, name, is_kbd=False, wide_kbd=False):
        nonlocal tags, depth
        x_offset = (KBD_X_SHIFT_WIDE if wide_kbd else KBD_X_SHIFT) if is_kbd else 0
        tags += make_place_object2_hidden(glyph_sprites[linkage], depth, x + x_offset, y, name)
        depth += 1

    # Slot 0: Confirm (under Submit button)
    place_hint_at("g_kbd_e",     slot0_x, btn_hint_y, "hg_e",   is_kbd=True)
    place_hint_at("g_xb_a",      slot0_x, btn_hint_y, "hg_a")
    place_hint_at("g_ps_cross",  slot0_x, btn_hint_y, "hg_cr")

    # Slot 1: Intimidate (under Intimidate button) — R key / X xbox / Square PS
    place_hint_at("g_kbd_r",     slot1_x, btn_hint_y, "hg_r_intim", is_kbd=True)
    place_hint_at("g_xb_x",      slot1_x, btn_hint_y, "hg_x_intim")
    place_hint_at("g_ps_square", slot1_x, btn_hint_y, "hg_sq_intim")

    # Slot 2: Cancel (under Cancel button)
    place_hint_at("g_kbd_tab",   slot2_x, btn_hint_y, "hg_tab", is_kbd=True, wide_kbd=True)
    place_hint_at("g_xb_b",      slot2_x, btn_hint_y, "hg_b")
    place_hint_at("g_ps_circle", slot2_x, btn_hint_y, "hg_ci")

    # Slot 3: Adjust (under gold slider)
    place_hint_at("g_kbd_arrows", slot3_x, adjust_hint_y, "hg_ar",  is_kbd=True, wide_kbd=True)
    place_hint_at("g_pad",        slot3_x, adjust_hint_y, "hg_pad")

    # Slot 3b: shoulder bumpers move the slider by 5 (gamepad only, right of "Adjust").
    # Both Xbox (LB/RB) and PlayStation (L1/R1) variants are placed hidden; C++ shows
    # the right pair for the active icon style and hides them all for keyboard.
    bump_x0 = slot3_x + 86
    place_hint_at("g_xb_lb", bump_x0,      adjust_hint_y, "hg_lb")
    place_hint_at("g_xb_rb", bump_x0 + 24, adjust_hint_y, "hg_rb")
    place_hint_at("g_ps_l1", bump_x0,      adjust_hint_y, "hg_l1")
    place_hint_at("g_ps_r1", bump_x0 + 24, adjust_hint_y, "hg_r1")

    # Counter-offer state alternate glyphs (reuse slot positions)
    # Slot 1 becomes Re-offer in counter state: R key / X xbox / Square PS
    place_hint_at("g_kbd_r",     slot1_x, btn_hint_y, "hg_r",   is_kbd=True)
    # Slot 2 becomes Walk Away in counter state: Tab / B / Circle
    place_hint_at("g_kbd_tab",   slot2_x, btn_hint_y, "hg_tab2", is_kbd=True, wide_kbd=True)
    place_hint_at("g_xb_b",      slot2_x, btn_hint_y, "hg_b2")
    place_hint_at("g_ps_circle", slot2_x, btn_hint_y, "hg_ci2")

    # Labels for each slot
    # Label offsets: Tab key (slot 2) needs more space than R/E keys (slots 0, 1)
    lbl_dx_0 = 18   # E key
    lbl_dx_1 = 18   # R key
    lbl_dx_2 = 26   # Tab key (wider glyph)
    lbl_dx_3 = 26   # Arrows (wider glyph)
    tags += make_place_object2(CHAR_HINT_LBL_1, depth, slot0_x + lbl_dx_0, btn_hint_lbl_y, "HintLbl1"); depth += 1
    tags += make_place_object2(CHAR_HINT_LBL_2, depth, slot1_x + lbl_dx_1, btn_hint_lbl_y, "HintLbl2"); depth += 1
    tags += make_place_object2(CHAR_HINT_LBL_3, depth, slot2_x + lbl_dx_2, btn_hint_lbl_y, "HintLbl3"); depth += 1
    tags += make_place_object2(CHAR_HINT_LBL_4, depth, slot3_x + lbl_dx_3, adjust_hint_lbl_y, "HintLbl4"); depth += 1
    # "by 5" label for the bumper cue, just right of the two bumper glyphs (gamepad only).
    tags += make_place_object2(CHAR_HINT_LBL_5, depth, bump_x0 + 48, adjust_hint_lbl_y, "HintLbl5"); depth += 1

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
    header += struct.pack('<H', 60 << 8)  # 60fps (8.8 fixed)
    header += struct.pack('<H', 1)         # frame count

    body = bytes(header) + bytes(tags)
    sig = b'FWS'
    version = struct.pack('B', 8)
    file_len = struct.pack('<I', len(body) + 8)

    return sig + version + file_len + body


def main():
    swf_data = build_swf()

    out_dir = r"E:\Skyrim Animation\SKSE\DynamicBarteringSKSE\assets\interface\DynamicBartering"
    os.makedirs(out_dir, exist_ok=True)

    out_path = os.path.join(out_dir, "BarterOffer.swf")
    with open(out_path, 'wb') as f:
        f.write(swf_data)
    print(f"Generated {out_path} ({len(swf_data)} bytes)")


if __name__ == '__main__':
    main()
