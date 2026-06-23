#include "PCH.h"
#include "UI/ScaleformUI.h"
#include "BarterManager.h"
#include "Hooks.h"
#include "Settings.h"
#include "BarterSounds.h"
#include "Integration/ChimBridge.h"
#include "DebugLog.h"
#include <limits>

namespace {
    // Set whenever a "by 5" step (bumpers/shoulder/PgUp-PgDn) adjusts the slider, so the
    // shared slider-movement feedback in AdvanceMovie can play the distinct big-step cue
    // instead of the single-step click. Cleared once that feedback frame consumes it.
    // UI input + Advance run on the main thread, so a plain bool is sufficient.
    bool g_step5Pending = false;

    // Build an inline keybind glyph: <img> referencing an exported SWF sprite
    // (linkage name set via ExportAssets in generate_swf.py).
    inline std::string GlyphImg(const char* linkage) {
        // vspace nudges the glyph onto the text baseline. Matches vanilla Skyrim's
        // inline-image convention (e.g. "<img src='BestIcon.png' vspace='2'>").
        // Requires _global.gfxExtensions=true in the SWF or GFx ignores <img>.
        return std::string("<img src='") + linkage + "' vspace='2' />";
    }
    inline bool UsePlayStationGlyphs() {
        return Settings::GetSingleton()->gamepadIconStyle == GamepadIconStyle::PlayStation;
    }
    // Logical action glyphs. Keyboard uses keycaps; gamepad respects the icon style.
    inline std::string GlyphAccept(bool gamepad) {
        if (!gamepad) return GlyphImg("g_kbd_e");
        return UsePlayStationGlyphs() ? GlyphImg("g_ps_cross") : GlyphImg("g_xb_a");
    }
    inline std::string GlyphCancel(bool gamepad) {
        if (!gamepad) return GlyphImg("g_kbd_tab");
        return UsePlayStationGlyphs() ? GlyphImg("g_ps_circle") : GlyphImg("g_xb_b");
    }
    inline std::string GlyphReoffer(bool gamepad) {
        if (!gamepad) return GlyphImg("g_kbd_r");
        return UsePlayStationGlyphs() ? GlyphImg("g_ps_square") : GlyphImg("g_xb_x");
    }
    inline std::string GlyphAdjust(bool gamepad) {
        return gamepad ? GlyphImg("g_pad") : GlyphImg("g_kbd_arrows");
    }
    inline std::string GlyphBumpers() {
        return UsePlayStationGlyphs() ? (GlyphImg("g_ps_l1") + GlyphImg("g_ps_r1"))
                                      : (GlyphImg("g_xb_lb") + GlyphImg("g_xb_rb"));
    }
    // Wrap a hint line. A leading non-breaking space ensures the first element is
    // not an <img> (GFx will not render an image as the very first item in a field).
    inline std::string HintLine(const std::string& inner) {
        return "<p align=\"center\"><font face=\"$EverywhereMediumFont\" size=\"10\" color=\"#9a8c78\">&#160;"
             + inner + "</font></p>";
    }
    inline std::string OfferHintHtml(bool gamepad) {
        std::string s = GlyphAccept(gamepad) + " Confirm  " + GlyphCancel(gamepad) + " Cancel  ";
        if (gamepad) s += GlyphBumpers() + " +/-5  ";
        s += GlyphAdjust(gamepad) + " Adjust";
        return HintLine(s);
    }
    inline std::string CounterHintHtml(bool gamepad) {
        return HintLine(GlyphAccept(gamepad) + " Accept  " + GlyphReoffer(gamepad) + " Re-offer  "
                        + GlyphCancel(gamepad) + " Walk Away");
    }
    inline std::string ResultHintHtml(bool gamepad) {
        return HintLine(GlyphAccept(gamepad) + " Retry  " + GlyphCancel(gamepad) + " Cancel");
    }

    // Relationship state name + color + a short descriptive blurb (à la the
    // reference relationship display).
    inline const char* RelStateName(int rel) {
        if (rel >= 60) return "Trusted";
        if (rel >= 30) return "Friendly";
        if (rel >= 10) return "Warm";
        if (rel >= -10) return "Neutral";
        if (rel >= -30) return "Cool";
        if (rel >= -60) return "Hostile";
        return "Despised";
    }
    inline const char* RelStateColor(int rel) {
        if (rel >= 30) return "#5ABF5A";
        if (rel >= 10) return "#9FC85A";
        if (rel >= -10) return "#D4B054";
        if (rel >= -30) return "#D08030";
        return "#C85050";
    }
    inline const char* RelBlurb(int rel) {
        if (rel >= 30) return "Values you as a friend. More flexible on price.";
        if (rel >= 10) return "Warming to you. Open to a good deal.";
        if (rel >= -10) return "Open to reasonable offers.";
        if (rel >= -30) return "Wary of you. Drives a harder bargain.";
        return "Dislikes lowball offers. Easily offended.";
    }

    // Map an acceptance chance to verdict text + color. The ACCEPT band starts at
    // the SAME threshold BarterManager uses to guarantee acceptance, so a shown
    // "will ACCEPT" can never be rejected on submit.
    inline void AcceptanceBand(float chance, std::string& text, std::string& color) {
        if (chance >= BarterManager::kGuaranteedAcceptThreshold) {
            text = "Merchant will ACCEPT this offer"; color = "#50B050";
        } else if (chance >= 60.0f) {
            text = "Merchant will likely accept";      color = "#80B050";
        } else if (chance >= 40.0f) {
            text = "Merchant looks uncertain";          color = "#D4B054";
        } else if (chance >= 20.0f) {
            text = "Merchant looks unimpressed";        color = "#D08030";
        } else {
            text = "Merchant will likely REFUSE";       color = "#B05050";
        }
    }
}

BarterOfferMenu::BarterOfferMenu() {
    DbgLog("BarterOfferMenu: Constructing menu, loading SWF from '{}'", MENU_PATH);

    // FxDelegate must be set up BEFORE LoadMovieEx so BSScaleformManager can connect it
    fxDelegate.reset(new RE::FxDelegate());
    auto callback = RE::make_gptr<FxDelegateCallback>();
    fxDelegate->RegisterHandler(callback.get());

    auto* scaleformManager = RE::BSScaleformManager::GetSingleton();
    if (scaleformManager) {
        scaleformManager->LoadMovieEx(this, MENU_PATH,
            [](RE::GFxMovieDef* a_def) -> void {
                a_def->SetState(
                    RE::GFxState::StateType::kLog,
                    RE::make_gptr<RE::GFxLog>().get());
            });
    } else {
        logger::error("BarterOfferMenu: BSScaleformManager is null!");
    }

    auto view = uiMovie;
    if (view) {
        view->SetMouseCursorCount(1);
        view->SetVisible(true);

        // Force 60 FPS minimum for smooth slider/button interaction
        RE::GFxValue highQuality;
        highQuality.SetNumber(2.0);  // HIGH quality
        view->SetVariable("_quality", highQuality);

        DbgLog("BarterOfferMenu: SWF loaded successfully (movie=0x{:X}), FxDelegate registered",
            reinterpret_cast<uintptr_t>(view.get()));
    } else {
        logger::error("BarterOfferMenu: Failed to load SWF from '{}' - uiMovie is null", MENU_PATH);
    }

    menuFlags.set(RE::UI_MENU_FLAGS::kModal);
    menuFlags.set(RE::UI_MENU_FLAGS::kRequiresUpdate);
    menuFlags.set(RE::UI_MENU_FLAGS::kRendersOffscreenTargets);
    menuFlags.set(RE::UI_MENU_FLAGS::kUsesCursor);
    depthPriority = 10;
    inputContext = Context::kNone;
    DbgLog("BarterOfferMenu: Menu flags set (Modal|RequiresUpdate|RendersOffscreen|UsesCursor), depth={}", depthPriority);

    // With SkyrimSouls - Unpaused Game Menus the barter menu keeps the world running so
    // CHIM NPCs can react live while haggling. Make sure this offer window never re-adds
    // a pause/freeze on top of that, and tag it with SkyrimSouls' own "unpaused" flag bit
    // (1<<28) so its combat-pause bookkeeping leaves us alone too.
    if (ChimBridge::SkyrimSoulsActive() && Settings::GetSingleton()->chimUnpauseOfferWindow) {
        constexpr auto kSoulsUnpaused = static_cast<RE::UI_MENU_FLAGS>(1u << 28);
        menuFlags.reset(RE::UI_MENU_FLAGS::kPausesGame);
        menuFlags.reset(RE::UI_MENU_FLAGS::kFreezeFrameBackground);
        menuFlags.set(RE::UI_MENU_FLAGS::kRequiresUpdate);
        menuFlags.set(kSoulsUnpaused);
        DbgLog("BarterOfferMenu: SkyrimSouls detected - offer window marked unpaused");
    }
}

void BarterOfferMenu::Register() {
    auto* ui = RE::UI::GetSingleton();
    if (ui) {
        ui->Register(MENU_NAME, Creator);
        DbgLog("BarterOfferMenu registered");
    }
}

RE::IMenu* BarterOfferMenu::Creator() {
    return new BarterOfferMenu();
}

void BarterOfferMenu::Show() {
    DbgLog("BarterOfferMenu::Show() - Requesting menu open");
    auto* msgQ = RE::UIMessageQueue::GetSingleton();
    if (msgQ) {
        msgQ->AddMessage(MENU_NAME, RE::UI_MESSAGE_TYPE::kShow, nullptr);
    } else {
        logger::error("BarterOfferMenu::Show() - UIMessageQueue is null!");
    }
}

void BarterOfferMenu::Hide() {
    DbgLog("BarterOfferMenu::Hide() - Requesting menu close");
    auto* msgQ = RE::UIMessageQueue::GetSingleton();
    if (msgQ) {
        msgQ->AddMessage(MENU_NAME, RE::UI_MESSAGE_TYPE::kHide, nullptr);
    }
}

void BarterOfferMenu::ApplyHintCells(int state) {
    if (!uiMovie) return;
    const bool gamepad = lastInputWasGamepad;
    const bool ps = Settings::GetSingleton()->gamepadIconStyle == GamepadIconStyle::PlayStation;

    auto setVis = [this](const char* path, bool v) {
        RE::GFxValue val; val.SetBoolean(v);
        uiMovie->SetVariable(path, val);
    };
    // Glyph icons were placed with alpha=0 (CxFormWithAlpha). _visible alone doesn't
    // change alpha, so we must also drive _alpha to make them appear/disappear.
    auto showGlyph = [this](const char* instanceName) {
        std::string base = std::string("_root.") + instanceName;
        RE::GFxValue t; t.SetBoolean(true);
        uiMovie->SetVariable((base + "._visible").c_str(), t);
        RE::GFxValue a; a.SetNumber(100.0);
        uiMovie->SetVariable((base + "._alpha").c_str(), a);
    };
    auto setLbl = [this](const char* path, const char* text) {
        std::string html =
            std::string("<p align=\"left\"><font face=\"$EverywhereMediumFont\" size=\"10\" color=\"#9a8c78\">")
            + text + "</font></p>";
        RE::GFxValue val; val.SetString(html.c_str());
        uiMovie->SetVariable(path, val);
    };

    // Hide every glyph variant: set _visible=false AND _alpha=0 (belt-and-suspenders
    // because the initial placement uses alpha=0 CxForm which persists).
    static const char* allGlyphNames[] = {
        "hg_e", "hg_a", "hg_cr",
        "hg_r_intim", "hg_x_intim", "hg_sq_intim",
        "hg_tab", "hg_b", "hg_ci",
        "hg_r",
        "hg_ar", "hg_pad",
        "hg_lb", "hg_rb", "hg_l1", "hg_r1",
        "hg_tab2", "hg_b2", "hg_ci2",
    };
    for (auto* name : allGlyphNames) {
        std::string base = std::string("_root.") + name;
        RE::GFxValue f; f.SetBoolean(false);
        uiMovie->SetVariable((base + "._visible").c_str(), f);
        RE::GFxValue z; z.SetNumber(0.0);
        uiMovie->SetVariable((base + "._alpha").c_str(), z);
    }
    setVis("_root.ButtonHintText._visible", false);
    setVis("_root.HintLbl1._visible", false);
    setVis("_root.HintLbl2._visible", false);
    setVis("_root.HintLbl3._visible", false);
    setVis("_root.HintLbl4._visible", false);
    setVis("_root.HintLbl5._visible", false);  // bumper "by 5" cue (gamepad offer only)

    if (state < 0) return;  // hide-all (e.g. accepted result screen)

    // Slot 0 (under Submit): Confirm / Accept / Retry -> E / A / Cross
    showGlyph(gamepad ? (ps ? "hg_cr" : "hg_a") : "hg_e");

    if (state == 1) {  // counter
        // Slot 1 (under Intimidate): Re-offer -> R / X / Square
        showGlyph(gamepad ? (ps ? "hg_sq_intim" : "hg_x_intim") : "hg_r");
        // Slot 2 (under Cancel): Walk Away -> Tab / B / Circle
        showGlyph(gamepad ? (ps ? "hg_ci2" : "hg_b2") : "hg_tab2");
        setLbl("_root.HintLbl1.htmlText", "Accept");
        setLbl("_root.HintLbl2.htmlText", "Re-offer");
        setLbl("_root.HintLbl3.htmlText", "Walk Away");
        setVis("_root.HintLbl4._visible", false);
    } else if (state == 2) {  // result
        // Slot 2 (under Cancel): Cancel -> Tab / B / Circle
        showGlyph(gamepad ? (ps ? "hg_ci" : "hg_b") : "hg_tab");
        setLbl("_root.HintLbl1.htmlText", "Retry");
        setLbl("_root.HintLbl2.htmlText", "");
        setLbl("_root.HintLbl3.htmlText", "Cancel");
        setVis("_root.HintLbl2._visible", false);
        setVis("_root.HintLbl4._visible", false);
    } else {  // offer
        // Slot 1 (under Intimidate): Intimidate -> R / X / Square
        showGlyph(gamepad ? (ps ? "hg_sq_intim" : "hg_x_intim") : "hg_r_intim");
        // Slot 2 (under Cancel): Cancel -> Tab / B / Circle
        showGlyph(gamepad ? (ps ? "hg_ci" : "hg_b") : "hg_tab");
        // Slot 3 (under slider): Adjust -> Arrows / DPad
        showGlyph(gamepad ? "hg_pad" : "hg_ar");
        setLbl("_root.HintLbl1.htmlText", "Confirm");
        setLbl("_root.HintLbl2.htmlText", "Intimidate");
        setLbl("_root.HintLbl3.htmlText", "Cancel");
        setLbl("_root.HintLbl4.htmlText", "Adjust");
        setVis("_root.HintLbl4._visible", true);
        // Slot 3b (right of Adjust): shoulder bumpers move the slider by 5. Gamepad
        // only -> show the LB/RB (or L1/R1) pair + "by 5"; hidden entirely on keyboard.
        // The pair is nudged left and scaled down a touch vs. the other (full-size)
        // glyphs so it reads as a secondary cue next to "Adjust".
        if (gamepad) {
            const char* g1 = ps ? "hg_l1" : "hg_lb";
            const char* g2 = ps ? "hg_r1" : "hg_rb";
            auto placeBumper = [this](const char* name, double x, double y, double scale) {
                std::string base = std::string("_root.") + name;
                RE::GFxValue v;
                v.SetNumber(x);     uiMovie->SetVariable((base + "._x").c_str(), v);
                v.SetNumber(y);     uiMovie->SetVariable((base + "._y").c_str(), v);
                v.SetNumber(scale); uiMovie->SetVariable((base + "._xscale").c_str(), v);
                v.SetNumber(scale); uiMovie->SetVariable((base + "._yscale").c_str(), v);
            };
            constexpr double kBumpY = 320.0, kBumpScale = 78.0, kBumpX0 = 686.0, kBumpStep = 22.0;
            showGlyph(g1);
            showGlyph(g2);
            placeBumper(g1, kBumpX0, kBumpY, kBumpScale);
            placeBumper(g2, kBumpX0 + kBumpStep, kBumpY, kBumpScale);
            setLbl("_root.HintLbl5.htmlText", "by 5");
            RE::GFxValue lx; lx.SetNumber(kBumpX0 + kBumpStep + 34.0);
            uiMovie->SetVariable("_root.HintLbl5._x", lx);
            setVis("_root.HintLbl5._visible", true);
        }
    }
    setVis("_root.HintLbl1._visible", true);
    setVis("_root.HintLbl2._visible", true);
    setVis("_root.HintLbl3._visible", true);
}

void BarterOfferMenu::PositionCoin() {
    if (!uiMovie) return;
    RE::GFxValue twVal;
    if (uiMovie->GetVariable(&twVal, "_root.PriceText.textWidth") && twVal.IsNumber()) {
        double tw = twVal.GetNumber();
        // PriceText is center-aligned with a visual center at stage x=640. Park the
        // coin just left of the number's left edge (640 - tw/2), with a small gap.
        double coinX = 640.0 - (tw / 2.0) - 22.0;
        RE::GFxValue cx; cx.SetNumber(coinX);
        uiMovie->SetVariable("_root.coinIcon._x", cx);
    }
}

void BarterOfferMenu::SetButtonFill(const char* buttonName, float progress) {
    if (!uiMovie) return;
    // Ease-in-out (smoothstep) so the bar accelerates into the fill then eases as it
    // tops out - a smooth "charging" feel rather than a linear crawl.
    const float t = std::clamp(progress, 0.0f, 1.0f);
    const float eased = t * t * (3.0f - 2.0f * t);
    RE::GFxValue xs; xs.SetNumber(static_cast<double>(eased * 100.0f));
    uiMovie->SetVariable(std::format("_root.{}.bgFill._xscale", buttonName).c_str(), xs);
    // The clip is placed with a ColorTransform alpha-multiply of 0 (that's how the SWF
    // generator hides clips), so toggling _visible alone leaves it transparent. Drive
    // _alpha to reveal it; the shape itself carries its own ~47% tint for the soft look.
    RE::GFxValue alpha; alpha.SetNumber(t > 0.0f ? 100.0 : 0.0);
    uiMovie->SetVariable(std::format("_root.{}.bgFill._alpha", buttonName).c_str(), alpha);
    RE::GFxValue vis; vis.SetBoolean(t > 0.0f);
    uiMovie->SetVariable(std::format("_root.{}.bgFill._visible", buttonName).c_str(), vis);
}

void BarterOfferMenu::UpdateHoldToConfirm(float interval, bool mouseHeld) {
    auto* settings = Settings::GetSingleton();

    // Disabled, or not in the offer state (counter/result use instant buttons), or still
    // in the open cooldown -> clear both bars and require a fresh press before charging.
    if (!uiMovie || !settings->holdToConfirm || showingCounter || showingResult || inputCooldown > 0.0f) {
        if (submitHoldElapsed != 0.0f || intimidateHoldElapsed != 0.0f) {
            submitHoldElapsed = 0.0f;
            intimidateHoldElapsed = 0.0f;
            SetButtonFill("btn_submit", 0.0f);
            SetButtonFill("btn_intimidate", 0.0f);
        }
        submitHoldArmed = false;
        intimidateHoldArmed = false;
        return;
    }

    auto* sink = InputDeviceSink::GetSingleton();
    const float dur = std::max(0.05f, settings->holdToConfirmSec);

    // Intimidate is only offered when the perk is present (button visible).
    bool intimidateAvailable = false;
    {
        RE::GFxValue vis;
        if (uiMovie->GetVariable(&vis, "_root.btn_intimidate._visible") && vis.IsBool())
            intimidateAvailable = vis.GetBool();
    }

    // hoveredButton indices match the buttons[] table: 0 = submit, 1 = intimidate.
    const bool mouseOnSubmit     = mouseHeld && hoveredButton == 0;
    const bool mouseOnIntimidate = mouseHeld && hoveredButton == 1;
    bool submitDown     = mouseOnSubmit || (!mouseHeld && sink->IsConfirmHeld());
    bool intimidateDown = intimidateAvailable &&
                          (mouseOnIntimidate || (!mouseHeld && sink->IsIntimidateHeld()));
    if (submitDown) intimidateDown = false;  // never charge both at once

    // --- Submit (gold) bar ---
    if (!submitDown) {
        submitHoldArmed = true;  // released -> re-arm for the next deliberate hold
        if (submitHoldElapsed != 0.0f) { submitHoldElapsed = 0.0f; SetButtonFill("btn_submit", 0.0f); }
    } else if (submitHoldArmed) {
        submitHoldElapsed += interval;
        SetButtonFill("btn_submit", submitHoldElapsed / dur);
        if (submitHoldElapsed >= dur) {
            submitHoldArmed = false;
            submitHoldElapsed = 0.0f;
            SetButtonFill("btn_submit", 0.0f);
            inputCooldown = 0.4f;
            RE::GFxValue sliderVal;
            uiMovie->GetVariable(&sliderVal, "_root.sliderValue");
            const int offeredPrice = sliderVal.IsNumber() ? static_cast<int>(std::round(sliderVal.GetNumber())) : 0;
            SKSE::GetTaskInterface()->AddTask([offeredPrice]() {
                BarterManager::GetSingleton()->OnPlayerOffer(offeredPrice);
            });
            return;
        }
    }

    // --- Intimidate (red) bar ---
    if (!intimidateDown) {
        intimidateHoldArmed = true;
        if (intimidateHoldElapsed != 0.0f) { intimidateHoldElapsed = 0.0f; SetButtonFill("btn_intimidate", 0.0f); }
    } else if (intimidateHoldArmed) {
        intimidateHoldElapsed += interval;
        SetButtonFill("btn_intimidate", intimidateHoldElapsed / dur);
        if (intimidateHoldElapsed >= dur) {
            intimidateHoldArmed = false;
            intimidateHoldElapsed = 0.0f;
            SetButtonFill("btn_intimidate", 0.0f);
            inputCooldown = 0.4f;
            SKSE::GetTaskInterface()->AddTask([]() {
                BarterManager::GetSingleton()->OnIntimidateAttempt();
            });
            return;
        }
    }
}

void BarterOfferMenu::AdvanceMovie(float a_interval, std::uint32_t a_currentTime) {
    // Tracks the slider value between frames to detect movement (any input method) and
    // play a feedback click. NaN = uninitialised / freshly opened (skip first compare).
    static double s_lastSliderValue = std::numeric_limits<double>::quiet_NaN();

    if (pendingOfferData.has_value() && uiMovie) {
        DbgLog("BarterOfferMenu::AdvanceMovie - Applying pending offer data");
        SetOfferData(pendingOfferData.value());
        pendingOfferData.reset();
        s_lastSliderValue = std::numeric_limits<double>::quiet_NaN();
    }

    // Tick down input cooldown (prevents activation key from auto-submitting)
    if (inputCooldown > 0.0f) {
        inputCooldown -= a_interval;
        // Drain any queued inputs during cooldown. Cancel is included so a latched
        // cancel/back press from the BarterMenu (e.g. the key that opened this window)
        // can't instantly close the freshly-opened offer window on its first frame.
        if (auto* sink = InputDeviceSink::GetSingleton()) {
            sink->ConsumeAccept();
            sink->ConsumeX();
            sink->ConsumeR();
            sink->ConsumeCancel();
        }
    }

    // Slider movement feedback: play a click whenever the value changes in the offer
    // state (covers mouse drag, D-pad/stick steps, and the LB/RB "by 5" bumpers).
    if (uiMovie && !showingCounter && !showingResult) {
        RE::GFxValue sv;
        if (uiMovie->GetVariable(&sv, "_root.sliderValue") && sv.IsNumber()) {
            const double cur = sv.GetNumber();
            if (!std::isnan(s_lastSliderValue) && cur != s_lastSliderValue) {
                BarterSounds::Play(g_step5Pending ? BarterSounds::Event::SliderStep5
                                                  : BarterSounds::Event::MoveSlider);
            }
            s_lastSliderValue = cur;
            // Consume the by-5 marker each feedback frame (whether or not the value moved,
            // so a clamped/no-op big step can't mislabel a later single step).
            g_step5Pending = false;
        }
    }

    // --- Responsive slider: C++ mouse drag tracking ---
    if (uiMovie && !showingResult) {
        bool mouseHeld = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;
        static bool mouseWasHeld = false;
        bool mouseJustPressed = mouseHeld && !mouseWasHeld;
        bool mouseJustReleased = !mouseHeld && mouseWasHeld;
        mouseWasHeld = mouseHeld;

        // Use ROOT space coordinates for all hit testing
        RE::GFxValue rootXM, rootYM;
        uiMovie->GetVariable(&rootXM, "_root._xmouse");
        uiMovie->GetVariable(&rootYM, "_root._ymouse");
        double mx = (rootXM.GetType() == RE::GFxValue::ValueType::kNumber) ? rootXM.GetNumber() : -1.0;
        double my = (rootYM.GetType() == RE::GFxValue::ValueType::kNumber) ? rootYM.GetNumber() : -1.0;

        // Slider absolute bounds in root space (must match generate_swf.py)
        constexpr double sliderAbsX = 540.0;  // 640 - 100
        constexpr double sliderAbsY = 298.0;  // panel_y(160) + 138
        constexpr double sliderTrackW = 200.0;
        constexpr double sliderTrackH = 8.0;

        // Only allow slider interaction in offer state (not counter or result)
        if (!showingCounter && !showingResult && mouseHeld) {
            // Start drag only if clicking within the slider track area (root space)
            if (!sliderDragging && mouseJustPressed) {
                if (mx >= sliderAbsX - 5 && mx <= sliderAbsX + sliderTrackW + 5 &&
                    my >= sliderAbsY - 12 && my <= sliderAbsY + sliderTrackH + 12) {
                    sliderDragging = true;
                }
            }

            // While dragging, update slider from mouse X position
            if (sliderDragging) {
                double localX = mx - sliderAbsX;
                if (localX < 0.0) localX = 0.0;
                if (localX > sliderTrackW) localX = sliderTrackW;

                RE::GFxValue sliderMinV, sliderMaxV;
                uiMovie->GetVariable(&sliderMinV, "_root.sliderMin");
                uiMovie->GetVariable(&sliderMaxV, "_root.sliderMax");
                double minVal = (sliderMinV.GetType() == RE::GFxValue::ValueType::kNumber) ? sliderMinV.GetNumber() : 0.0;
                double maxVal = (sliderMaxV.GetType() == RE::GFxValue::ValueType::kNumber) ? sliderMaxV.GetNumber() : 100.0;

                double newValue = minVal + (maxVal - minVal) * (localX / sliderTrackW);
                newValue = std::round(newValue);
                newValue = std::clamp(newValue, minVal, maxVal);
                RE::GFxValue val;
                val.SetNumber(newValue);
                uiMovie->SetVariable("_root.sliderValue", val);
                uiMovie->Invoke("_root.updateSliderDisplay", nullptr, nullptr, 0);
            }
        } else {
            sliderDragging = false;
        }

        // --- C++ Button hit-testing (bypasses broken AS mouse events) ---
        constexpr int panel_x = 470;
        constexpr int panel_y = 160;
        constexpr int btn_w = 80;
        constexpr int btn_h = 24;
        constexpr int btn_gap = 8;
        constexpr int btn_start_x = panel_x + 42;
        constexpr int btn_y_pos = panel_y + 252;

        struct ButtonDef { const char* name; int x; int y; };
        ButtonDef buttons[] = {
            // Offer state buttons
            {"btn_submit", btn_start_x, btn_y_pos},
            {"btn_intimidate", btn_start_x + btn_w + btn_gap, btn_y_pos},
            {"btn_cancel", btn_start_x + 2 * (btn_w + btn_gap), btn_y_pos},
            // Counter-offer state buttons
            {"btn_accept", btn_start_x, btn_y_pos},
            {"btn_reoffer", btn_start_x + btn_w + btn_gap, btn_y_pos},
            {"btn_walkaway", btn_start_x + 2 * (btn_w + btn_gap), btn_y_pos},
            // Result state button
            {"btn_continue", btn_start_x + btn_w + btn_gap, btn_y_pos},
        };
        constexpr int numButtons = 7;

        int newHovered = -1;
        for (int i = 0; i < numButtons; i++) {
            // Check button visibility first
            std::string visPath = std::format("_root.{}._visible", buttons[i].name);
            RE::GFxValue btnVis;
            uiMovie->GetVariable(&btnVis, visPath.c_str());
            if (btnVis.GetType() == RE::GFxValue::ValueType::kBoolean && !btnVis.GetBool())
                continue;

            if (mx >= buttons[i].x && mx <= buttons[i].x + btn_w &&
                my >= buttons[i].y && my <= buttons[i].y + btn_h) {
                newHovered = i;
                break;
            }
        }

        // Update hover state (use _alpha, keep bgNormal visible but dimmed)
        if (newHovered != hoveredButton) {
            // Unhover old - restore full opacity on normal, hide highlight
            if (hoveredButton >= 0 && hoveredButton < numButtons) {
                RE::GFxValue full, zero;
                full.SetNumber(100.0); zero.SetNumber(0.0);
                uiMovie->SetVariable(std::format("_root.{}.bgNormal._alpha", buttons[hoveredButton].name).c_str(), full);
                uiMovie->SetVariable(std::format("_root.{}.bgHighlight._alpha", buttons[hoveredButton].name).c_str(), zero);
            }
            // Hover new - dim normal, show highlight
            if (newHovered >= 0) {
                RE::GFxValue dim, full;
                dim.SetNumber(40.0); full.SetNumber(100.0);
                uiMovie->SetVariable(std::format("_root.{}.bgNormal._alpha", buttons[newHovered].name).c_str(), dim);
                uiMovie->SetVariable(std::format("_root.{}.bgHighlight._alpha", buttons[newHovered].name).c_str(), full);
            }
            hoveredButton = newHovered;
        }

        // Handle click on mouse-down within a button (don't require drag release).
        // When hold-to-confirm is on, Submit/Intimidate are driven by UpdateHoldToConfirm
        // (fill-and-commit) instead of an instant click; Cancel and the counter/result
        // buttons always commit instantly.
        const bool holdMode = Settings::GetSingleton()->holdToConfirm;
        if (mouseJustPressed && hoveredButton >= 0) {
            const char* btnName = buttons[hoveredButton].name;
            DbgLog("BarterOfferMenu: Button clicked: {}", btnName);
            if (std::string_view(btnName) == "btn_submit") {
                if (!holdMode) {
                    RE::GFxValue sliderVal;
                    uiMovie->GetVariable(&sliderVal, "_root.sliderValue");
                    if (sliderVal.GetType() == RE::GFxValue::ValueType::kNumber) {
                        int offeredPrice = static_cast<int>(std::round(sliderVal.GetNumber()));
                        SKSE::GetTaskInterface()->AddTask([offeredPrice]() {
                            BarterManager::GetSingleton()->OnPlayerOffer(offeredPrice);
                        });
                    }
                }
            } else if (std::string_view(btnName) == "btn_intimidate") {
                if (!holdMode) {
                    SKSE::GetTaskInterface()->AddTask([]() {
                        BarterManager::GetSingleton()->OnIntimidateAttempt();
                    });
                }
            } else if (std::string_view(btnName) == "btn_cancel") {
                SKSE::GetTaskInterface()->AddTask([]() {
                    BarterManager::GetSingleton()->OnCancelled();
                });
            } else if (std::string_view(btnName) == "btn_accept") {
                // Accept counter-offer
                SKSE::GetTaskInterface()->AddTask([]() {
                    BarterManager::GetSingleton()->OnCounterResponse(0);
                });
            } else if (std::string_view(btnName) == "btn_reoffer") {
                // Re-offer: restore slider UI for a new attempt
                showingCounter = false;
                RestoreOfferUI();
                SKSE::GetTaskInterface()->AddTask([]() {
                    BarterManager::GetSingleton()->OnCounterResponse(1);
                });
            } else if (std::string_view(btnName) == "btn_walkaway") {
                // Walk away from counter-offer
                SKSE::GetTaskInterface()->AddTask([]() {
                    BarterManager::GetSingleton()->OnCounterResponse(2);
                });
            } else if (std::string_view(btnName) == "btn_continue") {
                showingResult = false;
                if (lastResultAccepted) {
                    SKSE::GetTaskInterface()->AddTask([]() {
                        BarterManager::GetSingleton()->OnCancelled();
                    });
                } else {
                    RestoreOfferUI();
                }
            }
        }

        // Hold-to-fill confirm/intimidate: charge the gold/red bar while the trigger is
        // held (mouse on the button, A/E for submit, X/R for intimidate) and commit when
        // full. Runs every frame so it can also decay/hide the bar on release.
        UpdateHoldToConfirm(a_interval, mouseHeld);

        // --- Update input hints based on actual input device (via InputDeviceSink) ---
        auto* inputSink = InputDeviceSink::GetSingleton();
        bool currentGamepad = inputSink->IsUsingGamepad();
        if (currentGamepad != lastInputWasGamepad) {
            lastInputWasGamepad = currentGamepad;
            ApplyHintCells(showingCounter ? 1 : showingResult ? 2 : 0);
        }

        // Keep the coin glyph hugging the (center-aligned) price number every frame.
        if (!showingCounter) {
            PositionCoin();
        }

        // Ease the relationship-meter marker toward its target (visible only in the
        // offer state). Same idea as the slider handle: code-driven movie-clip motion.
        if (!showingCounter && !showingResult && relMarkerCurX >= 0.0f) {
            float diff = relMarkerTargetX - relMarkerCurX;
            if (std::abs(diff) > 0.25f) {
                relMarkerCurX += diff * 0.25f;
                RE::GFxValue mx; mx.SetNumber(relMarkerCurX);
                uiMovie->SetVariable("_root.relBarMC.marker._x", mx);
            }
        }

        // --- Raw gamepad input via InputDeviceSink (bypasses broken input context) ---

        // Handle gamepad accept/cancel buttons directly
        if (inputSink->ConsumeAccept()) {
            if (inputCooldown > 0.0f) {
                // Ignore - cooldown active
            } else if (showingResult) {
                showingResult = false;
                RestoreOfferUI();
            } else if (showingCounter) {
                // A button in counter state = accept the counter-offer
                inputCooldown = 0.4f;
                SKSE::GetTaskInterface()->AddTask([]() {
                    BarterManager::GetSingleton()->OnCounterResponse(0);
                });
            } else if (uiMovie && !Settings::GetSingleton()->holdToConfirm) {
                // Instant submit only when hold-to-confirm is off; otherwise the held A/E
                // input charges the fill via UpdateHoldToConfirm below.
                RE::GFxValue sliderVal;
                uiMovie->GetVariable(&sliderVal, "_root.sliderValue");
                if (sliderVal.GetType() == RE::GFxValue::ValueType::kNumber) {
                    int offeredPrice = static_cast<int>(std::round(sliderVal.GetNumber()));
                    inputCooldown = 0.4f;  // Prevent double-input
                    SKSE::GetTaskInterface()->AddTask([offeredPrice]() {
                        BarterManager::GetSingleton()->OnPlayerOffer(offeredPrice);
                    });
                }
            }
        }
        if (inputSink->ConsumeCancel()) {
            if (inputCooldown > 0.0f) {
                // Ignore - cooldown active (swallows the press that opened this window)
            } else if (showingResult) {
                showingResult = false;
                RestoreOfferUI();
            } else if (showingCounter) {
                // B button in counter state = walk away
                SKSE::GetTaskInterface()->AddTask([]() {
                    BarterManager::GetSingleton()->OnCounterResponse(2);
                });
            } else {
                SKSE::GetTaskInterface()->AddTask([]() {
                    BarterManager::GetSingleton()->OnCancelled();
                });
            }
        }
        // X button = re-offer (counter state) OR intimidate (offer state)
        if (inputSink->ConsumeX()) {
            if (showingCounter) {
                showingCounter = false;
                RestoreOfferUI();
                SKSE::GetTaskInterface()->AddTask([]() {
                    BarterManager::GetSingleton()->OnCounterResponse(1);
                });
            } else if (!showingResult && !Settings::GetSingleton()->holdToConfirm) {
                inputCooldown = 0.3f;
                SKSE::GetTaskInterface()->AddTask([]() {
                    BarterManager::GetSingleton()->OnIntimidateAttempt();
                });
            }
        }
        // Keyboard R = re-offer (counter state) OR intimidate (offer state)
        if (inputSink->ConsumeR()) {
            if (showingCounter) {
                showingCounter = false;
                RestoreOfferUI();
                SKSE::GetTaskInterface()->AddTask([]() {
                    BarterManager::GetSingleton()->OnCounterResponse(1);
                });
            } else if (!showingResult && !Settings::GetSingleton()->holdToConfirm) {
                inputCooldown = 0.3f;
                SKSE::GetTaskInterface()->AddTask([]() {
                    BarterManager::GetSingleton()->OnIntimidateAttempt();
                });
            }
        }
        // Consume Y to prevent accidental input passthrough
        inputSink->ConsumeY();

        // Handle D-pad/bumper directional input for slider (only in offer state). Read
        // (don't consume) the held direction so a sustained press keeps feeding the
        // hold-to-repeat block below instead of clearing itself after one frame.
        int rawDir = inputSink->GetGamepadDir();
        if (!showingCounter && !showingResult && rawDir != 0 && uiMovie) {
            // D-pad press: if it's a NEW direction, move exactly 1 and start grace timer
            if (rawDir != gamepadHoldDir) {
                // New direction or first press: exactly 1 step
                RE::GFxValue arg;
                int step = (rawDir == -5 || rawDir == 5) ? rawDir : (rawDir > 0 ? 1 : -1);
                if (step == 5 || step == -5) g_step5Pending = true;
                arg.SetNumber(static_cast<double>(step));
                uiMovie->Invoke("_root.adjustSlider", nullptr, &arg, 1);
                gamepadHoldDir = (rawDir > 0) ? 1 : -1;  // Normalize direction
                if (rawDir == -5 || rawDir == 5) gamepadHoldDir = rawDir;  // Keep bumper magnitude
                gamepadHoldTimer = 0.0f;
                gamepadGraceDone = false;
            }
        } else if (rawDir == 0 && !showingCounter && !showingResult) {
            // D-pad released - only clear if thumbstick isn't active
            float thumbXCheck = inputSink->GetThumbstickX();
            if (std::abs(thumbXCheck) <= 0.25f && gamepadHoldDir != 0) {
                gamepadHoldDir = 0;
                gamepadHoldTimer = 0.0f;
                gamepadGraceDone = false;
            }
        }

        // Handle thumbstick slider movement (only in offer state)
        float thumbX = inputSink->GetThumbstickX();
        if (!showingCounter && !showingResult && std::abs(thumbX) > 0.25f && uiMovie) {
            int dir = (thumbX < 0) ? -1 : 1;
            if (gamepadHoldDir == 0) {
                // First deflection: exactly 1 step, then start grace
                RE::GFxValue arg;
                arg.SetNumber(static_cast<double>(dir));
                uiMovie->Invoke("_root.adjustSlider", nullptr, &arg, 1);
                gamepadHoldDir = dir;
                gamepadHoldTimer = 0.0f;
                gamepadGraceDone = false;
            } else {
                // Already holding - direction maintained (handled by hold-to-repeat below)
                gamepadHoldDir = dir;
            }
        } else if (rawDir == 0 && std::abs(thumbX) <= 0.25f) {
            if (gamepadHoldDir != 0) {
                gamepadHoldDir = 0;
                gamepadHoldTimer = 0.0f;
                gamepadGraceDone = false;
            }
        }

        // --- Gamepad hold-to-repeat: accelerate slider movement AFTER grace period ---
        if (!showingCounter && !showingResult && gamepadHoldDir != 0) {
            gamepadHoldTimer += a_interval;
            // Grace period: 0.3s of no repeat
            if (gamepadHoldTimer >= 0.3f) {
                gamepadGraceDone = true;
            }
            // Only repeat after grace period
            if (gamepadGraceDone) {
                float repeatInterval = (gamepadHoldTimer < 1.0f) ? 0.08f : 0.03f;
                float speed = (gamepadHoldTimer < 1.0f) ? 1.0f : 2.0f;
                // Use frame-rate-independent accumulator
                static float repeatAccum = 0.0f;
                repeatAccum += a_interval;
                if (repeatAccum >= repeatInterval) {
                    repeatAccum -= repeatInterval;
                    RE::GFxValue arg;
                    int step = (std::abs(gamepadHoldDir) == 5) ? gamepadHoldDir : 
                               ((gamepadHoldDir > 0 ? 1 : -1) * static_cast<int>(speed));
                    if (step == 5 || step == -5) g_step5Pending = true;
                    arg.SetNumber(static_cast<double>(step));
                    uiMovie->Invoke("_root.adjustSlider", nullptr, &arg, 1);
                }
            }
        }

        // --- Dynamic UI updates: acceptance indicator + relationship preview (offer state only) ---
        static int updateCounter = 0;
        updateCounter++;
        if (!showingCounter && !showingResult &&
            (sliderDragging || gamepadHoldDir != 0 || (updateCounter % 10 == 0))) {
            // Hard-clamp sliderValue to never exceed sliderMax or go below sliderMin
            RE::GFxValue sliderVal, sliderMinV, sliderMaxV;
            uiMovie->GetVariable(&sliderVal, "_root.sliderValue");
            uiMovie->GetVariable(&sliderMinV, "_root.sliderMin");
            uiMovie->GetVariable(&sliderMaxV, "_root.sliderMax");
            if (sliderVal.GetType() == RE::GFxValue::ValueType::kNumber) {
                double val = sliderVal.GetNumber();
                double minV = (sliderMinV.GetType() == RE::GFxValue::ValueType::kNumber) ? sliderMinV.GetNumber() : 0.0;
                double maxV = (sliderMaxV.GetType() == RE::GFxValue::ValueType::kNumber) ? sliderMaxV.GetNumber() : 9999.0;
                if (val < minV || val > maxV) {
                    double clamped = std::clamp(val, minV, maxV);
                    RE::GFxValue clampedVal;
                    clampedVal.SetNumber(clamped);
                    uiMovie->SetVariable("_root.sliderValue", clampedVal);
                    uiMovie->Invoke("_root.updateSliderDisplay", nullptr, nullptr, 0);
                }
            }

            RE::GFxValue baseChanceVal, effPriceVal;
            uiMovie->GetVariable(&sliderVal, "_root.sliderValue");
            uiMovie->GetVariable(&baseChanceVal, "_root.acceptanceChance");
            uiMovie->GetVariable(&effPriceVal, "_root.effectivePrice");

            if (sliderVal.GetType() == RE::GFxValue::ValueType::kNumber &&
                effPriceVal.GetType() == RE::GFxValue::ValueType::kNumber) {
                double offeredGold = sliderVal.GetNumber();
                double effPrice = effPriceVal.GetNumber();
                (void)baseChanceVal;

                // Standing-effect percentage, oriented by deal direction. When BUYING,
                // paying above market (offered > market) is generous. When SELLING,
                // generosity is the opposite: asking LESS than market helps your
                // standing, asking more (overcharging) hurts it. So invert for sells.
                double rawPct = effPrice > 0 ? ((offeredGold - effPrice) / effPrice) * 100.0 : 0.0;
                double pct = currentIsBuying ? rawPct : -rawPct;

                // Use the AUTHORITATIVE chance (same code path as the real decision)
                // so the displayed verdict can never disagree with the outcome.
                float authChance = BarterManager::GetSingleton()->PreviewAcceptanceChance(
                    static_cast<int>(std::round(offeredGold)));

                std::string acceptText, acceptColor;
                AcceptanceBand(authChance, acceptText, acceptColor);

                auto html = std::format(
                    R"(<p align="center"><font face="$EverywhereMediumFont" size="10" color="{}">{}</font></p>)",
                    acceptColor, acceptText);
                RE::GFxValue htmlVal;
                htmlVal.SetString(html.c_str());
                uiMovie->SetVariable("_root.AcceptanceText.htmlText", htmlVal);

                // Detect that the player has actually moved the slider off its
                // starting (market) value. Until then we keep the relationship
                // blurb visible; afterwards we show the live offer-effect preview.
                if (std::abs(offeredGold - effPrice) >= 0.5) {
                    sliderTouched = true;
                }

                if (sliderTouched) {
                    std::string effectText;
                    std::string effectColor = "#807060";
                    if (pct < -15) {
                        effectText = "This offer will worsen your standing";
                        effectColor = "#B05050";
                    } else if (pct < -5) {
                        effectText = "This offer may slightly worsen your standing";
                        effectColor = "#D08030";
                    } else if (pct > 10) {
                        effectText = "This offer will improve your standing";
                        effectColor = "#50B050";
                    } else if (pct > 3) {
                        effectText = "This offer may slightly improve your standing";
                        effectColor = "#80B050";
                    } else {
                        effectText = "This offer has little effect on your standing";
                        effectColor = "#A09080";
                    }
                    auto effHtml = std::format(
                        R"(<p align="center"><font face="$EverywhereMediumFont" size="8" color="{}">{}</font></p>)",
                        effectColor, effectText);
                    RE::GFxValue effVal;
                    effVal.SetString(effHtml.c_str());
                    uiMovie->SetVariable("_root.RelEffectText.htmlText", effVal);
                }
            }
        }
    }

    RE::IMenu::AdvanceMovie(a_interval, a_currentTime);
}

RE::UI_MESSAGE_RESULTS BarterOfferMenu::ProcessMessage(RE::UIMessage& a_message) {
    switch (*a_message.type) {
        case RE::UI_MESSAGE_TYPE::kUserEvent: {
            auto* strData = static_cast<RE::BSUIMessageData*>(a_message.data);
            if (strData) {
                auto* userEvents = RE::UserEvents::GetSingleton();
                auto eventStr = strData->fixedStr;

                DbgLog("BarterOfferMenu event: {}", eventStr.c_str());

                if (eventStr == userEvents->cancel || eventStr == userEvents->back) {
                    // Ignore during input cooldown (prevents stale events after state transitions)
                    if (inputCooldown > 0.0f) {
                        return RE::UI_MESSAGE_RESULTS::kHandled;
                    }
                    DbgLog("BarterOfferMenu: Cancel/Back pressed");
                    gamepadHoldDir = 0;
                    if (showingResult) {
                        showingResult = false;
                        inputCooldown = 0.3f;
                        if (lastResultAccepted) {
                            SKSE::GetTaskInterface()->AddTask([]() {
                                BarterManager::GetSingleton()->OnCancelled();
                            });
                        } else {
                            RestoreOfferUI();
                        }
                        return RE::UI_MESSAGE_RESULTS::kHandled;
                    }
                    if (showingCounter) {
                        // Cancel/Back during counter = walk away
                        showingCounter = false;
                        SKSE::GetTaskInterface()->AddTask([]() {
                            BarterManager::GetSingleton()->OnCounterResponse(2);
                        });
                        return RE::UI_MESSAGE_RESULTS::kHandled;
                    }
                    SKSE::GetTaskInterface()->AddTask([]() {
                        BarterManager::GetSingleton()->OnCancelled();
                    });
                    return RE::UI_MESSAGE_RESULTS::kHandled;
                }

                if (eventStr == userEvents->accept) {
                    gamepadHoldDir = 0;
                    // Ignore accept during input cooldown (prevents activation key from auto-submitting)
                    if (inputCooldown > 0.0f) {
                        return RE::UI_MESSAGE_RESULTS::kHandled;
                    }
                    if (showingResult) {
                        DbgLog("BarterOfferMenu: Accept pressed in result state - returning to offer");
                        showingResult = false;
                        inputCooldown = 0.3f;
                        if (lastResultAccepted) {
                            SKSE::GetTaskInterface()->AddTask([]() {
                                BarterManager::GetSingleton()->OnCancelled();
                            });
                        } else {
                            RestoreOfferUI();
                        }
                        return RE::UI_MESSAGE_RESULTS::kHandled;
                    }
                    if (showingCounter) {
                        // Accept during counter = accept the counter-offer
                        DbgLog("BarterOfferMenu: Accept pressed in counter state - accepting counter");
                        inputCooldown = 0.4f;
                        SKSE::GetTaskInterface()->AddTask([]() {
                            BarterManager::GetSingleton()->OnCounterResponse(0);
                        });
                        return RE::UI_MESSAGE_RESULTS::kHandled;
                    }
                    // Hold-to-confirm: the held A/E charges the fill bar in AdvanceMovie
                    // instead of committing on the down-press. Consume the event so the
                    // press doesn't leak, but don't submit here.
                    if (Settings::GetSingleton()->holdToConfirm) {
                        return RE::UI_MESSAGE_RESULTS::kHandled;
                    }
                    DbgLog("BarterOfferMenu: Accept pressed - submitting offer");
                    if (uiMovie) {
                        RE::GFxValue sliderVal;
                        uiMovie->GetVariable(&sliderVal, "_root.sliderValue");
                        if (sliderVal.GetType() == RE::GFxValue::ValueType::kNumber) {
                            int offeredPrice = static_cast<int>(std::round(sliderVal.GetNumber()));
                            inputCooldown = 0.4f;  // Prevent double-input
                            SKSE::GetTaskInterface()->AddTask([offeredPrice]() {
                                BarterManager::GetSingleton()->OnPlayerOffer(offeredPrice);
                            });
                        }
                    }
                    return RE::UI_MESSAGE_RESULTS::kHandled;
                }

                // D-pad / arrow keys: adjust slider by 1 (only in offer state)
                if (!showingCounter && !showingResult &&
                    (eventStr == userEvents->left || eventStr == userEvents->right)) {
                    int dir = (eventStr == userEvents->left) ? -1 : 1;
                    if (uiMovie) {
                        RE::GFxValue arg;
                        arg.SetNumber(static_cast<double>(dir));
                        uiMovie->Invoke("_root.adjustSlider", nullptr, &arg, 1);
                    }
                    gamepadHoldDir = dir;
                    gamepadHoldTimer = 0.0f;
                    return RE::UI_MESSAGE_RESULTS::kHandled;
                }

                // Also catch "StrafeLeft"/"StrafeRight" which D-pad may generate in some contexts
                if (!showingCounter && !showingResult &&
                    (eventStr == userEvents->strafeLeft || eventStr == userEvents->strafeRight)) {
                    lastInputWasGamepad = true;
                    int dir = (eventStr == userEvents->strafeLeft) ? -1 : 1;
                    if (uiMovie) {
                        RE::GFxValue arg;
                        arg.SetNumber(static_cast<double>(dir));
                        uiMovie->Invoke("_root.adjustSlider", nullptr, &arg, 1);
                    }
                    gamepadHoldDir = dir;
                    gamepadHoldTimer = 0.0f;
                    return RE::UI_MESSAGE_RESULTS::kHandled;
                }

                // Bumpers (LB/RB) or PgUp/PgDn: adjust by 5
                if (!showingCounter && !showingResult &&
                    (eventStr == userEvents->prevPage || eventStr == userEvents->nextPage)) {
                    int dir = (eventStr == userEvents->prevPage) ? -5 : 5;
                    if (uiMovie) {
                        g_step5Pending = true;
                        RE::GFxValue arg;
                        arg.SetNumber(static_cast<double>(dir));
                        uiMovie->Invoke("_root.adjustSlider", nullptr, &arg, 1);
                    }
                    return RE::UI_MESSAGE_RESULTS::kHandled;
                }

                // Shoulder buttons as alternative large step
                if (!showingCounter && !showingResult &&
                    (eventStr == userEvents->leftEquip || eventStr == userEvents->rightEquip)) {
                    int dir = (eventStr == userEvents->leftEquip) ? -5 : 5;
                    if (uiMovie) {
                        g_step5Pending = true;
                        RE::GFxValue arg;
                        arg.SetNumber(static_cast<double>(dir));
                        uiMovie->Invoke("_root.adjustSlider", nullptr, &arg, 1);
                    }
                    return RE::UI_MESSAGE_RESULTS::kHandled;
                }

                // Up/Down: navigate buttons
                if (eventStr == userEvents->up || eventStr == userEvents->down) {
                    gamepadHoldDir = 0;
                    // Use navigateButtons to cycle focus
                    if (uiMovie) {
                        RE::GFxValue arg;
                        arg.SetNumber(eventStr == userEvents->up ? -1.0 : 1.0);
                        uiMovie->Invoke("_root.navigateButtons", nullptr, &arg, 1);
                    }
                    return RE::UI_MESSAGE_RESULTS::kHandled;
                }

                // Intimidate (offer state) / Re-offer (counter state): readyWeapon = R key / X gamepad
                if (eventStr == userEvents->readyWeapon) {
                    if (!showingResult) {
                        if (showingCounter) {
                            showingCounter = false;
                            RestoreOfferUI();
                            SKSE::GetTaskInterface()->AddTask([]() {
                                BarterManager::GetSingleton()->OnCounterResponse(1);
                            });
                        } else if (!Settings::GetSingleton()->holdToConfirm) {
                            // Hold-to-confirm: held X/R charges the red fill bar in
                            // AdvanceMovie; consume here without intimidating immediately.
                            inputCooldown = 0.3f;
                            SKSE::GetTaskInterface()->AddTask([]() {
                                BarterManager::GetSingleton()->OnIntimidateAttempt();
                            });
                        }
                        return RE::UI_MESSAGE_RESULTS::kHandled;
                    }
                }

                // Unrecognized event - consume it to prevent stray inputs from affecting UI
                gamepadHoldDir = 0;
            }
            // Consume all user events (don't let them reach the SWF or game)
            return RE::UI_MESSAGE_RESULTS::kHandled;
        }
        case RE::UI_MESSAGE_TYPE::kHide: {
            // Guard against a stale close. After cancelling an offer we queue a
            // kHide; if the player immediately selects another item, the menu is
            // reused for the new offer. Processing that already-queued kHide would
            // make the freshly opened window blink and vanish. Only honor kHide
            // when the barter is truly idle.
            if (BarterManager::GetSingleton()->GetState() != BarterState::Idle) {
                DbgLog("BarterOfferMenu: swallowing stale kHide (state != Idle) to prevent blink");
                return RE::UI_MESSAGE_RESULTS::kHandled;
            }
            break;
        }
        default:
            break;
    }
    return RE::IMenu::ProcessMessage(a_message);
}

void BarterOfferMenu::SetOfferData(const OfferData& data) {
    if (!uiMovie) {
        logger::error("BarterOfferMenu::SetOfferData - uiMovie is null!");
        return;
    }

    // Reset all state for a fresh offer
    currentIsBuying = data.isBuying;
    currentRelationship = data.relationship;
    relMarkerCurX = -1.0f;  // re-init the meter marker so it eases in from center
    showingCounter = false;
    showingResult = false;
    sliderDragging = false;
    gamepadHoldDir = 0;
    gamepadHoldTimer = 0.0f;
    gamepadGraceDone = false;
    hoveredButton = -1;
    inputCooldown = 0.3f;  // Prevent the activation key from immediately submitting

    // Fresh offer: clear any hold-to-fill charge and hide both fill bars.
    submitHoldElapsed = 0.0f;
    intimidateHoldElapsed = 0.0f;
    submitHoldArmed = false;
    intimidateHoldArmed = false;
    SetButtonFill("btn_submit", 0.0f);
    SetButtonFill("btn_intimidate", 0.0f);

    DbgLog("BarterOfferMenu::SetOfferData - item='{}', basePrice={}, effectivePrice={}, "
        "merchant='{}', personality='{}', relationship={}, slider=[{}%..{}%]",
        data.itemName, data.basePrice, data.effectivePrice,
        data.merchantName, data.personalityName, data.relationship,
        static_cast<int>(data.sliderMin * 100.0), static_cast<int>(data.sliderMax * 100.0));

    RE::GFxValue args[10];
    args[0].SetString(data.itemName.c_str());
    args[1].SetNumber(data.basePrice);
    args[2].SetNumber(data.effectivePrice);
    args[3].SetString(data.merchantName.c_str());
    args[4].SetString(data.personalityName.c_str());
    args[5].SetNumber(data.relationship);
    // Slider bounds come from the negotiated haggle range AROUND the (possibly price-
    // hiked) market value - not a flat 0..3x. data.sliderMin/Max are fractions of the
    // effective price from ComputeHaggleRange (relationship + personality + perks), so
    // the player can only push as far below/above market as their standing/skill earns.
    // Starting point is always the market/effective value (slider seeds to it in AS).
    const double eff = static_cast<double>(data.effectivePrice);
    int minOffer = static_cast<int>(std::floor(eff * (1.0 + data.sliderMin)));
    int maxOffer = static_cast<int>(std::ceil(eff * (1.0 + data.sliderMax)));
    if (minOffer < 0) minOffer = 0;                        // never negative gold
    if (maxOffer < minOffer + 1) maxOffer = minOffer + 1;  // always leave a usable span
    args[6].SetNumber(static_cast<double>(minOffer));
    args[7].SetNumber(static_cast<double>(maxOffer));
    args[8].SetBoolean(data.hasIntimidationPerk);
    args[9].SetNumber(data.acceptanceChance);

        DbgLog("BarterOfferMenu: Invoking _root.SetOfferData with 10 args");
    uiMovie->Invoke("_root.SetOfferData", nullptr, args, 10);

    RE::GFxValue stateArg;
    stateArg.SetString("offer");
        DbgLog("BarterOfferMenu: Invoking _root.setButtonState('offer')");
    uiMovie->Invoke("_root.setButtonState", nullptr, &stateArg, 1);

    // Diagnostic: verify button visibility after setButtonState
    RE::GFxValue btnVis;
    if (uiMovie->GetVariable(&btnVis, "_root.btn_submit._visible")) {
        DbgLog("BarterOfferMenu: btn_submit._visible = {}", btnVis.GetBool());
        if (!btnVis.GetBool()) {
            // AS function may have failed - force buttons visible from C++
            logger::warn("BarterOfferMenu: Buttons not visible after setButtonState - forcing from C++");
            RE::GFxValue trueVal;
            trueVal.SetBoolean(true);
            uiMovie->SetVariable("_root.btn_submit._visible", trueVal);
            uiMovie->SetVariable("_root.btn_intimidate._visible", trueVal);
            uiMovie->SetVariable("_root.btn_cancel._visible", trueVal);
        }
    } else {
        logger::error("BarterOfferMenu: Cannot read _root.btn_submit._visible - instance may not exist");
    }
    RE::GFxValue btnAlpha;
    if (uiMovie->GetVariable(&btnAlpha, "_root.btn_submit._alpha")) {
        DbgLog("BarterOfferMenu: btn_submit._alpha = {}", btnAlpha.GetNumber());
    }

    auto setHtml = [this](const char* fieldPath, const std::string& html) {
        RE::GFxValue val;
        val.SetString(html.c_str());
        bool success = uiMovie->SetVariable(fieldPath, val);
        if (!success) {
            logger::warn("BarterOfferMenu: SetVariable failed for '{}'", fieldPath);
        }
    };

    // Diagnostic: check if text field instances are accessible
    if (!uiMovie->IsAvailable("_root.MerchantName")) {
        logger::error("BarterOfferMenu: _root.MerchantName not available! SWF display list may be empty.");
    }
    if (!uiMovie->IsAvailable("_root.PriceText")) {
        logger::error("BarterOfferMenu: _root.PriceText not available!");
    }

    auto makeHtml = [](const char* font, int size, const char* color, const std::string& text) {
        return std::format(
            R"(<p align="center"><font face="{}" size="{}" color="{}" kerning="1">{}</font></p>)",
            font, size, color, text);
    };

    setHtml("_root.MerchantName.htmlText",
        makeHtml("$EverywhereBoldFont", 20, "#FFFFFF", data.merchantName));

    // Flavor line doubles as the buy/sell mode indicator. If the merchant already
    // refused a price for this item this session, warn the player here instead.
    if (data.sessionRejectedPrice > 0) {
        setHtml("_root.FlavorText.htmlText",
            std::format(R"(<p align="center"><font face="$EverywhereMediumFont" size="10" color="#C86464">They already refused {} gold for this &#8212; offer more.</font></p>)",
                data.sessionRejectedPrice));
    } else {
        const char* modeWord = data.isBuying ? "Buying" : "Selling";
        const char* modeColor = data.isBuying ? "#C8A050" : "#7FB0C8";
        setHtml("_root.FlavorText.htmlText",
            std::format(R"(<p align="center"><font face="$EverywhereMediumFont" size="10" color="{}">{}</font><font face="$EverywhereMediumFont" size="10" color="#999999"> &#8212; What did you have in mind?</font></p>)",
                modeColor, modeWord));
    }

    // Base price comparison
    setHtml("_root.BasePriceText.htmlText",
        makeHtml("$EverywhereMediumFont", 9, "#A09080",
            std::format("Market Price: {} gold", data.basePrice)));

    // Offer label clarifies the direction of gold flow. Big + gold so it pops.
    setHtml("_root.OfferLabel.htmlText",
        makeHtml("$EverywhereBoldFont", 14, "#E8C878",
            data.isBuying ? "You Pay" : "You Receive"));
    // Plain price string; the coin is a placed glyph repositioned by PositionCoin()
    // (inline <img> is silently dropped by this SWF's GFx build).
    setHtml("_root.PriceText.htmlText",
        makeHtml("$EverywhereBoldFont", 22, "#DAA520",
            std::format("{} gold", static_cast<int>(data.effectivePrice))));
    {
        RE::GFxValue showCoin; showCoin.SetBoolean(true);
        uiMovie->SetVariable("_root.coinIcon._visible", showCoin);
    }
    PositionCoin();
    // Track end labels reflect the actual negotiable range (min..max gold), so the
    // player can see how much room their standing/skill bought on each side of market.
    setHtml("_root.SliderText.htmlText",
        makeHtml("$EverywhereMediumFont", 9, "#C8C8C8",
            std::format("{} gold                    {} gold", minOffer, maxOffer)));

    // Acceptance chance indicator (colored by likelihood, same band as the live preview)
    {
        std::string acceptText, acceptColor;
        AcceptanceBand(data.acceptanceChance, acceptText, acceptColor);
        setHtml("_root.AcceptanceText.htmlText",
            makeHtml("$EverywhereMediumFont", 10, acceptColor.c_str(), acceptText));
    }

    // Relationship section (verbose: state + personality + descriptive blurb)
    setHtml("_root.ReactionText.htmlText",
        std::format(R"(<p align="center"><font face="$EverywhereMediumFont" size="9" color="#A09080">{} merchant  &#183;  Relationship: </font><font face="$EverywhereMediumFont" size="9" color="{}">{}</font></p>)",
            data.personalityName, RelStateColor(data.relationship), RelStateName(data.relationship)));

    // Distinct, colored relationship meter (red/yellow/green) with a moving marker.
    UpdateRelationshipMeter(data.relationship);

    // Descriptive blurb beneath the meter. Stays until the player actually moves
    // the slider, at which point the live offer-effect preview takes over.
    sliderTouched = false;
    setHtml("_root.RelEffectText.htmlText",
        makeHtml("$EverywhereMediumFont", 8, "#9A8C78", RelBlurb(data.relationship)));

    // Button hints (placed glyph icons reflecting the current input device)
    ApplyHintCells(0);

    // Intimidate button label: show percent chance directly on the button
    {
        float intChance = data.speechBonus / 200.0f;
        if (data.hasIntimidationPerk) intChance *= 2.0f;
        auto* player = RE::PlayerCharacter::GetSingleton();
        if (player) intChance += static_cast<float>(player->GetLevel()) * 0.002f;
        if (data.personalityName == "Timid") intChance += 0.25f;
        else if (data.personalityName == "Stern") intChance -= 0.20f;
        else if (data.personalityName == "Greedy") intChance -= 0.10f;
        else if (data.personalityName == "Sleazy") intChance += 0.05f;
        else if (data.personalityName == "Generous") intChance += 0.10f;
        intChance = std::clamp(intChance, 0.05f, 0.95f);
        int pct = static_cast<int>(intChance * 100.0f);

        std::string chanceColor = pct >= 60 ? "#66AA66" : (pct >= 35 ? "#AAAA66" : "#AA6666");
        std::string intLabel = std::format(
            "<p align='center'><font face='$EverywhereMediumFont' size='10' color='#CC4444'>Intimidate </font>"
            "<font face='$EverywhereMediumFont' size='9' color='{}'>({}%)</font></p>",
            chanceColor, pct);
        RE::GFxValue labelVal;
        labelVal.SetString(intLabel.c_str());
        uiMovie->SetVariable("_root.btn_intimidate.lbl.htmlText", labelVal);
    }

    // Deal history synopsis
    {
        std::string historyText;
        std::string historyColor = "#807060";
        if (!data.recentDealsJson.empty() && data.recentDealsJson != "[]") {
            try {
                auto deals = nlohmann::json::parse(data.recentDealsJson);
                if (deals.is_array() && !deals.empty()) {
                    int totalDeals = static_cast<int>(deals.size());
                    int lowballs = 0, fair = 0, generous = 0;
                    for (auto& d : deals) {
                        int base = d.value("basePrice", 0);
                        int offered = d.value("offeredPrice", 0);
                        if (base > 0) {
                            double ratio = static_cast<double>(offered) / base;
                            if (ratio < 0.7) lowballs++;
                            else if (ratio > 1.1) generous++;
                            else fair++;
                        }
                    }
                    if (lowballs > fair && lowballs > generous) {
                        historyText = "You have LOWBALLED this merchant in the past";
                        historyColor = "#B05050";
                    } else if (generous > fair && generous > lowballs) {
                        historyText = "You have been GENEROUS with this merchant before";
                        historyColor = "#50B050";
                    } else if (totalDeals >= 3) {
                        historyText = "You have traded fairly with this merchant";
                        historyColor = "#A09080";
                    } else {
                        historyText = std::format("({} previous deal{})", totalDeals, totalDeals > 1 ? "s" : "");
                        historyColor = "#706050";
                    }
                }
            } catch (...) {}
        }
        if (!historyText.empty()) {
            setHtml("_root.DealHistoryText.htmlText",
                makeHtml("$EverywhereMediumFont", 8, historyColor.c_str(), historyText));
        }
    }

    // Set button labels with proper HTML font tags
    setHtml("_root.btn_submit.labelField.htmlText",
        makeHtml("$EverywhereMediumFont", 9, "#DAA520", "Submit Offer"));
    setHtml("_root.btn_intimidate.labelField.htmlText",
        makeHtml("$EverywhereMediumFont", 9, "#CC4444", "Intimidate"));
    setHtml("_root.btn_cancel.labelField.htmlText",
        makeHtml("$EverywhereMediumFont", 9, "#A0A0A0", "Cancel"));
    setHtml("_root.btn_accept.labelField.htmlText",
        makeHtml("$EverywhereMediumFont", 9, "#66CC66", "Accept"));
    setHtml("_root.btn_reoffer.labelField.htmlText",
        makeHtml("$EverywhereMediumFont", 9, "#DAA520", "Re-Offer"));
    setHtml("_root.btn_walkaway.labelField.htmlText",
        makeHtml("$EverywhereMediumFont", 9, "#A0A0A0", "Walk Away"));
    setHtml("_root.btn_continue.labelField.htmlText",
        makeHtml("$EverywhereMediumFont", 9, "#FFFFFF", "Continue"));

    DbgLog("BarterOfferMenu: SetOfferData applied ({})", data.itemName);
}

void BarterOfferMenu::UpdateRelationshipMeter(int relationship) {
    if (!uiMovie) return;

    // Normalize -100..100 to a 0..120px position on the color-zoned track. The bar
    // background is colored (red -> amber -> green) so the marker's position alone
    // conveys standing; no growing fill is needed.
    double relNorm = std::clamp((relationship + 100.0) / 200.0, 0.0, 1.0);

    auto setVar = [this](const char* path, double num) {
        RE::GFxValue v; v.SetNumber(num);
        uiMovie->SetVariable(path, v);
    };

    // The marker sits at the standing position along the 120px track. Animate it:
    // set the target here; AdvanceMovie eases toward it.
    relMarkerTargetX = static_cast<float>(relNorm * 120.0);
    if (relMarkerCurX < 0.0f) {
        relMarkerCurX = 60.0f;  // start from the neutral midpoint for an intro slide
    }
    setVar("_root.relBarMC.marker._x", relMarkerCurX);
}

void BarterOfferMenu::UpdateRelationship(int relationship) {
    currentRelationship = relationship;
    // Only animate the meter while the offer view is actually showing; if we're on the
    // counter/result screen the stored value is re-applied by RestoreOfferUI on return.
    if (!showingCounter && !showingResult) {
        UpdateRelationshipMeter(relationship);
    }
}

void BarterOfferMenu::SetCounterOffer(int amount, int patience) {
    if (!uiMovie) return;

    showingCounter = true;
    showingResult = false;
    inputCooldown = 0.3f;  // Prevent lingering accept from instantly accepting counter

    // Hide the ENTIRE barter window FIRST (before any AS invocation that might re-show)
    RE::GFxValue hideVal;
    hideVal.SetBoolean(false);
    uiMovie->SetVariable("_root.panelBG._visible", hideVal);
    uiMovie->SetVariable("_root.sliderMC._visible", hideVal);
    uiMovie->SetVariable("_root.arrowLeft._visible", hideVal);
    uiMovie->SetVariable("_root.arrowRight._visible", hideVal);
    uiMovie->SetVariable("_root.SliderText._visible", hideVal);
    uiMovie->SetVariable("_root.AcceptanceText._visible", hideVal);
    uiMovie->SetVariable("_root.RelEffectText._visible", hideVal);
    uiMovie->SetVariable("_root.DealHistoryText._visible", hideVal);
    uiMovie->SetVariable("_root.MerchantName._visible", hideVal);
    uiMovie->SetVariable("_root.FlavorText._visible", hideVal);
    uiMovie->SetVariable("_root.ornament._visible", hideVal);
    uiMovie->SetVariable("_root.BasePriceText._visible", hideVal);
    uiMovie->SetVariable("_root.OfferLabel._visible", hideVal);
    uiMovie->SetVariable("_root.coinIcon._visible", hideVal);
    uiMovie->SetVariable("_root.relBarMC._visible", hideVal);
    // Hide offer buttons
    uiMovie->SetVariable("_root.btn_submit._visible", hideVal);
    uiMovie->SetVariable("_root.btn_intimidate._visible", hideVal);
    uiMovie->SetVariable("_root.btn_cancel._visible", hideVal);
    // Hide counter buttons (hints shown instead)
    uiMovie->SetVariable("_root.btn_accept._visible", hideVal);
    uiMovie->SetVariable("_root.btn_reoffer._visible", hideVal);
    uiMovie->SetVariable("_root.btn_walkaway._visible", hideVal);

    // Call AS to set state (after hiding so it can't re-show arrows)
    RE::GFxValue args[2];
    args[0].SetNumber(amount);
    args[1].SetNumber(patience);
    uiMovie->Invoke("_root.ShowCounterOffer", nullptr, args, 2);

    auto setHtml = [this](const char* fieldPath, const std::string& html) {
        RE::GFxValue val;
        val.SetString(html.c_str());
        uiMovie->SetVariable(fieldPath, val);
    };

    // Show only the counter text fields
    RE::GFxValue showVal;
    showVal.SetBoolean(true);
    uiMovie->SetVariable("_root.PriceText._visible", showVal);
    uiMovie->SetVariable("_root.StatusText._visible", showVal);
    uiMovie->SetVariable("_root.ReactionText._visible", showVal);

    // Prominent counter-offer display
    std::string counterHtml = std::format(
        R"(<p align="center"><font face="$EverywhereBoldFont" size="22" color="#FFCC00">Counter: {} gold</font></p>)",
        amount);
    std::string patienceDesc;
    if (patience <= 1) {
        patienceDesc = "Final offer \u2014 the merchant\u2019s patience is spent.";
    } else if (patience == 2) {
        patienceDesc = std::format("The merchant is losing patience. ({} attempts remain)", patience);
    } else {
        patienceDesc = std::format("The merchant is willing to negotiate further. ({} attempts remain)", patience);
    }
    std::string patienceHtml = std::format(
        R"(<p align="center"><font face="$EverywhereMediumFont" size="11" color="#C8C8C8">{}</font></p>)",
        patienceDesc);

    setHtml("_root.PriceText.htmlText", counterHtml);
    setHtml("_root.StatusText.htmlText",
        R"(<p align="center"><font face="$EverywhereMediumFont" size="12" color="#E0C080">The merchant makes a counter-offer</font></p>)");
    setHtml("_root.ReactionText.htmlText", patienceHtml);

    // Button hints for counter state (placed glyph icons)
    ApplyHintCells(1);

    DbgLog("BarterOfferMenu: SetCounterOffer - {} gold, patience {}", amount, patience);
}

void BarterOfferMenu::SetResult(bool accepted, int goldAmount, int relDelta) {
    if (!uiMovie) return;

    showingResult = true;
    showingCounter = false;
    lastResultAccepted = accepted;
    inputCooldown = 0.5f;

    // Hide the ENTIRE barter window (panel, decorations, all elements)
    RE::GFxValue hideVal;
    hideVal.SetBoolean(false);
    uiMovie->SetVariable("_root.panelBG._visible", hideVal);
    uiMovie->SetVariable("_root.sliderMC._visible", hideVal);
    uiMovie->SetVariable("_root.arrowLeft._visible", hideVal);
    uiMovie->SetVariable("_root.arrowRight._visible", hideVal);
    uiMovie->SetVariable("_root.SliderText._visible", hideVal);
    uiMovie->SetVariable("_root.AcceptanceText._visible", hideVal);
    uiMovie->SetVariable("_root.RelEffectText._visible", hideVal);
    uiMovie->SetVariable("_root.DealHistoryText._visible", hideVal);
    uiMovie->SetVariable("_root.ButtonHintText._visible", hideVal);
    uiMovie->SetVariable("_root.MerchantName._visible", hideVal);
    uiMovie->SetVariable("_root.FlavorText._visible", hideVal);
    uiMovie->SetVariable("_root.ornament._visible", hideVal);
    uiMovie->SetVariable("_root.BasePriceText._visible", hideVal);
    uiMovie->SetVariable("_root.OfferLabel._visible", hideVal);
    uiMovie->SetVariable("_root.coinIcon._visible", hideVal);
    uiMovie->SetVariable("_root.relBarMC._visible", hideVal);

    // Hide all buttons
    RE::GFxValue stateArg;
    stateArg.SetString("result");
    uiMovie->Invoke("_root.setButtonState", nullptr, &stateArg, 1);
    // Also hide the continue button (we don't need it)
    uiMovie->SetVariable("_root.btn_continue._visible", hideVal);

    auto setHtml = [this](const char* fieldPath, const std::string& html) {
        RE::GFxValue val;
        val.SetString(html.c_str());
        uiMovie->SetVariable(fieldPath, val);
    };

    // Show only the result text fields (PriceText, StatusText, ReactionText are still visible)
    RE::GFxValue showVal;
    showVal.SetBoolean(true);
    uiMovie->SetVariable("_root.PriceText._visible", showVal);
    uiMovie->SetVariable("_root.StatusText._visible", showVal);
    uiMovie->SetVariable("_root.ReactionText._visible", showVal);

    if (accepted) {
        std::string priceHtml =
            R"(<p align="center"><font face="$EverywhereBoldFont" size="28" color="#50FF50">ACCEPTED</font></p>)";
        // Show relationship effect clearly
        std::string subHtml;
        if (relDelta > 0) {
            subHtml = std::format(
                R"(<p align="center"><font face="$EverywhereMediumFont" size="11" color="#90C890">Relationship improved (+{})</font></p>)",
                relDelta);
        } else if (relDelta < 0) {
            subHtml = std::format(
                R"(<p align="center"><font face="$EverywhereMediumFont" size="11" color="#C89060">Relationship worsened ({})</font></p>)",
                relDelta);
        } else {
            subHtml =
                R"(<p align="center"><font face="$EverywhereMediumFont" size="11" color="#A0A090">Standing unchanged</font></p>)";
        }

        setHtml("_root.PriceText.htmlText", priceHtml);
        // Hide gold amount — just show empty status
        RE::GFxValue hideStatus;
        hideStatus.SetBoolean(false);
        uiMovie->SetVariable("_root.StatusText._visible", hideStatus);
        setHtml("_root.ReactionText.htmlText", subHtml);
        ApplyHintCells(-1);  // no hints on the accepted screen
    } else {
        std::string priceHtml =
            R"(<p align="center"><font face="$EverywhereBoldFont" size="28" color="#FF5050">REJECTED</font></p>)";
        // Relationship change is now chance-based, so the loss may be 0.
        std::string relHtml;
        if (relDelta < 0) {
            relHtml = std::format(
                R"(<p align="center"><font face="$EverywhereMediumFont" size="12" color="#FF8080">Relationship {}</font></p>)",
                relDelta);
        } else {
            relHtml =
                R"(<p align="center"><font face="$EverywhereMediumFont" size="12" color="#A09080">Relationship unchanged</font></p>)";
        }
        // Direction-aware advice
        std::string noteHtml;
        if (currentIsBuying) {
            noteHtml =
                R"(<p align="center"><font face="$EverywhereMediumFont" size="10" color="#C89060">They won't take that &#8212; offer more to win them over.</font></p>)";
        } else {
            noteHtml =
                R"(<p align="center"><font face="$EverywhereMediumFont" size="10" color="#C89060">They won't pay that much &#8212; lower your asking price.</font></p>)";
        }

        setHtml("_root.PriceText.htmlText", priceHtml);
        setHtml("_root.StatusText.htmlText", relHtml);
        setHtml("_root.ReactionText.htmlText", noteHtml);

        // Show the placed glyph keybind hints (Retry / Cancel) for the result screen.
        ApplyHintCells(2);
    }
}

void BarterOfferMenu::RestoreOfferUI() {
    if (!uiMovie) return;

    // Clear state flags
    showingCounter = false;
    showingResult = false;
    gamepadHoldDir = 0;
    gamepadHoldTimer = 0.0f;
    gamepadGraceDone = false;
    sliderDragging = false;
    hoveredButton = -1;

    // Reset hold-to-fill so a key still held from the previous screen can't instantly
    // re-charge; the player must press again. Also hide both fill bars.
    submitHoldElapsed = 0.0f;
    intimidateHoldElapsed = 0.0f;
    submitHoldArmed = false;
    intimidateHoldArmed = false;
    SetButtonFill("btn_submit", 0.0f);
    SetButtonFill("btn_intimidate", 0.0f);
    inputCooldown = 0.3f;  // brief guard so the returning key-press doesn't auto-charge

    // Tell the BarterManager we're retrying
    BarterManager::GetSingleton()->RetryOffer();

    // Restore button state to "offer"
    RE::GFxValue stateArg;
    stateArg.SetString("offer");
    uiMovie->Invoke("_root.setButtonState", nullptr, &stateArg, 1);

    // Force buttons visible from C++ (AS setButtonState is unreliable in this GFx build)
    RE::GFxValue trueVal;
    trueVal.SetBoolean(true);
    uiMovie->SetVariable("_root.btn_submit._visible", trueVal);
    uiMovie->SetVariable("_root.btn_intimidate._visible", trueVal);
    uiMovie->SetVariable("_root.btn_cancel._visible", trueVal);
    RE::GFxValue falseVal;
    falseVal.SetBoolean(false);
    uiMovie->SetVariable("_root.btn_accept._visible", falseVal);
    uiMovie->SetVariable("_root.btn_reoffer._visible", falseVal);
    uiMovie->SetVariable("_root.btn_walkaway._visible", falseVal);
    uiMovie->SetVariable("_root.btn_continue._visible", falseVal);

    // Show elements that were hidden during counter/result
    RE::GFxValue showVal;
    showVal.SetBoolean(true);
    // Panel structure
    uiMovie->SetVariable("_root.panelBG._visible", showVal);
    uiMovie->SetVariable("_root.MerchantName._visible", showVal);
    uiMovie->SetVariable("_root.FlavorText._visible", showVal);
    uiMovie->SetVariable("_root.ornament._visible", showVal);
    uiMovie->SetVariable("_root.BasePriceText._visible", showVal);
    uiMovie->SetVariable("_root.OfferLabel._visible", showVal);
    uiMovie->SetVariable("_root.coinIcon._visible", showVal);
    uiMovie->SetVariable("_root.relBarMC._visible", showVal);
    uiMovie->SetVariable("_root.PriceText._visible", showVal);
    uiMovie->SetVariable("_root.StatusText._visible", showVal);
    uiMovie->SetVariable("_root.ReactionText._visible", showVal);
    // Offer-specific elements
    uiMovie->SetVariable("_root.sliderMC._visible", showVal);
    uiMovie->SetVariable("_root.arrowLeft._visible", showVal);
    uiMovie->SetVariable("_root.arrowRight._visible", showVal);
    uiMovie->SetVariable("_root.SliderText._visible", showVal);
    uiMovie->SetVariable("_root.AcceptanceText._visible", showVal);
    uiMovie->SetVariable("_root.RelEffectText._visible", showVal);
    uiMovie->SetVariable("_root.DealHistoryText._visible", showVal);

    // Clear result/counter-specific text
    RE::GFxValue emptyVal;
    emptyVal.SetString("");
    uiMovie->SetVariable("_root.StatusText.htmlText", emptyVal);
    uiMovie->SetVariable("_root.ReactionText.htmlText", emptyVal);

    // Enforce slider clamp before restoring display
    RE::GFxValue sliderVal, sliderMinV, sliderMaxV;
    uiMovie->GetVariable(&sliderVal, "_root.sliderValue");
    uiMovie->GetVariable(&sliderMinV, "_root.sliderMin");
    uiMovie->GetVariable(&sliderMaxV, "_root.sliderMax");
    if (sliderVal.GetType() == RE::GFxValue::ValueType::kNumber) {
        double val = sliderVal.GetNumber();
        double minV = (sliderMinV.GetType() == RE::GFxValue::ValueType::kNumber) ? sliderMinV.GetNumber() : 0.0;
        double maxV = (sliderMaxV.GetType() == RE::GFxValue::ValueType::kNumber) ? sliderMaxV.GetNumber() : 9999.0;
        double clamped = std::clamp(val, minV, maxV);
        if (val != clamped) {
            RE::GFxValue clampedVal;
            clampedVal.SetNumber(clamped);
            uiMovie->SetVariable("_root.sliderValue", clampedVal);
        }
    }

    // Restore price display from current slider value
    uiMovie->Invoke("_root.updateSliderDisplay", nullptr, nullptr, 0);
    PositionCoin();

    // Restore input hints for offer state (placed glyph icons)
    ApplyHintCells(0);

    // Re-apply the meter from the latest standing so a relationship change that happened
    // while the result/counter screen was up (e.g. a failed intimidation) is reflected
    // the moment we return to the offer view.
    UpdateRelationshipMeter(currentRelationship);

    DbgLog("BarterOfferMenu: RestoreOfferUI - back to offer state");
}

void BarterOfferMenu::FxDelegateCallback::Accept(CallbackProcessor* a_cbReg) {
    a_cbReg->Process("OfferSubmit", OnOfferSubmit);
    a_cbReg->Process("CounterResponse", OnCounterResponse);
    a_cbReg->Process("IntimidateAttempt", OnIntimidateAttempt);
    a_cbReg->Process("CloseMenu", OnClose);
}

void BarterOfferMenu::OnOfferSubmit(const RE::FxDelegateArgs& a_params) {
    if (a_params.GetArgCount() < 1) return;
    // Backstop: if hold-to-confirm is on, an AS-driven submit is ignored (the hold-fill
    // in AdvanceMovie is the only commit path).
    if (Settings::GetSingleton()->holdToConfirm) return;
    int offeredPrice = static_cast<int>(a_params[0].GetNumber());
    DbgLog("BarterOfferMenu: OfferSubmit received - price={}", offeredPrice);
    SKSE::GetTaskInterface()->AddTask([offeredPrice]() {
        BarterManager::GetSingleton()->OnPlayerOffer(offeredPrice);
    });
}

void BarterOfferMenu::OnCounterResponse(const RE::FxDelegateArgs& a_params) {
    if (a_params.GetArgCount() < 1) return;
    int response = static_cast<int>(a_params[0].GetNumber());
    DbgLog("BarterOfferMenu: CounterResponse received - response={}", response);
    SKSE::GetTaskInterface()->AddTask([response]() {
        BarterManager::GetSingleton()->OnCounterResponse(response);
    });
}

void BarterOfferMenu::OnIntimidateAttempt(const RE::FxDelegateArgs&) {
    // Backstop: hold-to-confirm routes intimidate through the red fill bar instead.
    if (Settings::GetSingleton()->holdToConfirm) return;
    DbgLog("BarterOfferMenu: IntimidateAttempt received");
    SKSE::GetTaskInterface()->AddTask([]() {
        BarterManager::GetSingleton()->OnIntimidateAttempt();
    });
}

void BarterOfferMenu::OnClose(const RE::FxDelegateArgs&) {
    DbgLog("BarterOfferMenu: CloseMenu received from SWF");
    SKSE::GetTaskInterface()->AddTask([]() {
        BarterManager::GetSingleton()->OnCancelled();
    });
}

// ScaleformUIImpl

bool ScaleformUIImpl::Initialize() {
    BarterOfferMenu::Register();
    DbgLog("ScaleformUIImpl: BarterOfferMenu registered");
    return true;
}

void ScaleformUIImpl::ShowOffer(const OfferData& data) {
    DbgLog("ScaleformUIImpl::ShowOffer called for '{}'", data.itemName);

    // Store the data for deferred application - AdvanceMovie will pick it up
    BarterOfferMenu::pendingOfferData = data;

    SKSE::GetTaskInterface()->AddUITask([data]() {
        auto* ui = RE::UI::GetSingleton();
        if (!ui) return;

        auto menu = ui->GetMenu<BarterOfferMenu>(BarterOfferMenu::MENU_NAME);
        if (menu) {
            // Menu already exists, apply data directly
            DbgLog("ScaleformUIImpl: Menu already exists, setting offer data directly");
            menu->SetOfferData(data);
            BarterOfferMenu::pendingOfferData.reset();
        } else {
            // Menu doesn't exist yet - Show() will create it, AdvanceMovie will apply pending data
            DbgLog("ScaleformUIImpl: Showing menu, data will be applied in AdvanceMovie");
            BarterOfferMenu::Show();
        }
    });
}

void ScaleformUIImpl::ShowCounterOffer(int counterAmount, int patience) {
    SKSE::GetTaskInterface()->AddUITask([counterAmount, patience]() {
        auto* ui = RE::UI::GetSingleton();
        if (ui) {
            auto menu = ui->GetMenu<BarterOfferMenu>(BarterOfferMenu::MENU_NAME);
            if (menu) {
                menu->SetCounterOffer(counterAmount, patience);
            }
        }
    });
}

void ScaleformUIImpl::ShowResult(bool accepted, int goldAmount, int relDelta) {
    SKSE::GetTaskInterface()->AddUITask([accepted, goldAmount, relDelta]() {
        auto* ui = RE::UI::GetSingleton();
        if (ui) {
            auto menu = ui->GetMenu<BarterOfferMenu>(BarterOfferMenu::MENU_NAME);
            if (menu) {
                menu->SetResult(accepted, goldAmount, relDelta);
            }
        }
    });
}

void ScaleformUIImpl::UpdateRelationship(int effectiveRelationship) {
    SKSE::GetTaskInterface()->AddUITask([effectiveRelationship]() {
        auto* ui = RE::UI::GetSingleton();
        if (!ui) return;
        auto menu = ui->GetMenu<BarterOfferMenu>(BarterOfferMenu::MENU_NAME);
        if (menu) {
            menu->UpdateRelationship(effectiveRelationship);
        }
    });
}

void ScaleformUIImpl::Hide() {
    SKSE::GetTaskInterface()->AddUITask([]() {
        // Guard against a stale close: if the player started a new barter
        // interaction while this hide was queued (e.g. clicked another item
        // right after a deal closed), the offer menu is being reused for the
        // new offer. Closing it now would make the freshly-opened window blink
        // and vanish. Only actually close when the barter is truly idle.
        auto st = BarterManager::GetSingleton()->GetState();
        if (st != BarterState::Idle) {
            DbgLog("BarterOfferMenu: Skipping stale Hide - new interaction active (state={})",
                static_cast<int>(st));
            return;
        }
        BarterOfferMenu::Hide();
    });
}
