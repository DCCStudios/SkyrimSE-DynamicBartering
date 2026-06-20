#pragma once

struct PerkBonuses {
    int hagglingRank = 0;
    bool hasAllure = false;
    bool hasPersuasion = false;
    bool hasInvestor = false;
    bool hasIntimidation = false;
    bool hasMerchant = false;
    bool hasFence = false;
    bool hasMasterTrader = false;

    static PerkBonuses Detect(RE::Actor* player);
    float GetAcceptanceBonus(bool oppositeGender) const;
    float GetSliderRangeBonus() const;
};
