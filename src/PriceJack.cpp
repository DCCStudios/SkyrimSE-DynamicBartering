#include "PCH.h"
#include "PriceJack.h"
#include "Settings.h"

float PriceJack::GetMultiplier(int relationship, const MerchantPersonality& personality, bool hasInvestorPerk) {
    auto* settings = Settings::GetSingleton();

    // Relationship-based pricing can be disabled in the config menu
    if (!settings->relationshipPricing) return 1.0f;

    if (relationship >= settings->priceJackThreshold) return 1.0f;

    float baseJack = std::abs(relationship - settings->priceJackThreshold) * 0.002f;
    float mult = 1.0f + (baseJack * personality.priceJackMult * settings->priceJackIntensity);

    if (hasInvestorPerk) {
        mult = 1.0f + (mult - 1.0f) * 0.5f;
    }

    return mult;
}

std::string PriceJack::GetDescription(float multiplier) {
    if (multiplier <= 1.0f) return "";
    if (multiplier < 1.05f) return "Merchant seems slightly cold toward you";
    if (multiplier < 1.10f) return "Merchant seems displeased with you";
    if (multiplier < 1.20f) return "Merchant is clearly overcharging you";
    return "Merchant despises you and prices reflect it";
}
