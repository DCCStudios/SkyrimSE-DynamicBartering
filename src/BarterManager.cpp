#include "PCH.h"
#include "BarterManager.h"
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
    currentMerchant = nullptr;
    currentMerchantID = 0;

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
    currentMerchant = nullptr;
    currentMerchantID = 0;
    currentItem = nullptr;
    currentItemID = 0;
    RelationshipManager::GetSingleton()->SaveData();
}

void BarterManager::StartOffer(RE::TESBoundObject* item, int baseValue, bool isBuying, bool isStolen) {
    if (!currentMerchant || !item) return;
    auto* settings = Settings::GetSingleton();
    if (!settings->modEnabled) return;
    if (settings->skipBelowThreshold && baseValue < settings->valueThreshold) return;

    currentItem = item;
    currentItemID = item->GetFormID();
    currentIsBuying = isBuying;
    currentIsStolen = isStolen;

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

    AcceptanceContext aCtx;
    aCtx.speechSkill = cachedSpeech;
    aCtx.perks = cachedPerks;
    aCtx.relationship = memory.relationship;
    aCtx.personality = cachedPersonality;
    aCtx.memory = &memory;
    aCtx.offeredPrice = priceResult.effectivePrice;
    aCtx.basePrice = priceResult.effectivePrice;
    aCtx.oppositeGender = false;
    data.acceptanceChance = PriceCalculator::CalculateAcceptanceChance(aCtx);

    state = BarterState::ShowingOffer;
    UIBridge::GetSingleton()->ShowOffer(data);
    state = BarterState::WaitingForPlayer;
}

void BarterManager::OnPlayerOffer(int offeredPrice) {
    if (state != BarterState::WaitingForPlayer) return;

    if (offeredPrice < 0) {
        OnCancelled();
        return;
    }

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

    AcceptanceContext ctx;
    ctx.speechSkill = cachedSpeech;
    ctx.perks = cachedPerks;
    ctx.relationship = memory.relationship;
    ctx.personality = cachedPersonality;
    ctx.memory = &memory;
    ctx.offeredPrice = offeredPrice;
    ctx.basePrice = currentEffectivePrice;
    ctx.oppositeGender = oppositeGender;
    ctx.isStolen = currentIsStolen;

    float chance = PriceCalculator::CalculateAcceptanceChance(ctx);

    auto* settings = Settings::GetSingleton();
    if (settings->debugLogging) {
        logger::info("Offer: {} / Base: {} | Chance: {:.1f}%", offeredPrice, currentEffectivePrice, chance);
    }

    ResetDebugForceFlags();

    if (PriceCalculator::RollAcceptance(chance)) {
        ProcessAcceptance(offeredPrice);
    } else {
        ProcessRejection(offeredPrice);
    }
}

void BarterManager::ProcessAcceptance(int offeredPrice) {
    float ratio = (currentEffectivePrice > 0)
        ? static_cast<float>(offeredPrice) / static_cast<float>(currentEffectivePrice)
        : 1.0f;

    int relDelta = 1;
    if (ratio >= 1.0f) relDelta = 3;
    else if (ratio >= 0.9f) relDelta = 2;

    RelationshipManager::GetSingleton()->ModifyRelationship(currentMerchantID, relDelta);
    RecordAndClose(offeredPrice, true, false, 0);
    TransferItemAndGold(offeredPrice);

    // Show brief acceptance confirmation with amount, then auto-close
    UIBridge::GetSingleton()->ShowResult(true, offeredPrice);
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
                    currentItem = nullptr;
                    currentItemID = 0;
                    Hooks::interceptingTransaction = false;
                    Hooks::lastSelectedItem = nullptr;
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
        patienceRemaining
    );

    float ratio = (currentEffectivePrice > 0)
        ? static_cast<float>(offeredPrice) / static_cast<float>(currentEffectivePrice)
        : 1.0f;

    int relLoss = -2;
    if (ratio < 0.5f) relLoss = -static_cast<int>(cachedPersonality.offensePerInsult);
    else if (ratio < 0.7f) relLoss = -static_cast<int>(cachedPersonality.offensePerInsult * 0.5f);

    if (cachedPersonality.enjoysHaggling) relLoss = 0;

    RelationshipManager::GetSingleton()->ModifyRelationship(currentMerchantID, relLoss);

    if (counterResult.willCounter && patienceRemaining > 0) {
        currentCounterAmount = counterResult.counterAmount;
        patienceRemaining = counterResult.patienceRemaining;
        state = BarterState::ShowingCounterOffer;
        UIBridge::GetSingleton()->ShowCounterOffer(counterResult.counterAmount, patienceRemaining);
    } else {
        UIBridge::GetSingleton()->ShowResult(false, relLoss);
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
            Hooks::interceptingTransaction = false;
            Hooks::lastSelectedItem = nullptr;
            break;
    }
}

void BarterManager::OnIntimidateAttempt() {
    if (!cachedPerks.hasIntimidation) return;

    float roll = cachedSpeech / 100.0f * 2.0f;
    static std::mt19937 rng(std::random_device{}());
    std::uniform_real_distribution<float> dist(0.0f, 1.0f);

    if (dist(rng) < roll) {
        int intimidatedPrice = currentIsBuying
            ? static_cast<int>(currentEffectivePrice * 0.5f)
            : static_cast<int>(currentEffectivePrice * 1.5f);
        RelationshipManager::GetSingleton()->ModifyRelationship(currentMerchantID, -15);
        RecordAndClose(intimidatedPrice, true, false, 0);
        TransferItemAndGold(intimidatedPrice);
        // Close immediately on successful intimidation
        auto itemID = currentItemID;
        UIBridge::GetSingleton()->Hide();
        state = BarterState::Idle;
        currentItem = nullptr;
        currentItemID = 0;
        Hooks::interceptingTransaction = false;
        Hooks::lastSelectedItem = nullptr;
        RefreshBarterMenu(itemID);
        logger::info("BarterManager: Intimidation succeeded - deal done, window closed");
    } else {
        RelationshipManager::GetSingleton()->ModifyRelationship(currentMerchantID, -25);
        UIBridge::GetSingleton()->ShowResult(false, -25);
        state = BarterState::ShowingResult;
    }
}

void BarterManager::OnResultDismissed() {
    UIBridge::GetSingleton()->Hide();
    state = BarterState::Idle;
    currentItem = nullptr;
    currentItemID = 0;
    Hooks::interceptingTransaction = false;
    Hooks::lastSelectedItem = nullptr;
}

void BarterManager::RetryOffer() {
    logger::info("BarterManager: Player retrying offer after rejection");
    state = BarterState::WaitingForPlayer;
}

void BarterManager::OnCancelled() {
    logger::info("BarterManager: Offer cancelled by player");
    UIBridge::GetSingleton()->Hide();
    state = BarterState::Idle;
    currentItem = nullptr;
    currentItemID = 0;
    Hooks::interceptingTransaction = false;
    Hooks::lastSelectedItem = nullptr;
}

void BarterManager::FinalizeDeal(int finalPrice, bool wasCounter) {
    RelationshipManager::GetSingleton()->ModifyRelationship(currentMerchantID, 1);
    RecordAndClose(finalPrice, true, wasCounter, wasCounter ? currentCounterAmount : 0);
    TransferItemAndGold(finalPrice);

    // Show acceptance result (hides the window), then auto-close
    UIBridge::GetSingleton()->ShowResult(true, finalPrice);
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
                    currentItem = nullptr;
                    currentItemID = 0;
                    Hooks::interceptingTransaction = false;
                    Hooks::lastSelectedItem = nullptr;
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

    SKSE::GetTaskInterface()->AddUITask([finalPrice, isBuying]() {
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

        // Allow vanilla processing to proceed
        Hooks::interceptingTransaction = false;
        Hooks::lastSelectedItem = nullptr;

        // Directly invoke the "ItemSelect" GameDelegate callback with our custom price.
        // AS2 GameDelegate.call() prepends [methodName, responseID] then strips them, so the
        // native handler's args are [responseID, amount, unitPrice, isViewingVendorItems].
        // CommonLib's FxDelegateArgs consumes args[0] as the responseID and exposes args[1..].
        RE::GFxValue args[4];
        args[0].SetNumber(0.0);                                   // responseID (no response callback registered)
        args[1].SetNumber(1.0);                                   // count
        args[2].SetNumber(static_cast<double>(finalPrice));       // unit price (our negotiated price)
        args[3].SetBoolean(isBuying);                             // IsViewingVendorItems

        delegate->Callback(movie, "ItemSelect", args, 4);

        logger::info("TransferItemAndGold: Invoked ItemSelect callback (price={}, {})",
            finalPrice, isBuying ? "buying" : "selling");
    });
}

void BarterManager::RefreshBarterMenu(RE::FormID itemID) {
    SKSE::GetTaskInterface()->AddUITask([itemID]() {
        auto* ui = RE::UI::GetSingleton();
        if (!ui || !ui->IsMenuOpen(RE::BarterMenu::MENU_NAME)) {
            return;
        }

        auto* player = RE::PlayerCharacter::GetSingleton();
        auto* item = itemID ? RE::TESForm::LookupByID<RE::TESBoundObject>(itemID) : nullptr;

        // Resolve the merchant reference from the BarterMenu's target handle
        RE::TESObjectREFR* merchantRef = nullptr;
        auto refHandle = RE::BarterMenu::GetTargetRefHandle();
        RE::NiPointer<RE::TESObjectREFR> refPtr;
        if (RE::LookupReferenceByHandle(refHandle, refPtr) && refPtr) {
            merchantRef = refPtr.get();
        }

        // Refresh both sides of the barter so item lists and gold totals update live
        if (player) {
            RE::SendUIMessage::SendInventoryUpdateMessage(player, item);
        }
        if (merchantRef) {
            RE::SendUIMessage::SendInventoryUpdateMessage(merchantRef, item);
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
