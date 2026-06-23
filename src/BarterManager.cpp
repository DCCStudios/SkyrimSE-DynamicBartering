#include "PCH.h"
#include "BarterManager.h"
#include "CartManager.h"
#include "RelationshipManager.h"
#include "PriceJack.h"
#include "MilestoneManager.h"
#include "Tutorial.h"
#include "Settings.h"
#include <thread>
#include "Hooks.h"
#include "UI/UIBridge.h"
#include "BarterSounds.h"
#include "DebugLog.h"

namespace {
    // Value-weighted average specialty match across every item in the cart, so a cart
    // dominated by a merchant's specialty haggles more easily than a mixed bag.
    float ComputeCartSpecialtyFactor(MerchantCategory cat) {
        auto* cart = CartManager::GetSingleton();
        double total = 0.0, acc = 0.0;
        for (const auto& e : cart->GetEntries()) {
            auto* obj = RE::TESForm::LookupByID<RE::TESBoundObject>(e.formID);
            if (!obj) continue;
            double w = static_cast<double>(std::max(1, std::abs(e.count * e.marketUnitPrice)));
            acc += Merchants::SpecialtyFactor(cat, Merchants::CategorizeItem(obj)) * w;
            total += w;
        }
        return total > 0.0 ? static_cast<float>(acc / total) : 0.0f;
    }
}

void BarterManager::OnBarterMenuCreated(RE::BarterMenu* menu) {
    (void)menu;
    DbgLog("BarterMenu PostCreate intercepted");
}

void BarterManager::OnBarterOpen() {
    state = BarterState::Idle;
    barterActive = true;
    isCartMode = false;
    currentMerchant = nullptr;
    currentMerchantID = 0;
    sessionRejections.clear();
    ChimBridge::ResetSession();  // start a fresh buffer for deferred (paused-menu) reactions
    CartManager::GetSingleton()->Clear();

    auto* player = RE::PlayerCharacter::GetSingleton();
    if (!player) return;

    auto menuRef = RE::MenuTopicManager::GetSingleton();
    if (menuRef && menuRef->speaker) {
        auto handle = menuRef->speaker;
        auto speakerPtr = handle.get();
        if (speakerPtr) {
            currentMerchant = speakerPtr->As<RE::Actor>();
        }
    }

    cachedCategory = MerchantCategory::Generalist;
    cachedSpecialtyFactor = 0.0f;

    if (currentMerchant) {
        currentMerchantID = currentMerchant->GetFormID();
        cachedPerks = PerkBonuses::Detect(player);
        cachedPersonality = RelationshipManager::GetSingleton()->GetPersonality(currentMerchant);
        cachedCategory = Merchants::DetectCategory(currentMerchant);
        cachedSpeech = player->AsActorValueOwner()
            ? player->AsActorValueOwner()->GetActorValue(RE::ActorValue::kSpeech)
            : 15.0f;
        patienceRemaining = cachedPersonality.patienceRounds;

        auto merchantName = currentMerchant->GetName();
        auto& memory = RelationshipManager::GetSingleton()->GetOrCreate(
            currentMerchantID, merchantName ? merchantName : "Unknown");

        DbgLog("Barter opened with {} (relationship: {}, personality: {}, specialty: {})",
            merchantName ? merchantName : "Unknown",
            memory.relationship,
            MerchantPersonality::TraitToString(cachedPersonality.trait),
            Merchants::MerchantCategoryToString(cachedCategory));
    }

    // Apply any newly-earned milestone reputation (one-shot, retroactive-safe).
    MilestoneManager::Evaluate();

    // First-run tutorial: explain the cart workflow the first time a barter opens.
    Tutorial::OnBarterOpened();
}

void BarterManager::OnBarterClose() {
    if (state != BarterState::Idle) {
        UIBridge::GetSingleton()->Hide();
    }
    // Hand off the buffered session. Barter is entered from dialogue, and NPC voice stays
    // muted while the dialogue menu is up, so if it's still open we hold the summary until
    // it closes (CHIM voices/remembers the whole visit once the player is back in-world).
    {
        auto* ui = RE::UI::GetSingleton();
        const bool dialogueOpen = ui && ui->IsMenuOpen(RE::DialogueMenu::MENU_NAME);
        ChimBridge::OnBarterClosed(dialogueOpen);
    }
    state = BarterState::Idle;
    barterActive = false;
    isCartMode = false;
    currentMerchant = nullptr;
    currentMerchantID = 0;
    currentItem = nullptr;
    currentItemID = 0;
    sessionRejections.clear();
    CartManager::GetSingleton()->Clear();
    RelationshipManager::GetSingleton()->SaveData();
}

void BarterManager::StartOffer(RE::TESBoundObject* item, int baseValue, bool isBuying, bool isStolen, int amount) {
    if (!currentMerchant || !item) return;
    auto* settings = Settings::GetSingleton();
    if (!settings->modEnabled) return;
    if (settings->skipBelowThreshold && baseValue < settings->valueThreshold) return;

    isCartMode = false;
    currentItem = item;
    currentItemID = item->GetFormID();
    currentIsBuying = isBuying;
    currentIsStolen = isStolen;
    currentAmount = (amount >= 1) ? amount : 1;
    currentRawBaseValue = baseValue;

    ShowSingleOffer(false);
}

void BarterManager::ShowSingleOffer(bool refresh) {
    if (!currentMerchant || !currentItem) return;
    auto* settings = Settings::GetSingleton();

    auto& memory = RelationshipManager::GetSingleton()->GetOrCreate(
        currentMerchantID, currentMerchant->GetName() ? currentMerchant->GetName() : "Unknown");

    // Effective relationship folds in any category-wide milestone bonus.
    int effRel = RelationshipManager::GetSingleton()->GetEffectiveRelationship(
        currentMerchantID, cachedCategory);

    // Real relationship/personality base-price effect (bidirectional). The mod always
    // applies this to its own negotiation (the price we read off the list row is the
    // raw vanilla value); the optional vanilla item-card wrapper independently rewrites
    // the *display* number to match, so there's no double-apply.
    float realMult = PriceJack::GetBuySellMultiplier(
        effRel, cachedPersonality, currentIsBuying, cachedPerks.hasInvestor);

    // Per-item specialty match (cached for the acceptance context).
    cachedSpecialtyFactor = settings->specialtyHaggling
        ? Merchants::SpecialtyFactor(cachedCategory, Merchants::CategorizeItem(currentItem))
        : 0.0f;

    PriceContext pCtx;
    pCtx.player = RE::PlayerCharacter::GetSingleton();
    pCtx.merchant = currentMerchant;
    pCtx.item = currentItem;
    pCtx.itemBaseValue = currentRawBaseValue;
    pCtx.isBuying = currentIsBuying;
    pCtx.isStolen = currentIsStolen;
    pCtx.personality = cachedPersonality;

    auto priceResult = PriceCalculator::CalculatePrice(pCtx, effRel, realMult);
    currentBasePrice = priceResult.basePrice;
    currentEffectivePrice = priceResult.effectivePrice;

    OfferData data;
    data.itemName = currentItem->GetName() ? currentItem->GetName() : "Unknown Item";
    data.basePrice = priceResult.basePrice;
    data.effectivePrice = priceResult.effectivePrice;
    data.isBuying = currentIsBuying;
    {
        auto it = sessionRejections.find(currentItemID);
        data.sessionRejectedPrice = (it != sessionRejections.end()) ? it->second : 0;
    }
    data.merchantName = currentMerchant->GetName() ? currentMerchant->GetName() : "Unknown";
    data.personalityName = MerchantPersonality::TraitToString(cachedPersonality.trait);
    data.relationship = effRel;
    data.speechBonus = (cachedSpeech / 100.0f) * settings->speechWeight;
    data.hasIntimidationPerk = cachedPerks.hasIntimidation;
    data.sliderMin = priceResult.sliderMin;
    data.sliderMax = priceResult.sliderMax;
    data.priceJackMult = realMult;

    std::string perks;
    if (cachedPerks.hagglingRank > 0) perks += "Haggling " + std::to_string(cachedPerks.hagglingRank) + " ";
    if (cachedPerks.hasAllure) perks += "Allure ";
    if (cachedPerks.hasPersuasion) perks += "Persuasion ";
    if (cachedPerks.hasIntimidation) perks += "Intimidation ";
    if (cachedPerks.hasInvestor) perks += "Investor ";
    if (cachedPerks.hasFence) perks += "Fence ";
    if (cachedPerks.hasMasterTrader) perks += "Master Trader ";
    data.perkSummary = perks;

    nlohmann::json dealsJson = nlohmann::json::array();
    for (const auto& d : memory.recentDeals) {
        dealsJson.push_back(d.ToJson());
    }
    data.recentDealsJson = dealsJson.dump();

    // Initial displayed chance for the starting offer (= market price), using the
    // exact same context the real decision will use, so the verdict is consistent.
    data.acceptanceChance = PriceCalculator::CalculateAcceptanceChance(
        BuildAcceptanceContext(priceResult.effectivePrice));

    if (refresh) {
        // Live refresh of an already-open window: just re-push fresh numbers + meter
        // (no tutorial, no open sound, no state change).
        UIBridge::GetSingleton()->ShowOffer(data);
        return;
    }

    // Claim the session up front so cart input can't re-trigger while the (deferred)
    // tutorial popup is up; the window itself opens once the popup is dismissed.
    state = BarterState::ShowingOffer;
    OfferData shown = data;
    Tutorial::OnOfferWindowOpened([this, shown]() {
        UIBridge::GetSingleton()->ShowOffer(shown);
        BarterSounds::Play(BarterSounds::Event::OpenOffer);
        state = BarterState::WaitingForPlayer;
    });
}

void BarterManager::StartCartOffer() {
    if (!currentMerchant) return;
    auto* settings = Settings::GetSingleton();
    if (!settings->modEnabled) return;

    auto* cart = CartManager::GetSingleton();
    if (cart->IsEmpty()) return;

    isCartMode = true;
    ShowCartOffer(false);
}

void BarterManager::ShowCartOffer(bool refresh) {
    auto* settings = Settings::GetSingleton();
    auto* cart = CartManager::GetSingleton();
    if (!currentMerchant || cart->IsEmpty()) return;

    currentItem = nullptr;
    currentItemID = 0;
    currentAmount = 1;

    int buySubtotal = cart->GetBuySubtotal();
    int sellSubtotal = cart->GetSellSubtotal();

    auto& memory = RelationshipManager::GetSingleton()->GetOrCreate(
        currentMerchantID, currentMerchant->GetName() ? currentMerchant->GetName() : "Unknown");

    int effRel = RelationshipManager::GetSingleton()->GetEffectiveRelationship(
        currentMerchantID, cachedCategory);

    // Stored cart prices are the raw vanilla values, so the relationship base-price
    // effect is applied here (buys discounted / sells boosted by standing). The vanilla
    // item-card wrapper only rewrites the display, so this is the single application.
    float buyMult = PriceJack::GetBuySellMultiplier(effRel, cachedPersonality, true, cachedPerks.hasInvestor);
    float sellMult = PriceJack::GetBuySellMultiplier(effRel, cachedPersonality, false, cachedPerks.hasInvestor);
    int adjBuy = static_cast<int>(std::lround(buySubtotal * buyMult));
    int adjSell = static_cast<int>(std::lround(sellSubtotal * sellMult));
    int netAmount = adjBuy - adjSell;
    bool netIsBuying = (netAmount >= 0);

    currentIsBuying = netIsBuying;
    currentIsStolen = false;
    currentBasePrice = std::abs(netAmount);
    currentEffectivePrice = std::abs(netAmount);

    // Cart-wide specialty match (value-weighted across the items).
    cachedSpecialtyFactor = settings->specialtyHaggling
        ? ComputeCartSpecialtyFactor(cachedCategory) : 0.0f;

    OfferData data;
    data.itemName = "Cart (" + std::to_string(cart->Count()) + " items)";
    data.basePrice = currentBasePrice;
    data.effectivePrice = currentEffectivePrice;
    data.isBuying = netIsBuying;
    data.sessionRejectedPrice = 0;
    data.merchantName = currentMerchant->GetName() ? currentMerchant->GetName() : "Unknown";
    data.personalityName = MerchantPersonality::TraitToString(cachedPersonality.trait);
    data.relationship = effRel;
    data.speechBonus = (cachedSpeech / 100.0f) * settings->speechWeight;
    data.hasIntimidationPerk = cachedPerks.hasIntimidation;
    PriceCalculator::ComputeHaggleRange(netIsBuying, effRel, cachedPersonality,
        cachedPerks.GetSliderRangeBonus(), data.sliderMin, data.sliderMax);
    data.priceJackMult = PriceJack::GetBuySellMultiplier(
        effRel, cachedPersonality, netIsBuying, cachedPerks.hasInvestor);

    std::string perks;
    if (cachedPerks.hagglingRank > 0) perks += "Haggling " + std::to_string(cachedPerks.hagglingRank) + " ";
    if (cachedPerks.hasAllure) perks += "Allure ";
    if (cachedPerks.hasPersuasion) perks += "Persuasion ";
    if (cachedPerks.hasIntimidation) perks += "Intimidation ";
    if (cachedPerks.hasInvestor) perks += "Investor ";
    if (cachedPerks.hasFence) perks += "Fence ";
    if (cachedPerks.hasMasterTrader) perks += "Master Trader ";
    data.perkSummary = perks;

    nlohmann::json dealsJson = nlohmann::json::array();
    for (const auto& d : memory.recentDeals) {
        dealsJson.push_back(d.ToJson());
    }
    data.recentDealsJson = dealsJson.dump();

    data.acceptanceChance = PriceCalculator::CalculateAcceptanceChance(
        BuildAcceptanceContext(currentEffectivePrice));

    DbgLog("StartCartOffer: {} items, buy={}, sell={}, net={} ({})",
        cart->Count(), buySubtotal, sellSubtotal, netAmount,
        netIsBuying ? "player pays" : "player receives");

    if (refresh) {
        // Live refresh of an already-open cart window: re-push fresh numbers + meter.
        UIBridge::GetSingleton()->ShowOffer(data);
        return;
    }

    // Claim the session up front so cart input can't re-trigger while the (deferred)
    // tutorial popup is up; the window itself opens once the popup is dismissed.
    state = BarterState::ShowingOffer;
    OfferData shown = data;
    Tutorial::OnOfferWindowOpened([this, shown]() {
        UIBridge::GetSingleton()->ShowOffer(shown);
        BarterSounds::Play(BarterSounds::Event::OpenOffer);
        state = BarterState::WaitingForPlayer;
    });
}

void BarterManager::RefreshActiveOffer() {
    // Only meaningful while an offer window is open and awaiting the player.
    if (state != BarterState::WaitingForPlayer || !currentMerchant) return;
    if (isCartMode) {
        ShowCartOffer(true);
    } else if (currentItem) {
        ShowSingleOffer(true);
    }
    // Nudge the vanilla item cards to re-render so their displayed prices pick up the
    // new standing immediately (the card wrapper reads the live multiplier).
    RefreshBarterMenu(currentItemID);
}

AcceptanceContext BarterManager::BuildAcceptanceContext(int offeredPrice) {
    AcceptanceContext ctx;
    auto& memory = RelationshipManager::GetSingleton()->GetOrCreate(
        currentMerchantID, currentMerchant ? (currentMerchant->GetName() ? currentMerchant->GetName() : "Unknown") : "Unknown");

    bool oppositeGender = false;
    if (auto* player = RE::PlayerCharacter::GetSingleton()) {
        if (currentMerchant) {
            auto playerBase = player->GetActorBase();
            auto merchantBase = currentMerchant->GetActorBase();
            if (playerBase && merchantBase) {
                oppositeGender = (playerBase->GetSex() != merchantBase->GetSex());
            }
        }
    }

    ctx.speechSkill = cachedSpeech;
    ctx.perks = cachedPerks;
    ctx.relationship = RelationshipManager::GetSingleton()->GetEffectiveRelationship(
        currentMerchantID, cachedCategory);
    ctx.personality = cachedPersonality;
    ctx.memory = &memory;
    ctx.offeredPrice = offeredPrice;
    ctx.basePrice = currentEffectivePrice;
    ctx.oppositeGender = oppositeGender;
    ctx.isStolen = currentIsStolen;
    ctx.isBuying = currentIsBuying;
    ctx.specialtyFactor = cachedSpecialtyFactor;
    auto it = sessionRejections.find(currentItemID);
    ctx.sessionRejectedPrice = (it != sessionRejections.end()) ? it->second : 0;
    return ctx;
}

float BarterManager::GetCurrentPriceMult(bool buying) const {
    if (currentMerchantID == 0) return 1.0f;
    int effRel = RelationshipManager::GetSingleton()->GetEffectiveRelationship(
        currentMerchantID, cachedCategory);
    return PriceJack::GetBuySellMultiplier(effRel, cachedPersonality, buying, cachedPerks.hasInvestor);
}

float BarterManager::PreviewAcceptanceChance(int offeredPrice) {
    if (!currentMerchant) return 0.0f;
    return PriceCalculator::CalculateAcceptanceChance(BuildAcceptanceContext(offeredPrice));
}

void BarterManager::RecordSessionRejection(int offeredPrice) {
    if (currentItemID == 0 || offeredPrice <= 0) return;
    auto& slot = sessionRejections[currentItemID];
    if (offeredPrice > slot) slot = offeredPrice;
}

void BarterManager::OnPlayerOffer(int offeredPrice) {
    if (state != BarterState::WaitingForPlayer) return;

    if (offeredPrice < 0) {
        OnCancelled();
        return;
    }

    BarterSounds::Play(BarterSounds::Event::AcceptOffer);

    float chance = PreviewAcceptanceChance(offeredPrice);

    auto* settings = Settings::GetSingleton();
    if (settings->debugLogging) {
        DbgLog("Offer: {} / Base: {} | Chance: {:.1f}%", offeredPrice, currentEffectivePrice, chance);
    }

    ResetDebugForceFlags();

    // A displayed "Merchant will ACCEPT" (chance >= threshold) must never be
    // rejected. At/above the guaranteed band we accept deterministically; below
    // it we roll. Offering at/above market price returns 99 from the calculator,
    // which is above the threshold, so above-market offers always go through.
    bool accept = (chance >= kGuaranteedAcceptThreshold) || PriceCalculator::RollAcceptance(chance);

    if (accept) {
        ProcessAcceptance(offeredPrice);
    } else {
        RecordSessionRejection(offeredPrice);
        ProcessRejection(offeredPrice);
    }
}

int BarterManager::RollRelationshipChange(float chancePercent, int delta, const char* reason) {
    if (delta == 0 || chancePercent <= 0.0f) return 0;
    static std::mt19937 rng(std::random_device{}());
    std::uniform_real_distribution<float> dist(0.0f, 100.0f);
    if (dist(rng) < chancePercent) {
        RelationshipManager::GetSingleton()->ModifyRelationship(currentMerchantID, delta);
        if (Settings::GetSingleton()->debugLogging) {
            DbgLog("Relationship {}{} ({}) [{:.0f}% chance hit]",
                delta > 0 ? "+" : "", delta, reason, chancePercent);
        }
        // Push the new standing to any open offer window so its meter updates live
        // (e.g. after a failed intimidation, the marker slides to the worse position).
        int newEffRel = RelationshipManager::GetSingleton()->GetEffectiveRelationship(
            currentMerchantID, cachedCategory);
        UIBridge::GetSingleton()->UpdateRelationship(newEffRel);
        return delta;
    }
    if (Settings::GetSingleton()->debugLogging) {
        DbgLog("Relationship unchanged ({}) [{:.0f}% chance missed]", reason, chancePercent);
    }
    return 0;
}

void BarterManager::EmitChimEvent(ChimBridge::Action action, int offeredPrice, bool bigMoment, int counterPrice) {
    if (!Settings::GetSingleton()->enableChim) return;
    if (!currentMerchant) return;

    ChimBridge::BarterEvent e;
    const char* mname = currentMerchant->GetName();
    e.merchantName = (mname && *mname) ? mname : "the merchant";
    e.merchantFormID = currentMerchantID;
    e.personality = MerchantPersonality::TraitToString(cachedPersonality.trait);
    e.relationship = RelationshipManager::GetSingleton()->GetRelationship(currentMerchantID);
    e.action = action;
    if (isCartMode) {
        e.itemName = "the goods";
    } else {
        const char* iname = currentItem ? currentItem->GetName() : nullptr;
        e.itemName = (iname && *iname) ? iname : "the goods";
    }
    e.marketPrice = currentEffectivePrice;
    e.offeredPrice = offeredPrice;
    e.counterPrice = counterPrice;
    e.goldDelta = currentIsBuying ? offeredPrice : -offeredPrice;  // +player pays / -player receives
    e.isBuying = currentIsBuying;
    e.isStolen = currentIsStolen;
    e.isBigMoment = bigMoment;
    // Submit() routes this live (SkyrimSouls present) or buffers it for a session
    // summary on barter close (SkyrimSouls absent, menu pauses the game).
    ChimBridge::Submit(e);
}

void BarterManager::ApplyQuickDealRelationship() {
    if (!Settings::GetSingleton()->modEnabled) return;
    if (currentMerchantID == 0) return;

    // Mirror the offer window's market-price outcome (genRatio == 1.0 ->
    // "deal at/above market"): 65% chance of +2 standing. RollRelationshipChange
    // keys off currentMerchantID, which is valid for the whole barter session.
    const int delta = RollRelationshipChange(65.0f, 2, "quick deal at market");
    if (delta == 0) return;  // roll missed -> no change, so no notification

    const char* mname = currentMerchant ? currentMerchant->GetName() : nullptr;
    std::string merchant = (mname && *mname) ? mname : "The merchant";
    std::string msg = (delta > 0)
        ? merchant + " appreciates your business. (Standing +" + std::to_string(delta) + ")"
        : merchant + " resents the deal. (Standing " + std::to_string(delta) + ")";
    RE::DebugNotification(msg.c_str());
}

void BarterManager::ProcessAcceptance(int offeredPrice) {
    // Generosity ratio toward the merchant (>1 = the player gave the merchant a
    // better-than-market deal). Direction-aware: buying generously = paying more;
    // selling generously = asking less.
    float genRatio = 1.0f;
    if (currentEffectivePrice > 0) {
        genRatio = currentIsBuying
            ? static_cast<float>(offeredPrice) / static_cast<float>(currentEffectivePrice)
            : (offeredPrice > 0 ? static_cast<float>(currentEffectivePrice) / static_cast<float>(offeredPrice) : 2.0f);
    }

    // Chance-based standing gain. Generosity is the most reliably appreciated;
    // fair deals less so; deals struck in the player's favour only occasionally
    // warm a merchant.
    int relGain = 0;
    ChimBridge::Action chimAction;
    bool chimBig = false;
    if (genRatio >= 1.1f)       { relGain = RollRelationshipChange(80.0f, 3, "generous deal"); chimAction = ChimBridge::Action::Generous; chimBig = true; }
    else if (genRatio >= 1.0f)  { relGain = RollRelationshipChange(65.0f, 2, "deal at/above market"); chimAction = ChimBridge::Action::Fair; }
    else if (genRatio >= 0.9f)  { relGain = RollRelationshipChange(45.0f, 1, "fair deal"); chimAction = ChimBridge::Action::Fair; }
    else                        { relGain = RollRelationshipChange(25.0f, 1, "deal in player's favour"); chimAction = ChimBridge::Action::Lowball; }

    EmitChimEvent(chimAction, offeredPrice, chimBig);
    RecordAndClose(offeredPrice, true, false, 0);
    if (isCartMode) {
        TransferCart(offeredPrice);
    } else {
        TransferItemAndGold(offeredPrice);
    }

    // Show brief acceptance confirmation with amount, then auto-close
    UIBridge::GetSingleton()->ShowResult(true, offeredPrice, relGain);
    BarterSounds::PlayDelayed(BarterSounds::Event::OfferAccepted, 320);
    state = BarterState::ShowingResult;
    DbgLog("BarterManager: Offer accepted for {} gold - showing confirmation", offeredPrice);

    // Auto-close after a brief delay (1 second)
    SKSE::GetTaskInterface()->AddTask([this]() {
        std::thread([this]() {
            std::this_thread::sleep_for(std::chrono::milliseconds(1200));
            SKSE::GetTaskInterface()->AddTask([this]() {
                if (state == BarterState::ShowingResult) {
                    auto itemID = currentItemID;
                    UIBridge::GetSingleton()->Hide();
                    state = BarterState::Idle;
                    Hooks::lastNegotiationEnd = std::chrono::steady_clock::now();
                    currentItem = nullptr;
                    currentItemID = 0;
                    Hooks::lastNegotiationEnd = std::chrono::steady_clock::now();
                    RefreshBarterMenu(itemID);
                }
            });
        }).detach();
    });
}

void BarterManager::ProcessRejection(int offeredPrice) {
    auto counterResult = CounterOffer::Calculate(
        offeredPrice,
        currentEffectivePrice,
        RelationshipManager::GetSingleton()->GetRelationship(currentMerchantID),
        cachedPersonality,
        patienceRemaining,
        currentIsBuying  // net direction for carts: net-sell counters downward
    );

    // Greed = how far the offer is in the player's favour vs market (direction-aware):
    // buying -> wanting to pay much less; selling -> demanding much more.
    float greed = 0.0f;
    if (currentEffectivePrice > 0) {
        greed = currentIsBuying
            ? 1.0f - static_cast<float>(offeredPrice) / static_cast<float>(currentEffectivePrice)
            : static_cast<float>(offeredPrice) / static_cast<float>(currentEffectivePrice) - 1.0f;
    }

    // Chance-based standing loss. A greedy rejected offer is the most likely to
    // offend; merchants who enjoy haggling shrug it off entirely.
    int appliedLoss = 0;
    if (!cachedPersonality.enjoysHaggling) {
        int insult = static_cast<int>(cachedPersonality.offensePerInsult);
        if (insult < 2) insult = 2;
        if (greed > 0.5f)       appliedLoss = RollRelationshipChange(60.0f, -insult, "rejected greedy offer");
        else if (greed > 0.3f)  appliedLoss = RollRelationshipChange(40.0f, -std::max(1, insult / 2), "rejected steep offer");
        else                    appliedLoss = RollRelationshipChange(20.0f, -2, "rejected offer");
    }

    if (counterResult.willCounter && patienceRemaining > 0) {
        currentCounterAmount = counterResult.counterAmount;
        patienceRemaining = counterResult.patienceRemaining;
        state = BarterState::ShowingCounterOffer;
        UIBridge::GetSingleton()->ShowCounterOffer(counterResult.counterAmount, patienceRemaining);
        BarterSounds::PlayDelayed(BarterSounds::Event::CounterOffer, 320);
        // Merchant proposes a counter - a prime moment for them to justify their price.
        EmitChimEvent(ChimBridge::Action::Counter, offeredPrice, true, counterResult.counterAmount);
    } else {
        UIBridge::GetSingleton()->ShowResult(false, 0, appliedLoss);
        BarterSounds::PlayDelayed(BarterSounds::Event::OfferRejected, 320);
        EmitChimEvent(ChimBridge::Action::CounterReject, offeredPrice, greed > 0.5f);
        RecordAndClose(offeredPrice, false, false, 0);
        state = BarterState::ShowingResult;
    }
}

void BarterManager::OnCounterResponse(int response) {
    switch (response) {
        case 0:  // Accept counter
            BarterSounds::Play(BarterSounds::Event::OfferAccepted);
            FinalizeDeal(currentCounterAmount, true);
            break;
        case 1:  // Re-offer
            BarterSounds::Play(BarterSounds::Event::ReOffer);
            state = BarterState::WaitingForPlayer;
            break;
        case 2:  // Walk away
            BarterSounds::Play(BarterSounds::Event::CancelOffer);
            RelationshipManager::GetSingleton()->ModifyRelationship(currentMerchantID, -2);
            EmitChimEvent(ChimBridge::Action::WalkAway, 0, false);
            RecordAndClose(0, false, false, currentCounterAmount);
            UIBridge::GetSingleton()->Hide();
            state = BarterState::Idle;
            break;
    }
}

void BarterManager::OnIntimidateAttempt() {
    BarterSounds::Play(BarterSounds::Event::Intimidate);

    // Base success chance from Speech skill (0-100 → 0.0-0.5 base)
    float baseChance = cachedSpeech / 200.0f;

    // Perk bonus: Intimidation perk doubles the base and raises cap
    if (cachedPerks.hasIntimidation) {
        baseChance *= 2.0f;
    }

    // Player level bonus (higher level = more intimidating, +1% per 5 levels)
    auto* player = RE::PlayerCharacter::GetSingleton();
    int playerLevel = player ? player->GetLevel() : 1;
    baseChance += static_cast<float>(playerLevel) * 0.002f;

    // Merchant personality modifier
    switch (cachedPersonality.trait) {
        case MerchantPersonality::Trait::Timid:
            baseChance += 0.25f;  // timid merchants are easily intimidated
            break;
        case MerchantPersonality::Trait::Stern:
            baseChance -= 0.20f;  // stern merchants resist intimidation
            break;
        case MerchantPersonality::Trait::Greedy:
            baseChance -= 0.10f;  // greedy merchants are stubborn
            break;
        case MerchantPersonality::Trait::Sleazy:
            baseChance += 0.05f;  // sleazy merchants fold under real pressure
            break;
        case MerchantPersonality::Trait::Generous:
            baseChance += 0.10f;  // generous merchants don't want confrontation
            break;
        default:
            break;
    }

    // Clamp to [5%, 95%]
    float roll = std::clamp(baseChance, 0.05f, 0.95f);

    // Relationship penalty on failure scales with merchant personality
    int failPenalty;
    if (cachedPerks.hasIntimidation) {
        failPenalty = -10;  // the perk helps soften the blow
    } else {
        failPenalty = -20;
    }
    // Stern merchants are extra offended by intimidation attempts
    if (cachedPersonality.trait == MerchantPersonality::Trait::Stern) {
        failPenalty -= 10;
    }

    static std::mt19937 rng(std::random_device{}());
    std::uniform_real_distribution<float> dist(0.0f, 1.0f);

    DbgLog("BarterManager: Intimidate attempt - chance={:.0f}%, speech={}, level={}, perk={}, personality={}",
        roll * 100.0f, cachedSpeech, playerLevel, cachedPerks.hasIntimidation,
        MerchantPersonality::TraitToString(cachedPersonality.trait));

    if (dist(rng) < roll) {
        int intimidatedPrice = currentIsBuying
            ? static_cast<int>(currentEffectivePrice * 0.5f)
            : static_cast<int>(currentEffectivePrice * 1.5f);
        RollRelationshipChange(85.0f, -15, "intimidation succeeded");
        EmitChimEvent(ChimBridge::Action::IntimidateSuccess, intimidatedPrice, true);
        RecordAndClose(intimidatedPrice, true, false, 0);
        if (isCartMode) {
            TransferCart(intimidatedPrice);
        } else {
            TransferItemAndGold(intimidatedPrice);
        }
        auto itemID = currentItemID;
        UIBridge::GetSingleton()->Hide();
        state = BarterState::Idle;
        Hooks::lastNegotiationEnd = std::chrono::steady_clock::now();
        currentItem = nullptr;
        currentItemID = 0;
        RefreshBarterMenu(itemID);
        BarterSounds::PlayDelayed(BarterSounds::Event::OfferAccepted, 320);
        DbgLog("BarterManager: Intimidation succeeded - deal done, window closed");
    } else {
        int loss = RollRelationshipChange(95.0f, failPenalty, "intimidation failed");
        UIBridge::GetSingleton()->ShowResult(false, 0, loss);
        BarterSounds::PlayDelayed(BarterSounds::Event::OfferRejected, 320);
        EmitChimEvent(ChimBridge::Action::IntimidateFail, 0, true);
        state = BarterState::ShowingResult;
    }
}

void BarterManager::OnResultDismissed() {
    UIBridge::GetSingleton()->Hide();
    state = BarterState::Idle;
    Hooks::lastNegotiationEnd = std::chrono::steady_clock::now();
    currentItem = nullptr;
    currentItemID = 0;
}

void BarterManager::RetryOffer() {
    DbgLog("BarterManager: Player retrying offer after rejection");
    state = BarterState::WaitingForPlayer;
}

void BarterManager::OnCancelled() {
    DbgLog("BarterManager: Offer cancelled by player");
    BarterSounds::Play(BarterSounds::Event::CancelOffer);
    UIBridge::GetSingleton()->Hide();
    state = BarterState::Idle;
    Hooks::lastNegotiationEnd = std::chrono::steady_clock::now();
    currentItem = nullptr;
    currentItemID = 0;
}

void BarterManager::FinalizeDeal(int finalPrice, bool wasCounter) {
    int relGain = RollRelationshipChange(50.0f, 1, "counter-offer accepted");
    EmitChimEvent(ChimBridge::Action::CounterAccept, finalPrice, false);
    RecordAndClose(finalPrice, true, wasCounter, wasCounter ? currentCounterAmount : 0);
    if (isCartMode) {
        TransferCart(finalPrice);
    } else {
        TransferItemAndGold(finalPrice);
    }

    // Show acceptance result (hides the window), then auto-close
    UIBridge::GetSingleton()->ShowResult(true, finalPrice, relGain);
    state = BarterState::ShowingResult;
    DbgLog("BarterManager: Counter-offer accepted for {} gold - showing confirmation", finalPrice);

    // Auto-close after brief delay
    SKSE::GetTaskInterface()->AddTask([this]() {
        std::thread([this]() {
            std::this_thread::sleep_for(std::chrono::milliseconds(1200));
            SKSE::GetTaskInterface()->AddTask([this]() {
                if (state == BarterState::ShowingResult) {
                    auto itemID = currentItemID;
                    UIBridge::GetSingleton()->Hide();
                    state = BarterState::Idle;
                    Hooks::lastNegotiationEnd = std::chrono::steady_clock::now();
                    currentItem = nullptr;
                    currentItemID = 0;
                    Hooks::lastNegotiationEnd = std::chrono::steady_clock::now();
                    RefreshBarterMenu(itemID);
                }
            });
        }).detach();
    });
}

bool BarterManager::ValidateGoldBalance(int amount, bool playerPays) const {
    if (playerPays) {
        auto* player = RE::PlayerCharacter::GetSingleton();
        if (!player) return false;
        auto goldObj = RE::TESForm::LookupByID<RE::TESBoundObject>(0x0000000F);
        if (!goldObj) return false;
        return player->GetItemCount(goldObj) >= amount;
    } else {
        if (!currentMerchant) return false;
        return currentMerchant->GetGoldAmount() >= amount;
    }
}

namespace { void ReconcileGold(int delta); }  // defined in the anon namespace below

void BarterManager::TransferItemAndGold(int finalPrice) {
    bool isBuying = currentIsBuying;
    int amount = (currentAmount >= 1) ? currentAmount : 1;

    SKSE::GetTaskInterface()->AddUITask([finalPrice, isBuying, amount]() {
        auto* ui = RE::UI::GetSingleton();
        if (!ui) {
            logger::error("TransferItemAndGold: UI singleton null");
            return;
        }

        auto barterMenu = ui->GetMenu(RE::BarterMenu::MENU_NAME);
        if (!barterMenu) {
            logger::error("TransferItemAndGold: BarterMenu not open");
            return;
        }

        auto* movie = barterMenu->uiMovie.get();
        if (!movie) {
            logger::error("TransferItemAndGold: uiMovie null");
            return;
        }

        auto* delegate = barterMenu->fxDelegate.get();
        if (!delegate) {
            logger::error("TransferItemAndGold: fxDelegate null");
            return;
        }

        // The native ItemSelect handler reads the per-unit price from
        // ItemCard_mc.itemInfo.value (NOT from our callback argument). We must
        // patch the ItemCard's value to our negotiated per-unit price before
        // invoking, or the engine uses the original market price for the gold
        // exchange (explains why underpaying works — same direction as vanilla's
        // rounding — but overpaying doesn't take the extra gold).
        // Vanilla floors the per-unit price before multiplying by count, so a fractional
        // unit (finalPrice / amount) drops gold on stacks. Use a whole-gold unit and
        // reconcile the sub-unit remainder directly so the total is exact.
        const long base = static_cast<long>(finalPrice) / amount;
        const int  remGold = finalPrice - static_cast<int>(base) * amount;  // 0..amount-1
        double unitPrice = static_cast<double>(base);

        RE::GFxValue itemCardInfo;
        if (movie->GetVariable(&itemCardInfo, "_root.Menu_mc.ItemCard_mc.itemInfo")) {
            if (itemCardInfo.IsObject()) {
                RE::GFxValue pv;
                pv.SetNumber(unitPrice);
                itemCardInfo.SetMember("value", pv);
                DbgLog("TransferItemAndGold: Set ItemCard_mc.itemInfo.value = {:.2f}", unitPrice);
            }
        }
        // Also try the flat path some SWF layouts use:
        RE::GFxValue itemCardInfo2;
        if (movie->GetVariable(&itemCardInfo2, "_root.ItemCard_mc.itemInfo")) {
            if (itemCardInfo2.IsObject()) {
                RE::GFxValue pv;
                pv.SetNumber(unitPrice);
                itemCardInfo2.SetMember("value", pv);
            }
        }

        RE::GFxValue args[4];
        args[0].SetNumber(0.0);                                   // responseID (no response callback registered)
        args[1].SetNumber(static_cast<double>(amount));           // count (stack quantity)
        args[2].SetNumber(unitPrice);                             // per-unit negotiated price
        args[3].SetBoolean(isBuying);                             // IsViewingVendorItems

        // Our interceptor now owns "ItemSelect"; flag the replay so it forwards to the
        // captured vanilla handler instead of re-opening the negotiation window.
        Hooks::replayingItemSelect = true;
        delegate->Callback(movie, "ItemSelect", args, 4);
        Hooks::replayingItemSelect = false;

        // Reconcile the sub-unit remainder (player owes it when buying, is owed when selling).
        if (remGold != 0) ReconcileGold(isBuying ? remGold : -remGold);

        DbgLog("TransferItemAndGold: Invoked ItemSelect callback (total={}, x{}, unit={}, rem={}, {})",
            finalPrice, amount, base, remGold, isBuying ? "buying" : "selling");
    });
}

void BarterManager::QuickTransferMarket(RE::FormID formID, int count, bool isBuying, int unitPrice) {
    const int amount = (count >= 1) ? count : 1;
    SKSE::GetTaskInterface()->AddUITask([formID, amount, isBuying, unitPrice]() {
        auto* ui = RE::UI::GetSingleton();
        auto barterMenu = ui ? ui->GetMenu(RE::BarterMenu::MENU_NAME) : nullptr;
        if (!barterMenu) { logger::error("QuickTransferMarket: BarterMenu not open"); return; }
        auto* menu = static_cast<RE::BarterMenu*>(barterMenu.get());
        auto* movie = barterMenu->uiMovie.get();
        auto* delegate = barterMenu->fxDelegate.get();
        if (!movie || !delegate) { logger::error("QuickTransferMarket: movie/delegate null"); return; }

        // Drive the engine's selection to this item so the ItemCard reflects it.
        Hooks::SelectCartItem(menu, formID, isBuying);

        const double unit = static_cast<double>(unitPrice);
        for (const char* path : { "_root.Menu_mc.ItemCard_mc.itemInfo", "_root.ItemCard_mc.itemInfo" }) {
            RE::GFxValue info;
            if (movie->GetVariable(&info, path) && info.IsObject()) {
                RE::GFxValue pv; pv.SetNumber(unit);
                info.SetMember("value", pv);
            }
        }

        RE::GFxValue args[4];
        args[0].SetNumber(0.0);
        args[1].SetNumber(static_cast<double>(amount));
        args[2].SetNumber(unit);
        args[3].SetBoolean(isBuying);

        Hooks::replayingItemSelect = true;
        delegate->Callback(movie, "ItemSelect", args, 4);
        Hooks::replayingItemSelect = false;
        DbgLog("QuickTransferMarket: {} x{} @ {} unit ({})",
            formID, amount, unitPrice, isBuying ? "buy" : "sell");
    });
}

namespace {
    // Direct inventory move for a single cart item + its gold share. Used only as a
    // fallback when the vanilla ItemSelect selection can't be verified for an item.
    void DirectTransferEntry(const CartEntry& e, double unitPrice) {
        auto* player = RE::PlayerCharacter::GetSingleton();
        if (!player) return;

        RE::TESObjectREFR* merchantRef = nullptr;
        auto refHandle = RE::BarterMenu::GetTargetRefHandle();
        RE::NiPointer<RE::TESObjectREFR> refPtr;
        if (RE::LookupReferenceByHandle(refHandle, refPtr) && refPtr) merchantRef = refPtr.get();
        if (!merchantRef) return;

        auto* obj = RE::TESForm::LookupByID<RE::TESBoundObject>(e.formID);
        if (!obj) return;
        auto* gold = RE::TESForm::LookupByID<RE::TESBoundObject>(0x0000000F);

        const int cnt = (e.count >= 1) ? e.count : 1;
        const int goldAmt = static_cast<int>(unitPrice * cnt + 0.5);
        using REASON = RE::ITEM_REMOVE_REASON;

        if (e.isBuying) {
            merchantRef->RemoveItem(obj, cnt, REASON::kStoreInContainer, nullptr, player);
            if (gold && goldAmt > 0) player->RemoveItem(gold, goldAmt, REASON::kStoreInContainer, nullptr, merchantRef);
        } else {
            player->RemoveItem(obj, cnt, REASON::kStoreInContainer, nullptr, merchantRef);
            if (gold && goldAmt > 0) merchantRef->RemoveItem(gold, goldAmt, REASON::kStoreInContainer, nullptr, player);
        }
    }

    // Move a small gold remainder so the PLAYER'S net gold for the deal is exact.
    // delta>0 = player still owes that much; delta<0 = player is owed that much change.
    //
    // Critical: a vendor's barter gold lives in the merchant *faction container*, not the
    // merchant actor's own inventory, so pulling change out of `merchantRef` usually finds
    // ~0 gold and silently moves nothing - which left the player overpaying (e.g. charged
    // 42 on a 38 deal). The player's gold is what matters, so we always adjust the player
    // directly: debit reliably when they owe, credit them directly when owed change.
    void ReconcileGold(int delta) {
        if (delta == 0) return;
        auto* player = RE::PlayerCharacter::GetSingleton();
        if (!player) return;
        auto* gold = RE::TESForm::LookupByID<RE::TESBoundObject>(0x0000000F);
        if (!gold) return;
        using REASON = RE::ITEM_REMOVE_REASON;
        if (delta > 0) {
            // Player still owes: take it from the player. Route to the merchant when we can
            // resolve them (keeps gold conserved); the player always has it since they buy.
            RE::TESObjectREFR* merchantRef = nullptr;
            auto refHandle = RE::BarterMenu::GetTargetRefHandle();
            RE::NiPointer<RE::TESObjectREFR> refPtr;
            if (RE::LookupReferenceByHandle(refHandle, refPtr) && refPtr) merchantRef = refPtr.get();
            player->RemoveItem(gold, delta, REASON::kStoreInContainer, nullptr, merchantRef);
        } else {
            // Player is owed change: credit them directly. Don't depend on the merchant
            // actor having loose gold (it lives in the vendor container, not the actor).
            player->AddObjectToContainer(gold, nullptr, -delta, nullptr);
        }
        DbgLog("ReconcileGold: adjusted player net by {} gold", -delta);
    }

    // Re-enter the sequential step after a short delay (lets the list rebuild).
    void ScheduleCartStep(std::shared_ptr<std::function<void(std::size_t, bool)>> step,
                          std::size_t idx, bool switched, int ms) {
        std::thread([step, idx, switched, ms]() {
            std::this_thread::sleep_for(std::chrono::milliseconds(ms));
            SKSE::GetTaskInterface()->AddTask([step, idx, switched]() {
                (*step)(idx, switched);
            });
        }).detach();
    }
}

void BarterManager::TransferCart(int finalPrice) {
    auto* cart = CartManager::GetSingleton();
    auto entries = cart->GetEntries();  // copy since we'll clear
    if (entries.empty()) {
        logger::warn("TransferCart: cart is empty, nothing to transfer");
        return;
    }

    // Distribute the negotiated net across per-item unit prices so the vanilla
    // transactions (player pays for buys, receives for sells) sum to the deal:
    //   sum(buy.unit*count) - sum(sell.unit*count) == signedNet
    // where signedNet is positive when the player pays.
    const int buySubtotal  = cart->GetBuySubtotal();
    const int sellSubtotal = cart->GetSellSubtotal();
    const int signedNet     = currentIsBuying ? finalPrice : -finalPrice;

    double buyFactor = 1.0, sellFactor = 1.0;
    if (buySubtotal > 0) {
        buyFactor = static_cast<double>(signedNet + sellSubtotal) / static_cast<double>(buySubtotal);
        if (buyFactor < 0.0) buyFactor = 0.0;
    } else if (sellSubtotal > 0) {
        sellFactor = static_cast<double>(-signedNet) / static_cast<double>(sellSubtotal);
        if (sellFactor < 0.0) sellFactor = 0.0;
    }

    // Vanilla's ItemSelect FLOORS the per-unit price before multiplying by the stack
    // count, so a fractional unit price silently loses gold (e.g. 0.6/unit x2 pays 0,
    // 12.8/unit x1 pays 12). We therefore allocate WHOLE-gold unit prices, then nudge
    // them so the signed total matches the negotiated net exactly. Any tiny remainder
    // that can't be expressed in whole units is reconciled with a direct gold move.
    const int n = static_cast<int>(entries.size());
    auto stackCount = [&](int i) { return entries[i].count >= 1 ? entries[i].count : 1; };

    std::vector<long> unit(n, 0);
    for (int i = 0; i < n; ++i) {
        const double factor = entries[i].isBuying ? buyFactor : sellFactor;
        long v = static_cast<long>(static_cast<double>(entries[i].marketUnitPrice) * factor + 0.5);
        if (v < 0) v = 0;
        unit[i] = v;
    }
    auto signedSum = [&]() -> long {
        long t = 0;
        for (int i = 0; i < n; ++i) t += (entries[i].isBuying ? 1 : -1) * unit[i] * stackCount(i);
        return t;
    };
    long residual = static_cast<long>(signedNet) - signedSum();  // >0 => player still owes
    // Pass 1: count==1 items absorb the remainder one gold at a time (exact).
    for (int i = 0; i < n && residual != 0; ++i) {
        if (stackCount(i) != 1) continue;
        const long sign = entries[i].isBuying ? 1 : -1;
        long d = residual * sign;            // each +1 unit shifts the signed total by `sign`
        long nv = unit[i] + d;
        if (nv < 0) { d = -unit[i]; nv = 0; }
        unit[i] = nv;
        residual -= sign * d;
    }
    // Pass 2: stacked items absorb whole-stack (count-sized) chunks of what's left.
    for (int i = 0; i < n && residual != 0; ++i) {
        const long c = stackCount(i);
        if (c == 1) continue;
        const long sign = entries[i].isBuying ? 1 : -1;
        long steps = (residual * sign) / c;
        if (steps == 0) continue;
        long nv = unit[i] + steps;
        if (nv < 0) { steps = -unit[i]; nv = 0; }
        unit[i] = nv;
        residual -= sign * steps * c;
    }

    std::vector<double> unitPrices;
    unitPrices.reserve(n);
    for (int i = 0; i < n; ++i) unitPrices.push_back(static_cast<double>(unit[i]));

    const int residualGold = static_cast<int>(residual);  // leftover to reconcile at the end
    DbgLog("TransferCart: {} items, net={} ({}), buy={}, sell={}, residual={}",
        entries.size(), std::abs(signedNet), currentIsBuying ? "player pays" : "player receives",
        buySubtotal, sellSubtotal, residualGold);

    auto entriesPtr = std::make_shared<std::vector<CartEntry>>(std::move(entries));
    auto pricesPtr  = std::make_shared<std::vector<double>>(std::move(unitPrices));
    auto step       = std::make_shared<std::function<void(std::size_t, bool)>>();

    // Sequential, vanilla-first transfer. For each cart item we make the engine
    // select that exact row on the correct buy/sell side, then replay the vanilla
    // "ItemSelect" so the game performs the transaction (ownership, perks, merchant
    // gold caps, UI updates). If the row can't be verified, we fall back to a direct
    // move for that one item so the cart always settles correctly.
    *step = [this, entriesPtr, pricesPtr, step, residualGold](std::size_t idx, bool switched) {
        SKSE::GetTaskInterface()->AddUITask([this, idx, switched, entriesPtr, pricesPtr, step, residualGold]() {
            if (idx >= entriesPtr->size()) {
                // Reconcile any sub-stack rounding remainder so the player's net is exact.
                if (residualGold != 0) ReconcileGold(residualGold);
                CartManager::GetSingleton()->Clear();
                isCartMode = false;
                Hooks::lastNegotiationEnd = std::chrono::steady_clock::now();
                RefreshBarterMenu(0);
                DbgLog("TransferCart: sequence complete ({} items)", entriesPtr->size());
                return;
            }

            auto* ui = RE::UI::GetSingleton();
            auto menuPtr = ui ? ui->GetMenu<RE::BarterMenu>() : nullptr;
            auto* menu = menuPtr.get();
            if (!menu || !menu->uiMovie) {
                logger::error("TransferCart[{}]: BarterMenu unavailable", idx);
                return;
            }

            const auto& entry = (*entriesPtr)[idx];
            const double unit = (*pricesPtr)[idx];

            // Ensure the correct side is visible; the rebuild is async, so switch
            // and retry the same item on the next pass.
            if (Hooks::CurrentSideIsBuying(menu) != entry.isBuying && !switched) {
                Hooks::SwitchBarterSide(menu, entry.isBuying);
                ScheduleCartStep(step, idx, true, 140);
                return;
            }

            bool usedVanilla = false;
            if (Hooks::SelectCartItem(menu, entry.formID, entry.isBuying)) {
                auto* movie = menu->uiMovie.get();
                auto* delegate = menu->fxDelegate.get();
                if (movie && delegate) {
                    // Patch the ItemCard's per-unit value to the negotiated price
                    // (the native ItemSelect reads the gold amount from here).
                    RE::GFxValue info;
                    if (movie->GetVariable(&info, "_root.Menu_mc.ItemCard_mc.itemInfo") && info.IsObject()) {
                        RE::GFxValue pv; pv.SetNumber(unit);
                        info.SetMember("value", pv);
                    }
                    RE::GFxValue args[4];
                    args[0].SetNumber(0.0);
                    args[1].SetNumber(static_cast<double>(entry.count));
                    args[2].SetNumber(unit);
                    args[3].SetBoolean(entry.isBuying);

                    Hooks::replayingItemSelect = true;
                    delegate->Callback(movie, "ItemSelect", args, 4);
                    Hooks::replayingItemSelect = false;
                    usedVanilla = true;
                    DbgLog("TransferCart[{}]: ItemSelect {} x{} @ {:.1f} ({})",
                        idx, entry.name, entry.count, unit, entry.isBuying ? "buy" : "sell");
                }
            }

            if (!usedVanilla) {
                DirectTransferEntry(entry, unit);
                DbgLog("TransferCart[{}]: direct fallback {} x{} @ {:.1f} ({})",
                    idx, entry.name, entry.count, unit, entry.isBuying ? "buy" : "sell");
            }

            ScheduleCartStep(step, idx + 1, false, 110);
        });
    };

    (*step)(0, false);
}

void BarterManager::RefreshBarterMenu(RE::FormID itemID) {
    SKSE::GetTaskInterface()->AddUITask([itemID]() {
        auto* ui = RE::UI::GetSingleton();
        if (!ui || !ui->IsMenuOpen(RE::BarterMenu::MENU_NAME)) {
            return;
        }

        auto* player = RE::PlayerCharacter::GetSingleton();
        auto* item = itemID ? RE::TESForm::LookupByID<RE::TESBoundObject>(itemID) : nullptr;
        auto* gold = RE::TESForm::LookupByID<RE::TESBoundObject>(0x0000000F);

        // Resolve the merchant reference from the BarterMenu's target handle
        RE::TESObjectREFR* merchantRef = nullptr;
        auto refHandle = RE::BarterMenu::GetTargetRefHandle();
        RE::NiPointer<RE::TESObjectREFR> refPtr;
        if (RE::LookupReferenceByHandle(refHandle, refPtr) && refPtr) {
            merchantRef = refPtr.get();
        }

        // Backstop refresh for both sides of the barter so the item lists AND the
        // player/vendor gold totals update live even if the engine's own post-
        // transaction inventory update was missed. Sending the gold object as well
        // forces the BarterMenu to recompute the bottom-bar gold counters.
        if (player) {
            RE::SendUIMessage::SendInventoryUpdateMessage(player, item);
            if (gold) RE::SendUIMessage::SendInventoryUpdateMessage(player, gold);
        }
        if (merchantRef) {
            RE::SendUIMessage::SendInventoryUpdateMessage(merchantRef, item);
            if (gold) RE::SendUIMessage::SendInventoryUpdateMessage(merchantRef, gold);
        }

        DbgLog("RefreshBarterMenu: Sent inventory update messages");
    });
}

void BarterManager::ResetDebugForceFlags() {
    auto* settings = Settings::GetSingleton();
    settings->forceAccept = false;
    settings->forceReject = false;
    settings->forceCounter = false;
}

void BarterManager::RecordAndClose(int offeredPrice, bool accepted, bool wasCounter, int counterAmt) {
    // A cart deal has no single item id, but it's still a dealing worth remembering
    // (and it's the mod's primary flow), so only bail when there's nothing to record at all.
    if (currentMerchantID == 0) return;
    if (currentItemID == 0 && !isCartMode) return;

    auto* calendar = RE::Calendar::GetSingleton();
    float gameDay = calendar ? calendar->GetDaysPassed() : 0.0f;

    std::string itemName = "Unknown";
    if (isCartMode) {
        auto* cart = CartManager::GetSingleton();
        const int n = cart ? cart->Count() : 0;
        itemName = "Cart (" + std::to_string(n) + (n == 1 ? " item)" : " items)");
    } else if (currentItem) {
        const char* name = currentItem->GetName();
        if (name) itemName = name;
    }

    DealRecord deal;
    deal.itemFormID = currentItemID;  // 0 for carts; the history synopsis keys off price only
    deal.itemName = itemName;
    deal.basePrice = currentEffectivePrice;
    deal.offeredPrice = offeredPrice;
    deal.accepted = accepted;
    deal.wasCounterOffer = wasCounter;
    deal.counterAmount = counterAmt;
    deal.timestamp = gameDay;

    RelationshipManager::GetSingleton()->RecordDeal(currentMerchantID, deal);
}
