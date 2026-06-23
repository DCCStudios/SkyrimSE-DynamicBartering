#pragma once

#include <cstdint>

// UI theme presets. The "Default" theme is the original gold/parchment look; the
// others recolor the barter UI to echo popular Skyrim UI overhauls. Themes only
// drive COLOR + STYLING at runtime (one SWF, live-switchable). Fonts are inherited
// from whatever the user's installed UI overhaul remaps the $Everywhere* aliases to,
// so a "Nordic UI" theme automatically renders in Sovngarde when Nordic UI is present.
enum class UITheme : int {
    Default = 0,      // gold / dark parchment (original)
    Outlander = 1,    // green + parchment (Quill)
    Oathvein = 2,     // crimson on charcoal (Jost)
    Edge = 3,         // muted tan-gold, minimalist (Futura Book BT)
    Nordic = 4,       // desaturated amber + white on charcoal (Sovngarde)
    Untarnished = 5,  // monochrome silver/white (Futura Book BT)
    NewHorizons = 6,  // bright yellow-gold on dark (High Voltage)
    Dragonborn = 7,   // violet accents + gold values, ornate (Sovngarde)
    SkyUI = 8,        // neutral gray + subtle gold (default Skyrim font)

    kTotal = 9
};

// All colors are HTML hex strings (with leading '#') so they can be dropped straight
// into the GFx htmlText we build. Only the "theme identity" colors live here; purely
// functional colors (intimidate red, cancel gray, accept green, the relationship-meter
// and acceptance-band green->red gradients, discount green / markup orange) stay fixed
// across every theme because they convey meaning rather than style.
struct ThemePalette {
    const char* name;

    const char* accent;         // primary accent: price number, panel header, submit/re-offer label, bullets, even-trade
    const char* accentBright;   // brightest accent: counter-offer price
    const char* accentLight;    // light accent: "You Pay" label, cart prompt key, counter subtitle
    const char* accentDim;      // dim accent: small hint text
    const char* textBright;     // headline text: merchant name, "Continue"
    const char* textSecondary;  // body text: slider end labels, cart item names, counter description
    const char* textMuted;      // muted labels/hints: market price, blurbs, glyph hint row
    const char* buyMode;        // "Buying" mode word (gold-family by default; sell stays a fixed cool blue)

    // Tint target for baked GOLD-family clips (corners, ornament, slider arrows/handle,
    // submit button background + hold-fill). The runtime applies a multiply color-
    // transform = target / GOLD_BASE so the gold art shifts to this hue while keeping
    // its gradient/shading. For the Default theme this equals GOLD_BASE (no change).
    std::uint8_t tintR;
    std::uint8_t tintG;
    std::uint8_t tintB;
};

// Representative mid-gold of the baked art, used as the denominator for the tint
// multiply. Matches the gold fills authored in generate_swf.py (e.g. 218,165,32).
inline constexpr std::uint8_t kGoldBaseR = 218;
inline constexpr std::uint8_t kGoldBaseG = 165;
inline constexpr std::uint8_t kGoldBaseB = 32;

const ThemePalette& GetThemePalette(UITheme a_theme);
// Convenience: the palette for the currently-selected theme (reads Settings).
const ThemePalette& CurrentTheme();
const char*         ThemeDisplayName(UITheme a_theme);
