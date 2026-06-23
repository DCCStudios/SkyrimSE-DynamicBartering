#pragma once
#include "PerkDetector.h"
#include "MerchantPersonality.h"
#include "DealMemory.h"

struct PriceContext {
    RE::Actor* player = nullptr;
    RE::Actor* merchant = nullptr;
    RE::TESBoundObject* item = nullptr;
    int itemBaseValue = 0;
    bool isBuying = false;
    bool isStolen = false;
    MerchantPersonality personality;  // drives how far standing can widen the range
};

struct PriceResult {
    int basePrice = 0;
    int effectivePrice = 0;
    float sliderMin = -0.30f;
    float sliderMax = 0.30f;
    float priceJackMultiplier = 1.0f;
};

struct AcceptanceContext {
    float speechSkill = 0.0f;
    PerkBonuses perks;
    int relationship = 0;
    MerchantPersonality personality;
    const MerchantMemory* memory = nullptr;
    int offeredPrice = 0;
    int basePrice = 0;
    bool oppositeGender = false;
    bool isStolen = false;
    // true = player is buying (paying more is generous), false = selling (asking
    // less is generous). Drives which slider direction the merchant likes.
    bool isBuying = true;
    // Highest price already refused this session for this item (0 = none). If the
    // current offer is at or below this, the merchant is much less willing.
    int sessionRejectedPrice = 0;
    // Merchant specialty match for this item/cart: >0 = in-specialty (easier to
    // haggle), <0 = off-specialty (harder), 0 = neutral. Scaled by specialtyWeight.
    float specialtyFactor = 0.0f;
};

class PriceCalculator {
public:
    static PriceResult CalculatePrice(const PriceContext& ctx, int relationship, float priceJackMult);
    static float CalculateAcceptanceChance(const AcceptanceContext& ctx);
    static bool RollAcceptance(float chance);

    // Relationship-driven haggling range. Better standing (within the merchant's
    // personality bounds) widens the FAVORABLE side - a deeper buy discount / higher
    // sell overcharge; poor standing contracts it. Writes the resulting slider
    // fractions (offset from market price). Shared by single-item and cart offers.
    static void ComputeHaggleRange(bool isBuying, int effectiveRelationship,
                                   const MerchantPersonality& personality,
                                   float perkSliderBonus, float& outMin, float& outMax);

private:
    static float GetSpeechSkill(RE::Actor* player);
    static bool IsOppositeGender(RE::Actor* player, RE::Actor* merchant);
};
