#include "PCH.h"
#include "Theme.h"
#include "Settings.h"

namespace {
    // Order MUST match the UITheme enum. Each entry is a self-contained palette.
    constexpr ThemePalette kPalettes[static_cast<int>(UITheme::kTotal)] = {
        // ---- Default: gold on dark parchment (original look) --------------------
        {
            "Default",
            /*accent*/        "#DAA520",
            /*accentBright*/  "#FFCC00",
            /*accentLight*/   "#E8C878",
            /*accentDim*/     "#8C7B3C",
            /*textBright*/    "#FFFFFF",
            /*textSecondary*/ "#C8C8C8",
            /*textMuted*/     "#9A8C78",
            /*buyMode*/       "#C8A050",
            /*tint*/          218, 165, 32,
        },
        // ---- Outlander: green accents, parchment cream text (Quill) -------------
        {
            "Outlander UI",
            "#7DBE8A",
            "#A8E0B4",
            "#C9A6C4",  // soft magenta nod for the secondary accent
            "#5E8A6A",
            "#F0E6D2",
            "#CBBFA6",
            "#9C8F76",
            "#7DBE8A",
            125, 190, 138,
        },
        // ---- Oathvein: deep crimson on near-black charcoal (Jost) ---------------
        {
            "Oathvein UI",
            "#B23A3A",
            "#E05555",
            "#D98080",
            "#7E2A2A",
            "#F2EDE9",
            "#C9C2BC",
            "#8F8884",
            "#B23A3A",
            158, 43, 43,
        },
        // ---- Edge: muted tan-gold, thin & minimalist (Futura Book BT) -----------
        {
            "Edge UI",
            "#C7B187",
            "#E0D2B0",
            "#D8C49A",
            "#8C8064",
            "#ECE6DA",
            "#C4BCAA",
            "#8E867A",
            "#C7B187",
            199, 177, 135,
        },
        // ---- Nordic: desaturated amber + white on charcoal (Sovngarde) ----------
        {
            "Nordic UI",
            "#C0A062",
            "#E0C888",
            "#D2BA80",
            "#847046",
            "#FFFFFF",
            "#D0D0D0",
            "#9A9183",
            "#C0A062",
            192, 160, 98,
        },
        // ---- Untarnished: monochrome silver/white (Futura Book BT) --------------
        {
            "Untarnished UI",
            "#D8D8D8",
            "#FFFFFF",
            "#E8E8E8",
            "#888888",
            "#FFFFFF",
            "#CFCFCF",
            "#909090",
            "#D8D8D8",
            216, 216, 216,
        },
        // ---- New Horizons: bright yellow-gold on dark (High Voltage) ------------
        {
            "New Horizons UI",
            "#E8C53C",
            "#FFE066",
            "#F0D870",
            "#9C8528",
            "#FFFFFF",
            "#D2D2D2",
            "#8E8A7A",
            "#E8C53C",
            232, 197, 60,
        },
        // ---- Dragonborn: violet accents, gold values, ornate (Sovngarde) --------
        {
            "Dragonborn UI",
            /*accent (values stay gold)*/ "#D4AF37",
            "#F0D080",
            /*accentLight = violet labels*/ "#B488C8",
            "#6E5680",
            "#F0E8DC",
            "#C8C0D0",
            "#9A8FA0",
            "#D4AF37",
            /*ornaments -> violet*/ 180, 128, 200,
        },
        // ---- SkyUI: neutral gray, understated subtle gold (default font) --------
        {
            "SkyUI",
            "#D2D2D2",
            "#E8E8E8",
            "#E0C060",  // subtle gold nod (favorite-star hue)
            "#808080",
            "#FFFFFF",
            "#C8C8C8",
            "#9A9A9A",
            "#D2D2D2",
            210, 210, 210,
        },
    };
}

const ThemePalette& GetThemePalette(UITheme a_theme) {
    int idx = static_cast<int>(a_theme);
    if (idx < 0 || idx >= static_cast<int>(UITheme::kTotal)) {
        idx = 0;
    }
    return kPalettes[idx];
}

const ThemePalette& CurrentTheme() {
    return GetThemePalette(Settings::GetSingleton()->uiTheme);
}

const char* ThemeDisplayName(UITheme a_theme) {
    return GetThemePalette(a_theme).name;
}
