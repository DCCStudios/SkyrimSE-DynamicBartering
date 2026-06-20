#include "PCH.h"
#include "CounterOffer.h"
#include "Settings.h"

CounterOfferResult CounterOffer::Calculate(
    int playerOffer,
    int merchantPrice,
    int relationship,
    const MerchantPersonality& personality,
    int currentPatience
) {
    CounterOfferResult result;
    auto* settings = Settings::GetSingleton();

    // If player is offering at or above market price, non-greedy merchants accept gladly
    float offerRatio = (merchantPrice > 0)
        ? static_cast<float>(playerOffer) / static_cast<float>(merchantPrice)
        : 1.0f;

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

        int gap = merchantPrice - playerOffer;
        if (gap <= 0) {
            // Player offered above market — greedy merchant wants even more
            result.counterAmount = playerOffer + static_cast<int>(merchantPrice * 0.1f);
        } else {
            result.counterAmount = playerOffer + static_cast<int>(gap * split);
            result.counterAmount = std::max(result.counterAmount, playerOffer + 1);
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
