#pragma once
#include "PriceCalculator.h"
#include "CounterOffer.h"

struct OfferData {
    std::string itemName;
    int basePrice = 0;
    int effectivePrice = 0;
    std::string merchantName;
    std::string personalityName;
    int relationship = 0;
    float speechBonus = 0.0f;
    std::string perkSummary;
    std::string recentDealsJson;
    bool hasIntimidationPerk = false;
    float sliderMin = -0.30f;
    float sliderMax = 0.30f;
    float acceptanceChance = 50.0f;
    float priceJackMult = 1.0f;
};

enum class BarterState {
    Idle,
    ShowingOffer,
    WaitingForPlayer,
    ShowingCounterOffer,
    ShowingResult,
    Intimidating
};

class BarterManager {
public:
    static BarterManager* GetSingleton() {
        static BarterManager instance;
        return &instance;
    }

    void OnBarterMenuCreated(RE::BarterMenu* menu);
    void OnBarterOpen();
    void OnBarterClose();

    void StartOffer(RE::TESBoundObject* item, int baseValue, bool isBuying, bool isStolen);
    void OnPlayerOffer(int offeredPrice);
    void OnCounterResponse(int response);  // 0=accept, 1=re-offer, 2=walk away
    void OnIntimidateAttempt();

    // Called when the player dismisses the result screen
    void OnResultDismissed();

    // Called when the player wants to retry after a rejected offer
    void RetryOffer();

    // Called when the player cancels the barter UI without completing
    void OnCancelled();

    BarterState GetState() const { return state; }
    RE::Actor* GetCurrentMerchant() const { return currentMerchant; }
    bool IsBarterActive() const { return barterActive; }

private:
    BarterManager() = default;

    void ProcessAcceptance(int offeredPrice);
    void ProcessRejection(int offeredPrice);
    void FinalizeDeal(int finalPrice, bool wasCounter);
    void RecordAndClose(int offeredPrice, bool accepted, bool wasCounter, int counterAmt);

    void TransferItemAndGold(int finalPrice);
    static void RefreshBarterMenu(RE::FormID itemID);
    bool ValidateGoldBalance(int amount, bool playerPays) const;
    void ResetDebugForceFlags();

    BarterState state = BarterState::Idle;
    bool barterActive = false;

    RE::Actor* currentMerchant = nullptr;
    RE::TESBoundObject* currentItem = nullptr;
    RE::FormID currentMerchantID = 0;
    RE::FormID currentItemID = 0;
    int currentBasePrice = 0;
    int currentEffectivePrice = 0;
    bool currentIsBuying = false;
    bool currentIsStolen = false;
    int patienceRemaining = 3;
    int currentCounterAmount = 0;

    PerkBonuses cachedPerks;
    MerchantPersonality cachedPersonality;
    float cachedSpeech = 15.0f;
};
