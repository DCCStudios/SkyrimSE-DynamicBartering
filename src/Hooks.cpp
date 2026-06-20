#include "PCH.h"
#include "Hooks.h"
#include "BarterManager.h"
#include "Settings.h"

void Hooks::Install() {
    REL::Relocation<std::uintptr_t> barterVtbl{ RE::VTABLE_BarterMenu[0] };

    _PostCreateBart = barterVtbl.write_vfunc(0x2, &PostCreateBart);
    _ProcessMessageBart = barterVtbl.write_vfunc(0x4, &ProcessMessageBart);

    auto* ui = RE::UI::GetSingleton();
    if (ui) {
        ui->AddEventSink(BarterMenuEventSink::GetSingleton());
    }

    logger::info("Hooks installed (PostCreate + ProcessMessage)");
}

void Hooks::PostCreateBart(RE::BarterMenu* menu) {
    _PostCreateBart(menu);
    BarterManager::GetSingleton()->OnBarterMenuCreated(menu);
}

RE::UI_MESSAGE_RESULTS Hooks::ProcessMessageBart(RE::BarterMenu* menu, RE::UIMessage& a_message) {
    auto* mgr = BarterManager::GetSingleton();
    auto* settings = Settings::GetSingleton();

    if (!settings->modEnabled) {
        return _ProcessMessageBart(menu, a_message);
    }

    // If we approved a transaction, let it pass through to vanilla
    if (transactionApproved) {
        transactionApproved = false;
        interceptingTransaction = false;
        return _ProcessMessageBart(menu, a_message);
    }

    // While our offer UI is active, block ALL messages except kUpdate (needed for rendering)
    auto state = mgr->GetState();
    if (state != BarterState::Idle || interceptingTransaction) {
        auto msgType = a_message.type.get();
        if (msgType == RE::UI_MESSAGE_TYPE::kUpdate ||
            msgType == RE::UI_MESSAGE_TYPE::kShow ||
            msgType == RE::UI_MESSAGE_TYPE::kHide) {
            return _ProcessMessageBart(menu, a_message);
        }
        return RE::UI_MESSAGE_RESULTS::kHandled;
    }

    // Intercept user events — detect buy/sell confirmation
    if (a_message.type.get() == RE::UI_MESSAGE_TYPE::kUserEvent) {
        auto* strData = static_cast<RE::BSUIMessageData*>(a_message.data);
        if (strData) {
            auto eventStr = strData->fixedStr;
            if (eventStr == RE::UserEvents::GetSingleton()->accept) {
                // Don't intercept if the Quantity Menu is open (stacked items)
                auto* ui = RE::UI::GetSingleton();
                if (ui && ui->IsMenuOpen("Quantity Menu")) {
                    return _ProcessMessageBart(menu, a_message);
                }

                auto& runtimeData = menu->GetRuntimeData();
                auto* itemList = runtimeData.itemList;

                if (itemList) {
                    auto* selectedItem = itemList->GetSelectedItem();
                    if (selectedItem && selectedItem->data.objDesc) {
                        auto* entryData = selectedItem->data.objDesc;
                        RE::TESBoundObject* boundObj = entryData->object;

                        if (boundObj) {
                            int baseValue = entryData->GetValue();
                            int barterPrice = baseValue;
                            RE::GFxValue infoVal;
                            if (selectedItem->obj.GetMember("infoValue", &infoVal) && infoVal.IsNumber()) {
                                barterPrice = static_cast<int>(infoVal.GetNumber());
                            } else {
                                RE::GFxValue valMember;
                                if (selectedItem->obj.GetMember("value", &valMember) && valMember.IsNumber()) {
                                    barterPrice = static_cast<int>(valMember.GetNumber());
                                }
                            }
                            if (barterPrice <= 0) barterPrice = baseValue;

                            if (settings->skipBelowThreshold && barterPrice < settings->valueThreshold) {
                                return _ProcessMessageBart(menu, a_message);
                            }

                            bool isBuying = true;
                            RE::NiPointer<RE::TESObjectREFR> ownerRef;
                            if (RE::LookupReferenceByHandle(selectedItem->data.owner, ownerRef) && ownerRef) {
                                if (ownerRef->GetFormID() == RE::PlayerCharacter::GetSingleton()->GetFormID()) {
                                    isBuying = false;
                                }
                            }

                            bool isStolen = !entryData->IsOwnedBy(RE::PlayerCharacter::GetSingleton(), true);

                            interceptingTransaction = true;
                            lastSelectedItem = selectedItem;

                            const char* itemName = boundObj->GetName() ? boundObj->GetName() : "Unknown";
                            logger::info("Intercepted barter confirm: {} (baseValue: {}, barterPrice: {}, {})",
                                itemName, baseValue, barterPrice, isBuying ? "buying" : "selling");

                            mgr->StartOffer(boundObj, barterPrice, isBuying, isStolen);
                            return RE::UI_MESSAGE_RESULTS::kHandled;
                        }
                    }
                }
            }
        }
    }

    // Intercept mouse-click item selection (bypasses kUserEvent path)
    if (a_message.type.get() == RE::UI_MESSAGE_TYPE::kScaleformEvent) {
        auto* scaleData = static_cast<RE::BSUIScaleformData*>(a_message.data);
        if (scaleData && scaleData->scaleformEvent) {
            if (scaleData->scaleformEvent->type == RE::GFxEvent::EventType::kMouseDown) {
                // Get mouse coords from the event
                auto* mouseEvt = static_cast<RE::GFxMouseEvent*>(scaleData->scaleformEvent);
                float clickY = mouseEvt->y;

                // Only intercept if click is in the item list area (not tab buttons at top)
                // Tab buttons are typically in the top ~80px of the SWF viewport
                if (clickY > 80.0f) {
                    auto* ui = RE::UI::GetSingleton();
                    if (ui && ui->IsMenuOpen("Quantity Menu")) {
                        return _ProcessMessageBart(menu, a_message);
                    }

                    auto& runtimeData = menu->GetRuntimeData();
                    auto* itemList = runtimeData.itemList;
                    if (itemList) {
                        auto* selectedItem = itemList->GetSelectedItem();
                        if (selectedItem && selectedItem->data.objDesc) {
                            auto* entryData = selectedItem->data.objDesc;
                            RE::TESBoundObject* boundObj = entryData->object;
                            if (boundObj) {
                                int baseValue = entryData->GetValue();
                                int barterPrice = baseValue;
                                RE::GFxValue infoVal;
                                if (selectedItem->obj.GetMember("infoValue", &infoVal) && infoVal.IsNumber()) {
                                    barterPrice = static_cast<int>(infoVal.GetNumber());
                                } else {
                                    RE::GFxValue valMember;
                                    if (selectedItem->obj.GetMember("value", &valMember) && valMember.IsNumber()) {
                                        barterPrice = static_cast<int>(valMember.GetNumber());
                                    }
                                }
                                if (barterPrice <= 0) barterPrice = baseValue;

                                if (settings->skipBelowThreshold && barterPrice < settings->valueThreshold) {
                                    return _ProcessMessageBart(menu, a_message);
                                }

                                bool isBuying = true;
                                RE::NiPointer<RE::TESObjectREFR> ownerRef;
                                if (RE::LookupReferenceByHandle(selectedItem->data.owner, ownerRef) && ownerRef) {
                                    if (ownerRef->GetFormID() == RE::PlayerCharacter::GetSingleton()->GetFormID()) {
                                        isBuying = false;
                                    }
                                }

                                bool isStolen = !entryData->IsOwnedBy(RE::PlayerCharacter::GetSingleton(), true);

                                interceptingTransaction = true;
                                lastSelectedItem = selectedItem;

                                const char* itemName = boundObj->GetName() ? boundObj->GetName() : "Unknown";
                                logger::info("Intercepted barter mouse-click: {} (baseValue: {}, barterPrice: {}, {})",
                                    itemName, baseValue, barterPrice, isBuying ? "buying" : "selling");

                                mgr->StartOffer(boundObj, barterPrice, isBuying, isStolen);
                                return RE::UI_MESSAGE_RESULTS::kHandled;
                            }
                        }
                    }
                }
                // Not in item area or no item - pass through for tab switching etc.
                return _ProcessMessageBart(menu, a_message);
            }
        }
    }

    // kScaleformEvent pass-through when idle — needed for normal barter menu operation
    // (item scrolling, tab navigation, etc.)
    // Once interceptingTransaction is set (by the kUserEvent handler above),
    // these are already blocked by the guard at the top of this function.
    return _ProcessMessageBart(menu, a_message);
}

void Hooks::InvokeVanillaConfirm() {
    transactionApproved = true;
}

RE::BSEventNotifyControl BarterMenuEventSink::ProcessEvent(
    const RE::MenuOpenCloseEvent* a_event,
    RE::BSTEventSource<RE::MenuOpenCloseEvent>*) {

    if (!a_event) return RE::BSEventNotifyControl::kContinue;
    if (!Settings::GetSingleton()->modEnabled) return RE::BSEventNotifyControl::kContinue;

    if (a_event->menuName == RE::BarterMenu::MENU_NAME) {
        if (a_event->opening) {
            BarterManager::GetSingleton()->OnBarterOpen();
        } else {
            Hooks::interceptingTransaction = false;
            Hooks::transactionApproved = false;
            Hooks::lastSelectedItem = nullptr;
            BarterManager::GetSingleton()->OnBarterClose();
        }
    }

    return RE::BSEventNotifyControl::kContinue;
}
