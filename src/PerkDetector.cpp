#include "PCH.h"
#include "PerkDetector.h"
#include "Settings.h"

PerkBonuses PerkBonuses::Detect(RE::Actor* player) {
    PerkBonuses b;
    if (!player) return b;

    auto lookupPerk = [](RE::FormID id) -> RE::BGSPerk* {
        return RE::TESForm::LookupByID<RE::BGSPerk>(id);
    };

    if (auto p = lookupPerk(0x000C07D1); p && player->HasPerk(p)) b.hagglingRank = 5;
    else if (auto p2 = lookupPerk(0x000C07D0); p2 && player->HasPerk(p2)) b.hagglingRank = 4;
    else if (auto p3 = lookupPerk(0x000C07CF); p3 && player->HasPerk(p3)) b.hagglingRank = 3;
    else if (auto p4 = lookupPerk(0x000C07CE); p4 && player->HasPerk(p4)) b.hagglingRank = 2;
    else if (auto p5 = lookupPerk(0x000BE128); p5 && player->HasPerk(p5)) b.hagglingRank = 1;

    if (auto p = lookupPerk(0x00058F75); p && player->HasPerk(p)) b.hasAllure = true;
    if (auto p = lookupPerk(0x001090A2); p && player->HasPerk(p)) b.hasPersuasion = true;
    if (auto p = lookupPerk(0x00058F7B); p && player->HasPerk(p)) b.hasInvestor = true;
    if (auto p = lookupPerk(0x00105F29); p && player->HasPerk(p)) b.hasIntimidation = true;
    if (auto p = lookupPerk(0x00058F7A); p && player->HasPerk(p)) b.hasMerchant = true;
    if (auto p = lookupPerk(0x00058F79); p && player->HasPerk(p)) b.hasFence = true;
    if (auto p = lookupPerk(0x001090A5); p && player->HasPerk(p)) b.hasMasterTrader = true;

    return b;
}

float PerkBonuses::GetAcceptanceBonus(bool oppositeGender) const {
    auto* settings = Settings::GetSingleton();
    float bonus = 0.0f;
    bonus += hagglingRank * settings->hagglingPerkBonus;
    if (hasAllure && oppositeGender) bonus += settings->allureBonus;
    if (hasPersuasion) bonus += settings->persuasionPerkBonus;
    return bonus;
}

float PerkBonuses::GetSliderRangeBonus() const {
    return hagglingRank * 0.02f;
}
