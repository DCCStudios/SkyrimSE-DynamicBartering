#pragma once
#include "PriceCalculator.h"
#include "CounterOffer.h"
#include "MerchantCategory.h"
#include "Hold.h"
#include "Integration/ChimBridge.h"

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
    bool isBuying = true;             // player is buying from merchant (vs selling)
    int sessionRejectedPrice = 0;     // highest price already refused this session for this item (0 = none)
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

    // baseValue is the TOTAL vanilla market price for `amount` units (so the whole
    // negotiation is in total-gold terms). amount is the quantity selected (>=1).
    void StartOffer(RE::TESBoundObject* item, int baseValue, bool isBuying, bool isStolen, int amount = 1);
    void StartCartOffer();  // Cart-wide negotiation over CartManager's net

    // Recompute and re-push the currently-open offer window (prices, slider range,
    // acceptance, relationship meter) from live data. No-op unless an offer window is
    // open and awaiting the player. Used after a relationship change made outside the
    // negotiation (e.g. the SKSE-menu debug slider) so it applies immediately.
    void RefreshActiveOffer();

    // Immediately buy/sell a single item at its market price (no negotiation), by
    // selecting its row and replaying the vanilla ItemSelect. Used by the cart
    // "quick buy/sell" popup when the player activates an item already in the cart.
    void QuickTransferMarket(RE::FormID formID, int count, bool isBuying, int unitPrice);

    // Apply the same chance-based standing change a market-price deal grants in the
    // offer window (genRatio == 1.0), for a one-off quick/insta transaction, and
    // surface the result through the vanilla corner-notification system. Uses the
    // session merchant (set on barter open).
    void ApplyQuickDealRelationship();
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

    // The merchant's detected specialty for the current session (Generalist when no
    // merchant). Used by the cart UI / config menu price-effect readout.
    MerchantCategory GetCurrentCategory() const { return cachedCategory; }

    // Buy/sell base-price multiplier the relationship + personality currently impose
    // on this merchant (1.0 = neutral; <1 buy discount / >1 buy markup). Used by the
    // cart discount text and the vanilla item-card price wrapper.
    float GetCurrentPriceMult(bool buying) const;

    // Authoritative acceptance chance for the current item at a hypothetical price.
    // Used by the UI so the displayed verdict matches the real decision exactly.
    float PreviewAcceptanceChance(int offeredPrice);
    // Threshold at/above which an offer is GUARANTEED to be accepted (no RNG).
    // The UI shows "Merchant will ACCEPT" only at/above this same chance, so a
    // displayed ACCEPT can never be rejected on submit.
    static constexpr float kGuaranteedAcceptThreshold = 85.0f;

private:
    BarterManager() = default;

    // Build the OfferData from current session/cached state and show (or, when
    // refresh=true, live-update) the offer window. Shared by Start*Offer + RefreshActiveOffer.
    void ShowSingleOffer(bool refresh);
    void ShowCartOffer(bool refresh);

    AcceptanceContext BuildAcceptanceContext(int offeredPrice);
    void ProcessAcceptance(int offeredPrice);
    void ProcessRejection(int offeredPrice);
    void FinalizeDeal(int finalPrice, bool wasCounter);
    void RecordAndClose(int offeredPrice, bool accepted, bool wasCounter, int counterAmt);
    void RecordSessionRejection(int offeredPrice);

    // Probabilistic relationship change: applies `delta` with `chancePercent` odds.
    // Returns the delta actually applied (0 if the roll missed).
    int RollRelationshipChange(float chancePercent, int delta, const char* reason);

    // Push a barter outcome to CHIM (no-op unless enabled + reachable).
    void EmitChimEvent(ChimBridge::Action action, int offeredPrice, bool bigMoment, int counterPrice = 0);

    void TransferItemAndGold(int finalPrice);
    void TransferCart(int finalNetPrice);  // Multi-item transfer loop
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
    int currentRawBaseValue = 0;       // raw vanilla market value passed to StartOffer (for live refresh)
    bool currentIsBuying = false;
    bool currentIsStolen = false;
    int currentAmount = 1;            // quantity being transacted (stacks)
    int patienceRemaining = 3;
    int currentCounterAmount = 0;
    bool isCartMode = false;           // true when negotiating a cart-wide offer

    // Per-session memory of refused prices, keyed by item FormID. Cleared when the
    // barter menu opens/closes. If the player re-offers at/under a refused price for
    // the same item, acceptance becomes much less likely.
    std::unordered_map<RE::FormID, int> sessionRejections;

    PerkBonuses cachedPerks;
    MerchantPersonality cachedPersonality;
    float cachedSpeech = 15.0f;
    MerchantCategory cachedCategory = MerchantCategory::Generalist;
    Hold cachedHold = Hold::None;  // merchant's home hold (for Thane / civil-war standing)
    // Specialty match for the current offer (single item or value-weighted cart),
    // computed once per offer and read by BuildAcceptanceContext.
    float cachedSpecialtyFactor = 0.0f;
};
