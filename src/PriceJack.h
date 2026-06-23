#pragma once
#include "MerchantPersonality.h"

class PriceJack {
public:
    // Legacy markup-only multiplier (>= 1.0). Kept for callers that only want the
    // "poor standing makes things pricier" effect.
    static float GetMultiplier(int relationship, const MerchantPersonality& personality, bool hasInvestorPerk = false);

    // Bidirectional, direction-aware base-price multiplier driven by the player's
    // standing + the merchant's personality. Good standing earns a discount when
    // buying (mult < 1) and a bonus when selling (mult > 1); poor standing imposes a
    // markup when buying (mult > 1) and a worse price when selling (mult < 1).
    // Returns 1.0 when relationship pricing is disabled. Clamped by the configured
    // maxBuyDiscount / maxSellMarkup on the favorable side.
    static float GetBuySellMultiplier(int relationship, const MerchantPersonality& personality,
                                      bool isBuying, bool hasInvestorPerk = false);

    static std::string GetDescription(float multiplier);
};
