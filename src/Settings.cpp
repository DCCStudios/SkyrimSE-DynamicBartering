#include "PCH.h"
#include "Settings.h"

std::string Settings::GetConfigPath() const {
    return "Data/SKSE/Plugins/DynamicBartering/DynamicBartering.ini";
}

void Settings::Load() {
    CSimpleIniA ini;
    ini.SetUnicode();

    auto path = GetConfigPath();
    SI_Error rc = ini.LoadFile(path.c_str());
    if (rc < 0) {
        logger::info("No config file found at {}, using defaults", path);
        Save();
        return;
    }

    // General
    modEnabled = ini.GetBoolValue("General", "bEnabled", modEnabled);
    uiMode = static_cast<UIMode>(ini.GetLongValue("General", "iUIMode", static_cast<int>(uiMode)));
    showAcceptanceHint = ini.GetBoolValue("General", "bShowAcceptanceHint", showAcceptanceHint);
    showRelationshipPreview = ini.GetBoolValue("General", "bShowRelationshipPreview", showRelationshipPreview);
    popupDelayMs = static_cast<int>(ini.GetLongValue("General", "iPopupDelayMs", popupDelayMs));
    skipBelowThreshold = ini.GetBoolValue("General", "bSkipBelowThreshold", skipBelowThreshold);
    valueThreshold = static_cast<int>(ini.GetLongValue("General", "iValueThreshold", valueThreshold));

    // Pricing
    sliderRangeMin = static_cast<float>(ini.GetDoubleValue("Pricing", "fSliderRangeMin", sliderRangeMin));
    sliderRangeMax = static_cast<float>(ini.GetDoubleValue("Pricing", "fSliderRangeMax", sliderRangeMax));
    baseAcceptanceChance = static_cast<float>(ini.GetDoubleValue("Pricing", "fBaseAcceptanceChance", baseAcceptanceChance));
    speechWeight = static_cast<float>(ini.GetDoubleValue("Pricing", "fSpeechWeight", speechWeight));
    hagglingPerkBonus = static_cast<float>(ini.GetDoubleValue("Pricing", "fHagglingPerkBonus", hagglingPerkBonus));
    persuasionPerkBonus = static_cast<float>(ini.GetDoubleValue("Pricing", "fPersuasionPerkBonus", persuasionPerkBonus));
    allureBonus = static_cast<float>(ini.GetDoubleValue("Pricing", "fAllureBonus", allureBonus));
    relationshipWeight = static_cast<float>(ini.GetDoubleValue("Pricing", "fRelationshipWeight", relationshipWeight));
    personalityWeight = static_cast<float>(ini.GetDoubleValue("Pricing", "fPersonalityWeight", personalityWeight));
    dealHistoryWeight = static_cast<float>(ini.GetDoubleValue("Pricing", "fDealHistoryWeight", dealHistoryWeight));
    greedFactor = static_cast<float>(ini.GetDoubleValue("Pricing", "fGreedFactor", greedFactor));
    useVanillaBasePrice = ini.GetBoolValue("Pricing", "bUseVanillaBasePrice", useVanillaBasePrice);
    stolenItemPenalty = static_cast<float>(ini.GetDoubleValue("Pricing", "fStolenItemPenalty", stolenItemPenalty));
    fencePerkReduction = static_cast<float>(ini.GetDoubleValue("Pricing", "fFencePerkReduction", fencePerkReduction));

    // Relationships
    relationshipPricing = ini.GetBoolValue("Relationships", "bRelationshipPricing", relationshipPricing);
    relGainFairDeal = static_cast<float>(ini.GetDoubleValue("Relationships", "fRelGainFairDeal", relGainFairDeal));
    relLossInsult = static_cast<float>(ini.GetDoubleValue("Relationships", "fRelLossInsult", relLossInsult));
    relDecayRate = static_cast<float>(ini.GetDoubleValue("Relationships", "fRelDecayRate", relDecayRate));
    relMax = static_cast<int>(ini.GetLongValue("Relationships", "iRelMax", relMax));
    relMin = static_cast<int>(ini.GetLongValue("Relationships", "iRelMin", relMin));
    priceJackThreshold = static_cast<int>(ini.GetLongValue("Relationships", "iPriceJackThreshold", priceJackThreshold));
    priceJackIntensity = static_cast<float>(ini.GetDoubleValue("Relationships", "fPriceJackIntensity", priceJackIntensity));

    // Personalities
    counterOfferBaseChance = static_cast<float>(ini.GetDoubleValue("Personalities", "fCounterOfferBaseChance", counterOfferBaseChance));
    counterOfferPatience = static_cast<int>(ini.GetLongValue("Personalities", "iCounterOfferPatience", counterOfferPatience));

    // Debug
    debugLogging = ini.GetBoolValue("Debug", "bDebugLogging", debugLogging);
    showRollInConsole = ini.GetBoolValue("Debug", "bShowRollInConsole", showRollInConsole);

    logger::info("Settings loaded from {}", path);
}

void Settings::Save() {
    CSimpleIniA ini;
    ini.SetUnicode();

    // General
    ini.SetBoolValue("General", "bEnabled", modEnabled);
    ini.SetLongValue("General", "iUIMode", static_cast<int>(uiMode));
    ini.SetBoolValue("General", "bShowAcceptanceHint", showAcceptanceHint);
    ini.SetBoolValue("General", "bShowRelationshipPreview", showRelationshipPreview);
    ini.SetLongValue("General", "iPopupDelayMs", popupDelayMs);
    ini.SetBoolValue("General", "bSkipBelowThreshold", skipBelowThreshold);
    ini.SetLongValue("General", "iValueThreshold", valueThreshold);

    // Pricing
    ini.SetDoubleValue("Pricing", "fSliderRangeMin", sliderRangeMin);
    ini.SetDoubleValue("Pricing", "fSliderRangeMax", sliderRangeMax);
    ini.SetDoubleValue("Pricing", "fBaseAcceptanceChance", baseAcceptanceChance);
    ini.SetDoubleValue("Pricing", "fSpeechWeight", speechWeight);
    ini.SetDoubleValue("Pricing", "fHagglingPerkBonus", hagglingPerkBonus);
    ini.SetDoubleValue("Pricing", "fPersuasionPerkBonus", persuasionPerkBonus);
    ini.SetDoubleValue("Pricing", "fAllureBonus", allureBonus);
    ini.SetDoubleValue("Pricing", "fRelationshipWeight", relationshipWeight);
    ini.SetDoubleValue("Pricing", "fPersonalityWeight", personalityWeight);
    ini.SetDoubleValue("Pricing", "fDealHistoryWeight", dealHistoryWeight);
    ini.SetDoubleValue("Pricing", "fGreedFactor", greedFactor);
    ini.SetBoolValue("Pricing", "bUseVanillaBasePrice", useVanillaBasePrice);
    ini.SetDoubleValue("Pricing", "fStolenItemPenalty", stolenItemPenalty);
    ini.SetDoubleValue("Pricing", "fFencePerkReduction", fencePerkReduction);

    // Relationships
    ini.SetBoolValue("Relationships", "bRelationshipPricing", relationshipPricing);
    ini.SetDoubleValue("Relationships", "fRelGainFairDeal", relGainFairDeal);
    ini.SetDoubleValue("Relationships", "fRelLossInsult", relLossInsult);
    ini.SetDoubleValue("Relationships", "fRelDecayRate", relDecayRate);
    ini.SetLongValue("Relationships", "iRelMax", relMax);
    ini.SetLongValue("Relationships", "iRelMin", relMin);
    ini.SetLongValue("Relationships", "iPriceJackThreshold", priceJackThreshold);
    ini.SetDoubleValue("Relationships", "fPriceJackIntensity", priceJackIntensity);

    // Personalities
    ini.SetDoubleValue("Personalities", "fCounterOfferBaseChance", counterOfferBaseChance);
    ini.SetLongValue("Personalities", "iCounterOfferPatience", counterOfferPatience);

    // Debug
    ini.SetBoolValue("Debug", "bDebugLogging", debugLogging);
    ini.SetBoolValue("Debug", "bShowRollInConsole", showRollInConsole);

    auto path = GetConfigPath();
    std::filesystem::create_directories(std::filesystem::path(path).parent_path());
    ini.SaveFile(path.c_str());
    logger::info("Settings saved to {}", path);
}
