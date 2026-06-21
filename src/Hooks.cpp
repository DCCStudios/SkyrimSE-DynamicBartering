#include "PCH.h"
#include "Hooks.h"
#include "BarterManager.h"
#include "Settings.h"

namespace {
    // Proxy callback-registrar handed to the vanilla BarterMenu::Accept. It forwards
    // every registration to the real registrar untouched EXCEPT "ItemSelect", whose
    // handler we swap for our own (capturing the original so we can replay it with a
    // negotiated price). Lives only for the duration of the Accept call.
    class ItemSelectProxy : public RE::FxDelegateHandler::CallbackProcessor {
    public:
        RE::FxDelegateHandler::CallbackProcessor* real = nullptr;

        void Process(const RE::GString& a_methodName, RE::FxDelegateHandler::CallbackFn* a_method) override {
            if (a_methodName == "ItemSelect") {
                Hooks::originalItemSelect = a_method;
                real->Process(a_methodName, &Hooks::ItemSelectInterceptor);
                logger::info("Captured vanilla ItemSelect callback; installed interceptor");
            } else {
                real->Process(a_methodName, a_method);
            }
        }
    };
}

void Hooks::Install() {
    REL::Relocation<std::uintptr_t> barterVtbl{ RE::VTABLE_BarterMenu[0] };

    _AcceptBart = barterVtbl.write_vfunc(0x1, &AcceptBart);
    _PostCreateBart = barterVtbl.write_vfunc(0x2, &PostCreateBart);
    _ProcessMessageBart = barterVtbl.write_vfunc(0x4, &ProcessMessageBart);

    auto* ui = RE::UI::GetSingleton();
    if (ui) {
        ui->AddEventSink(BarterMenuEventSink::GetSingleton());
    }

    logger::info("Hooks installed (Accept + PostCreate + ProcessMessage)");
}

void Hooks::AcceptBart(RE::BarterMenu* menu, RE::FxDelegateHandler::CallbackProcessor* a_cbReg) {
    ItemSelectProxy proxy;
    proxy.real = a_cbReg;
    // Vanilla Accept registers all its GameDelegate callbacks through our proxy,
    // letting us substitute the ItemSelect handler before they hit the FxDelegate.
    _AcceptBart(menu, &proxy);
}

void Hooks::ItemSelectInterceptor(const RE::FxDelegateArgs& a_params) {
    // If WE triggered this (replaying a negotiated deal), go straight to vanilla.
    if (replayingItemSelect) {
        if (originalItemSelect) originalItemSelect(a_params);
        return;
    }

    auto* settings = Settings::GetSingleton();
    auto* mgr = BarterManager::GetSingleton();

    auto passThrough = [&]() {
        if (originalItemSelect) originalItemSelect(a_params);
    };

    // Disabled, no active barter session, or already negotiating -> vanilla behaviour.
    if (!settings->modEnabled || !mgr->IsBarterActive() || mgr->GetState() != BarterState::Idle) {
        passThrough();
        return;
    }

    // Cooldown: ignore ItemSelect for a brief period after the last negotiation
    // ended to prevent the "quick open/close" glitch from stale inputs.
    // BLOCK entirely (don't pass to vanilla) to avoid accidental purchases.
    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - lastNegotiationEnd).count();
    if (elapsed < 400) {
        logger::info("ItemSelectInterceptor: Cooldown active ({}ms since last negotiation), blocking", elapsed);
        return;
    }

    // Vanilla ItemSelect args (FxDelegate strips the response id): [amount, unitValue, isVendor].
    const std::uint32_t argc = a_params.GetArgCount();
    int amount = (argc > 0 && a_params[0].IsNumber()) ? static_cast<int>(a_params[0].GetNumber()) : 1;
    int unitValue = (argc > 1 && a_params[1].IsNumber()) ? static_cast<int>(a_params[1].GetNumber()) : 0;
    bool isBuying = (argc > 2) ? a_params[2].GetBool() : true;  // IsViewingVendorItems == buying
    if (amount < 1) amount = 1;

    auto* menu = static_cast<RE::BarterMenu*>(a_params.GetHandler());
    auto* player = RE::PlayerCharacter::GetSingleton();
    if (!menu || !player) { passThrough(); return; }

    auto& runtimeData = menu->GetRuntimeData();
    auto* itemList = runtimeData.itemList;
    auto* selectedItem = itemList ? itemList->GetSelectedItem() : nullptr;
    if (!selectedItem || !selectedItem->data.objDesc) { passThrough(); return; }

    auto* entryData = selectedItem->data.objDesc;
    RE::TESBoundObject* boundObj = entryData->object;
    if (!boundObj) { passThrough(); return; }

    // Unit value should come from the card (post buy/sell multiplier). Fall back to
    // the list entry's value if the arg was missing.
    if (unitValue <= 0) {
        RE::GFxValue infoVal;
        if (selectedItem->obj.GetMember("infoValue", &infoVal) && infoVal.IsNumber()) {
            unitValue = static_cast<int>(infoVal.GetNumber());
        } else {
            RE::GFxValue valMember;
            if (selectedItem->obj.GetMember("value", &valMember) && valMember.IsNumber()) {
                unitValue = static_cast<int>(valMember.GetNumber());
            }
        }
        if (unitValue <= 0) unitValue = entryData->GetValue();
    }

    int totalMarket = unitValue * amount;
    if (totalMarket <= 0) { passThrough(); return; }

    if (settings->skipBelowThreshold && totalMarket < settings->valueThreshold) {
        passThrough();
        return;
    }

    bool isStolen = !entryData->IsOwnedBy(player, true);

    lastSelectedItem = selectedItem;
    interceptingTransaction = true;

    const char* itemName = boundObj->GetName() ? boundObj->GetName() : "Unknown";
    logger::info("Intercepted ItemSelect: {} x{} (unit: {}, total: {}, {})",
        itemName, amount, unitValue, totalMarket, isBuying ? "buying" : "selling");

    // Open negotiation for the whole selected quantity. We deliberately DO NOT call
    // the vanilla handler now; the deal completes (or is abandoned) from the UI.
    mgr->StartOffer(boundObj, totalMarket, isBuying, isStolen, amount);
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

    // While our offer UI is active, block player INPUT messages, but always let
    // rendering/visibility AND inventory-refresh messages reach the vanilla menu.
    //
    // kInventoryUpdate (8) is critical: the vanilla BarterMenu rebuilds its item
    // lists AND recomputes the player/vendor gold totals (then drives the
    // UpdatePlayerInfo / UpdateItemCardInfo AS callbacks) inside ProcessMessage
    // when it receives this message. The engine posts it after our negotiated
    // ItemSelect transaction. If we swallow it here, the displayed inventory and
    // gold go stale until the menu is reopened, and the stale item list makes
    // ItemList::GetSelectedItem() return null afterwards, which silently breaks
    // selecting any further items. So we must let it through regardless of state.
    auto state = mgr->GetState();
    if (state != BarterState::Idle) {
        auto msgType = a_message.type.get();
        if (msgType == RE::UI_MESSAGE_TYPE::kUpdate ||
            msgType == RE::UI_MESSAGE_TYPE::kShow ||
            msgType == RE::UI_MESSAGE_TYPE::kReshow ||
            msgType == RE::UI_MESSAGE_TYPE::kHide ||
            msgType == RE::UI_MESSAGE_TYPE::kForceHide ||
            msgType == RE::UI_MESSAGE_TYPE::kInventoryUpdate) {
            if (msgType == RE::UI_MESSAGE_TYPE::kInventoryUpdate && settings->debugLogging) {
                logger::info("ProcessMessageBart: passing kInventoryUpdate through to vanilla (state={}) - live refresh",
                    static_cast<int>(state));
            }
            return _ProcessMessageBart(menu, a_message);
        }
        return RE::UI_MESSAGE_RESULTS::kHandled;
    }

    // When idle, the barter window is NOT open: let every input flow to vanilla
    // normally (scrolling, tab/inventory swap, activating items). The negotiation is
    // opened from ItemSelectInterceptor, which fires only on an actual buy/sell — so
    // tab/name-swap clicks no longer trigger it, and stacked items go through the
    // Quantity Menu first, then open the window for the chosen amount.
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
