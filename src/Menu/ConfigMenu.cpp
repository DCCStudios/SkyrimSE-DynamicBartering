#include "PCH.h"
#include "Menu/ConfigMenu.h"
#include "Settings.h"
#include "RelationshipManager.h"
#include "MerchantPersonality.h"
#include "MerchantCategory.h"
#include "PriceJack.h"
#include "Tutorial.h"
#include "BarterManager.h"
#include "UI/UIBridge.h"
#include "Integration/ChimBridge.h"

#include <cstdarg>

// Minimal header for SKSEMenuFramework - only what we need
namespace SKSEMenuFramework {
    namespace Model {
        typedef void(__stdcall* RenderFunction)();
        using AddSectionItemFunction = void (*)(const char* path, RenderFunction rendererFunction);
    }
    namespace Internal {
        template <class T>
        T GetFunction(LPCSTR name) {
            static auto menuFramework = GetModuleHandle(L"SKSEMenuFramework");
            if (!menuFramework) return nullptr;
            return reinterpret_cast<T>(GetProcAddress(menuFramework, name));
        }
        inline std::string key;
    }
    inline void SetSection(std::string key) { Internal::key = key; }
    inline void AddSectionItem(std::string menu, Model::RenderFunction rendererFunction) {
        static auto func = Internal::GetFunction<Model::AddSectionItemFunction>("AddSectionItem");
        if (func) func((Internal::key + "/" + menu).c_str(), rendererFunction);
    }
    inline bool IsInstalled() {
        return GetModuleHandle(L"SKSEMenuFramework") != nullptr;
    }
}

// Forward declare ImGui functions we use (provided by SKSEMenuFramework)
struct ImGuiContext;
typedef unsigned int ImGuiID;

extern "C" {
    typedef bool (*ImGui_Begin_t)(const char*, bool*, int);
    typedef void (*ImGui_End_t)();
    typedef bool (*ImGui_Checkbox_t)(const char*, bool*);
    typedef bool (*ImGui_SliderFloat_t)(const char*, float*, float, float, const char*, int);
    typedef bool (*ImGui_SliderInt_t)(const char*, int*, int, int, const char*, int);
    typedef void (*ImGui_Text_t)(const char*, ...);
    typedef void (*ImGui_Separator_t)();
    typedef bool (*ImGui_Button_t)(const char*, ...);
    typedef bool (*ImGui_BeginTabBar_t)(const char*, int);
    typedef void (*ImGui_EndTabBar_t)();
    typedef bool (*ImGui_BeginTabItem_t)(const char*, bool*, int);
    typedef void (*ImGui_EndTabItem_t)();
    typedef bool (*ImGui_Combo_t)(const char*, int*, const char* const*, int, int);
    typedef void (*ImGui_BeginDisabled_t)(bool);
    typedef void (*ImGui_EndDisabled_t)();
}

namespace {
    typedef bool (*ImGui_IsItemHovered_t)(int);
    typedef void (*ImGui_SetTooltipV_t)(const char*, va_list);

    // SKSEMenuFramework only exports the cimgui `...V` variadic forwarder, so wrap it.
    void DynSetTooltip(ImGui_SetTooltipV_t fn, const char* fmt, ...) {
        va_list args;
        va_start(args, fmt);
        fn(fmt, args);
        va_end(args);
    }

    // Show a hover tooltip on the most recently submitted widget. Call immediately
    // after the widget. "%s" guards against stray '%' in the tooltip text.
    void Tip(const char* text) {
        auto mf = GetModuleHandleW(L"SKSEMenuFramework");
        if (!mf) return;
        static auto hovered = reinterpret_cast<ImGui_IsItemHovered_t>(GetProcAddress(mf, "igIsItemHovered"));
        static auto settip = reinterpret_cast<ImGui_SetTooltipV_t>(GetProcAddress(mf, "igSetTooltipV"));
        if (hovered && settip && hovered(0)) {
            DynSetTooltip(settip, "%s", text);
        }
    }

    // Inline, always-visible help text printed BETWEEN controls so the effect of a
    // slider is explained right under it (not just on hover). Rendered greyed via
    // igTextDisabled when available, falling back to plain igText. "%s" guards '%'.
    // Embed "\n" for multi-line; lines are indented by the caller.
    void Desc(const char* text) {
        auto mf = GetModuleHandleW(L"SKSEMenuFramework");
        if (!mf) return;
        static auto disabled = reinterpret_cast<ImGui_Text_t>(GetProcAddress(mf, "igTextDisabled"));
        static auto plain = reinterpret_cast<ImGui_Text_t>(GetProcAddress(mf, "igText"));
        if (disabled) disabled("%s", text);
        else if (plain) plain("%s", text);
    }
}

void ConfigMenu::Register() {
    if (!SKSEMenuFramework::IsInstalled()) {
        logger::info("SKSEMenuFramework not installed, config menu unavailable");
        return;
    }

    SKSEMenuFramework::SetSection("DynamicBartering");
    SKSEMenuFramework::AddSectionItem("Settings", RenderMenu);
    logger::info("ConfigMenu registered with SKSEMenuFramework");
}

void ConfigMenu::RenderMenu() {
    auto menuFramework = GetModuleHandle(L"SKSEMenuFramework");
    if (!menuFramework) return;

    // Reset relationship tab tracking each frame; RenderRelationshipsTab sets it true
    relationshipsTabActive = false;

    auto ImGui_BeginTabBar = reinterpret_cast<ImGui_BeginTabBar_t>(GetProcAddress(menuFramework, "igBeginTabBar"));
    auto ImGui_EndTabBar = reinterpret_cast<ImGui_EndTabBar_t>(GetProcAddress(menuFramework, "igEndTabBar"));
    auto ImGui_BeginTabItem = reinterpret_cast<ImGui_BeginTabItem_t>(GetProcAddress(menuFramework, "igBeginTabItem"));
    auto ImGui_EndTabItem = reinterpret_cast<ImGui_EndTabItem_t>(GetProcAddress(menuFramework, "igEndTabItem"));

    if (!ImGui_BeginTabBar || !ImGui_EndTabBar) {
        RenderGeneralTab();
        return;
    }

    if (ImGui_BeginTabBar("DynBarterTabs", 0)) {
        if (ImGui_BeginTabItem && ImGui_BeginTabItem("General", nullptr, 0)) {
            RenderGeneralTab();
            if (ImGui_EndTabItem) ImGui_EndTabItem();
        }
        if (ImGui_BeginTabItem && ImGui_BeginTabItem("Cart", nullptr, 0)) {
            RenderCartTab();
            if (ImGui_EndTabItem) ImGui_EndTabItem();
        }
        if (ImGui_BeginTabItem && ImGui_BeginTabItem("Pricing", nullptr, 0)) {
            RenderPricingTab();
            if (ImGui_EndTabItem) ImGui_EndTabItem();
        }
        if (ImGui_BeginTabItem && ImGui_BeginTabItem("Relationships", nullptr, 0)) {
            RenderRelationshipsTab();
            if (ImGui_EndTabItem) ImGui_EndTabItem();
        }
        if (ImGui_BeginTabItem && ImGui_BeginTabItem("Personalities", nullptr, 0)) {
            RenderPersonalitiesTab();
            if (ImGui_EndTabItem) ImGui_EndTabItem();
        }
        if (ImGui_BeginTabItem && ImGui_BeginTabItem("Debug", nullptr, 0)) {
            RenderDebugTab();
            if (ImGui_EndTabItem) ImGui_EndTabItem();
        }
        ImGui_EndTabBar();
    }
}

void ConfigMenu::RenderGeneralTab() {
    auto menuFramework = GetModuleHandle(L"SKSEMenuFramework");
    if (!menuFramework) return;

    auto ImGui_Checkbox = reinterpret_cast<ImGui_Checkbox_t>(GetProcAddress(menuFramework, "igCheckbox"));
    auto ImGui_SliderInt = reinterpret_cast<ImGui_SliderInt_t>(GetProcAddress(menuFramework, "igSliderInt"));
    auto ImGui_SliderFloat = reinterpret_cast<ImGui_SliderFloat_t>(GetProcAddress(menuFramework, "igSliderFloat"));
    auto ImGui_Combo = reinterpret_cast<ImGui_Combo_t>(GetProcAddress(menuFramework, "igCombo_Str_arr"));
    auto ImGui_Text = reinterpret_cast<ImGui_Text_t>(GetProcAddress(menuFramework, "igText"));
    auto ImGui_Separator = reinterpret_cast<ImGui_Separator_t>(GetProcAddress(menuFramework, "igSeparator"));
    auto ImGui_Button = reinterpret_cast<ImGui_Button_t>(GetProcAddress(menuFramework, "igButton"));
    auto ImGui_BeginDisabled = reinterpret_cast<ImGui_BeginDisabled_t>(GetProcAddress(menuFramework, "igBeginDisabled"));
    auto ImGui_EndDisabled = reinterpret_cast<ImGui_EndDisabled_t>(GetProcAddress(menuFramework, "igEndDisabled"));

    auto* s = Settings::GetSingleton();
    if (!s) return;

    if (ImGui_Checkbox) {
        ImGui_Checkbox("Enable Mod", &s->modEnabled);
        Tip("Master switch. When off, bartering behaves exactly like vanilla.");
        ImGui_Checkbox("Block Quick Buy/Sell (cart-only)", &s->blockQuickBuy);
        Tip("On: clicking an item adds it to the barter cart instead of buying/selling "
            "instantly. Hold the barter/activate key (or mouse) to open the negotiation.\n"
            "Off: clicking buys/sells as usual; tap the barter key to add to the cart.");
        if (ImGui_Text) {
            ImGui_Text("  When on: tap to add to cart; hold barter/activate/mouse to negotiate.");
        }
        ImGui_Checkbox("Show Acceptance Hint", &s->showAcceptanceHint);
        Tip("Shows how likely the merchant is to accept your current offer.");
        ImGui_Checkbox("Show Relationship Preview", &s->showRelationshipPreview);
        Tip("Shows how this deal will change your standing with the merchant.");
        ImGui_Checkbox("Skip Items Below Threshold", &s->skipBelowThreshold);
        Tip("Cheap items (under 'Value Threshold' below) buy/sell instantly and skip "
            "negotiation, so you don't haggle over a single septim.");
    }
    if (ImGui_SliderInt) {
        ImGui_SliderInt("Popup Delay (ms)", &s->popupDelayMs, 0, 1000, "%d", 0);
        Tip("Delay before the negotiation window appears after you start a deal.");
        Desc("  Higher = a longer beat between starting a deal and the offer window\n"
             "  opening (lets a click/sound settle first). Lower = snappier, more\n"
             "  instant. 0 opens immediately; ~200ms feels natural.");

        ImGui_SliderInt("Value Threshold", &s->valueThreshold, 0, 500, "%d", 0);
        Tip("Items worth less than this (in gold) skip negotiation when "
            "'Skip Items Below Threshold' is on.");
        Desc("  Raise it to auto-buy/sell more (even mid-value) junk at market price\n"
             "  without haggling; lower it (toward 0) to negotiate almost everything.\n"
             "  Only matters when 'Skip Items Below Threshold' is on.");
    }

    if (ImGui_Checkbox) {
        // Re-arming the tutorial replays both popups; once both are seen it auto-disables.
        bool tut = s->tutorialEnabled;
        if (ImGui_Checkbox("Show Tutorial", &tut)) {
            if (tut) {
                Tutorial::Rearm();   // turn on + clear seen flags so it replays
            } else {
                s->tutorialEnabled = false;
                s->Save();
            }
        }
        Tip("Replays the first-run tutorial: a cart-basics popup when you next open a "
            "barter menu, and an offer-window popup the first time you negotiate. "
            "Auto-disables again once both have been shown.");
    }

    if (ImGui_Separator) ImGui_Separator();
    if (ImGui_Text) ImGui_Text("UI Backend");

    if (ImGui_Combo) {
        static int uiModeIdx = static_cast<int>(s->uiMode);
        static const char* uiModeItems[] = { "Auto", "Scaleform (SWF)", "PrismaUI (HTML)" };
        if (ImGui_Combo("UI Mode", &uiModeIdx, uiModeItems, 3, 3)) {
            UIMode newMode = static_cast<UIMode>(uiModeIdx);
            s->uiMode = newMode;
            UIBridge::GetSingleton()->SwitchMode(newMode);
        }
        Tip("Which renderer draws the offer window.\n"
            "Auto: pick the best available.\n"
            "Scaleform (SWF): native Skyrim UI, widest compatibility.\n"
            "PrismaUI (HTML): richer styling, requires the PrismaUI plugin.");
    } else if (ImGui_Button) {
        // Fallback: use buttons when combo isn't available
        auto currentMode = s->uiMode;
        if (ImGui_Text) {
            const char* modeStr = currentMode == UIMode::Auto ? "Auto" :
                                  currentMode == UIMode::ScaleformSWF ? "Scaleform (SWF)" : "PrismaUI (HTML)";
            ImGui_Text("Current: %s", modeStr);
        }
        if (ImGui_Button("Use SWF", 0) && currentMode != UIMode::ScaleformSWF) {
            s->uiMode = UIMode::ScaleformSWF;
            UIBridge::GetSingleton()->SwitchMode(UIMode::ScaleformSWF);
        }
        if (ImGui_Button("Use PrismaUI", 0) && currentMode != UIMode::PrismaUI) {
            s->uiMode = UIMode::PrismaUI;
            UIBridge::GetSingleton()->SwitchMode(UIMode::PrismaUI);
        }
        if (ImGui_Button("Auto", 0) && currentMode != UIMode::Auto) {
            s->uiMode = UIMode::Auto;
            UIBridge::GetSingleton()->SwitchMode(UIMode::Auto);
        }
    } else if (ImGui_Text) {
        ImGui_Text("UI Mode: %s (change in INI)",
            s->uiMode == UIMode::ScaleformSWF ? "SWF" :
            s->uiMode == UIMode::PrismaUI ? "PrismaUI" : "Auto");
    }

    if (ImGui_Separator) ImGui_Separator();
    if (ImGui_Text) ImGui_Text("Controller Glyphs");

    if (ImGui_Combo) {
        static int iconStyleIdx = static_cast<int>(s->gamepadIconStyle);
        static const char* iconStyleItems[] = { "Xbox", "PlayStation" };
        if (ImGui_Combo("Gamepad Icon Style", &iconStyleIdx, iconStyleItems, 2, 2)) {
            s->gamepadIconStyle = static_cast<GamepadIconStyle>(iconStyleIdx);
            s->Save();
        }
        Tip("Which controller button glyphs to show in prompts (Xbox A/B/X/Y or "
            "PlayStation shapes). Has no effect when playing with mouse & keyboard.");
    } else if (ImGui_Button) {
        if (ImGui_Text) {
            ImGui_Text("Gamepad Icons: %s",
                s->gamepadIconStyle == GamepadIconStyle::PlayStation ? "PlayStation" : "Xbox");
        }
        if (ImGui_Button("Use Xbox", 0)) {
            s->gamepadIconStyle = GamepadIconStyle::Xbox;
            s->Save();
        }
        if (ImGui_Button("Use PlayStation", 0)) {
            s->gamepadIconStyle = GamepadIconStyle::PlayStation;
            s->Save();
        }
    }

    if (ImGui_Separator) ImGui_Separator();
    if (ImGui_Text) ImGui_Text("Sound");

    if (ImGui_Checkbox) {
        ImGui_Checkbox("Enable Sounds", &s->enableSounds);
        Tip("Play custom UI sounds for cart and offer actions.");
        if (ImGui_Checkbox("Use Vanilla Cart Sounds", &s->useVanillaCartSounds)) {
            s->Save();
        }
        Tip("On: adding/removing an item to/from the cart plays that item's own vanilla "
            "pickup/putdown sound - the same cue Skyrim plays on a quick buy/sell (a clink "
            "for coins, a thunk for armor, etc.). Off: use the mod's generic AddToCart / "
            "RemoveFromCart WAVs.");
    }
    if (ImGui_SliderFloat) {
        ImGui_SliderFloat("Sound Volume", &s->soundVolume, 0.0f, 1.0f, "%.2f", 0);
        Tip("Volume of the mod's custom UI sounds (0 = silent, 1 = full).");
        Desc("  Scales every cue this mod plays (cart clicks, slider ticks, accept/\n"
             "  reject jingles, and the vanilla item sounds). 0.00 = fully silent,\n"
             "  1.00 = full volume. Drop it if the feedback sounds louder than the game.");
    }
    if (ImGui_Text) {
        ImGui_Text("  Place WAVs in Data/SKSE/Plugins/DynamicBartering/Sounds/.");
        ImGui_Text("  Variants supported: AddToCart.wav, AddToCart_1.wav, ...");
    }

    if (ImGui_Separator) ImGui_Separator();
    if (ImGui_Text) ImGui_Text("CHIM Integration (AI NPC reactions)");

    // The enable toggle stays interactive; everything below it is greyed out
    // (ImGui disabled state) until CHIM integration is turned on.
    if (ImGui_Checkbox) {
        if (ImGui_Checkbox("Enable CHIM Integration", &s->enableChim)) {
            s->Save();
            ChimBridge::Initialize();  // (re)probe whenever it's toggled on
        }
        Tip("Sends a summary of each bartering visit to a running CHIM / HerikaServer so "
            "AI NPCs remember your haggling and comment on it. Completely inert if CHIM "
            "is not installed or its server is unreachable.");
    }

    const bool canDisable = ImGui_BeginDisabled && ImGui_EndDisabled;
    const bool greyOut = !s->enableChim && canDisable;
    if (greyOut) ImGui_BeginDisabled(true);

    if (ImGui_Checkbox) {
        if (ImGui_Checkbox("Don't pause offer window (SkyrimSouls)", &s->chimUnpauseOfferWindow)) {
            s->Save();
        }
        Tip("Requires SkyrimSouls. When set, the negotiation window won't re-pause the "
            "game, keeping the world live like the rest of your menus.");
        if (ImGui_Checkbox("Live Context Logging (memory only)", &s->chimLiveContextLogging)) {
            s->Save();
        }
        Tip("Also feed each individual outcome to the merchant's memory the moment it "
            "happens (no spoken line - Skyrim mutes NPC voice while a menu is open). "
            "Off by default; the after-close summary already covers memory.");
    }
    if (ImGui_SliderInt) {
        ImGui_SliderInt("Request Timeout (ms)", &s->chimTimeoutMs, 500, 15000, "%d", 0);
        Tip("How long to wait for the CHIM server per request before giving up.");
        Desc("  Higher = more patience for a slow/busy AI server (fewer dropped\n"
             "  reactions) but a longer hitch if it's unreachable. Lower = fails fast\n"
             "  and stays responsive. 3000ms is a good default for a local server.");

        if (ImGui_SliderInt("Reaction Cooldown (s)", &s->chimReactionCooldownSec, 0, 120, "%d", 0)) {
            s->Save();
        }
        Tip("Minimum seconds between a merchant's spoken reactions (intimidation / "
            "overpay / insult / closing remark). 0 = no cooldown.");
        Desc("  Higher = the merchant comments more rarely (calmer, less chatty).\n"
             "  Lower/0 = they react to nearly every notable moment, which can feel\n"
             "  spammy and burn AI requests. ~20s keeps remarks feeling deliberate.");

        if (ImGui_SliderInt("Counter-Offer Cooldown (s)", &s->chimCounterCooldownSec, 0, 120, "%d", 0)) {
            s->Save();
        }
        Tip("Minimum seconds between a merchant voicing counter-offer justifications "
            "during a fast multi-round haggle. 0 = no cooldown.");
        Desc("  Higher = fewer spoken justifications during rapid back-and-forth\n"
             "  haggling. Lower/0 = they talk through most counters. Keep it short\n"
             "  (~6s) so quick re-offers still occasionally get a line.");

        if (ImGui_SliderInt("Send Delay After Close (s)", &s->chimSendDelaySec, 0, 30, "%d", 0)) {
            s->Save();
        }
        Tip("Extra wait after you close the dialogue menu before the visit summary is "
            "sent, giving the game time to fully unpause so the NPC can speak.");
        Desc("  Raise this if the merchant's closing remark gets swallowed because the\n"
             "  game hadn't unpaused yet (Skyrim mutes NPC voice under menus). Lower\n"
             "  for a snappier comment. ~3-4s is usually enough.");
    }
    if (ImGui_Text) {
        ImGui_Text("  The merchant comments once you close the barter & dialogue menus");
        ImGui_Text("  (Skyrim mutes NPC voice while any menu is open), and remembers the");
        ImGui_Text("  visit for future conversations.");
        ImGui_Text("  Server: %s", s->chimServerUrl.c_str());
        ImGui_Text("  (edit sServerUrl in the INI to change)");
        ImGui_Text("  Status: %s", ChimBridge::IsAvailable() ? "CHIM reachable" : "not detected");
        if (ChimBridge::SkyrimSoulsActive()) {
            ImGui_Text("  SkyrimSouls: detected - offer window stays unpaused");
        } else {
            ImGui_Text("  SkyrimSouls: not detected");
        }
    }
    if (ImGui_Button) {
        if (ImGui_Button("Re-check CHIM Connection", 0)) {
            ChimBridge::Initialize();
        }
        Tip("Re-probe the CHIM server now (e.g. after starting it).");
    }

    if (greyOut) ImGui_EndDisabled();
}

void ConfigMenu::RenderCartTab() {
    auto menuFramework = GetModuleHandle(L"SKSEMenuFramework");
    if (!menuFramework) return;

    auto ImGui_SliderFloat = reinterpret_cast<ImGui_SliderFloat_t>(GetProcAddress(menuFramework, "igSliderFloat"));
    auto ImGui_Text = reinterpret_cast<ImGui_Text_t>(GetProcAddress(menuFramework, "igText"));
    auto ImGui_Separator = reinterpret_cast<ImGui_Separator_t>(GetProcAddress(menuFramework, "igSeparator"));
    auto ImGui_Button = reinterpret_cast<ImGui_Button_t>(GetProcAddress(menuFramework, "igButton"));
    auto ImGui_Checkbox = reinterpret_cast<ImGui_Checkbox_t>(GetProcAddress(menuFramework, "igCheckbox"));

    auto* s = Settings::GetSingleton();
    if (!s) return;

    if (ImGui_Text) {
        ImGui_Text("Hold the Barter key/button to negotiate the whole cart.");
        ImGui_Text("Tap it to add/remove the highlighted item.");
    }
    if (ImGui_Separator) ImGui_Separator();

    if (ImGui_Checkbox) {
        if (ImGui_Checkbox("Cart Visible by Default", &s->cartVisibleByDefault)) {
            s->Save();
        }
        Tip("Show the barter cart panel the moment the menu opens, instead of only "
            "after you add the first item.");
        Desc("  On = the cart panel is on screen as soon as you start bartering (empty,\n"
             "  with a hint and your current standing/discount), so you always see it.\n"
             "  Off = the panel stays hidden until you add your first item to the cart.");
    }

    if (ImGui_Separator) ImGui_Separator();

    if (ImGui_Checkbox) {
        if (ImGui_Checkbox("Hold to Confirm / Intimidate", &s->holdToConfirm)) {
            s->Save();
        }
        Tip("Make the offer window's Submit (gold) and Intimidate (red) buttons fill up "
            "as you HOLD them, committing only when full. Cancel is always instant.");
        Desc("  On = press and HOLD Submit or Intimidate; the button fills with color and\n"
             "  the action fires when it tops off (prevents fat-finger offers/threats).\n"
             "  Off = a single press/click commits instantly, as before.");
    }
    if (s->holdToConfirm && ImGui_SliderFloat) {
        ImGui_SliderFloat("Hold to Confirm Time (sec)", &s->holdToConfirmSec, 0.2f, 2.0f, "%.2f", 0);
        Tip("How long you must hold Submit/Intimidate for the bar to fill and commit.");
        Desc("  Lower = a quick press-and-hold commits almost immediately (snappy, but\n"
             "  easier to trigger by accident). Higher = a longer, more deliberate hold\n"
             "  is required. ~0.65s is a brisk, intentional press.");
    }

    if (ImGui_Separator) ImGui_Separator();

    if (ImGui_SliderFloat) {
        // Tap window: release sooner = tap; hold past it engages the hold.
        ImGui_SliderFloat("Hold Tap Window (sec)", &s->cartHoldThreshold, 0.1f, 1.0f, "%.2f", 0);
        Tip("Release the barter key within this time to count as a TAP (add/remove the "
            "highlighted item). Hold longer to start opening the cart offer.");
        Desc("  Higher = you can hold the key longer and it still counts as a quick TAP\n"
             "  (add/remove) - forgiving, but opening the offer takes a beat longer.\n"
             "  Lower = taps must be snappy and holds register almost instantly.");

        // After the tap window, the meter fills for this long before the offer opens.
        ImGui_SliderFloat("Hold Fill Time (sec)", &s->cartHoldFillTime, 0.1f, 1.5f, "%.2f", 0);
        Tip("After the tap window, how long you must keep holding (meter fills) before "
            "the cart negotiation opens.");
        Desc("  Higher = you must keep holding longer (fuller meter) before the\n"
             "  negotiation opens - harder to trigger by accident. Lower = the offer\n"
             "  window pops almost as soon as you pass the tap window.");
    }

    if (ImGui_Separator) ImGui_Separator();
    if (ImGui_Text) {
        ImGui_Text("Cart Window Position (stage is ~1280 x 720)");
        ImGui_Text("Changes apply live while the barter menu is open.");
    }

    if (ImGui_SliderFloat) {
        // Allow a little overscan past the stage so the panel can be tucked anywhere.
        ImGui_SliderFloat("Position X", &s->cartPanelX, -100.0f, 1280.0f, "%.0f", 0);
        Tip("Horizontal position of the cart panel on screen (stage is ~1280 wide).");
        Desc("  Lower = panel moves toward the LEFT edge; higher = toward the RIGHT.\n"
             "  0 is the left edge, ~1280 the right. Negative values tuck it slightly\n"
             "  off-screen left.");

        ImGui_SliderFloat("Position Y", &s->cartPanelY, -100.0f, 720.0f, "%.0f", 0);
        Tip("Vertical position of the cart panel on screen (stage is ~720 tall).");
        Desc("  Lower = panel moves toward the TOP; higher = toward the BOTTOM.\n"
             "  0 is the top edge, ~720 the bottom. Use it to clear the vanilla\n"
             "  item list or your other HUD widgets.");

        ImGui_SliderFloat("Scale", &s->cartPanelScale, 0.5f, 1.5f, "%.2f", 0);
        Tip("Size of the cart panel (1.0 = default).");
        Desc("  Higher = a bigger, easier-to-read panel that takes more screen space.\n"
             "  Lower = a more compact panel. 1.00 is the designed size; 0.95 trims it\n"
             "  slightly at common resolutions.");
    }

    if (ImGui_Separator) ImGui_Separator();
    if (ImGui_Button) {
        if (ImGui_Button("Reset Position", 0)) {
            s->cartPanelX = 596.0f;
            s->cartPanelY = 110.0f;
            s->cartPanelScale = 1.0f;
        }
        if (ImGui_Button("Save Cart Settings", 0)) {
            s->Save();
        }
    }
    if (ImGui_Text) ImGui_Text("Tip: use Save to keep position across sessions.");
}

void ConfigMenu::RenderPricingTab() {
    auto menuFramework = GetModuleHandle(L"SKSEMenuFramework");
    if (!menuFramework) return;

    auto ImGui_SliderFloat = reinterpret_cast<ImGui_SliderFloat_t>(GetProcAddress(menuFramework, "igSliderFloat"));
    auto ImGui_Checkbox = reinterpret_cast<ImGui_Checkbox_t>(GetProcAddress(menuFramework, "igCheckbox"));
    auto ImGui_Text = reinterpret_cast<ImGui_Text_t>(GetProcAddress(menuFramework, "igText"));
    auto ImGui_Separator = reinterpret_cast<ImGui_Separator_t>(GetProcAddress(menuFramework, "igSeparator"));

    auto* s = Settings::GetSingleton();
    if (!s) return;

    if (ImGui_Text) {
        ImGui_Text("How likely a merchant is to ACCEPT your offer. Each factor below adds");
        ImGui_Text("(or subtracts) percentage points from the acceptance chance.");
        ImGui_Text("Higher = easier haggling. Hover any option for details.");
    }
    if (ImGui_Separator) ImGui_Separator();

    if (ImGui_SliderFloat) {
        ImGui_SliderFloat("Base Acceptance %", &s->baseAcceptanceChance, 0.0f, 100.0f, "%.1f", 0);
        Tip("Starting chance a merchant accepts a fair offer, before any bonuses. "
            "This is the foundation every other factor adds to.");
        Desc("  Raise it for an easier, more forgiving economy where deals land readily;\n"
             "  lower it for a hard-bargaining world where you must earn every yes.\n"
             "  50% is neutral - a fair offer is a coin flip before skills/standing.");

        ImGui_SliderFloat("Speech Weight", &s->speechWeight, 0.0f, 50.0f, "%.1f", 0);
        Tip("How much your Speech skill improves acceptance. Higher = your Speech level "
            "matters more when haggling.");
        Desc("  Higher = leveling Speech pays off hard (a 100 Speech silver tongue gets\n"
             "  big discounts); 0 makes your Speech skill irrelevant to haggling.\n"
             "  This is the points added at Speech 100, scaled down at lower levels.");

        ImGui_SliderFloat("Haggling Perk Bonus/Rank", &s->hagglingPerkBonus, 0.0f, 10.0f, "%.1f", 0);
        Tip("Acceptance bonus for each rank of the vanilla Haggling perk you own.");
        Desc("  Points added PER perk rank (x5 ranks at the cap). Higher = the Haggling\n"
             "  perks feel powerful and worth the points; 0 makes them do nothing here.");

        ImGui_SliderFloat("Persuasion Perk Bonus", &s->persuasionPerkBonus, 0.0f, 30.0f, "%.1f", 0);
        Tip("Acceptance bonus if you have the Persuasion perk.");
        Desc("  Flat points granted once you own the Persuasion perk. Higher = a bigger\n"
             "  reward for investing in it; 0 = the perk has no bartering effect.");

        ImGui_SliderFloat("Allure Bonus", &s->allureBonus, 0.0f, 20.0f, "%.1f", 0);
        Tip("Acceptance bonus from the Allure perk when trading with the opposite sex.");
        Desc("  Flat points with the Allure perk when the merchant is the opposite sex.\n"
             "  Higher = stronger 'charm' discount; 0 = the perk is inert here.");

        ImGui_SliderFloat("Relationship Weight", &s->relationshipWeight, 0.0f, 30.0f, "%.1f", 0);
        Tip("How strongly your standing with a merchant sways acceptance. Friends accept "
            "more readily; merchants who dislike you push back. (See the Relationships tab.)");
        Desc("  Higher = your reputation with each merchant matters a lot (friends give,\n"
             "  enemies refuse); 0 = standing no longer affects whether they say yes.\n"
             "  This is the swing at max standing, scaled by your actual relationship.");

        ImGui_SliderFloat("Personality Weight", &s->personalityWeight, 0.0f, 25.0f, "%.1f", 0);
        Tip("How strongly a merchant's personality (Greedy, Generous, etc.) affects "
            "acceptance. (See the Personalities tab.)");
        Desc("  Higher = personalities feel distinct (a Greedy trader is genuinely harder\n"
             "  than a Generous one); 0 = everyone haggles the same regardless of type.");

        ImGui_SliderFloat("Deal History Weight", &s->dealHistoryWeight, 0.0f, 15.0f, "%.1f", 0);
        Tip("How much your past dealings with a merchant matter. A history of fair deals "
            "earns trust; repeated lowballing makes them wary.");
        Desc("  Higher = a track record really sticks (fair regulars get easier deals,\n"
             "  serial lowballers get punished); 0 = each offer is judged fresh.");

        ImGui_SliderFloat("Greed Factor", &s->greedFactor, 0.5f, 3.0f, "%.2f", 0);
        Tip("Overall merchant greed. Higher = merchants hold out harder for profit and "
            "accept fewer bargains. 1.0 is balanced.");
        Desc("  The global money-hungriness dial. Above 1.0 = everyone drives a harder\n"
             "  bargain and discounts shrink; below 1.0 = a softer, buyer-friendly market.\n"
             "  1.0 is the balanced baseline.");

        ImGui_SliderFloat("Stolen Item Penalty", &s->stolenItemPenalty, 0.0f, 50.0f, "%.1f", 0);
        Tip("Acceptance penalty when selling stolen goods to a fence.");
        Desc("  Points SUBTRACTED when fencing stolen goods. Higher = crime really cuts\n"
             "  into your fence price; 0 = stolen items sell just like honest ones.");

        ImGui_SliderFloat("Fence Perk Reduction %", &s->fencePerkReduction, 0.0f, 100.0f, "%.0f", 0);
        Tip("How much the Fence perk cancels the Stolen Item Penalty (100% = no penalty).");
        Desc("  How much of the stolen penalty the Fence perk erases. 100% = the perk\n"
             "  fully cancels it (stolen sells like honest); 0% = the perk gives no relief.");
    }
    if (ImGui_Checkbox) {
        ImGui_Checkbox("Use Vanilla Base Price", &s->useVanillaBasePrice);
        Tip("On: start from Skyrim's own barter price (includes Speech skill & perks), "
            "then apply this mod's adjustments. Off: start from the item's raw gold value.");
    }

    if (ImGui_Separator) ImGui_Separator();
    if (ImGui_Text) {
        ImGui_Text("Relationship-Driven Haggling Range");
        ImGui_Text("Your standing widens how good a deal you can reach: liked merchants");
        ImGui_Text("allow deeper buy discounts / higher sell overcharges, disliked ones hold firm.");
    }
    if (ImGui_SliderFloat) {
        ImGui_SliderFloat("Range Weight", &s->relHaggleRangeWeight, 0.0f, 1.0f, "%.2f", 0);
        Tip("How strongly relationship widens (when liked) or contracts (when disliked) "
            "the reachable haggling range. 0 = standing doesn't change the range; higher "
            "= a beloved merchant gives a lot more room. Bounded per-merchant by personality.");
        Desc("  Higher = reputation dramatically changes how good a deal you can reach\n"
             "  (loved merchants give deep discounts, hated ones barely budge). 0 =\n"
             "  everyone offers the same range regardless of how they feel about you.");

        ImGui_SliderFloat("Neutral Haggle Tightness", &s->neutralHaggleScale, 0.0f, 1.0f, "%.2f", 0);
        Tip("Scales the haggling room you get at NEUTRAL standing, before any "
            "relationship/perk bonus. Lower = tighter deals out of the gate. The "
            "baseline is also shaped by personality (Greedy/Stern give less, "
            "Generous/Sleazy more) even at neutral.");
        Desc("  Lower = starting deals hug the market price (you must build a\n"
             "  relationship to earn real discounts). Higher = generous room even with\n"
             "  strangers. 1.0 restores the old wide neutral spread; ~0.45 keeps it tight.");

        ImGui_SliderFloat("Max Buy Discount", &s->maxBuyDiscount, 0.0f, 0.9f, "%.2f", 0);
        Tip("Hard cap on how far below market a deal can be pushed when buying "
            "(0.60 = up to 60% off), even for a beloved merchant.");
        Desc("  The best-case price floor when buying. 0.60 = even a beloved merchant\n"
             "  won't go below 40% of market; raise it to allow steeper bargains, lower\n"
             "  it to keep buying expensive no matter your standing.");

        ImGui_SliderFloat("Max Sell Markup", &s->maxSellMarkup, 0.0f, 0.9f, "%.2f", 0);
        Tip("Hard cap on how far above market you can sell for when liked "
            "(0.60 = up to 60% over market).");
        Desc("  The best-case price ceiling when selling. 0.60 = you can squeeze up to\n"
             "  +60% over market from a friendly merchant; raise it for richer payouts,\n"
             "  lower it to keep selling closer to market value.");
    }

    if (ImGui_Separator) ImGui_Separator();
    if (ImGui_Text) ImGui_Text("Merchant Specialties");
    if (ImGui_Checkbox) {
        if (ImGui_Checkbox("Specialty Haggling", &s->specialtyHaggling)) s->Save();
        Tip("Merchants haggle more readily on goods they actually deal in (a blacksmith "
            "on weapons & armor, an apothecary on potions & ingredients, a court wizard on "
            "spell tomes & enchanted gear) and resist clearly off-specialty items.");
    }
    if (ImGui_SliderFloat) {
        ImGui_SliderFloat("Specialty Weight", &s->specialtyWeight, 0.0f, 30.0f, "%.1f", 0);
        Tip("Acceptance-chance points a full in-specialty match adds (and an off-specialty "
            "mismatch subtracts). 0 = specialties don't affect haggling.");
        Desc("  Higher = specialization really matters (a blacksmith caves on a sword but\n"
             "  digs in on a potion); 0 = a merchant treats every item type the same.\n"
             "  Only applies while 'Specialty Haggling' above is on.");
    }

    if (ImGui_Separator) ImGui_Separator();
    if (ImGui_Text) ImGui_Text("Vanilla Price Display");
    if (ImGui_Checkbox) {
        if (ImGui_Checkbox("Reflect Relationship in Vanilla Prices", &s->showRelationshipInVanillaPrices)) {
            s->Save();
        }
        Tip("Rewrites the vanilla BarterMenu item-card price so it matches the "
            "relationship/personality-adjusted price you'll actually pay. Chains with "
            "other price mods (DPF / Dynamic Prices). Off: the effect only shows in this "
            "mod's own offer/cart window and the negotiated gold.");
    }
}

void ConfigMenu::RenderRelationshipsTab() {
    auto menuFramework = GetModuleHandle(L"SKSEMenuFramework");
    if (!menuFramework) return;

    auto ImGui_SliderFloat = reinterpret_cast<ImGui_SliderFloat_t>(GetProcAddress(menuFramework, "igSliderFloat"));
    auto ImGui_SliderInt = reinterpret_cast<ImGui_SliderInt_t>(GetProcAddress(menuFramework, "igSliderInt"));
    auto ImGui_Text = reinterpret_cast<ImGui_Text_t>(GetProcAddress(menuFramework, "igText"));
    auto ImGui_Separator = reinterpret_cast<ImGui_Separator_t>(GetProcAddress(menuFramework, "igSeparator"));
    auto ImGui_Button = reinterpret_cast<ImGui_Button_t>(GetProcAddress(menuFramework, "igButton"));
    auto ImGui_Checkbox = reinterpret_cast<ImGui_Checkbox_t>(GetProcAddress(menuFramework, "igCheckbox"));

    auto* s = Settings::GetSingleton();
    if (!s) return;

    // Reload data once when tab becomes active (transition from inactive -> active)
    bool justOpened = !relationshipsTabActive;
    relationshipsTabActive = true;
    if (justOpened) {
        RelationshipManager::GetSingleton()->LoadData();
    }

    // --- Relationship Settings ---
    if (ImGui_Text) ImGui_Text("== Relationship Settings ==");
    if (ImGui_Checkbox) {
        ImGui_Checkbox("Relationship Affects Prices", &s->relationshipPricing);
        Tip("On: a merchant who dislikes you charges MORE (the 'price jack' below), and "
            "your standing also sways how readily they accept offers. Off: relationship "
            "never changes prices.");
    }
    if (ImGui_Text) {
        ImGui_Text("When on, low relationships raise prices and high");
        ImGui_Text("relationships improve haggling success.");
    }
    if (ImGui_Separator) ImGui_Separator();

    if (ImGui_SliderFloat) {
        ImGui_SliderFloat("Rel Gain on Fair Deal", &s->relGainFairDeal, 1.0f, 10.0f, "%.1f", 0);
        Tip("How many relationship points you gain from a fair/generous deal.");
        Desc("  Higher = friendships build fast - a few honest deals quickly win a\n"
             "  merchant over; lower = trust is earned slowly over many visits.\n"
             "  (Relationships run from -100 to +100.)");

        ImGui_SliderFloat("Rel Loss on Insult", &s->relLossInsult, 1.0f, 20.0f, "%.1f", 0);
        Tip("How many relationship points you lose from an insultingly low offer.");
        Desc("  Higher = merchants take lowballs/insults hard and sour quickly; lower =\n"
             "  they're thick-skinned and forgive cheeky offers. Usually set above the\n"
             "  gain above so one insult undoes several fair deals.");

        ImGui_SliderFloat("Decay Rate/Day", &s->relDecayRate, 0.0f, 1.0f, "%.2f", 0);
        Tip("How fast relationships drift back toward neutral each in-game day. "
            "0 = relationships never fade.");
        Desc("  Higher = standing fades toward neutral quickly, so you must keep visiting\n"
             "  to maintain a bond (or a grudge); 0 = reputations are permanent and never\n"
             "  drift on their own.");

        ImGui_SliderFloat("Price Jack Intensity", &s->priceJackIntensity, 0.5f, 3.0f, "%.2f", 0);
        Tip("How sharply a low relationship inflates prices. Higher = a disliked "
            "merchant overcharges you much more steeply.");
        Desc("  Higher = being disliked hurts your wallet hard (steep overcharges from\n"
             "  enemies); lower = poor standing only nudges prices up. Pairs with the\n"
             "  threshold below, which sets WHEN the markup begins.");
    }
    if (ImGui_SliderInt) {
        ImGui_SliderInt("Price Jack Threshold", &s->priceJackThreshold, -50, 0, "%d", 0);
        Tip("Relationship level at/below which prices start to RISE. Only relationships "
            "below this raise prices. e.g. -10 means prices climb once you drop under -10.");
        Desc("  Closer to 0 = even slightly-cool merchants start charging more (harsh);\n"
             "  more negative (e.g. -40) = only merchants who truly hate you overcharge.\n"
             "  Sets the point where the markup above kicks in.");

        ImGui_SliderInt("Price Break Threshold", &s->priceBreakThreshold, 0, 50, "%d", 0);
        Tip("Relationship level at/above which you start earning a base-price BREAK "
            "(cheaper buys / better sells). Mirrors the jack threshold on the good side.");
        Desc("  Lower = friendly merchants start giving price breaks sooner (generous);\n"
             "  higher (e.g. 40) = only your closest allies reward you with better base\n"
             "  prices. The good-standing mirror of the jack threshold above.");
    }
    if (ImGui_Checkbox) {
        if (ImGui_Checkbox("Milestone Reputation", &s->milestoneReputation)) s->Save();
        Tip("Major story milestones lift your standing with a whole merchant category at "
            "once (Arch-Mage -> magic traders, Thieves Guild Master -> fences, Companions "
            "Harbinger -> blacksmiths, Bards College -> innkeepers). Applied once each, "
            "retroactively.");
    }

    if (ImGui_Separator) ImGui_Separator();
    if (ImGui_Text) ImGui_Text("== Merchant Relationships (from co-save) ==");
    if (ImGui_Text) ImGui_Text("Adjust relationships below. Changes auto-save.");

    auto* relMgr = RelationshipManager::GetSingleton();
    auto& allData = relMgr->GetAllData();

    if (allData.empty()) {
        if (ImGui_Text) ImGui_Text("  (No merchants interacted with yet)");
    } else {
        // Track changes so we can save at the end
        bool anyChanged = false;

        // Sort merchants by name for display stability
        std::vector<std::pair<RE::FormID, const MerchantMemory*>> sorted;
        sorted.reserve(allData.size());
        for (const auto& [id, mem] : allData) {
            sorted.push_back({id, &mem});
        }
        std::sort(sorted.begin(), sorted.end(), [](const auto& a, const auto& b) {
            return a.second->merchantName < b.second->merchantName;
        });

        for (const auto& [id, memPtr] : sorted) {
            const auto& mem = *memPtr;
            if (ImGui_Separator) ImGui_Separator();

            // Label with name, FormID, and stats
            if (ImGui_Text) {
                const char* relLabel =
                    mem.relationship >= 50 ? "Trusted" :
                    mem.relationship >= 20 ? "Warm" :
                    mem.relationship >= 5  ? "Friendly" :
                    mem.relationship >= -5 ? "Neutral" :
                    mem.relationship >= -20 ? "Cool" :
                    mem.relationship >= -50 ? "Hostile" : "Despised";

                ImGui_Text("%s [0x%08X] - %s (%d)",
                    mem.merchantName.c_str(), id, relLabel, mem.relationship);
                ImGui_Text("  Deals: %d | Accepted: %d | Lowballs: %d",
                    mem.totalDeals, mem.acceptedDeals, mem.lowballCount);
            }

            // Editable relationship slider
            if (ImGui_SliderInt) {
                // Need a unique label per slider for ImGui
                char sliderLabel[128];
                snprintf(sliderLabel, sizeof(sliderLabel), "##rel_%08X", id);
                int relValue = mem.relationship;
                int relMin = s->relMin;
                int relMax = s->relMax;
                if (ImGui_SliderInt(sliderLabel, &relValue, relMin, relMax, "%d", 0)) {
                    relMgr->SetRelationship(id, relValue);
                    anyChanged = true;
                    // Apply immediately to any open negotiation with this merchant: the
                    // offer window recomputes prices/meter and the vanilla cards re-render.
                    auto* mgr = BarterManager::GetSingleton();
                    if (mgr->GetCurrentMerchant() && mgr->GetCurrentMerchant()->GetFormID() == id) {
                        mgr->RefreshActiveOffer();
                    }
                }
                Tip("Drag to set your standing with this merchant. Negative = they "
                    "dislike you (worse prices & tougher haggling); positive = they "
                    "favour you. Changes auto-save.");
            }

            // Show how this relationship is affecting the merchant's prices right now,
            // so the price-jack effect is concrete and verifiable per merchant.
            if (ImGui_Text) {
                if (!s->relationshipPricing) {
                    ImGui_Text("  Price effect: off ('Relationship Affects Prices' disabled)");
                } else {
                    auto* actor = RE::TESForm::LookupByID<RE::Actor>(id);
                    MerchantPersonality pers = actor
                        ? MerchantPersonality::DetectFromActor(actor)
                        : MerchantPersonality::FromTrait(MerchantPersonality::Trait::Fair);
                    MerchantCategory cat = actor
                        ? Merchants::DetectCategory(actor) : MerchantCategory::Generalist;
                    int effRel = relMgr->GetEffectiveRelationship(id, cat);
                    // Bidirectional buy effect: <1 discount, >1 markup.
                    float buyMult = PriceJack::GetBuySellMultiplier(effRel, pers, true, false);
                    int discPct = static_cast<int>((1.0f - buyMult) * 100.0f + (buyMult <= 1.0f ? 0.5f : -0.5f));
                    if (discPct > 0) {
                        ImGui_Text("  Price effect: %d%% buy discount (%s, specialty: %s)",
                            discPct, MerchantPersonality::TraitToString(pers.trait),
                            Merchants::MerchantCategoryToString(cat));
                    } else if (discPct < 0) {
                        ImGui_Text("  Price effect: +%d%% buy markup (%s, specialty: %s)",
                            -discPct, MerchantPersonality::TraitToString(pers.trait),
                            Merchants::MerchantCategoryToString(cat));
                    } else {
                        ImGui_Text("  Price effect: market prices (%s, specialty: %s)",
                            MerchantPersonality::TraitToString(pers.trait),
                            Merchants::MerchantCategoryToString(cat));
                    }
                    int catOff = relMgr->GetCategoryReputation(cat);
                    if (catOff != 0) {
                        ImGui_Text("    (includes %+d category reputation; effective standing %d)",
                            catOff, effRel);
                    }
                }
            }

            // Reset button per merchant
            if (ImGui_Button) {
                char btnLabel[128];
                snprintf(btnLabel, sizeof(btnLabel), "Reset##%08X", id);
                if (ImGui_Button(btnLabel, 0)) {
                    relMgr->ResetMerchant(id);
                    anyChanged = true;
                }
            }
        }

        if (anyChanged) {
            relMgr->SaveData();
        }
    }

    if (ImGui_Separator) ImGui_Separator();
    if (ImGui_Text) ImGui_Text("== Category Reputation (milestone bonuses) ==");
    {
        auto catRep = relMgr->GetCategoryReputationSnapshot();
        bool anyCat = false;
        for (const auto& [catInt, offset] : catRep) {
            if (offset == 0) continue;
            anyCat = true;
            if (ImGui_Text) {
                ImGui_Text("  %s: %+d",
                    Merchants::MerchantCategoryToString(static_cast<MerchantCategory>(catInt)),
                    offset);
            }
        }
        if (!anyCat && ImGui_Text) {
            ImGui_Text("  (No milestones reached yet - keep adventuring!)");
        }
    }

    if (ImGui_Separator) ImGui_Separator();

    if (ImGui_Button) {
        if (ImGui_Button("Reset All Relationships", 0)) {
            relMgr->ResetAll();
            relMgr->SaveData();
        }
        if (ImGui_Button("Reload from Co-Save", 0)) {
            relMgr->LoadData();
        }
        if (ImGui_Button("Save Settings", 0)) {
            s->Save();
        }
    }
}

void ConfigMenu::RenderPersonalitiesTab() {
    auto menuFramework = GetModuleHandle(L"SKSEMenuFramework");
    if (!menuFramework) return;

    auto ImGui_Text = reinterpret_cast<ImGui_Text_t>(GetProcAddress(menuFramework, "igText"));
    auto ImGui_SliderFloat = reinterpret_cast<ImGui_SliderFloat_t>(GetProcAddress(menuFramework, "igSliderFloat"));
    auto ImGui_SliderInt = reinterpret_cast<ImGui_SliderInt_t>(GetProcAddress(menuFramework, "igSliderInt"));

    auto* s = Settings::GetSingleton();
    if (!s) return;

    if (ImGui_Text) {
        ImGui_Text("Greedy: -15%% accept, 8 offense, 20%% counter, 30%% split, 1.5x jack");
        ImGui_Text("Fair: 0%% accept, 4 offense, 40%% counter, 50%% split, 1.0x jack");
        ImGui_Text("Generous: +10%% accept, 2 offense, 60%% counter, 70%% split, 0.5x jack");
        ImGui_Text("Sleazy: +5%% accept, 0 offense, 80%% counter, 40-60%% split, 0.8x jack");
    }
    if (ImGui_SliderFloat) {
        ImGui_SliderFloat("Counter Base Chance", &s->counterOfferBaseChance, 0.0f, 100.0f, "%.0f%%", 0);
        Tip("Baseline chance a merchant makes a counter-offer instead of a flat yes/no. "
            "Personality adjusts this up or down.");
        Desc("  Higher = more back-and-forth haggling, with merchants countering instead\n"
             "  of giving a flat yes/no; 0 = they only ever accept or reject. Personality\n"
             "  shifts this per merchant. ~30% keeps counters as an occasional spice.");
    }
    if (ImGui_SliderInt) {
        ImGui_SliderInt("Patience Rounds", &s->counterOfferPatience, 1, 5, "%d", 0);
        Tip("How many haggling rounds a merchant tolerates before walking away from the "
            "deal. Personality can shorten or extend this.");
        Desc("  Higher = merchants endure more re-offer rounds before walking away (long\n"
             "  drawn-out haggles); lower = they lose patience fast and end the deal.\n"
             "  Personality can shorten or extend this per merchant.");
    }
}

void ConfigMenu::RenderDebugTab() {
    auto menuFramework = GetModuleHandle(L"SKSEMenuFramework");
    if (!menuFramework) return;

    auto ImGui_Checkbox = reinterpret_cast<ImGui_Checkbox_t>(GetProcAddress(menuFramework, "igCheckbox"));
    auto ImGui_Text = reinterpret_cast<ImGui_Text_t>(GetProcAddress(menuFramework, "igText"));
    auto ImGui_Separator = reinterpret_cast<ImGui_Separator_t>(GetProcAddress(menuFramework, "igSeparator"));
    auto ImGui_Button = reinterpret_cast<ImGui_Button_t>(GetProcAddress(menuFramework, "igButton"));

    auto* s = Settings::GetSingleton();
    if (!s) return;

    if (ImGui_Checkbox) {
        ImGui_Checkbox("Debug Logging (verbose)", &s->debugLogging);
        Tip("Writes detailed per-event logging to DynamicBarteringSKSE.log. Leave off "
            "for normal play; turn on when troubleshooting.");
        ImGui_Checkbox("Show Roll in Console", &s->showRollInConsole);
        Tip("Prints each acceptance dice-roll to the in-game console (~ key).");
        ImGui_Checkbox("Force Next Accept", &s->forceAccept);
        Tip("Testing: the merchant accepts your next offer no matter what.");
        ImGui_Checkbox("Force Next Reject", &s->forceReject);
        Tip("Testing: the merchant rejects your next offer no matter what.");
        ImGui_Checkbox("Force Next Counter", &s->forceCounter);
        Tip("Testing: the merchant makes a counter-offer on your next offer.");
    }

    if (ImGui_Separator) ImGui_Separator();
    if (ImGui_Text) ImGui_Text("Debug Tools:");

    if (ImGui_Button) {
        if (ImGui_Button("Dump Merchant Data to Log", 0)) {
            auto& allData = RelationshipManager::GetSingleton()->GetAllData();
            for (const auto& [id, mem] : allData) {
                logger::info("Merchant 0x{:08X} '{}': rel={} deals={} accepted={} lowballs={}",
                    id, mem.merchantName, mem.relationship, mem.totalDeals,
                    mem.acceptedDeals, mem.lowballCount);
            }
        }
        if (ImGui_Button("Save Data Now", 0)) {
            RelationshipManager::GetSingleton()->SaveData();
            Settings::GetSingleton()->Save();
        }
    }
}
