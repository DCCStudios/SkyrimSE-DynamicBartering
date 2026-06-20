#pragma once
#include "MerchantPersonality.h"

struct CounterOfferResult {
    bool willCounter = false;
    int counterAmount = 0;
    int patienceRemaining = 0;
};

class CounterOffer {
public:
    static CounterOfferResult Calculate(
        int playerOffer,
        int merchantPrice,
        int relationship,
        const MerchantPersonality& personality,
        int currentPatience
    );

    static bool RollCounterChance(float chance);
};
