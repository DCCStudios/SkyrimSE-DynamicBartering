#pragma once

struct MerchantPersonality {
    enum class Trait { Greedy, Fair, Generous, Sleazy, Stern, Timid };

    Trait trait = Trait::Fair;
    float acceptanceMod = 0.0f;
    float offensePerInsult = 4.0f;
    float counterChance = 0.4f;
    float counterSplit = 0.5f;
    float priceJackMult = 1.0f;
    int patienceRounds = 3;
    bool enjoysHaggling = false;
    // Scales how much the player's relationship can widen (or, when disliked,
    // contract) the achievable haggling range. Tight-fisted personalities (Greedy,
    // Stern) keep the range narrow; generous/easygoing ones (Generous, Sleazy,
    // Timid) let standing open it up much further. Multiplies the relationship-driven
    // range expansion in PriceCalculator::CalculatePrice.
    float haggleRangeScale = 1.0f;

    static MerchantPersonality FromTrait(Trait t);
    static MerchantPersonality DetectFromActor(RE::Actor* merchant);
    static const char* TraitToString(Trait t);
    static Trait StringToTrait(const std::string& s);
};
