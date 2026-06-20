#pragma once
#include "MerchantPersonality.h"

class PriceJack {
public:
    static float GetMultiplier(int relationship, const MerchantPersonality& personality, bool hasInvestorPerk = false);
    static std::string GetDescription(float multiplier);
};
