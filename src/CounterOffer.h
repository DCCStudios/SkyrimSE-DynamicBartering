#pragma once
#include "MerchantPersonality.h"

struct CounterOfferResult {
    bool willCounter = false;
    int counterAmount = 0;
    int patienceRemaining = 0;
};

class CounterOffer {
public:
    // isBuying: true when the player is paying the merchant (a counter raises the
    // price toward market); false when selling, i.e. the merchant pays the player
    // (a counter LOWERS what they're willing to pay, toward market). Combined carts
    // pass the net direction here so a net-sell cart counters downward.
    static CounterOfferResult Calculate(
        int playerOffer,
        int merchantPrice,
        int relationship,
        const MerchantPersonality& personality,
        int currentPatience,
        bool isBuying = true
    );

    static bool RollCounterChance(float chance);
};
