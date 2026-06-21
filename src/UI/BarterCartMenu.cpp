#include "PCH.h"
#include "UI/BarterCartMenu.h"
#include "UI/ScaleformUI.h"
#include "CartManager.h"
#include "Hooks.h"
#include "Settings.h"

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
    constexpr std::int32_t kTrim        = 0xC8B43C;  // gold trim
    constexpr std::int32_t kTrimDim     = 0x6B5E2A;  // dim gold (separators)
    constexpr std::int32_t kMeterFill   = 0xE0C864;  // bright gold
    constexpr std::int32_t kMeterTrack  = 0x000000;

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
    constexpr double kHintY     = 72.0;
    constexpr double kSep2Y     = 96.0;
    constexpr double kItemsY    = 102.0;

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

    // Draw the keybind prompt glyph. Keyboard => rounded gold key-cap with the
    // letter; gamepad => a filled controller face button with the correctly
    // coloured PlayStation/Xbox symbol, so it reads as a real pad button rather
    // than a plain square text box.
    void DrawPromptGlyph(RE::GFxMovieView* m, int glyph) {
        // glyph: 0 = Xbox Y, 1 = PlayStation (triangle), 2 = keyboard B
        const bool gamepad = (glyph != 2);

        // (Re)create the art clip so repeated calls don't stack drawings.
        m->Invoke("_root.DBPrompt.glyphArt.removeMovieClip", nullptr, nullptr, 0);
        CreateClip(m, "_root.DBPrompt", "glyphArt", 0.0);
        const char* art = "_root.DBPrompt.glyphArt";

        const double S = 20.0;        // button footprint
        const double cx = S * 0.5, cy = S * 0.5;

        if (!gamepad) {
            // Keyboard key-cap: dark rounded square with gold trim + "B".
            LineStyle(m, art, 1.5, kTrim, 100.0);
            BeginFill(m, art, kPanelBg, 95.0);
            // simple square cap (rounded look comes from the small size)
            MoveTo(m, art, 1.0, 1.0);
            LineTo(m, art, S - 1.0, 1.0);
            LineTo(m, art, S - 1.0, S - 1.0);
            LineTo(m, art, 1.0, S - 1.0);
            LineTo(m, art, 1.0, 1.0);
            EndFill(m, art);
            SetStr(m, "_root.DBPrompt.keyField.htmlText", MakeHtml("B", 13, "#F0DCA0", "center"));
            SetBool(m, "_root.DBPrompt.keyField._visible", true);
            return;
        }

        // Gamepad: round button housing.
        const std::int32_t housing = 0x16140F;   // near-black housing
        LineStyle(m, art, 1.0, 0x000000, 0.0);
        BeginFill(m, art, housing, 100.0);
        DrawCircle(m, art, cx, cy, S * 0.5 - 0.5);
        EndFill(m, art);

        if (glyph == 1) {
            // PlayStation triangle (green), drawn as a filled symbol.
            LineStyle(m, art, 1.4, 0x59D17A, 100.0);
            const double r = 5.6;
            MoveTo(m, art, cx, cy - r);
            LineTo(m, art, cx + r * 0.866, cy + r * 0.5);
            LineTo(m, art, cx - r * 0.866, cy + r * 0.5);
            LineTo(m, art, cx, cy - r);
            SetBool(m, "_root.DBPrompt.keyField._visible", false);
        } else {
            // Xbox "Y" (amber) rendered as a coloured letter on the housing.
            SetStr(m, "_root.DBPrompt.keyField.htmlText", MakeHtml("Y", 13, "#E6C24A", "center"));
            SetBool(m, "_root.DBPrompt.keyField._visible", true);
        }
    }

    // Gold corner brackets + dim section separators, drawn into _root.DBCart.frame.
    void DrawCartFrame(RE::GFxMovieView* m) {
        const char* f = "_root.DBCart.frame";
        const double W = kPanelW, H = kPanelH, L = 16.0;

        LineStyle(m, f, 2.0, kTrim, 100.0);
        // Top-left
        MoveTo(m, f, 0.0, L);   LineTo(m, f, 0.0, 0.0); LineTo(m, f, L, 0.0);
        // Top-right
        MoveTo(m, f, W - L, 0.0); LineTo(m, f, W, 0.0); LineTo(m, f, W, L);
        // Bottom-left
        MoveTo(m, f, 0.0, H - L); LineTo(m, f, 0.0, H); LineTo(m, f, L, H);
        // Bottom-right
        MoveTo(m, f, W - L, H); LineTo(m, f, W, H); LineTo(m, f, W, H - L);

        // Section separators
        LineStyle(m, f, 1.0, kTrimDim, 90.0);
        MoveTo(m, f, kPad, kSep1Y);     LineTo(m, f, W - kPad, kSep1Y);
        MoveTo(m, f, kPad, kSep2Y);     LineTo(m, f, W - kPad, kSep2Y);
    }
}

void BarterCartMenu::OnBarterOpen() {
    built = false;
    lastPromptVisible = true;
    lastGlyph = -1;
    lastPanelVisible = true;
    lastCartCount = -1;
    lastNet = -2147483647;
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
    SetStr(m, "_root.DBPrompt.labelField.htmlText", MakeHtml("Barter", 13, "#E6D796"));

    CreateText(m, "_root.DBPrompt", "meterTrack", 3.0, 0.0, kPromptH + 3.0, kMeterW, 4.0);
    StyleText(m, "_root.DBPrompt.meterTrack", false, true, kMeterTrack, true, kTrim);

    CreateText(m, "_root.DBPrompt", "meterFill", 4.0, 1.0, kPromptH + 4.0, kMeterW - 2.0, 2.0);
    StyleText(m, "_root.DBPrompt.meterFill", false, true, kMeterFill, false, 0);

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
    CreateText(m, "_root.DBCart", "bg", 1.0, 0.0, 0.0, kPanelW, kPanelH);
    StyleText(m, "_root.DBCart.bg", false, true, kPanelBg, false, 0);
    SetNum(m, "_root.DBCart.bg._alpha", 92.0);

    CreateClip(m, "_root.DBCart", "frame", 2.0);
    DrawCartFrame(m);

    // Header / totals / hint (fixed) and the item list (flows down).
    CreateText(m, "_root.DBCart", "header", 3.0, kPad, kHeaderY, kPanelW - kPad * 2.0, 24.0);
    StyleText(m, "_root.DBCart.header", false, false, 0, false, 0);
    SetStr(m, "_root.DBCart.header.htmlText", MakeHtml("BARTER CART", 15, "#E0C864", "left"));

    CreateText(m, "_root.DBCart", "totals", 4.0, kPad, kTotalsY, kPanelW - kPad * 2.0, 26.0);
    StyleText(m, "_root.DBCart.totals", false, false, 0, false, 0);

    CreateText(m, "_root.DBCart", "hint", 5.0, kPad, kHintY, kPanelW - kPad * 2.0, 22.0);
    StyleText(m, "_root.DBCart.hint", true, false, 0, false, 0);
    SetStr(m, "_root.DBCart.hint.htmlText",
        MakeHtml("Hold the Barter button to open the offer.", 11, "#8C7B3C", "left"));

    CreateText(m, "_root.DBCart", "items", 6.0, kPad, kItemsY,
               kPanelW - kPad * 2.0, kPanelH - kItemsY - kPad);
    StyleText(m, "_root.DBCart.items", true, false, 0, false, 0);

    SetBool(m, "_root.DBCart._visible", false);

    built = true;
    logger::info("CartOverlay: injected DBPrompt + DBCart into BarterMenu movie");
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
        const int glyph = !gamepad ? 2 : (ps ? 1 : 0);
        if (glyph != lastGlyph) {
            lastGlyph = glyph;
            DrawPromptGlyph(m, glyph);
        }
        SetNum(m, "_root.DBPrompt._x", static_cast<double>(Hooks::promptX));
        SetNum(m, "_root.DBPrompt._y", static_cast<double>(Hooks::promptY));
    }

    // Hold meter (fills while the cart button is held).
    float frac = 0.0f;
    if (Hooks::cartHoldActive && show) {
        float thr = Settings::GetSingleton()->cartHoldThreshold;
        if (thr > 0.0f) frac = Hooks::cartHoldTimer / thr;
        if (frac > 1.0f) frac = 1.0f;
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
    const int cartCount = static_cast<int>(cart->Count());

    const bool panelVisible = cartCount > 0;
    if (panelVisible != lastPanelVisible) {
        lastPanelVisible = panelVisible;
        SetBool(m, "_root.DBCart._visible", panelVisible);
    }
    if (!panelVisible) {
        lastCartCount = cartCount;
        return;
    }

    const int net = cart->GetNetAmount();
    if (cartCount == lastCartCount && net == lastNet) return;
    lastCartCount = cartCount;
    lastNet = net;

    // Header: title + item count.
    std::string header =
        "<font color=\"#E0C864\" size=\"15\"><b>BARTER CART</b></font>"
        "<font color=\"#808080\" size=\"12\">  (" + std::to_string(cartCount) + ")</font>";
    SetStr(m, "_root.DBCart.header.htmlText", MakeHtml(header, 15, "#E0C864", "left"));

    // Totals: net is positive when the player pays (net buy), negative when the
    // player receives gold (net sell). Sell items contribute +gold to the player.
    std::string netLabel;
    const char* netCol;
    if (net > 0)      { netLabel = "You Pay: " + std::to_string(net); netCol = "#E0B070"; }
    else if (net < 0) { netLabel = "You Receive: " + std::to_string(-net); netCol = "#9ACD6A"; }
    else              { netLabel = "Even Trade"; netCol = "#C8B464"; }
    SetStr(m, "_root.DBCart.totals.htmlText",
        MakeHtml("<b>" + netLabel + "</b>", 14, netCol, "left"));

    // Items: one per line. Buying = gold leaving the player (shown -), selling =
    // gold coming to the player (shown +). A bullet + a faint separator per row.
    std::string inner;
    for (const auto& e : cart->GetEntries()) {
        const int line = e.count * e.marketUnitPrice;
        std::string name = EscapeHtml(e.name);
        if (e.count > 1) name += "  x" + std::to_string(e.count);
        const char* col = e.isBuying ? "#E0B070" : "#9ACD6A";
        const char* sign = e.isBuying ? "-" : "+";
        inner +=
            "<font color=\"#C8B464\" size=\"11\">\xE2\x80\xA2 </font>"          // bullet
            "<font color=\"#DCDCDC\" size=\"12\">" + name + "</font>"
            "  <font color=\"" + std::string(col) + "\" size=\"12\"><b>" + sign +
            std::to_string(line) + "</b></font>"
            "<br/><font color=\"#3A3526\" size=\"6\"> </font><br/>";            // thin spacer row
    }

    SetStr(m, "_root.DBCart.items.htmlText", MakeHtml(inner, 12, "#DCDCDC", "left"));
}
