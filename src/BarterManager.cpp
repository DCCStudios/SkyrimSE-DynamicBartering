#include "PCH.h"
#include "BarterManager.h"
#include "CartManager.h"
#include "RelationshipManager.h"
#include "PriceJack.h"
#include "Settings.h"
#include <thread>
#include "Hooks.h"
#include "UI/UIBridge.h"

void BarterManager::OnBarterMenuCreated(RE::BarterMenu* menu) {
    (void)menu;
    logger::info("BarterMenu PostCreate intercepted");
}

void BarterManager::OnBarterOpen() {
    state = BarterState::Idle;
    barterActive = true;
    isCartMode = false;
    currentMerchant = nullptr;
    currentMerchantID = 0;
    sessionRejections.clear();
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

    if (currentMerchant) {
        currentMerchantID = currentMerchant->GetFormID();
        cachedPerks = PerkBonuses::Detect(player);
        cachedPersonality = RelationshipManager::GetSingleton()->GetPersonality(currentMerchant);
        cachedSpeech = player->AsActorValueOwner()
            ? player->AsActorValueOwner()->GetActorValue(RE::ActorValue::kSpeech)
            : 15.0f;
        patienceRemaining = cachedPersonality.patienceRounds;

        auto merchantName = currentMerchant->GetName();
        auto& memory = RelationshipManager::GetSingleton()->GetOrCreate(
            currentMerchantID, merchantName ? merchantName : "Unknown");

        logger::info("Barter opened with {} (relationship: {}, personality: {})",
            merchantName ? merchantName : "Unknown",
            memory.relationship,
            MerchantPersonality::TraitToString(cachedPersonality.trait));
    }
}

void BarterManager::OnBarterClose() {
    if (state != BarterState::Idle) {
        UIBridge::GetSingleton()->Hide();
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

    currentItem = item;
    currentItemID = item->GetFormID();
    currentIsBuying = isBuying;
    currentIsStolen = isStolen;
    currentAmount = (amount >= 1) ? amount : 1;

    auto& memory = RelationshipManager::GetSingleton()->GetOrCreate(
        currentMerchantID, currentMerchant->GetName() ? currentMerchant->GetName() : "Unknown");

    float priceJackMult = PriceJack::GetMultiplier(
        memory.relationship, cachedPersonality, cachedPerks.hasInvestor);

    PriceContext pCtx;
    pCtx.player = RE::PlayerCharacter::GetSingleton();
    pCtx.merchant = currentMerchant;
    pCtx.item = item;
    pCtx.itemBaseValue = baseValue;
    pCtx.isBuying = isBuying;
    pCtx.isStolen = isStolen;

    auto priceResult = PriceCalculator::CalculatePrice(pCtx, memory.relationship, priceJackMult);
    currentBasePrice = priceResult.basePrice;
    currentEffectivePrice = priceResult.effectivePrice;

    OfferData data;
    data.itemName = item->GetName() ? item->GetName() : "Unknown Item";
    data.basePrice = priceResult.basePrice;
    data.effectivePrice = priceResult.effectivePrice;
    data.isBuying = isBuying;
    {
        auto it = sessionRejections.find(currentItemID);
        data.sessionRejectedPrice = (it != sessionRejections.end()) ? it->second : 0;
    }
    data.merchantName = currentMerchant->GetName() ? currentMerchant->GetName() : "Unknown";
    data.personalityName = MerchantPersonality::TraitToString(cachedPersonality.trait);
    data.relationship = memory.relationship;
    data.speechBonus = (cachedSpeech / 100.0f) * settings->speechWeight;
    data.hasIntimidationPerk = cachedPerks.hasIntimidation;
    data.sliderMin = priceResult.sliderMin;
    data.sliderMax = priceResult.sliderMax;
    data.priceJackMult = priceJackMult;

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

    state = BarterState::ShowingOffer;
    UIBridge::GetSingleton()->ShowOffer(data);
    state = BarterState::WaitingForPlayer;
}

void BarterManager::StartCartOffer() {
    if (!currentMerchant) return;
    auto* settings = Settings::GetSingleton();
    if (!settings->modEnabled) return;

    auto* cart = CartManager::GetSingleton();
    if (cart->IsEmpty()) return;

    isCartMode = true;
    currentItem = nullptr;
    currentItemID = 0;
    currentAmount = 1;

    int netAmount = cart->GetNetAmount();
    int buySubtotal = cart->GetBuySubtotal();
    int sellSubtotal = cart->GetSellSubtotal();
    bool netIsBuying = (netAmount >= 0);

    currentIsBuying = netIsBuying;
    currentIsStolen = false;
    currentBasePrice = std::abs(netAmount);
    currentEffectivePrice = std::abs(netAmount);

    auto& memory = RelationshipManager::GetSingleton()->GetOrCreate(
        currentMerchantID, currentMerchant->GetName() ? currentMerchant->GetName() : "Unknown");

    OfferData data;
    data.itemName = "Cart (" + std::to_string(cart->Count()) + " items)";
    data.basePrice = currentBasePrice;
    data.effectivePrice = currentEffectivePrice;
    data.isBuying = netIsBuying;
    data.sessionRejectedPrice = 0;
    data.merchantName = currentMerchant->GetName() ? currentMerchant->GetName() : "Unknown";
    data.personalityName = MerchantPersonality::TraitToString(cachedPersonality.trait);
    data.relationship = memory.relationship;
    data.speechBonus = (cachedSpeech / 100.0f) * settings->speechWeight;
    data.hasIntimidationPerk = cachedPerks.hasIntimidation;
    data.sliderMin = settings->sliderRangeMin;
    data.sliderMax = settings->sliderRangeMax;
    data.priceJackMult = 1.0f;

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

    logger::info("StartCartOffer: {} items, buy={}, sell={}, net={} ({})",
        cart->Count(), buySubtotal, sellSubtotal, netAmount,
        netIsBuying ? "player pays" : "player receives");

    state = BarterState::ShowingOffer;
    UIBridge::GetSingleton()->ShowOffer(data);
    state = BarterState::WaitingForPlayer;
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
    ctx.relationship = memory.relationship;
    ctx.personality = cachedPersonality;
    ctx.memory = &memory;
    ctx.offeredPrice = offeredPrice;
    ctx.basePrice = currentEffectivePrice;
    ctx.oppositeGender = oppositeGender;
    ctx.isStolen = currentIsStolen;
    ctx.isBuying = currentIsBuying;
    auto it = sessionRejections.find(currentItemID);
    ctx.sessionRejectedPrice = (it != sessionRejections.end()) ? it->second : 0;
    return ctx;
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

    float chance = PreviewAcceptanceChance(offeredPrice);

    auto* settings = Settings::GetSingleton();
    if (settings->debugLogging) {
        logger::info("Offer: {} / Base: {} | Chance: {:.1f}%", offeredPrice, currentEffectivePrice, chance);
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
            logger::info("Relationship {}{} ({}) [{:.0f}% chance hit]",
                delta > 0 ? "+" : "", delta, reason, chancePercent);
        }
        return delta;
    }
    if (Settings::GetSingleton()->debugLogging) {
        logger::info("Relationship unchanged ({}) [{:.0f}% chance missed]", reason, chancePercent);
    }
    return 0;
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
    if (genRatio >= 1.1f)       relGain = RollRelationshipChange(80.0f, 3, "generous deal");
    else if (genRatio >= 1.0f)  relGain = RollRelationshipChange(65.0f, 2, "deal at/above market");
    else if (genRatio >= 0.9f)  relGain = RollRelationshipChange(45.0f, 1, "fair deal");
    else                        relGain = RollRelationshipChange(25.0f, 1, "deal in player's favour");

    RecordAndClose(offeredPrice, true, false, 0);
    if (isCartMode) {
        TransferCart(offeredPrice);
    } else {
        TransferItemAndGold(offeredPrice);
    }

    // Show brief acceptance confirmation with amount, then auto-close
    UIBridge::GetSingleton()->ShowResult(true, offeredPrice, relGain);
    state = BarterState::ShowingResult;
    logger::info("BarterManager: Offer accepted for {} gold - showing confirmation", offeredPrice);

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
    } else {
        UIBridge::GetSingleton()->ShowResult(false, 0, appliedLoss);
        RecordAndClose(offeredPrice, false, false, 0);
        state = BarterState::ShowingResult;
    }
}

void BarterManager::OnCounterResponse(int response) {
    switch (response) {
        case 0:  // Accept counter
            FinalizeDeal(currentCounterAmount, true);
            break;
        case 1:  // Re-offer
            state = BarterState::WaitingForPlayer;
            break;
        case 2:  // Walk away
            RelationshipManager::GetSingleton()->ModifyRelationship(currentMerchantID, -2);
            RecordAndClose(0, false, false, currentCounterAmount);
            UIBridge::GetSingleton()->Hide();
            state = BarterState::Idle;
            break;
    }
}

void BarterManager::OnIntimidateAttempt() {
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

    logger::info("BarterManager: Intimidate attempt - chance={:.0f}%, speech={}, level={}, perk={}, personality={}",
        roll * 100.0f, cachedSpeech, playerLevel, cachedPerks.hasIntimidation,
        MerchantPersonality::TraitToString(cachedPersonality.trait));

    if (dist(rng) < roll) {
        int intimidatedPrice = currentIsBuying
            ? static_cast<int>(currentEffectivePrice * 0.5f)
            : static_cast<int>(currentEffectivePrice * 1.5f);
        RollRelationshipChange(85.0f, -15, "intimidation succeeded");
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
        logger::info("BarterManager: Intimidation succeeded - deal done, window closed");
    } else {
        int loss = RollRelationshipChange(95.0f, failPenalty, "intimidation failed");
        UIBridge::GetSingleton()->ShowResult(false, 0, loss);
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
    logger::info("BarterManager: Player retrying offer after rejection");
    state = BarterState::WaitingForPlayer;
}

void BarterManager::OnCancelled() {
    logger::info("BarterManager: Offer cancelled by player");
    UIBridge::GetSingleton()->Hide();
    state = BarterState::Idle;
    Hooks::lastNegotiationEnd = std::chrono::steady_clock::now();
    currentItem = nullptr;
    currentItemID = 0;
}

void BarterManager::FinalizeDeal(int finalPrice, bool wasCounter) {
    int relGain = RollRelationshipChange(50.0f, 1, "counter-offer accepted");
    RecordAndClose(finalPrice, true, wasCounter, wasCounter ? currentCounterAmount : 0);
    if (isCartMode) {
        TransferCart(finalPrice);
    } else {
        TransferItemAndGold(finalPrice);
    }

    // Show acceptance result (hides the window), then auto-close
    UIBridge::GetSingleton()->ShowResult(true, finalPrice, relGain);
    state = BarterState::ShowingResult;
    logger::info("BarterManager: Counter-offer accepted for {} gold - showing confirmation", finalPrice);

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
        double unitPrice = static_cast<double>(finalPrice) / static_cast<double>(amount);

        RE::GFxValue itemCardInfo;
        if (movie->GetVariable(&itemCardInfo, "_root.Menu_mc.ItemCard_mc.itemInfo")) {
            if (itemCardInfo.IsObject()) {
                RE::GFxValue pv;
                pv.SetNumber(unitPrice);
                itemCardInfo.SetMember("value", pv);
                logger::info("TransferItemAndGold: Set ItemCard_mc.itemInfo.value = {:.2f}", unitPrice);
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

        logger::info("TransferItemAndGold: Invoked ItemSelect callback (total={}, x{}, unit={:.2f}, {})",
            finalPrice, amount, unitPrice, isBuying ? "buying" : "selling");
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
        logger::info("QuickTransferMarket: {} x{} @ {} unit ({})",
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

    std::vector<double> unitPrices;
    unitPrices.reserve(entries.size());
    for (const auto& e : entries) {
        double unit = static_cast<double>(e.marketUnitPrice) * (e.isBuying ? buyFactor : sellFactor);
        if (unit < 0.0) unit = 0.0;
        unitPrices.push_back(unit);
    }

    logger::info("TransferCart: {} items, net={} ({}), buy={}, sell={}",
        entries.size(), std::abs(signedNet), currentIsBuying ? "player pays" : "player receives",
        buySubtotal, sellSubtotal);

    auto entriesPtr = std::make_shared<std::vector<CartEntry>>(std::move(entries));
    auto pricesPtr  = std::make_shared<std::vector<double>>(std::move(unitPrices));
    auto step       = std::make_shared<std::function<void(std::size_t, bool)>>();

    // Sequential, vanilla-first transfer. For each cart item we make the engine
    // select that exact row on the correct buy/sell side, then replay the vanilla
    // "ItemSelect" so the game performs the transaction (ownership, perks, merchant
    // gold caps, UI updates). If the row can't be verified, we fall back to a direct
    // move for that one item so the cart always settles correctly.
    *step = [this, entriesPtr, pricesPtr, step](std::size_t idx, bool switched) {
        SKSE::GetTaskInterface()->AddUITask([this, idx, switched, entriesPtr, pricesPtr, step]() {
            if (idx >= entriesPtr->size()) {
                CartManager::GetSingleton()->Clear();
                isCartMode = false;
                Hooks::lastNegotiationEnd = std::chrono::steady_clock::now();
                RefreshBarterMenu(0);
                logger::info("TransferCart: sequence complete ({} items)", entriesPtr->size());
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
                    logger::info("TransferCart[{}]: ItemSelect {} x{} @ {:.1f} ({})",
                        idx, entry.name, entry.count, unit, entry.isBuying ? "buy" : "sell");
                }
            }

            if (!usedVanilla) {
                DirectTransferEntry(entry, unit);
                logger::info("TransferCart[{}]: direct fallback {} x{} @ {:.1f} ({})",
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

        logger::info("RefreshBarterMenu: Sent inventory update messages");
    });
}

void BarterManager::ResetDebugForceFlags() {
    auto* settings = Settings::GetSingleton();
    settings->forceAccept = false;
    settings->forceReject = false;
    settings->forceCounter = false;
}

void BarterManager::RecordAndClose(int offeredPrice, bool accepted, bool wasCounter, int counterAmt) {
    if (currentItemID == 0) return;

    auto* calendar = RE::Calendar::GetSingleton();
    float gameDay = calendar ? calendar->GetDaysPassed() : 0.0f;

    std::string itemName = "Unknown";
    if (currentItem) {
        const char* name = currentItem->GetName();
        if (name) itemName = name;
    }

    DealRecord deal;
    deal.itemFormID = currentItemID;
    deal.itemName = itemName;
    deal.basePrice = currentEffectivePrice;
    deal.offeredPrice = offeredPrice;
    deal.accepted = accepted;
    deal.wasCounterOffer = wasCounter;
    deal.counterAmount = counterAmt;
    deal.timestamp = gameDay;

    RelationshipManager::GetSingleton()->RecordDeal(currentMerchantID, deal);
}
