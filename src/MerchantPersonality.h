#pragma once

struct MerchantPersonality {
    enum class Trait { Greedy, Fair, Generous, Sleazy };

    Trait trait = Trait::Fair;
    float acceptanceMod = 0.0f;
    float offensePerInsult = 4.0f;
    float counterChance = 0.4f;
    float counterSplit = 0.5f;
    float priceJackMult = 1.0f;
    int patienceRounds = 3;
    bool enjoysHaggling = false;

    static MerchantPersonality FromTrait(Trait t);
    static MerchantPersonality DetectFromActor(RE::Actor* merchant);
    static const char* TraitToString(Trait t);
    static Trait StringToTrait(const std::string& s);
};
