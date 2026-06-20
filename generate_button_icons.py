"""
Generate button icons for DynamicBarteringSKSE at 4x resolution.

Each icon is 64x64 (displays at 16x16 in the SWF) for sharp rendering at high DPI.
Two states per icon: normal (muted) and highlighted (bright/glowing).

Icons:
  - submit_offer: Gold coins being offered (handshake gesture)
  - intimidate: Clenched fist / threat
  - cancel: X mark
  - accept: Checkmark
  - reoffer: Circular arrows (retry)
  - walkaway: Arrow pointing left/away
  - continue: Arrow pointing right
"""

from PIL import Image, ImageDraw, ImageFilter
import os

ICON_SIZE = 64  # 4x resolution of 16px display
OUTPUT_DIR = os.path.join(os.path.dirname(__file__), "assets", "icons", "buttons")
os.makedirs(OUTPUT_DIR, exist_ok=True)

# Color palette - Skyrim aesthetic
GOLD = (218, 165, 32, 255)
GOLD_BRIGHT = (255, 210, 80, 255)
GOLD_DIM = (160, 120, 20, 200)
RED = (180, 50, 50, 255)
RED_BRIGHT = (230, 70, 70, 255)
RED_DIM = (120, 35, 35, 200)
WHITE = (220, 215, 200, 255)
WHITE_BRIGHT = (255, 255, 255, 255)
WHITE_DIM = (150, 145, 135, 200)
GREEN = (80, 180, 80, 255)
GREEN_BRIGHT = (120, 220, 120, 255)
GREEN_DIM = (50, 120, 50, 200)


def make_icon(draw_func, color_normal, color_highlight, name):
    """Generate normal and highlighted versions of an icon."""
    for state, color in [("normal", color_normal), ("highlight", color_highlight)]:
        img = Image.new('RGBA', (ICON_SIZE, ICON_SIZE), (0, 0, 0, 0))
        draw = ImageDraw.Draw(img)
        draw_func(draw, color, state == "highlight")
        
        if state == "highlight":
            glow = img.copy()
            glow = glow.filter(ImageFilter.GaussianBlur(radius=2))
            composite = Image.alpha_composite(glow, img)
            img = composite
        
        path = os.path.join(OUTPUT_DIR, f"{name}_{state}.png")
        img.save(path)
    print(f"  Generated: {name}")


def draw_submit_offer(draw, color, bright):
    """Gold coins / pouch - represents making an offer."""
    c = ICON_SIZE
    # Draw three overlapping coins
    r = c // 5  # coin radius
    positions = [(c//2 - r, c//2), (c//2, c//2 - r//2), (c//2 + r, c//2)]
    
    for px, py in positions:
        # Coin body
        draw.ellipse([px - r, py - r, px + r, py + r], fill=color)
        # Inner detail (smaller circle)
        inner_r = r // 2
        inner_color = tuple(min(255, v + 40) for v in color[:3]) + (color[3],)
        draw.ellipse([px - inner_r, py - inner_r, px + inner_r, py + inner_r], 
                     outline=inner_color, width=2)
    
    # Upward arrow (offering gesture)
    arrow_x = c // 2
    arrow_top = c // 6
    arrow_bot = c // 2 - r - 4
    draw.line([(arrow_x, arrow_bot), (arrow_x, arrow_top)], fill=color, width=3)
    draw.polygon([(arrow_x - 6, arrow_top + 6), (arrow_x, arrow_top - 2), 
                  (arrow_x + 6, arrow_top + 6)], fill=color)


def draw_intimidate(draw, color, bright):
    """Clenched fist - represents intimidation."""
    c = ICON_SIZE
    cx, cy = c // 2, c // 2
    
    # Fist shape (simplified)
    # Palm
    palm_w, palm_h = c // 3, c // 3
    draw.rounded_rectangle(
        [cx - palm_w, cy - palm_h//2, cx + palm_w//2, cy + palm_h],
        radius=6, fill=color)
    
    # Fingers (4 small rounded rectangles on top)
    fw = palm_w * 2 // 5
    fh = palm_h // 2
    for i in range(4):
        fx = cx - palm_w + i * (fw + 1) + 2
        fy = cy - palm_h//2 - fh + 4
        draw.rounded_rectangle([fx, fy, fx + fw, fy + fh + 2], radius=3, fill=color)
    
    # Thumb
    draw.rounded_rectangle(
        [cx + palm_w//2 - 2, cy - 2, cx + palm_w//2 + 6, cy + palm_h//2],
        radius=3, fill=color)
    
    # Impact lines radiating from fist
    if bright:
        line_color = tuple(min(255, v + 60) for v in color[:3]) + (180,)
    else:
        line_color = tuple(v for v in color[:3]) + (120,)
    
    # Top-left rays
    draw.line([(cx - palm_w - 6, cy - palm_h - 2), (cx - palm_w - 12, cy - palm_h - 8)], 
              fill=line_color, width=2)
    draw.line([(cx - palm_w - 4, cy - 2), (cx - palm_w - 12, cy - 4)], 
              fill=line_color, width=2)
    draw.line([(cx - palm_w - 2, cy - palm_h + 4), (cx - palm_w - 10, cy - palm_h)], 
              fill=line_color, width=2)


def draw_cancel(draw, color, bright):
    """X mark - represents canceling."""
    c = ICON_SIZE
    margin = c // 4
    width = 5 if bright else 4
    
    draw.line([(margin, margin), (c - margin, c - margin)], fill=color, width=width)
    draw.line([(c - margin, margin), (margin, c - margin)], fill=color, width=width)
    
    # Circle outline
    draw.ellipse([margin - 4, margin - 4, c - margin + 4, c - margin + 4], 
                 outline=color, width=2)


def draw_accept(draw, color, bright):
    """Checkmark - represents accepting."""
    c = ICON_SIZE
    width = 5 if bright else 4
    
    # Checkmark path
    points = [
        (c // 5, c // 2),          # start left
        (c * 2 // 5, c * 3 // 4),  # bottom of check
        (c * 4 // 5, c // 4),      # top right
    ]
    draw.line([points[0], points[1]], fill=color, width=width)
    draw.line([points[1], points[2]], fill=color, width=width)
    
    # Subtle circle
    margin = c // 6
    draw.ellipse([margin, margin, c - margin, c - margin], outline=color, width=2)


def draw_reoffer(draw, color, bright):
    """Circular arrows - represents re-offering/retry."""
    c = ICON_SIZE
    cx, cy = c // 2, c // 2
    r = c // 3
    width = 4 if bright else 3
    
    # Arc (top half)
    draw.arc([cx - r, cy - r, cx + r, cy + r], start=200, end=340, fill=color, width=width)
    # Arc (bottom half)
    draw.arc([cx - r, cy - r, cx + r, cy + r], start=20, end=160, fill=color, width=width)
    
    # Arrow head on top arc (pointing clockwise at ~340 degrees = top-right)
    import math
    angle_top = math.radians(340)
    ax = cx + int(r * math.cos(angle_top))
    ay = cy + int(r * math.sin(angle_top))
    draw.polygon([
        (ax, ay - 7), (ax + 7, ay + 2), (ax - 2, ay + 5)
    ], fill=color)
    
    # Arrow head on bottom arc (pointing clockwise at ~160 degrees = bottom-left)
    angle_bot = math.radians(160)
    bx = cx + int(r * math.cos(angle_bot))
    by = cy + int(r * math.sin(angle_bot))
    draw.polygon([
        (bx, by + 7), (bx - 7, by - 2), (bx + 2, by - 5)
    ], fill=color)


def draw_walkaway(draw, color, bright):
    """Arrow pointing left - represents walking away."""
    c = ICON_SIZE
    cy = c // 2
    margin = c // 5
    width = 4 if bright else 3
    
    # Arrow shaft
    draw.line([(c - margin, cy), (margin + 6, cy)], fill=color, width=width)
    
    # Arrow head (pointing left)
    head_size = c // 5
    draw.polygon([
        (margin, cy),
        (margin + head_size, cy - head_size),
        (margin + head_size, cy + head_size),
    ], fill=color)
    
    # Small door/exit symbol on the right
    door_x = c - margin - 4
    door_h = c // 3
    draw.rectangle([door_x, cy - door_h//2, door_x + 6, cy + door_h//2], 
                   outline=color, width=2)


def draw_continue(draw, color, bright):
    """Arrow pointing right - represents continuing."""
    c = ICON_SIZE
    cy = c // 2
    margin = c // 5
    width = 4 if bright else 3
    
    # Arrow shaft
    draw.line([(margin, cy), (c - margin - 6, cy)], fill=color, width=width)
    
    # Arrow head (pointing right)
    head_size = c // 5
    draw.polygon([
        (c - margin, cy),
        (c - margin - head_size, cy - head_size),
        (c - margin - head_size, cy + head_size),
    ], fill=color)


def main():
    print("Generating button icons at 64x64 (4x resolution)...")
    
    make_icon(draw_submit_offer, GOLD_DIM, GOLD_BRIGHT, "btn_submit")
    make_icon(draw_intimidate, RED_DIM, RED_BRIGHT, "btn_intimidate")
    make_icon(draw_cancel, WHITE_DIM, WHITE_BRIGHT, "btn_cancel")
    make_icon(draw_accept, GREEN_DIM, GREEN_BRIGHT, "btn_accept")
    make_icon(draw_reoffer, GOLD_DIM, GOLD_BRIGHT, "btn_reoffer")
    make_icon(draw_walkaway, WHITE_DIM, WHITE_BRIGHT, "btn_walkaway")
    make_icon(draw_continue, WHITE_DIM, WHITE_BRIGHT, "btn_continue")
    
    print(f"\nAll icons saved to: {OUTPUT_DIR}")
    print("Each icon has _normal and _highlight variants.")


if __name__ == "__main__":
    main()
