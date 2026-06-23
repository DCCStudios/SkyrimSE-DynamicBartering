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

float PriceJack::GetBuySellMultiplier(int relationship, const MerchantPersonality& personality,
                                      bool isBuying, bool hasInvestorPerk) {
    auto* settings = Settings::GetSingleton();
    if (!settings->relationshipPricing) return 1.0f;

    // favor > 0 = the player is in good standing (earns a break); favor < 0 = poor
    // standing (earns a penalty). Both sides are scaled by the merchant's personality.
    float favor = 0.0f;
    if (relationship <= settings->priceJackThreshold) {
        // Poor standing: personalities with a high priceJackMult gouge harder.
        float jack = std::abs(relationship - settings->priceJackThreshold) * 0.002f *
                     personality.priceJackMult * settings->priceJackIntensity;
        favor = -jack;
    } else if (relationship >= settings->priceBreakThreshold) {
        // Good standing: generous personalities (low priceJackMult) give bigger breaks.
        float genScale = std::clamp(2.0f - personality.priceJackMult, 0.25f, 1.75f);
        float brk = (relationship - settings->priceBreakThreshold) * 0.002f *
                    genScale * settings->priceJackIntensity;
        favor = brk;
    }

    // Investor perk softens the swing in both directions.
    if (hasInvestorPerk) favor *= 0.5f;

    // Bound the raw favor so the unfavorable side can't run away; the favorable side
    // is additionally capped by the configured max discount/markup below.
    favor = std::clamp(favor, -0.5f, 0.75f);

    float mult;
    if (isBuying) {
        // Good favor -> cheaper to buy (mult < 1); poor favor -> markup (mult > 1).
        mult = 1.0f - favor;
        mult = std::clamp(mult, 1.0f - settings->maxBuyDiscount, 1.5f);
    } else {
        // Good favor -> you receive more (mult > 1); poor favor -> you receive less.
        mult = 1.0f + favor;
        mult = std::clamp(mult, 0.5f, 1.0f + settings->maxSellMarkup);
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
