#pragma once

#include "Theme.h"  // UITheme

enum class UIMode : int { Auto = 0, ScaleformSWF = 1, PrismaUI = 2 };
enum class GamepadIconStyle : int { Xbox = 0, PlayStation = 1 };

class Settings {
public:
    static Settings* GetSingleton() {
        static Settings instance;
        return &instance;
    }

    void Load();
    void Save();

    // General
    bool modEnabled = true;
    UIMode uiMode = UIMode::ScaleformSWF;
    bool showAcceptanceHint = true;
    bool showRelationshipPreview = true;
    GamepadIconStyle gamepadIconStyle = GamepadIconStyle::Xbox;  // controller glyph set for keybind hints
    // Visual theme for the barter UI (colors + tints). Fonts follow the user's installed
    // UI overhaul automatically (we use the $Everywhere* font aliases), so picking the
    // theme that matches an installed overhaul makes the window blend in.
    UITheme uiTheme = UITheme::Default;
    int popupDelayMs = 200;
    bool skipBelowThreshold = false;
    int valueThreshold = 50;
    // Cart panel is shown the moment the barter menu opens (with an empty-state hint),
    // not just after the first item is added.
    bool cartVisibleByDefault = true;
    // Hold-to-confirm on the offer window's Submit (gold fill) and Intimidate (red fill)
    // buttons: the button fills horizontally as you hold it and only commits when full.
    // Cancel is unaffected (always instant). Off = single press/click commits instantly.
    bool holdToConfirm = true;
    float holdToConfirmSec = 0.65f;  // hold time to fully charge a confirm/intimidate
    // Two-step in-game tutorial. tutorialEnabled is armed on first install; the two
    // *Seen flags track which popups have been shown. When both are seen the tutorial
    // auto-disables; re-enabling it in the menu clears both seen flags so it replays.
    bool tutorialEnabled = true;
    bool tutorialCartSeen = false;
    bool tutorialOfferSeen = false;
    // When true, the vanilla instant buy/sell (item select) is blocked entirely:
    // tapping activate/the barter key adds the highlighted item to the cart, and
    // holding the barter key, the activate key, or the mouse button opens the
    // barter offer window. When false, vanilla quick buy/sell works as normal.
    bool blockQuickBuy = true;

    // Cart system
    // Tap window: release before this = a TAP (add/remove item). Hold past it and the
    // hold "engages" - only THEN does the fill meter start.
    float cartHoldThreshold = 0.35f;
    // After the tap window, the meter fills over this long before the cart offer opens.
    float cartHoldFillTime = 0.5f;
    // Cart panel placement (authored BarterMenu stage is ~1280x720). Defaults sit
    // the panel just left of the merchant, matching the outlined target spot.
    float cartPanelX = 596.0f;
    float cartPanelY = 110.0f;
    float cartPanelScale = 1.0f;  // 0.5..1.5 uniform scale of the cart panel

    // Pricing
    float sliderRangeMin = -0.30f;
    float sliderRangeMax = 0.30f;
    float baseAcceptanceChance = 50.0f;
    float speechWeight = 20.0f;
    float hagglingPerkBonus = 5.0f;
    float persuasionPerkBonus = 15.0f;
    float allureBonus = 10.0f;
    float relationshipWeight = 15.0f;
    float personalityWeight = 20.0f;
    float dealHistoryWeight = 2.0f;
    float greedFactor = 1.5f;
    bool useVanillaBasePrice = true;
    float stolenItemPenalty = 30.0f;
    float fencePerkReduction = 66.0f;

    // --- Relationship-driven haggling range -------------------------------------
    // How strongly the player's standing widens (when liked) or contracts (when
    // disliked) the achievable haggling spread. At +/-100 relationship this adds/
    // subtracts up to (relHaggleRangeWeight * personality.haggleRangeScale) to the
    // favorable side of the range (deeper buy discounts / higher sell overcharges).
    float relHaggleRangeWeight = 0.35f;
    // Scales the NEUTRAL-standing baseline haggle room (the spread you get at 0
    // relationship before any standing bonus). Lower = tighter deals out of the gate;
    // relationship + perks then open it up from there. The baseline is additionally
    // shaped by the merchant's personality (Greedy/Stern give less, Generous/Sleazy
    // more) even at neutral standing.
    float neutralHaggleScale = 0.45f;
    // Hard caps on the favorable side of the slider so even a beloved merchant can't
    // be pushed past these (fraction of market price).
    float maxBuyDiscount = 0.60f;   // up to 60% off when buying
    float maxSellMarkup = 0.60f;    // up to 60% over market when selling

    // --- Merchant specialties ---------------------------------------------------
    // Merchants haggle more readily on goods they actually deal in. specialtyWeight
    // is how many acceptance-chance points a full in-specialty match adds (and an
    // off-specialty mismatch subtracts).
    bool specialtyHaggling = true;
    float specialtyWeight = 12.0f;

    // Reflect the relationship/personality price effect directly in the vanilla
    // BarterMenu item-card price (via a Scaleform UpdateItemCardInfo wrapper that
    // chains with other price mods). Off -> the effect only shows in the mod's own
    // offer/cart UI and the negotiated gold.
    bool showRelationshipInVanillaPrices = true;

    // Relationships
    bool relationshipPricing = true;  // relationship influences prices & acceptance
    float relGainFairDeal = 3.0f;
    float relLossInsult = 5.0f;
    float relDecayRate = 0.1f;
    int relMax = 100;
    int relMin = -100;
    int priceJackThreshold = -20;
    float priceJackIntensity = 1.0f;
    // Good standing also earns a base-price break (and bad standing a markup): this
    // is the relationship at/above which buy discounts (and sell bonuses) kick in,
    // mirroring priceJackThreshold on the favorable side.
    int priceBreakThreshold = 20;
    // Widespread reputation gains across a whole merchant category when the player
    // hits major milestones (e.g. Archmage -> magic traders, Guildmaster -> fences).
    // Also covers per-hold Thane bonuses (becoming Thane lifts that hold's merchants).
    bool milestoneReputation = true;
    // Civil-war standing: once you join a side, merchants in holds your side controls
    // warm to you and merchants in enemy-held holds turn cold. Recomputed as holds
    // change hands. Gated behind milestoneReputation as well.
    bool civilWarReputation = true;
    int civilWarRepBonus = 8;  // standing swing per hold (your side: +, enemy side: -)

    // Personalities
    float counterOfferBaseChance = 30.0f;
    int counterOfferPatience = 3;

    // Sound
    bool enableSounds = true;
    float soundVolume = 1.0f;  // 0..1 master volume for all of this mod's UI sounds
    // When true, adding/removing an item to/from the cart plays that item's own vanilla
    // pickup/putdown sound (the cue Skyrim would play on a quick buy/sell), instead of
    // the mod's generic AddToCart/RemoveFromCart WAVs. Falls back to the WAV cue when the
    // item has no resolvable sound.
    bool useVanillaCartSounds = true;

    // CHIM / HerikaServer integration. When enabled, barter outcomes are pushed to a
    // running CHIM server so AI NPCs can detect and react to haggling/intimidation.
    // Off by default so the mod is inert for non-CHIM users.
    bool enableChim = false;
    std::string chimServerUrl = "http://localhost:8081";  // base URL; comm.php is derived from it
    int chimTimeoutMs = 3000;                              // per-request WinHTTP timeout
    bool chimImmediateReactions = true;                    // queue spoken barks for big moments
    // Per-merchant cooldowns (seconds) sent to the server so it throttles spoken
    // reactions. Big moments (intimidation/overpay/insult) and counter-offers each
    // have their own timer so they don't mute one another.
    int chimReactionCooldownSec = 20;
    int chimCounterCooldownSec = 6;
    // Skyrim won't voice NPC lines while a menu is the topmost view (even unpaused),
    // so barter events are buffered during the menu and sent once it closes. This is an
    // optional extra delay (seconds) after close before sending, giving the game time to
    // unpause and the player to settle back into the world so the reaction is audible.
    int chimSendDelaySec = 3;
    // Also push each outcome to CHIM live, mid-haggle, as memory only (no spoken bark,
    // since Skyrim mutes voice AND subtitles while the menu is open). This just feeds the
    // merchant's running memory so it "knows" the blow-by-blow if you talk to it later.
    // The audible reaction still comes from the after-close summary. Off by default.
    bool chimLiveContextLogging = false;
    // When SkyrimSouls (Unpaused Game Menus) is installed it keeps the world running
    // during the barter menu so CHIM can speak live. This makes sure our custom offer
    // window never re-introduces a pause on top of that. Ignored if SkyrimSouls is absent.
    bool chimUnpauseOfferWindow = true;

    // Debug
    bool debugLogging = false;
    bool showRollInConsole = false;
    bool forceAccept = false;
    bool forceReject = false;
    bool forceCounter = false;

private:
    Settings() = default;
    std::string GetConfigPath() const;
};
