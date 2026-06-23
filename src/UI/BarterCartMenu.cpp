#include "PCH.h"
#include "UI/BarterCartMenu.h"
#include "UI/ScaleformUI.h"
#include "CartManager.h"
#include "BarterManager.h"
#include "Hooks.h"
#include "Settings.h"
#include "Theme.h"
#include "DebugLog.h"

#include <cmath>

namespace {
    // Build an htmlText string with the embedded GFx font markup. Without the
    // <font face="$EverywhereMediumFont"> wrapper (plus embedFonts=true on the field)
    // dynamically created text will not render at all in Skyrim's GFx.
    std::string MakeHtml(const std::string& inner, int size, const char* color, const char* align = "left") {
        return std::string("<p align=\"") + align +
               "\"><font face=\"$EverywhereMediumFont\" size=\"" + std::to_string(size) +
               "\" color=\"" + color + "\">" + inner + "</font></p>";
    }

    std::string EscapeHtml(const std::string& s) {
        std::string out;
        out.reserve(s.size());
        for (char c : s) {
            switch (c) {
                case '&': out += "&amp;"; break;
                case '<': out += "&lt;"; break;
                case '>': out += "&gt;"; break;
                default: out += c; break;
            }
        }
        return out;
    }

    // --- path-based GFx setters (match the project's existing SetVariable style) ---
    void SetNum(RE::GFxMovieView* m, const char* path, double v) {
        RE::GFxValue val; val.SetNumber(v); m->SetVariable(path, val);
    }
    void SetBool(RE::GFxMovieView* m, const char* path, bool v) {
        RE::GFxValue val; val.SetBoolean(v); m->SetVariable(path, val);
    }
    void SetStr(RE::GFxMovieView* m, const char* path, const std::string& v) {
        RE::GFxValue val; val.SetString(v.c_str()); m->SetVariable(path, val);
    }

    // createEmptyMovieClip(name, depth) on a parent path (e.g. "_root").
    void CreateClip(RE::GFxMovieView* m, const char* parentPath, const char* name, double depth) {
        RE::GFxValue args[2];
        args[0].SetString(name);
        args[1].SetNumber(depth);
        std::string fn = std::string(parentPath) + ".createEmptyMovieClip";
        m->Invoke(fn.c_str(), nullptr, args, 2);
    }

    // createTextField(name, depth, x, y, w, h) on a parent clip path.
    void CreateText(RE::GFxMovieView* m, const char* parentPath, const char* name,
                    double depth, double x, double y, double w, double h) {
        RE::GFxValue args[6];
        args[0].SetString(name);
        args[1].SetNumber(depth);
        args[2].SetNumber(x);
        args[3].SetNumber(y);
        args[4].SetNumber(w);
        args[5].SetNumber(h);
        std::string fn = std::string(parentPath) + ".createTextField";
        m->Invoke(fn.c_str(), nullptr, args, 6);
    }

    // Configure a freshly created TextField: embedded fonts, optional bg/border.
    void StyleText(RE::GFxMovieView* m, const std::string& base, bool multiline,
                   bool bg, std::int32_t bgColor, bool border, std::int32_t borderColor) {
        SetBool(m, (base + ".embedFonts").c_str(), true);
        SetBool(m, (base + ".selectable").c_str(), false);
        SetBool(m, (base + ".html").c_str(), true);
        SetBool(m, (base + ".multiline").c_str(), multiline);
        SetBool(m, (base + ".wordWrap").c_str(), multiline);
        SetBool(m, (base + ".mouseEnabled").c_str(), false);
        if (bg) {
            SetBool(m, (base + ".background").c_str(), true);
            SetNum(m, (base + ".backgroundColor").c_str(), static_cast<double>(bgColor));
        }
        if (border) {
            SetBool(m, (base + ".border").c_str(), true);
            SetNum(m, (base + ".borderColor").c_str(), static_cast<double>(borderColor));
        }
    }

    constexpr std::int32_t kPanelBg     = 0x0B0906;  // very dark warm (matches offer window)
    constexpr std::int32_t kCartBg      = 0x161616;  // dark grey (cart window fill)
    constexpr std::int32_t kTrim        = 0xC8B43C;  // gold trim
    constexpr std::int32_t kTrimDim     = 0x6B5E2A;  // dim gold (separators)
    constexpr std::int32_t kMeterFill   = 0xE0C864;  // bright gold
    constexpr std::int32_t kMeterTrack  = 0x000000;

    // Parse an "#RRGGBB" palette string into a 0xRRGGBB int for GFx line/fill colors.
    inline std::int32_t HexToInt(const char* hex, std::int32_t fallback) {
        if (!hex) return fallback;
        if (*hex == '#') ++hex;
        std::int32_t v = 0;
        int digits = 0;
        for (; *hex && digits < 6; ++hex, ++digits) {
            char c = *hex;
            int d;
            if (c >= '0' && c <= '9') d = c - '0';
            else if (c >= 'a' && c <= 'f') d = c - 'a' + 10;
            else if (c >= 'A' && c <= 'F') d = c - 'A' + 10;
            else return fallback;
            v = (v << 4) | d;
        }
        return digits == 6 ? v : fallback;
    }
    // Theme-aware versions of the gold trim ints (fall back to the originals).
    inline std::int32_t ThemeTrim()      { return HexToInt(CurrentTheme().accent,    kTrim); }
    inline std::int32_t ThemeTrimDim()   { return HexToInt(CurrentTheme().accentDim, kTrimDim); }
    inline std::int32_t ThemeMeterFill() { return HexToInt(CurrentTheme().accent,    kMeterFill); }

    // Cart panel: a tall, narrow column hugging the RIGHT edge of the screen
    // (authored BarterMenu stage is ~1280x720). The vanilla item list sits on the
    // left and the merchant NPC + 3D item preview composite over the center/right,
    // so the far-right margin is the clear 2D area that avoids both.
    constexpr double kPanelX = 1040.0;
    constexpr double kPanelY = 90.0;
    constexpr double kPanelW = 230.0;
    constexpr double kPanelH = 440.0;

    // Internal layout (relative to the panel).
    constexpr double kPad       = 12.0;
    constexpr double kHeaderY   = 10.0;
    constexpr double kSep1Y     = 38.0;
    constexpr double kTotalsY   = 44.0;
    constexpr double kRelInfoY  = 70.0;   // relationship discount/markup line
    constexpr double kHintY     = 94.0;
    constexpr double kSep2Y     = 118.0;
    constexpr double kItemsY    = 124.0;

    // Gamepad fallback anchor (used only if the selected row can't be resolved).
    constexpr double kPromptKeyW = 22.0;
    constexpr double kPromptH    = 20.0;
    constexpr double kMeterW     = 110.0;

    // Path-based MovieClip drawing-API helpers (lineStyle / moveTo / lineTo).
    void LineStyle(RE::GFxMovieView* m, const char* clip, double thick, std::int32_t rgb, double alpha) {
        RE::GFxValue args[3];
        args[0].SetNumber(thick);
        args[1].SetNumber(static_cast<double>(rgb));
        args[2].SetNumber(alpha);
        m->Invoke((std::string(clip) + ".lineStyle").c_str(), nullptr, args, 3);
    }
    void MoveTo(RE::GFxMovieView* m, const char* clip, double x, double y) {
        RE::GFxValue args[2]; args[0].SetNumber(x); args[1].SetNumber(y);
        m->Invoke((std::string(clip) + ".moveTo").c_str(), nullptr, args, 2);
    }
    void LineTo(RE::GFxMovieView* m, const char* clip, double x, double y) {
        RE::GFxValue args[2]; args[0].SetNumber(x); args[1].SetNumber(y);
        m->Invoke((std::string(clip) + ".lineTo").c_str(), nullptr, args, 2);
    }

    void BeginFill(RE::GFxMovieView* m, const char* clip, std::int32_t rgb, double alpha) {
        RE::GFxValue a[2]; a[0].SetNumber(static_cast<double>(rgb)); a[1].SetNumber(alpha);
        m->Invoke((std::string(clip) + ".beginFill").c_str(), nullptr, a, 2);
    }
    void EndFill(RE::GFxMovieView* m, const char* clip) {
        m->Invoke((std::string(clip) + ".endFill").c_str(), nullptr, nullptr, 0);
    }

    // Approximate a circle with a polygon (smooth enough at button size).
    void DrawCircle(RE::GFxMovieView* m, const char* clip, double cx, double cy, double r) {
        constexpr int kSeg = 20;
        MoveTo(m, clip, cx + r, cy);
        for (int i = 1; i <= kSeg; ++i) {
            const double a = (2.0 * 3.14159265 * i) / kSeg;
            LineTo(m, clip, cx + r * std::cos(a), cy + r * std::sin(a));
        }
    }

    // DirectInput keyboard scan code -> short display label. Covers the keys a
    // user is realistically likely to bind "Activate" to; nullptr if unknown.
    const char* ScanCodeToLabel(std::uint32_t sc) {
        switch (sc) {
            case 0x02: return "1"; case 0x03: return "2"; case 0x04: return "3";
            case 0x05: return "4"; case 0x06: return "5"; case 0x07: return "6";
            case 0x08: return "7"; case 0x09: return "8"; case 0x0A: return "9";
            case 0x0B: return "0";
            case 0x10: return "Q"; case 0x11: return "W"; case 0x12: return "E";
            case 0x13: return "R"; case 0x14: return "T"; case 0x15: return "Y";
            case 0x16: return "U"; case 0x17: return "I"; case 0x18: return "O";
            case 0x19: return "P";
            case 0x1E: return "A"; case 0x1F: return "S"; case 0x20: return "D";
            case 0x21: return "F"; case 0x22: return "G"; case 0x23: return "H";
            case 0x24: return "J"; case 0x25: return "K"; case 0x26: return "L";
            case 0x2C: return "Z"; case 0x2D: return "X"; case 0x2E: return "C";
            case 0x2F: return "V"; case 0x30: return "B"; case 0x31: return "N";
            case 0x32: return "M";
            case 0x1C: return "Ent";   // Enter
            case 0x39: return "Spc";   // Space
            case 0x0F: return "Tab";
            default:   return nullptr;
        }
    }

    // Resolve the keyboard key currently bound to the menu "Activate" event,
    // returning a short label. Falls back to "E" (the vanilla menu default).
    std::string GetActivateKeyLabel() {
        auto* controlMap = RE::ControlMap::GetSingleton();
        auto* userEvents = RE::UserEvents::GetSingleton();
        if (controlMap && userEvents) {
            const std::uint32_t key = controlMap->GetMappedKey(
                userEvents->activate, RE::INPUT_DEVICE::kKeyboard,
                RE::UserEvents::INPUT_CONTEXT_ID::kItemMenu);
            if (const char* label = ScanCodeToLabel(key)) {
                return label;
            }
        }
        return "E";
    }

    // Draw the keybind prompt glyph. Keyboard => rounded gold key-cap with the
    // letter; gamepad => a filled controller face button with the correctly
    // coloured PlayStation/Xbox symbol, so it reads as a real pad button rather
    // than a plain square text box.
    //
    // glyph codes:
    //   0 = Xbox Y, 1 = PlayStation triangle, 2 = keyboard "B"  (barter key)
    //   3 = Xbox A, 4 = PlayStation cross,    5 = keyboard Activate key
    // Codes 3-5 are used when "Block Quick Buy" repurposes Activate as the
    // add-to-cart action.
    // Draws into <base>.glyphArt, using a sibling <base>.keyField text field for
    // lettered buttons. `base` is e.g. "_root.DBPrompt" or "_root.DBCart.hintGlyph".
    void DrawPromptGlyph(RE::GFxMovieView* m, int glyph, const char* base) {
        const bool gamepad = (glyph != 2 && glyph != 5);
        const std::string artPath = std::string(base) + ".glyphArt";
        const std::string keyField = std::string(base) + ".keyField";
        const char* art = artPath.c_str();

        // (Re)create the art clip so repeated calls don't stack drawings.
        m->Invoke((artPath + ".removeMovieClip").c_str(), nullptr, nullptr, 0);
        CreateClip(m, base, "glyphArt", 0.0);

        const double S = 20.0;        // button footprint
        const double cx = S * 0.5, cy = S * 0.5;

        if (!gamepad) {
            // Keyboard key-cap: dark rounded square with gold trim + the letter.
            // Barter key (2) shows "B"; Activate key (5) shows the bound key.
            const std::string label = (glyph == 5) ? GetActivateKeyLabel() : std::string("B");
            LineStyle(m, art, 1.5, ThemeTrim(), 100.0);
            BeginFill(m, art, kPanelBg, 95.0);
            // simple square cap (rounded look comes from the small size)
            MoveTo(m, art, 1.0, 1.0);
            LineTo(m, art, S - 1.0, 1.0);
            LineTo(m, art, S - 1.0, S - 1.0);
            LineTo(m, art, 1.0, S - 1.0);
            LineTo(m, art, 1.0, 1.0);
            EndFill(m, art);
            SetStr(m, (keyField + ".htmlText").c_str(), MakeHtml(EscapeHtml(label), 13, CurrentTheme().accentLight, "center"));
            SetBool(m, (keyField + "._visible").c_str(), true);
            return;
        }

        // Gamepad: round button housing.
        const std::int32_t housing = 0x16140F;   // near-black housing
        LineStyle(m, art, 1.0, 0x000000, 0.0);
        BeginFill(m, art, housing, 100.0);
        DrawCircle(m, art, cx, cy, S * 0.5 - 0.5);
        EndFill(m, art);

        if (glyph == 1) {
            // PlayStation triangle (green) - barter key.
            LineStyle(m, art, 1.4, 0x59D17A, 100.0);
            const double r = 5.6;
            MoveTo(m, art, cx, cy - r);
            LineTo(m, art, cx + r * 0.866, cy + r * 0.5);
            LineTo(m, art, cx - r * 0.866, cy + r * 0.5);
            LineTo(m, art, cx, cy - r);
            SetBool(m, (keyField + "._visible").c_str(), false);
        } else if (glyph == 4) {
            // PlayStation cross "X" (light blue) - Activate key.
            LineStyle(m, art, 1.8, 0x6FA8FF, 100.0);
            const double r = 4.8;
            MoveTo(m, art, cx - r, cy - r); LineTo(m, art, cx + r, cy + r);
            MoveTo(m, art, cx + r, cy - r); LineTo(m, art, cx - r, cy + r);
            SetBool(m, (keyField + "._visible").c_str(), false);
        } else if (glyph == 3) {
            // Xbox "A" (green) - Activate key.
            SetStr(m, (keyField + ".htmlText").c_str(), MakeHtml("A", 13, "#6FBF4F", "center"));
            SetBool(m, (keyField + "._visible").c_str(), true);
        } else {
            // Xbox "Y" (amber) - barter key.
            SetStr(m, (keyField + ".htmlText").c_str(), MakeHtml("Y", 13, "#E6C24A", "center"));
            SetBool(m, (keyField + "._visible").c_str(), true);
        }
    }

    // Thin gold outline + dim section separators, drawn into _root.DBCart.frame.
    void DrawCartFrame(RE::GFxMovieView* m) {
        const char* f = "_root.DBCart.frame";
        const double W = kPanelW, H = kPanelH;

        // Thin continuous accent border around the whole panel (themed).
        LineStyle(m, f, 1.25, ThemeTrim(), 100.0);
        MoveTo(m, f, 0.0, 0.0);
        LineTo(m, f, W, 0.0);
        LineTo(m, f, W, H);
        LineTo(m, f, 0.0, H);
        LineTo(m, f, 0.0, 0.0);

        // Section separators
        LineStyle(m, f, 1.0, ThemeTrimDim(), 90.0);
        MoveTo(m, f, kPad, kSep1Y);     LineTo(m, f, W - kPad, kSep1Y);
        MoveTo(m, f, kPad, kSep2Y);     LineTo(m, f, W - kPad, kSep2Y);
    }
}

void BarterCartMenu::OnBarterOpen() {
    built = false;
    lastPromptVisible = true;
    lastGlyph = -1;
    // Build() injects the cart clip with _visible = false, so seed the cached
    // visibility to false too. Otherwise, when "cart visible by default" is on,
    // panelVisible (true) already equals the cached value and the show-it SetBool
    // never fires - leaving the panel stuck hidden the entire session.
    lastHintGlyph = -2;
    lastPanelVisible = false;
    lastCartCount = -1;
    lastNet = -2147483647;
    lastRelMilli = -2147483647;
    lastMeterVisible = true;
    lastMeterFrac = -1.0f;
    lastPanelX = -1.0e9f;
    lastPanelY = -1.0e9f;
    lastPanelScale = -1.0f;
}

void BarterCartMenu::OnBarterClose() {
    built = false;
}

void BarterCartMenu::Build(RE::GFxMovieView* m) {
    if (!m) return;

    // Sanity: confirm _root exists before injecting.
    RE::GFxValue root;
    if (!m->GetVariable(&root, "_root")) {
        logger::warn("CartOverlay::Build: _root not available yet");
        return;
    }

    // ---------------- Prompt clip: "[glyph] Barter" + hold meter ----------------
    CreateClip(m, "_root", "DBPrompt", 5000.0);

    CreateText(m, "_root.DBPrompt", "keyField", 1.0, 0.0, 0.0, kPromptKeyW, kPromptH);
    StyleText(m, "_root.DBPrompt.keyField", false, false, 0, false, 0);

    CreateText(m, "_root.DBPrompt", "labelField", 2.0, kPromptKeyW + 5.0, 0.0, 90.0, kPromptH);
    StyleText(m, "_root.DBPrompt.labelField", false, false, 0, false, 0);
    SetStr(m, "_root.DBPrompt.labelField.htmlText", MakeHtml("Barter", 13, CurrentTheme().accent));

    CreateText(m, "_root.DBPrompt", "meterTrack", 3.0, 0.0, kPromptH + 3.0, kMeterW, 4.0);
    StyleText(m, "_root.DBPrompt.meterTrack", false, true, kMeterTrack, true, kTrim);

    CreateText(m, "_root.DBPrompt", "meterFill", 4.0, 1.0, kPromptH + 4.0, kMeterW - 2.0, 2.0);
    StyleText(m, "_root.DBPrompt.meterFill", false, true, ThemeMeterFill(), false, 0);

    SetBool(m, "_root.DBPrompt._visible", false);
    SetBool(m, "_root.DBPrompt.meterTrack._visible", false);
    SetBool(m, "_root.DBPrompt.meterFill._visible", false);

    // ---------------- Cart panel (tall column; placement from Settings) ----------------
    CreateClip(m, "_root", "DBCart", 5001.0);
    {
        auto* s = Settings::GetSingleton();
        SetNum(m, "_root.DBCart._x", static_cast<double>(s->cartPanelX));
        SetNum(m, "_root.DBCart._y", static_cast<double>(s->cartPanelY));
        const double sc = static_cast<double>(s->cartPanelScale) * 100.0;
        SetNum(m, "_root.DBCart._xscale", sc);
        SetNum(m, "_root.DBCart._yscale", sc);
        lastPanelX = s->cartPanelX;
        lastPanelY = s->cartPanelY;
        lastPanelScale = s->cartPanelScale;
    }

    // Background fill (depth 1) + drawn gold frame/ornaments (depth 2).
    // Dark grey, semi-transparent (reads as "semi-transparent black").
    CreateText(m, "_root.DBCart", "bg", 1.0, 0.0, 0.0, kPanelW, kPanelH);
    StyleText(m, "_root.DBCart.bg", false, true, kCartBg, false, 0);
    SetNum(m, "_root.DBCart.bg._alpha", 80.0);

    CreateClip(m, "_root.DBCart", "frame", 2.0);
    DrawCartFrame(m);

    // Header / totals / hint (fixed) and the item list (flows down).
    CreateText(m, "_root.DBCart", "header", 3.0, kPad, kHeaderY, kPanelW - kPad * 2.0, 24.0);
    StyleText(m, "_root.DBCart.header", false, false, 0, false, 0);
    SetStr(m, "_root.DBCart.header.htmlText", MakeHtml("BARTER CART", 15, CurrentTheme().accent, "left"));

    CreateText(m, "_root.DBCart", "totals", 4.0, kPad, kTotalsY, kPanelW - kPad * 2.0, 26.0);
    StyleText(m, "_root.DBCart.totals", false, false, 0, false, 0);

    // Relationship standing -> price effect (green discount / red markup).
    CreateText(m, "_root.DBCart", "relInfo", 8.0, kPad, kRelInfoY, kPanelW - kPad * 2.0, 22.0);
    StyleText(m, "_root.DBCart.relInfo", true, false, 0, false, 0);

    // Hint = [barter-key glyph] + "Hold to open the offer." The glyph sits to the
    // LEFT of the text and is (re)drawn each frame in UpdateCartPanel (the input
    // device / icon style can change at runtime).
    const double kHintGlyphW = 22.0;
    CreateClip(m, "_root.DBCart", "hintGlyph", 5.0);
    SetNum(m, "_root.DBCart.hintGlyph._x", kPad);
    SetNum(m, "_root.DBCart.hintGlyph._y", kHintY - 2.0);
    CreateText(m, "_root.DBCart.hintGlyph", "keyField", 1.0, 0.0, 0.0, 20.0, 20.0);
    StyleText(m, "_root.DBCart.hintGlyph.keyField", false, false, 0, false, 0);

    CreateText(m, "_root.DBCart", "hint", 6.0, kPad + kHintGlyphW + 4.0, kHintY,
               kPanelW - kPad * 2.0 - kHintGlyphW - 4.0, 22.0);
    StyleText(m, "_root.DBCart.hint", true, false, 0, false, 0);
    SetStr(m, "_root.DBCart.hint.htmlText",
        MakeHtml("Hold to open the offer.", 11, CurrentTheme().accentDim, "left"));

    CreateText(m, "_root.DBCart", "items", 7.0, kPad, kItemsY,
               kPanelW - kPad * 2.0, kPanelH - kItemsY - kPad);
    StyleText(m, "_root.DBCart.items", true, false, 0, false, 0);

    SetBool(m, "_root.DBCart._visible", false);

    built = true;
    DbgLog("CartOverlay: injected DBPrompt + DBCart into BarterMenu movie");
}

void BarterCartMenu::Update(RE::GFxMovieView* m) {
    if (!m) return;
    if (!built) {
        Build(m);
        if (!built) return;
    }
    UpdateCartPlacement(m);
    UpdatePromptAndMeter(m);
    UpdateCartPanel(m);
}

// Mirror the cart panel placement from Settings every frame so the SKSE Menu
// Framework sliders move/scale the panel live (without reopening the barter menu).
void BarterCartMenu::UpdateCartPlacement(RE::GFxMovieView* m) {
    auto* s = Settings::GetSingleton();
    if (s->cartPanelX != lastPanelX) {
        lastPanelX = s->cartPanelX;
        SetNum(m, "_root.DBCart._x", static_cast<double>(s->cartPanelX));
    }
    if (s->cartPanelY != lastPanelY) {
        lastPanelY = s->cartPanelY;
        SetNum(m, "_root.DBCart._y", static_cast<double>(s->cartPanelY));
    }
    if (s->cartPanelScale != lastPanelScale) {
        lastPanelScale = s->cartPanelScale;
        const double sc = static_cast<double>(s->cartPanelScale) * 100.0;
        SetNum(m, "_root.DBCart._xscale", sc);
        SetNum(m, "_root.DBCart._yscale", sc);
    }
}

void BarterCartMenu::UpdatePromptAndMeter(RE::GFxMovieView* m) {
    // Visibility + placement are computed each frame in Hooks::AdvanceMovieBart
    // (mouse hover-test vs. gamepad row anchor), using the reliable RE itemList.
    const bool show = Hooks::promptShow;

    if (show != lastPromptVisible) {
        lastPromptVisible = show;
        SetBool(m, "_root.DBPrompt._visible", show);
    }

    if (show) {
        const bool gamepad = Hooks::promptGamepad;
        const bool ps = Settings::GetSingleton()->gamepadIconStyle == GamepadIconStyle::PlayStation;
        // When Block Quick Buy is on, Activate is the add-to-cart button, so the
        // prompt shows the Activate glyph (Xbox A / PS cross / bound keyboard key)
        // instead of the barter-key glyph (Xbox Y / PS triangle / "B").
        const bool block = Settings::GetSingleton()->blockQuickBuy;
        int glyph;
        if (!gamepad) {
            glyph = block ? 5 : 2;
        } else if (block) {
            glyph = ps ? 4 : 3;
        } else {
            glyph = ps ? 1 : 0;
        }
        if (glyph != lastGlyph) {
            lastGlyph = glyph;
            DrawPromptGlyph(m, glyph, "_root.DBPrompt");
        }
        SetNum(m, "_root.DBPrompt._x", static_cast<double>(Hooks::promptX));
        SetNum(m, "_root.DBPrompt._y", static_cast<double>(Hooks::promptY));
    }

    // Hold meter: stays empty during the tap window, then fills over the fill time
    // (so a quick tap never flashes the bar - it only climbs once the hold engages).
    float frac = 0.0f;
    if (Hooks::cartHoldActive && show) {
        auto* s = Settings::GetSingleton();
        // Block quick buy/sell removes the tap-window delay for the barter key, so the
        // meter starts filling immediately on press (matches Hooks::AdvanceMovieBart).
        const float tapWindow = s->blockQuickBuy ? 0.0f : s->cartHoldThreshold;
        const float fillTime  = s->cartHoldFillTime;
        if (fillTime > 0.0f && Hooks::cartHoldTimer > tapWindow) {
            frac = (Hooks::cartHoldTimer - tapWindow) / fillTime;
            if (frac > 1.0f) frac = 1.0f;
        }
    }
    const bool meterVisible = show && frac > 0.0f;
    if (meterVisible != lastMeterVisible) {
        lastMeterVisible = meterVisible;
        SetBool(m, "_root.DBPrompt.meterTrack._visible", meterVisible);
        SetBool(m, "_root.DBPrompt.meterFill._visible", meterVisible);
    }
    if (meterVisible && frac != lastMeterFrac) {
        lastMeterFrac = frac;
        SetNum(m, "_root.DBPrompt.meterFill._width", static_cast<double>((kMeterW - 2.0) * frac));
    }
}

void BarterCartMenu::UpdateCartPanel(RE::GFxMovieView* m) {
    auto* cart = CartManager::GetSingleton();
    auto* settings = Settings::GetSingleton();
    const int cartCount = static_cast<int>(cart->Count());

    // Visible by default the moment the menu opens (with an empty-state hint), not
    // just after the first add. Falls back to count-gated when the option is off.
    const bool panelVisible = settings->cartVisibleByDefault || cartCount > 0;
    if (panelVisible != lastPanelVisible) {
        lastPanelVisible = panelVisible;
        SetBool(m, "_root.DBCart._visible", panelVisible);
    }
    if (!panelVisible) {
        lastCartCount = cartCount;
        return;
    }

    // Cart hint glyph = the BARTER key/button to HOLD (always the barter variant,
    // never the activate glyph). Redraw only when the input device/style changes.
    {
        const bool gamepad = Hooks::promptGamepad;
        const bool ps = Settings::GetSingleton()->gamepadIconStyle == GamepadIconStyle::PlayStation;
        const int hg = !gamepad ? 2 : (ps ? 1 : 0);
        if (hg != lastHintGlyph) {
            lastHintGlyph = hg;
            DrawPromptGlyph(m, hg, "_root.DBCart.hintGlyph");
        }
    }

    // Relationship standing -> base-price effect (shown as a buy discount/markup %).
    auto* mgr = BarterManager::GetSingleton();
    const bool hasMerchant = mgr->GetCurrentMerchant() != nullptr;
    const float buyMult = mgr->GetCurrentPriceMult(true);
    const float sellMult = mgr->GetCurrentPriceMult(false);

    // Net the player pays/receives using the SAME standing-adjusted prices the offer
    // window and the vanilla item cards use (raw subtotal x per-direction multiplier,
    // rounded at the subtotal level exactly like BarterManager::ShowCartOffer), so all
    // three displays agree. The cart still stores raw values; this is display-only.
    const int adjBuy  = static_cast<int>(std::lround(cart->GetBuySubtotal()  * buyMult));
    const int adjSell = static_cast<int>(std::lround(cart->GetSellSubtotal() * sellMult));
    const int net = adjBuy - adjSell;
    const int relMilli = static_cast<int>(std::lround((buyMult - 1.0f) * 1000.0f));

    if (cartCount == lastCartCount && net == lastNet && relMilli == lastRelMilli) return;
    lastCartCount = cartCount;
    lastNet = net;
    lastRelMilli = relMilli;

    const ThemePalette& th = CurrentTheme();

    // Header: title + item count.
    std::string header =
        "<font color=\"" + std::string(th.accent) + "\" size=\"15\"><b>BARTER CART</b></font>"
        "<font color=\"" + std::string(th.textMuted) + "\" size=\"12\">  (" + std::to_string(cartCount) + ")</font>";
    SetStr(m, "_root.DBCart.header.htmlText", MakeHtml(header, 15, th.accent, "left"));

    // Totals: net is positive when the player pays (net buy), negative when the
    // player receives gold (net sell). Sell items contribute +gold to the player.
    // "You Receive" stays a fixed functional green; "You Pay"/"Even" follow the theme.
    std::string netLabel;
    const char* netCol;
    if (cartCount == 0) { netLabel = "Cart is empty"; netCol = th.textMuted; }
    else if (net > 0)   { netLabel = "You Pay: " + std::to_string(net) + " Gold"; netCol = th.accent; }
    else if (net < 0)   { netLabel = "You Receive: " + std::to_string(-net) + " Gold"; netCol = "#9ACD6A"; }
    else                { netLabel = "Even Trade"; netCol = th.accent; }
    SetStr(m, "_root.DBCart.totals.htmlText",
        MakeHtml("<b>" + netLabel + "</b>", 14, netCol, "left"));

    // Relationship discount / markup line (green = favorable, red = unfavorable).
    {
        const int discPct = static_cast<int>(std::lround((1.0f - buyMult) * 100.0f));
        std::string relText;
        const char* relCol;
        if (!hasMerchant || !settings->relationshipPricing || discPct == 0) {
            relText = "Standing: market prices";
            relCol = th.textMuted;
        } else if (discPct > 0) {
            relText = "Standing: " + std::to_string(discPct) + "% buy discount";
            relCol = "#9ACD6A";
        } else {
            relText = "Standing: +" + std::to_string(-discPct) + "% buy markup";
            relCol = "#D98A6A";
        }
        SetStr(m, "_root.DBCart.relInfo.htmlText", MakeHtml(relText, 11, relCol, "left"));
    }

    // Hint adapts to whether anything is staged yet.
    SetStr(m, "_root.DBCart.hint.htmlText",
        MakeHtml(cartCount == 0 ? "Add items, then hold to open." : "Hold to open the offer.",
                 11, th.accentDim, "left"));

    // Items: one per line. Buying = gold leaving the player (shown -), selling =
    // gold coming to the player (shown +). A bullet + a faint separator per row.
    //
    // Dynamic fit: the items field has a fixed height, so as more rows are added we
    // shrink the row text to keep everything visible, down to a minimum readable size.
    // Each row costs roughly (font + spacer)*~1.3px of leading; solve for the largest
    // font in [kMinItemFont, kBaseItemFont] that keeps all rows inside the field.
    constexpr int    kBaseItemFont = 12;
    constexpr int    kMinItemFont  = 9;
    constexpr double kItemsH       = kPanelH - kItemsY - kPad;  // available list height
    const int n = cartCount > 0 ? cartCount : 1;
    int itemFont = kMinItemFont;  // floor if nothing larger fits
    for (int f = kBaseItemFont; f >= kMinItemFont; --f) {
        const int spacer = (f - 6) > 3 ? (f - 6) : 3;        // matches spacer row below
        const double rowH = (static_cast<double>(f) + spacer) * 1.32 + 1.0;
        if (n * rowH <= kItemsH) { itemFont = f; break; }    // largest size that fits
    }
    const int spacerFont = (itemFont - 6) > 3 ? (itemFont - 6) : 3;
    const int bulletFont = itemFont > 1 ? itemFont - 1 : itemFont;
    const std::string fs = std::to_string(itemFont);
    const std::string bs = std::to_string(bulletFont);
    const std::string ss = std::to_string(spacerFont);

    std::string inner;
    for (const auto& e : cart->GetEntries()) {
        // Standing-adjusted per-line price (matches the vanilla item cards, which round
        // per item). Buys use the buy multiplier, sells the sell multiplier.
        const float lineMult = e.isBuying ? buyMult : sellMult;
        const int line = static_cast<int>(std::lround(e.count * e.marketUnitPrice * lineMult));
        std::string name = EscapeHtml(e.name);
        if (e.count > 1) name += "  x" + std::to_string(e.count);
        // Buy price uses the theme accent; sell price stays a fixed functional green.
        const char* col = e.isBuying ? th.accent : "#9ACD6A";
        const char* sign = e.isBuying ? "-" : "+";
        inner +=
            "<font color=\"" + std::string(th.accent) + "\" size=\"" + bs + "\">\xE2\x80\xA2 </font>"  // bullet
            "<font color=\"" + std::string(th.textSecondary) + "\" size=\"" + fs + "\">" + name + "</font>"
            "  <font color=\"" + std::string(col) + "\" size=\"" + fs + "\"><b>" + sign +
            std::to_string(line) + "</b></font>"
            "<br/><font color=\"#3A3526\" size=\"" + ss + "\"> </font><br/>";   // thin spacer row
    }

    SetStr(m, "_root.DBCart.items.htmlText", MakeHtml(inner, itemFont, th.textSecondary, "left"));
}
