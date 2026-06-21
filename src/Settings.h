#pragma once

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
    int popupDelayMs = 200;
    bool skipBelowThreshold = false;
    int valueThreshold = 50;

    // Cart system
    float cartHoldThreshold = 0.6f;  // seconds to hold Y/B before opening cart negotiation
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

    // Relationships
    bool relationshipPricing = true;  // relationship influences prices & acceptance
    float relGainFairDeal = 3.0f;
    float relLossInsult = 5.0f;
    float relDecayRate = 0.1f;
    int relMax = 100;
    int relMin = -100;
    int priceJackThreshold = -20;
    float priceJackIntensity = 1.0f;

    // Personalities
    float counterOfferBaseChance = 30.0f;
    int counterOfferPatience = 3;

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
