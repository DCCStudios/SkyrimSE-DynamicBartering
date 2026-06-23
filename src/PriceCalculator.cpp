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
    ComputeHaggleRange(ctx.isBuying, relationship, ctx.personality,
                       perks.GetSliderRangeBonus(), result.sliderMin, result.sliderMax);
    result.priceJackMultiplier = priceJackMult;

    return result;
}

void PriceCalculator::ComputeHaggleRange(bool isBuying, int effectiveRelationship,
                                         const MerchantPersonality& personality,
                                         float perkSliderBonus, float& outMin, float& outMax) {
    auto* settings = Settings::GetSingleton();

    // Neutral-standing baseline favorable room. Tightened by neutralHaggleScale and
    // shaped by the merchant's PERSONALITY so even at 0 relationship a Greedy/Stern
    // merchant (haggleRangeScale 0.6) opens far less than a Generous one (1.4). This is
    // the spread you get out of the gate, before standing/perks widen it.
    const float favorBase = std::abs(settings->sliderRangeMin) *
                            settings->neutralHaggleScale * personality.haggleRangeScale;
    const float unfavor = settings->sliderRangeMax + perkSliderBonus;

    // Standing widens (liked) or contracts (disliked) the favorable side, scaled by
    // the merchant's personality. relNorm in [-1, +1].
    float relNorm = 0.0f;
    if (settings->relationshipPricing) {
        relNorm = std::clamp(static_cast<float>(effectiveRelationship) / 100.0f, -1.0f, 1.0f);
    }
    const float room = relNorm * settings->relHaggleRangeWeight * personality.haggleRangeScale;

    // Favorable extent: baseline + standing room + perk bonus, floored at 0 (a
    // strongly-disliked merchant offers no room beyond market) and capped.
    const float cap = isBuying ? settings->maxBuyDiscount : settings->maxSellMarkup;
    float favor = std::clamp(favorBase + room + perkSliderBonus, 0.0f, cap);

    if (isBuying) {
        outMin = -favor;     // how far below market the player may push (discount)
        outMax = unfavor;    // may also offer above market
    } else {
        outMax = favor;      // how far above market the player may ask (overcharge)
        outMin = -unfavor;
    }
}

float PriceCalculator::CalculateAcceptanceChance(const AcceptanceContext& ctx) {
    auto* settings = Settings::GetSingleton();

    if (settings->forceAccept) return 100.0f;
    if (settings->forceReject) return 0.0f;

    // "greed" = how much more favorable to the player than market the offer is,
    // as a fraction of market. Direction depends on buy vs sell:
    //   buying  -> player wants to pay LESS    -> greed = (base - offered)/base
    //   selling -> player wants to receive MORE -> greed = (offered - base)/base
    // greed <= 0 means the offer is at least as good for the merchant as market,
    // which the merchant almost always takes.
    float greed = 0.0f;
    if (ctx.basePrice > 0) {
        if (ctx.isBuying) {
            greed = 1.0f - (static_cast<float>(ctx.offeredPrice) / static_cast<float>(ctx.basePrice));
        } else {
            greed = (static_cast<float>(ctx.offeredPrice) / static_cast<float>(ctx.basePrice)) - 1.0f;
        }
    }

    if (ctx.basePrice > 0 && greed <= 0.0f) {
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

    // Merchant specialty: more willing on goods they deal in, more resistant on
    // off-specialty items. specialtyFactor is roughly [-1, +1].
    if (settings->specialtyHaggling) {
        chance += ctx.specialtyFactor * settings->specialtyWeight;
    }

    // Deal history
    if (ctx.memory) {
        int fairDeals = ctx.memory->GetConsecutiveFairDeals();
        chance += std::min(static_cast<float>(fairDeals) * settings->dealHistoryWeight, 10.0f);

        int recentLowballs = ctx.memory->GetRecentLowballCount(3);
        chance -= std::min(static_cast<float>(recentLowballs) * 3.0f, 15.0f);
    }

    // Greed penalty: the further the offer is in the player's favour, the less
    // likely the merchant accepts. (greed <= 0 already returned 99 above.)
    //
    // Relationship-driven haggling range: a liked merchant tolerates deeper
    // player-favorable offers (the penalty per point of greed shrinks, so a better
    // price is reachable); a disliked one resists sooner. The strength of this is
    // bounded by the merchant's personality (haggleRangeScale), so take-it-or-leave-it
    // traits barely budge while easygoing ones open right up.
    if (greed > 0.0f) {
        float effGreedFactor = settings->greedFactor;
        if (settings->relationshipPricing) {
            float relNorm = std::clamp(static_cast<float>(ctx.relationship) / 100.0f, -1.0f, 1.0f);
            float influence = std::clamp(
                relNorm * settings->relHaggleRangeWeight * ctx.personality.haggleRangeScale,
                -0.6f, 0.8f);
            effGreedFactor *= (1.0f - influence);
        }
        chance -= greed * 100.0f * effGreedFactor;
    }

    // Stolen item penalty
    if (ctx.isStolen) {
        float penalty = settings->stolenItemPenalty;
        if (ctx.perks.hasFence) {
            penalty *= (1.0f - settings->fencePerkReduction / 100.0f);
        }
        chance -= penalty;
    }

    // Session memory: the merchant already refused this price for this item, so
    // re-offering something no better (for the merchant) is far less likely to
    // land. When buying that means offering the same or LESS; when selling it
    // means asking the same or MORE.
    if (ctx.sessionRejectedPrice > 0) {
        bool noBetter = ctx.isBuying ? (ctx.offeredPrice <= ctx.sessionRejectedPrice)
                                     : (ctx.offeredPrice >= ctx.sessionRejectedPrice);
        if (noBetter) {
            chance = chance * 0.2f - 10.0f;
        }
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
