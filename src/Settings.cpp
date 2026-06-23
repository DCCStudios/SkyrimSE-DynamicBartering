#include "PCH.h"
#include "Settings.h"
#include "DebugLog.h"

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
    gamepadIconStyle = static_cast<GamepadIconStyle>(ini.GetLongValue("General", "iGamepadIconStyle", static_cast<int>(gamepadIconStyle)));
    {
        int themeIdx = static_cast<int>(ini.GetLongValue("General", "iUITheme", static_cast<int>(uiTheme)));
        if (themeIdx < 0 || themeIdx >= static_cast<int>(UITheme::kTotal)) themeIdx = 0;
        uiTheme = static_cast<UITheme>(themeIdx);
    }
    popupDelayMs = static_cast<int>(ini.GetLongValue("General", "iPopupDelayMs", popupDelayMs));
    skipBelowThreshold = ini.GetBoolValue("General", "bSkipBelowThreshold", skipBelowThreshold);
    valueThreshold = static_cast<int>(ini.GetLongValue("General", "iValueThreshold", valueThreshold));
    blockQuickBuy = ini.GetBoolValue("General", "bBlockQuickBuy", blockQuickBuy);
    cartVisibleByDefault = ini.GetBoolValue("General", "bCartVisibleByDefault", cartVisibleByDefault);
    holdToConfirm = ini.GetBoolValue("General", "bHoldToConfirm", holdToConfirm);
    holdToConfirmSec = static_cast<float>(ini.GetDoubleValue("General", "fHoldToConfirmSec", holdToConfirmSec));
    tutorialEnabled = ini.GetBoolValue("General", "bTutorialEnabled", tutorialEnabled);
    tutorialCartSeen = ini.GetBoolValue("General", "bTutorialCartSeen", tutorialCartSeen);
    tutorialOfferSeen = ini.GetBoolValue("General", "bTutorialOfferSeen", tutorialOfferSeen);

    // Cart
    cartHoldThreshold = static_cast<float>(ini.GetDoubleValue("Cart", "fHoldThreshold", cartHoldThreshold));
    cartHoldFillTime = static_cast<float>(ini.GetDoubleValue("Cart", "fHoldFillTime", cartHoldFillTime));
    cartPanelX = static_cast<float>(ini.GetDoubleValue("Cart", "fPanelX", cartPanelX));
    cartPanelY = static_cast<float>(ini.GetDoubleValue("Cart", "fPanelY", cartPanelY));
    cartPanelScale = static_cast<float>(ini.GetDoubleValue("Cart", "fPanelScale", cartPanelScale));

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
    relHaggleRangeWeight = static_cast<float>(ini.GetDoubleValue("Pricing", "fRelHaggleRangeWeight", relHaggleRangeWeight));
    neutralHaggleScale = static_cast<float>(ini.GetDoubleValue("Pricing", "fNeutralHaggleScale", neutralHaggleScale));
    maxBuyDiscount = static_cast<float>(ini.GetDoubleValue("Pricing", "fMaxBuyDiscount", maxBuyDiscount));
    maxSellMarkup = static_cast<float>(ini.GetDoubleValue("Pricing", "fMaxSellMarkup", maxSellMarkup));
    specialtyHaggling = ini.GetBoolValue("Pricing", "bSpecialtyHaggling", specialtyHaggling);
    specialtyWeight = static_cast<float>(ini.GetDoubleValue("Pricing", "fSpecialtyWeight", specialtyWeight));
    showRelationshipInVanillaPrices = ini.GetBoolValue("Pricing", "bShowRelationshipInVanillaPrices", showRelationshipInVanillaPrices);

    // Relationships
    relationshipPricing = ini.GetBoolValue("Relationships", "bRelationshipPricing", relationshipPricing);
    relGainFairDeal = static_cast<float>(ini.GetDoubleValue("Relationships", "fRelGainFairDeal", relGainFairDeal));
    relLossInsult = static_cast<float>(ini.GetDoubleValue("Relationships", "fRelLossInsult", relLossInsult));
    relDecayRate = static_cast<float>(ini.GetDoubleValue("Relationships", "fRelDecayRate", relDecayRate));
    relMax = static_cast<int>(ini.GetLongValue("Relationships", "iRelMax", relMax));
    relMin = static_cast<int>(ini.GetLongValue("Relationships", "iRelMin", relMin));
    priceJackThreshold = static_cast<int>(ini.GetLongValue("Relationships", "iPriceJackThreshold", priceJackThreshold));
    priceJackIntensity = static_cast<float>(ini.GetDoubleValue("Relationships", "fPriceJackIntensity", priceJackIntensity));
    priceBreakThreshold = static_cast<int>(ini.GetLongValue("Relationships", "iPriceBreakThreshold", priceBreakThreshold));
    milestoneReputation = ini.GetBoolValue("Relationships", "bMilestoneReputation", milestoneReputation);
    civilWarReputation = ini.GetBoolValue("Relationships", "bCivilWarReputation", civilWarReputation);
    civilWarRepBonus = static_cast<int>(ini.GetLongValue("Relationships", "iCivilWarRepBonus", civilWarRepBonus));

    // Personalities
    counterOfferBaseChance = static_cast<float>(ini.GetDoubleValue("Personalities", "fCounterOfferBaseChance", counterOfferBaseChance));
    counterOfferPatience = static_cast<int>(ini.GetLongValue("Personalities", "iCounterOfferPatience", counterOfferPatience));

    // Sound
    enableSounds = ini.GetBoolValue("Sound", "bEnableSounds", enableSounds);
    soundVolume = static_cast<float>(ini.GetDoubleValue("Sound", "fSoundVolume", soundVolume));
    useVanillaCartSounds = ini.GetBoolValue("Sound", "bUseVanillaCartSounds", useVanillaCartSounds);

    // CHIM
    enableChim = ini.GetBoolValue("CHIM", "bEnableChim", enableChim);
    chimServerUrl = ini.GetValue("CHIM", "sServerUrl", chimServerUrl.c_str());
    chimTimeoutMs = static_cast<int>(ini.GetLongValue("CHIM", "iTimeoutMs", chimTimeoutMs));
    chimImmediateReactions = ini.GetBoolValue("CHIM", "bImmediateReactions", chimImmediateReactions);
    chimReactionCooldownSec = static_cast<int>(ini.GetLongValue("CHIM", "iReactionCooldownSec", chimReactionCooldownSec));
    chimCounterCooldownSec = static_cast<int>(ini.GetLongValue("CHIM", "iCounterCooldownSec", chimCounterCooldownSec));
    chimUnpauseOfferWindow = ini.GetBoolValue("CHIM", "bUnpauseOfferWindow", chimUnpauseOfferWindow);
    chimSendDelaySec = static_cast<int>(ini.GetLongValue("CHIM", "iSendDelaySec", chimSendDelaySec));
    chimLiveContextLogging = ini.GetBoolValue("CHIM", "bLiveContextLogging", chimLiveContextLogging);

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
    ini.SetLongValue("General", "iGamepadIconStyle", static_cast<int>(gamepadIconStyle));
    ini.SetLongValue("General", "iUITheme", static_cast<int>(uiTheme));
    ini.SetLongValue("General", "iPopupDelayMs", popupDelayMs);
    ini.SetBoolValue("General", "bSkipBelowThreshold", skipBelowThreshold);
    ini.SetLongValue("General", "iValueThreshold", valueThreshold);
    ini.SetBoolValue("General", "bBlockQuickBuy", blockQuickBuy);
    ini.SetBoolValue("General", "bCartVisibleByDefault", cartVisibleByDefault);
    ini.SetBoolValue("General", "bHoldToConfirm", holdToConfirm);
    ini.SetDoubleValue("General", "fHoldToConfirmSec", holdToConfirmSec);
    ini.SetBoolValue("General", "bTutorialEnabled", tutorialEnabled);
    ini.SetBoolValue("General", "bTutorialCartSeen", tutorialCartSeen);
    ini.SetBoolValue("General", "bTutorialOfferSeen", tutorialOfferSeen);

    // Cart
    ini.SetDoubleValue("Cart", "fHoldThreshold", cartHoldThreshold);
    ini.SetDoubleValue("Cart", "fHoldFillTime", cartHoldFillTime);
    ini.SetDoubleValue("Cart", "fPanelX", cartPanelX);
    ini.SetDoubleValue("Cart", "fPanelY", cartPanelY);
    ini.SetDoubleValue("Cart", "fPanelScale", cartPanelScale);

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
    ini.SetDoubleValue("Pricing", "fRelHaggleRangeWeight", relHaggleRangeWeight);
    ini.SetDoubleValue("Pricing", "fNeutralHaggleScale", neutralHaggleScale);
    ini.SetDoubleValue("Pricing", "fMaxBuyDiscount", maxBuyDiscount);
    ini.SetDoubleValue("Pricing", "fMaxSellMarkup", maxSellMarkup);
    ini.SetBoolValue("Pricing", "bSpecialtyHaggling", specialtyHaggling);
    ini.SetDoubleValue("Pricing", "fSpecialtyWeight", specialtyWeight);
    ini.SetBoolValue("Pricing", "bShowRelationshipInVanillaPrices", showRelationshipInVanillaPrices);

    // Relationships
    ini.SetBoolValue("Relationships", "bRelationshipPricing", relationshipPricing);
    ini.SetDoubleValue("Relationships", "fRelGainFairDeal", relGainFairDeal);
    ini.SetDoubleValue("Relationships", "fRelLossInsult", relLossInsult);
    ini.SetDoubleValue("Relationships", "fRelDecayRate", relDecayRate);
    ini.SetLongValue("Relationships", "iRelMax", relMax);
    ini.SetLongValue("Relationships", "iRelMin", relMin);
    ini.SetLongValue("Relationships", "iPriceJackThreshold", priceJackThreshold);
    ini.SetDoubleValue("Relationships", "fPriceJackIntensity", priceJackIntensity);
    ini.SetLongValue("Relationships", "iPriceBreakThreshold", priceBreakThreshold);
    ini.SetBoolValue("Relationships", "bMilestoneReputation", milestoneReputation);
    ini.SetBoolValue("Relationships", "bCivilWarReputation", civilWarReputation);
    ini.SetLongValue("Relationships", "iCivilWarRepBonus", civilWarRepBonus);

    // Personalities
    ini.SetDoubleValue("Personalities", "fCounterOfferBaseChance", counterOfferBaseChance);
    ini.SetLongValue("Personalities", "iCounterOfferPatience", counterOfferPatience);

    // Sound
    ini.SetBoolValue("Sound", "bEnableSounds", enableSounds);
    ini.SetDoubleValue("Sound", "fSoundVolume", soundVolume);
    ini.SetBoolValue("Sound", "bUseVanillaCartSounds", useVanillaCartSounds);

    // CHIM
    ini.SetBoolValue("CHIM", "bEnableChim", enableChim);
    ini.SetValue("CHIM", "sServerUrl", chimServerUrl.c_str());
    ini.SetLongValue("CHIM", "iTimeoutMs", chimTimeoutMs);
    ini.SetBoolValue("CHIM", "bImmediateReactions", chimImmediateReactions);
    ini.SetLongValue("CHIM", "iReactionCooldownSec", chimReactionCooldownSec);
    ini.SetLongValue("CHIM", "iCounterCooldownSec", chimCounterCooldownSec);
    ini.SetBoolValue("CHIM", "bUnpauseOfferWindow", chimUnpauseOfferWindow);
    ini.SetLongValue("CHIM", "iSendDelaySec", chimSendDelaySec);
    ini.SetBoolValue("CHIM", "bLiveContextLogging", chimLiveContextLogging);

    // Debug
    ini.SetBoolValue("Debug", "bDebugLogging", debugLogging);
    ini.SetBoolValue("Debug", "bShowRollInConsole", showRollInConsole);

    auto path = GetConfigPath();
    std::filesystem::create_directories(std::filesystem::path(path).parent_path());
    ini.SaveFile(path.c_str());
    DbgLog("Settings saved to {}", path);
}
