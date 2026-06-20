#include "PCH.h"
#include "MerchantPersonality.h"

MerchantPersonality MerchantPersonality::FromTrait(Trait t) {
    MerchantPersonality p;
    p.trait = t;
    switch (t) {
        case Trait::Greedy:
            p.acceptanceMod = -15.0f;
            p.offensePerInsult = 8.0f;
            p.counterChance = 0.20f;
            p.counterSplit = 0.30f;
            p.priceJackMult = 1.5f;
            p.patienceRounds = 2;
            p.enjoysHaggling = false;
            break;
        case Trait::Fair:
            p.acceptanceMod = 0.0f;
            p.offensePerInsult = 4.0f;
            p.counterChance = 0.40f;
            p.counterSplit = 0.50f;
            p.priceJackMult = 1.0f;
            p.patienceRounds = 3;
            p.enjoysHaggling = false;
            break;
        case Trait::Generous:
            p.acceptanceMod = 10.0f;
            p.offensePerInsult = 2.0f;
            p.counterChance = 0.60f;
            p.counterSplit = 0.70f;
            p.priceJackMult = 0.5f;
            p.patienceRounds = 4;
            p.enjoysHaggling = false;
            break;
        case Trait::Sleazy:
            p.acceptanceMod = 5.0f;
            p.offensePerInsult = 0.0f;
            p.counterChance = 0.80f;
            p.counterSplit = 0.50f;
            p.priceJackMult = 0.8f;
            p.patienceRounds = 4;
            p.enjoysHaggling = true;
            break;
    }
    return p;
}

MerchantPersonality MerchantPersonality::DetectFromActor(RE::Actor* merchant) {
    if (!merchant) return FromTrait(Trait::Fair);

    auto* npc = merchant->GetActorBase();
    if (!npc) return FromTrait(Trait::Fair);

    // Check factions to determine personality
    // Thieves Guild fence faction -> Sleazy
    auto tgFence = RE::TESForm::LookupByID<RE::TESFaction>(0x0001F3B5);
    if (tgFence && merchant->IsInFaction(tgFence)) {
        return FromTrait(Trait::Sleazy);
    }

    // Khajiit race -> Generous (they're traveling merchants, used to haggling)
    if (npc->GetRace()) {
        auto raceID = npc->GetRace()->GetFormID();
        if (raceID == 0x00013745 || raceID == 0x00013746) {
            return FromTrait(Trait::Generous);
        }
    }

    // Some specific known merchants
    auto formID = npc->GetFormID();
    switch (formID) {
        case 0x0001A672:  // Belethor
        case 0x00013BAD:  // Maven Black-Briar
            return FromTrait(Trait::Greedy);
        case 0x00013BBE:  // Adrianne Avenicci
        case 0x00013BA9:  // Arcadia
            return FromTrait(Trait::Fair);
    }

    return FromTrait(Trait::Fair);
}

const char* MerchantPersonality::TraitToString(Trait t) {
    switch (t) {
        case Trait::Greedy: return "Greedy";
        case Trait::Fair: return "Fair";
        case Trait::Generous: return "Generous";
        case Trait::Sleazy: return "Sleazy";
    }
    return "Fair";
}

MerchantPersonality::Trait MerchantPersonality::StringToTrait(const std::string& s) {
    if (s == "Greedy") return Trait::Greedy;
    if (s == "Generous") return Trait::Generous;
    if (s == "Sleazy") return Trait::Sleazy;
    return Trait::Fair;
}
