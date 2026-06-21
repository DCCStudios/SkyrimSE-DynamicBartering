#include "PCH.h"
#include "CounterOffer.h"
#include "Settings.h"

CounterOfferResult CounterOffer::Calculate(
    int playerOffer,
    int merchantPrice,
    int relationship,
    const MerchantPersonality& personality,
    int currentPatience,
    bool isBuying
) {
    CounterOfferResult result;
    auto* settings = Settings::GetSingleton();

    // merchantFavor measures how good the offer is *for the merchant* relative to
    // market, so the willCounter logic reads identically in both directions:
    //   buying  -> paying MORE is better for the merchant -> offer / market
    //   selling -> being paid LESS is better for the merchant -> market / offer
    // >= 1.0 means the offer already favours the merchant (they accept gladly);
    // < 1.0 means the player is asking for a deal in their own favour.
    float merchantFavor = 1.0f;
    if (merchantPrice > 0 && playerOffer > 0) {
        merchantFavor = isBuying
            ? static_cast<float>(playerOffer) / static_cast<float>(merchantPrice)
            : static_cast<float>(merchantPrice) / static_cast<float>(playerOffer);
    }
    float offerRatio = merchantFavor;

    if (offerRatio >= 1.0f && personality.trait != MerchantPersonality::Trait::Greedy) {
        result.willCounter = false;
        return result;
    }

    if (settings->forceCounter) {
        result.willCounter = true;
    } else {
        float counterChance = settings->counterOfferBaseChance;
        counterChance += personality.counterChance * 100.0f - 30.0f;
        counterChance += (static_cast<float>(relationship) / 100.0f) * 10.0f;

        // Heavy insult penalty — very low offers make merchants refuse to negotiate
        if (offerRatio < 0.5f) {
            counterChance -= 50.0f;
        }

        // Above market price significantly reduces counter chance (even for greedy)
        if (offerRatio >= 1.0f) {
            counterChance -= 70.0f;
        } else if (offerRatio >= 0.9f) {
            counterChance -= 30.0f;
        }

        counterChance = std::clamp(counterChance, 5.0f, 95.0f);
        result.willCounter = RollCounterChance(counterChance);
    }

    if (result.willCounter) {
        float split = personality.counterSplit;
        // Relationship adjusts split generosity
        split += (static_cast<float>(relationship) / 100.0f) * 0.1f;
        split = std::clamp(split, 0.2f, 0.8f);

        // For Sleazy personality, randomize the split a bit
        if (personality.trait == MerchantPersonality::Trait::Sleazy) {
            static std::mt19937 rng(std::random_device{}());
            std::uniform_real_distribution<float> dist(-0.1f, 0.1f);
            split += dist(rng);
            split = std::clamp(split, 0.3f, 0.7f);
        }

        // The counter always lands between the player's offer and market, with
        // `split` controlling how far toward market. The direction differs:
        //   buying  -> counter is HIGHER than the offer (merchant wants more gold)
        //   selling -> counter is LOWER than the offer (merchant pays you less)
        if (isBuying) {
            int gap = merchantPrice - playerOffer;
            if (gap <= 0) {
                // Player already offered at/above market — greedy merchant wants more.
                result.counterAmount = playerOffer + static_cast<int>(merchantPrice * 0.1f);
            } else {
                result.counterAmount = playerOffer + static_cast<int>(gap * split);
                result.counterAmount = std::max(result.counterAmount, playerOffer + 1);
            }
        } else {
            // Selling: the player asked for `playerOffer`; the merchant counters
            // by offering to pay LESS, moving down toward the market value.
            int gap = playerOffer - merchantPrice;
            if (gap <= 0) {
                // Player already asked at/below market — greedy merchant pays even less.
                result.counterAmount = playerOffer - static_cast<int>(merchantPrice * 0.1f);
            } else {
                result.counterAmount = playerOffer - static_cast<int>(gap * split);
                result.counterAmount = std::min(result.counterAmount, playerOffer - 1);
            }
            // Never counter with a non-positive (or negative) payout.
            result.counterAmount = std::max(result.counterAmount, 1);
        }

        result.patienceRemaining = currentPatience - 1;
    }

    return result;
}

bool CounterOffer::RollCounterChance(float chance) {
    static std::mt19937 rng(std::random_device{}());
    std::uniform_real_distribution<float> dist(0.0f, 100.0f);
    return dist(rng) < chance;
}
