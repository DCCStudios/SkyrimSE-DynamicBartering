#include "PCH.h"
#include "PriceCalculator.h"
#include "Settings.h"

PriceResult PriceCalculator::CalculatePrice(const PriceContext& ctx, int relationship, float priceJackMult) {
    PriceResult result;
    auto* settings = Settings::GetSingleton();

    // itemBaseValue is now the actual vanilla barter price (what the game would charge)
    // We use this directly as our starting point for negotiation
    result.basePrice = ctx.itemBaseValue;
    if (result.basePrice < 1) result.basePrice = 1;

    // Apply price jacking from bad relationship (makes items more expensive)
    result.effectivePrice = static_cast<int>(result.basePrice * priceJackMult);
    if (result.effectivePrice < 1) result.effectivePrice = 1;

    auto perks = PerkBonuses::Detect(ctx.player);
    result.sliderMin = settings->sliderRangeMin - perks.GetSliderRangeBonus();
    result.sliderMax = settings->sliderRangeMax + perks.GetSliderRangeBonus();
    result.priceJackMultiplier = priceJackMult;

    return result;
}

float PriceCalculator::CalculateAcceptanceChance(const AcceptanceContext& ctx) {
    auto* settings = Settings::GetSingleton();

    if (settings->forceAccept) return 100.0f;
    if (settings->forceReject) return 0.0f;

    // Offering at or above market price is almost always accepted
    if (ctx.basePrice > 0 && ctx.offeredPrice >= ctx.basePrice) {
        return 99.0f;
    }

    float chance = settings->baseAcceptanceChance;

    // Speech skill bonus
    chance += (ctx.speechSkill / 100.0f) * settings->speechWeight;

    // Perk bonuses
    chance += ctx.perks.GetAcceptanceBonus(ctx.oppositeGender);

    // Relationship bonus (toggleable in config menu)
    if (settings->relationshipPricing) {
        chance += (static_cast<float>(ctx.relationship) / 100.0f) * settings->relationshipWeight;
    }

    // Personality modifier
    chance += ctx.personality.acceptanceMod;

    // Deal history
    if (ctx.memory) {
        int fairDeals = ctx.memory->GetConsecutiveFairDeals();
        chance += std::min(static_cast<float>(fairDeals) * settings->dealHistoryWeight, 10.0f);

        int recentLowballs = ctx.memory->GetRecentLowballCount(3);
        chance -= std::min(static_cast<float>(recentLowballs) * 3.0f, 15.0f);
    }

    // Discount penalty (greed)
    float discount = 0.0f;
    if (ctx.basePrice > 0) {
        discount = 1.0f - (static_cast<float>(ctx.offeredPrice) / static_cast<float>(ctx.basePrice));
    }
    if (discount > 0.0f) {
        chance -= discount * 100.0f * settings->greedFactor;
    }

    // Bonus for offering more than base price
    if (discount < 0.0f) {
        chance += std::abs(discount) * 50.0f;
    }

    // Stolen item penalty
    if (ctx.isStolen) {
        float penalty = settings->stolenItemPenalty;
        if (ctx.perks.hasFence) {
            penalty *= (1.0f - settings->fencePerkReduction / 100.0f);
        }
        chance -= penalty;
    }

    return std::clamp(chance, 0.0f, 99.0f);
}

bool PriceCalculator::RollAcceptance(float chance) {
    static std::mt19937 rng(std::random_device{}());
    std::uniform_real_distribution<float> dist(0.0f, 100.0f);
    return dist(rng) < chance;
}

float PriceCalculator::GetSpeechSkill(RE::Actor* player) {
    if (!player) return 15.0f;
    auto avOwner = player->AsActorValueOwner();
    if (!avOwner) return 15.0f;
    return avOwner->GetActorValue(RE::ActorValue::kSpeech);
}

bool PriceCalculator::IsOppositeGender(RE::Actor* player, RE::Actor* merchant) {
    if (!player || !merchant) return false;
    auto playerBase = player->GetActorBase();
    auto merchantBase = merchant->GetActorBase();
    if (!playerBase || !merchantBase) return false;
    return playerBase->GetSex() != merchantBase->GetSex();
}
