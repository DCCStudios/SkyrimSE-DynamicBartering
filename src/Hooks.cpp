#include "PCH.h"
#include "Hooks.h"
#include "BarterManager.h"
#include "CartManager.h"
#include "Settings.h"
#include "UI/ScaleformUI.h"
#include "UI/BarterCartMenu.h"

// Lightweight debug log gated behind Settings::debugLogging so per-frame polling of
// the highlighted item doesn't spam the log. Implemented as a macro because the SKSE
// logger captures std::source_location via deduction guides and cannot be forwarded
// through a wrapper template.
#define DbgLog(...)                                            \
    do {                                                       \
        if (Settings::GetSingleton()->debugLogging) {          \
            logger::info(__VA_ARGS__);                         \
        }                                                      \
    } while (0)

namespace {
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

    // Helper: read the highlighted item's details from the open BarterMenu.
    // Returns false if nothing is highlighted or data can't be resolved.
    struct HighlightedItemInfo {
        RE::FormID formID = 0;
        int count = 1;
        bool isBuying = true;
        int marketUnitPrice = 0;
        std::string name;
        bool stolen = false;
    };

    bool DetermineIsBuying(RE::BarterMenu* menu);  // defined below

    bool ReadHighlightedItem(RE::BarterMenu* menu, HighlightedItemInfo& out) {
        if (!menu) { return false; }
        auto* player = RE::PlayerCharacter::GetSingleton();
        if (!player) { return false; }

        auto& runtimeData = menu->GetRuntimeData();
        auto* itemList = runtimeData.itemList;
        if (!itemList) { return false; }

        // Try GetSelectedItem first (works for mouse clicks)
        auto* selectedItem = itemList->GetSelectedItem();

        // If null, read the highlighted index from Flash (works for gamepad D-pad navigation)
        if (!selectedItem) {
            RE::GFxValue selectedIdx;
            if (itemList->root.GetMember("selectedIndex", &selectedIdx) && selectedIdx.IsNumber()) {
                int idx = static_cast<int>(selectedIdx.GetNumber());
                if (idx >= 0 && idx < static_cast<int>(itemList->items.size())) {
                    selectedItem = itemList->items[idx];
                    DbgLog("ReadHighlightedItem: Used Flash selectedIndex={}", idx);
                }
            }
            // Still null? Try the entryList's selectedIndex via movie variable
            if (!selectedItem && menu->uiMovie) {
                RE::GFxValue listObj;
                if (menu->uiMovie->GetVariable(&listObj, "_root.Menu_mc.inventoryLists.itemList")) {
                    RE::GFxValue idxVal;
                    if (listObj.GetMember("selectedIndex", &idxVal) && idxVal.IsNumber()) {
                        int idx = static_cast<int>(idxVal.GetNumber());
                        if (idx >= 0 && idx < static_cast<int>(itemList->items.size())) {
                            selectedItem = itemList->items[idx];
                            DbgLog("ReadHighlightedItem: Used movie var selectedIndex={}", idx);
                        }
                    }
                }
            }
        }

        if (!selectedItem) { return false; }
        if (!selectedItem->data.objDesc) { return false; }

        auto* entryData = selectedItem->data.objDesc;
        RE::TESBoundObject* boundObj = entryData->object;
        if (!boundObj) { return false; }

        out.formID = boundObj->GetFormID();
        out.name = boundObj->GetName() ? boundObj->GetName() : "Unknown";

        // Determine buy/sell from the active category. Vanilla BarterMenu computes
        // IsViewingVendorItems() as: iSelectedCategory < CategoriesList.dividerIndex
        // (categories before the divider are the vendor's = buying; after = the
        // player's = selling). We read the live values off Menu_mc.
        out.isBuying = DetermineIsBuying(menu);

        // Get unit price from the item card (post buy/sell multiplier)
        int unitValue = 0;
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
        if (unitValue <= 0) return false;

        out.marketUnitPrice = unitValue;

        // Count: use the entry's count for stacks
        out.count = (entryData->countDelta > 0) ? entryData->countDelta : 1;

        out.stolen = !entryData->IsOwnedBy(player, true);
        return true;
    }

    // Reads a numeric member from a GFxValue object (0.0 if absent/non-numeric).
    double GfxNum(RE::GFxValue& obj, const char* key) {
        RE::GFxValue v;
        return (obj.GetMember(key, &v) && v.IsNumber()) ? v.GetNumber() : 0.0;
    }

    // --- Buy/sell direction ----------------------------------------------------
    // Primary: call the menu's own AS function isViewingVendorItems() on Menu_mc.
    // This is the same source of truth the engine uses (vanilla CommonLib's
    // BarterMenu::IsViewingVendorItems() just invokes this AS method), and it
    // works regardless of skin (vanilla vs SkyUI), unlike probing member paths
    // such as InventoryLists_mc/inventoryLists which differ between skins.
    bool DetermineIsBuying(RE::BarterMenu* menu) {
        if (!menu) return true;
        auto& rd = menu->GetRuntimeData();
        RE::GFxValue root = rd.root;  // Menu_mc
        if (!root.IsObject()) return true;

        // Try a few casings; vanilla exposes "isViewingVendorItems".
        for (const char* fn : { "isViewingVendorItems", "IsViewingVendorItems" }) {
            RE::GFxValue res;
            if (root.Invoke(fn, &res, nullptr, 0)) {
                if (res.IsBool()) { return res.GetBool(); }
                if (res.IsNumber()) { return res.GetNumber() != 0.0; }
            }
        }

        // Fallback: category divider. Vendor categories come before the divider
        // (buying); player categories after (selling). Member name differs by skin.
        RE::GFxValue invLists, catList;
        bool haveLists =
            (root.GetMember("InventoryLists_mc", &invLists) && invLists.IsObject()) ||
            (root.GetMember("inventoryLists", &invLists) && invLists.IsObject());
        if (haveLists && invLists.GetMember("CategoriesList", &catList) && catList.IsObject()) {
            int sel = -1, divider = -1;
            RE::GFxValue v;
            if (catList.GetMember("selectedIndex", &v) && v.IsNumber()) sel = static_cast<int>(v.GetNumber());
            if (root.GetMember("iSelectedCategory", &v) && v.IsNumber()) sel = static_cast<int>(v.GetNumber());
            if (catList.GetMember("dividerIndex", &v) && v.IsNumber()) divider = static_cast<int>(v.GetNumber());
            if (sel >= 0 && divider >= 0) return sel < divider;
        }
        return true;  // safe default: buying
    }

    // Resolve the on-screen renderer clip for the selected row.
    //
    // Confirmed from the loaded SkyUI (Dragonborn UI) inventorylists.swf:
    // skyui.components.list.BasicList exposes a 'selectedClip' getter that returns
    // entryClipManager.getClip(selectedEntry.clipIndex). NOTE: getClipByIndex takes
    // the *visible* clip index (0..maxVisible), NOT the absolute selectedIndex --
    // which is why getClipByIndex(selectedIndex) always failed for scrolled lists.
    bool ResolveRowClip(RE::GFxValue& listClip, RE::GFxValue& out) {
        // Primary: SkyUI 'selectedClip' getter (handles scroll offset internally).
        if (listClip.GetMember("selectedClip", &out) && out.IsDisplayObject()) {
            return true;
        }
        // Fallback: getClipByIndex(visibleClipIndex). The visible index lives on the
        // selected entry (clipIndex) and is mirrored in _curClipIndex.
        int vis = -1;
        RE::GFxValue se, ci;
        if (listClip.GetMember("selectedEntry", &se) && se.IsObject() &&
            se.GetMember("clipIndex", &ci) && ci.IsNumber()) {
            vis = static_cast<int>(ci.GetNumber());
        } else if (listClip.GetMember("_curClipIndex", &ci) && ci.IsNumber()) {
            vis = static_cast<int>(ci.GetNumber());
        }
        if (vis >= 0) {
            RE::GFxValue arg; arg.SetNumber(static_cast<double>(vis));
            if (listClip.Invoke("getClipByIndex", &out, &arg, 1) && out.IsDisplayObject()) {
                return true;
            }
        }
        return false;
    }

    // Computes the _root-space point just to the right of the selected row's name
    // text. Returns false if the row/text geometry can't be resolved.
    bool ComputeRowNameEnd(RE::GFxMovieView* movie, RE::GFxValue& entryClip, double& outX, double& outY) {
        if (!movie) return false;
        RE::GFxValue tf;
        // SkyUI TabularList names the first text column "textField0", which is the
        // item-name column (icons use "itemIcon"/"equipIcon" and don't take a slot).
        // Fall back to other common names for non-SkyUI skins.
        bool haveTf = entryClip.GetMember("textField0", &tf) && tf.IsObject();
        if (!haveTf) {
            for (const char* n : { "textField", "ItemName_tf", "itemName", "TextField", "label" }) {
                if (entryClip.GetMember(n, &tf) && tf.IsObject()) { haveTf = true; break; }
            }
        }
        if (!haveTf) {
            DbgLog("ComputeRowNameEnd: no name textField on row clip");
            return false;
        }

        double tx = GfxNum(tf, "_x");
        double tw = GfxNum(tf, "textWidth");
        if (tw <= 0.0) tw = GfxNum(tf, "_width");
        double ty = GfxNum(tf, "_y");
        double th = GfxNum(tf, "_height");
        if (th <= 0.0) th = 24.0;
        DbgLog("ComputeRowNameEnd: tx={} textWidth={} ty={} th={}", tx, tw, ty, th);

        RE::GFxValue pt;
        movie->CreateObject(&pt);
        RE::GFxValue px, py;
        px.SetNumber(tx + tw + 12.0);
        py.SetNumber(ty + th * 0.5);
        pt.SetMember("x", px);
        pt.SetMember("y", py);

        // entry-local -> global -> _root-local
        entryClip.Invoke("localToGlobal", nullptr, &pt, 1);
        RE::GFxValue rootVal;
        if (movie->GetVariable(&rootVal, "_root") && rootVal.IsObject()) {
            rootVal.Invoke("globalToLocal", nullptr, &pt, 1);
        }
        RE::GFxValue ox, oy;
        if (!pt.GetMember("x", &ox) || !pt.GetMember("y", &oy) || !ox.IsNumber() || !oy.IsNumber()) {
            return false;
        }
        outX = ox.GetNumber();
        // DBPrompt's glyph button and "Barter" label both center at local y~=10
        // (kPromptH*0.5), so subtract that to line the prompt up with the name's
        // vertical midpoint instead of sitting above it.
        outY = oy.GetNumber() - 10.0;
        return true;
    }

    // Per-frame prompt placement + visibility, written into Hooks:: statics.
    // Mouse: follows the cursor but only while actually hovering the selected row.
    // Gamepad: anchored just right of the highlighted item's name.
    void ComputePromptState(RE::BarterMenu* menu) {
        static double s_lastMouseX = -1.0e9;
        static double s_lastMouseY = -1.0e9;

        Hooks::promptShow = false;
        if (!menu || !menu->uiMovie) return;
        auto* movie = menu->uiMovie.get();
        auto& rd = menu->GetRuntimeData();
        if (!rd.itemList) return;
        RE::GFxValue listClip = rd.itemList->root;
        if (!listClip.IsObject()) return;

        // --- Robust device mode: cursor movement wins, else fall back to the sink.
        // This recovers correctly after alt-tab, where mouse-move events stop
        // reaching the InputDeviceSink but _root._xmouse still tracks the cursor.
        double mx = 0.0, my = 0.0;
        bool haveMouse = false;
        {
            RE::GFxValue vx, vy;
            if (movie->GetVariable(&vx, "_root._xmouse") && movie->GetVariable(&vy, "_root._ymouse") &&
                vx.IsNumber() && vy.IsNumber()) {
                mx = vx.GetNumber();
                my = vy.GetNumber();
                haveMouse = true;
            }
        }
        bool mouseMoved = haveMouse &&
            (std::abs(mx - s_lastMouseX) > 0.5 || std::abs(my - s_lastMouseY) > 0.5);
        if (haveMouse) { s_lastMouseX = mx; s_lastMouseY = my; }
        if (mouseMoved) {
            Hooks::promptGamepad = false;
        } else if (InputDeviceSink::GetSingleton()->IsUsingGamepad()) {
            Hooks::promptGamepad = true;
        }

        if (!Hooks::itemHighlighted) return;

        int selIdx = -1;
        RE::GFxValue v;
        if (listClip.GetMember("selectedIndex", &v) && v.IsNumber()) selIdx = static_cast<int>(v.GetNumber());
        if (selIdx < 0) return;

        // The selected ROW's on-screen renderer clip (SkyUI 'selectedClip' getter).
        RE::GFxValue entryClip;
        bool haveRow = ResolveRowClip(listClip, entryClip);

        if (Hooks::promptGamepad) {
            double px = 0.0, py = 0.0;
            if (haveRow && ComputeRowNameEnd(movie, entryClip, px, py)) {
                Hooks::promptShow = true;
                Hooks::promptX = static_cast<float>(px);
                Hooks::promptY = static_cast<float>(py);
                DbgLog("Gamepad prompt: anchored next to name at ({}, {})", px, py);
            } else {
                // Couldn't resolve the row geometry: anchor near the bottom info bar.
                Hooks::promptShow = true;
                Hooks::promptX = 250.0f;
                Hooks::promptY = 648.0f;
                static int s_lastFailSel = -999;
                if (selIdx != s_lastFailSel) {  // log once per selection, not per frame
                    s_lastFailSel = selIdx;
                    DbgLog("Gamepad prompt: row geometry unavailable (selIdx={}), fallback anchor", selIdx);
                }
            }
            return;
        }

        // --- Mouse: only show while the cursor is actually over the selected row.
        if (haveRow && haveMouse) {
            bool over = false;
            RE::GFxValue rootVal;
            if (movie->GetVariable(&rootVal, "_root") && rootVal.IsObject()) {
                RE::GFxValue pt;
                movie->CreateObject(&pt);
                RE::GFxValue px, py;
                px.SetNumber(mx);
                py.SetNumber(my);
                pt.SetMember("x", px);
                pt.SetMember("y", py);
                rootVal.Invoke("localToGlobal", nullptr, &pt, 1);
                RE::GFxValue gx, gy;
                if (pt.GetMember("x", &gx) && pt.GetMember("y", &gy) && gx.IsNumber() && gy.IsNumber()) {
                    RE::GFxValue htArgs[3];
                    htArgs[0].SetNumber(gx.GetNumber());
                    htArgs[1].SetNumber(gy.GetNumber());
                    htArgs[2].SetBoolean(true);
                    RE::GFxValue htRet;
                    if (entryClip.Invoke("hitTest", &htRet, htArgs, 3) && htRet.IsBool()) {
                        over = htRet.GetBool();
                    }
                }
            }
            if (over) {
                Hooks::promptShow = true;
                Hooks::promptX = static_cast<float>(mx + 22.0);
                Hooks::promptY = static_cast<float>(my - 6.0);
            }
            return;
        }

        // Degrade gracefully: if row geometry is unavailable, follow the cursor.
        if (haveMouse) {
            Hooks::promptShow = true;
            Hooks::promptX = static_cast<float>(mx + 22.0);
            Hooks::promptY = static_cast<float>(my - 6.0);
        }
    }

    // Confirmation popup shown when the player tries to quick buy/sell an item that
    // is already sitting in the barter cart. Button index maps:
    //   0 = open the cart offer window, 1 = quick buy/sell this item (remove from
    //   cart), 2 = cancel (do nothing).
    class CartItemChoiceCallback : public RE::IMessageBoxCallback {
    public:
        RE::FormID formID = 0;
        int count = 1;
        bool isBuying = true;
        int unitPrice = 0;

        void Run(Message a_msg) override {
            const auto idx = static_cast<std::uint32_t>(a_msg);
            if (idx == 0) {
                if (!CartManager::GetSingleton()->IsEmpty()) {
                    BarterManager::GetSingleton()->StartCartOffer();
                }
            } else if (idx == 1) {
                // Remove from the cart (either direction) then do the market transaction,
                // applying the same chance-based standing change + vanilla notification.
                CartManager::GetSingleton()->Remove(formID, true);
                CartManager::GetSingleton()->Remove(formID, false);
                BarterManager::GetSingleton()->ApplyQuickDealRelationship();
                BarterManager::GetSingleton()->QuickTransferMarket(formID, count, isBuying, unitPrice);
            }
            // idx == 2: cancel -> leave the cart untouched.
        }
    };

    void ShowCartItemPopup(const HighlightedItemInfo& info) {
        auto* factoryManager = RE::MessageDataFactoryManager::GetSingleton();
        auto* uiStr = RE::InterfaceStrings::GetSingleton();
        if (!factoryManager || !uiStr) return;
        auto* factory = factoryManager->GetCreator<RE::MessageBoxData>(uiStr->messageBoxData);
        if (!factory) return;
        auto* mbox = factory->Create();
        if (!mbox) return;

        auto* cb = new CartItemChoiceCallback();
        cb->formID = info.formID;
        cb->count = info.count;
        cb->isBuying = info.isBuying;
        cb->unitPrice = info.marketUnitPrice;
        mbox->callback = RE::BSTSmartPointer<RE::IMessageBoxCallback>(cb);

        mbox->bodyText = (info.name + " is already in your barter cart.").c_str();
        mbox->buttonText.push_back("Open Barter Offer");
        mbox->buttonText.push_back(info.isBuying ? "Buy Now (remove from cart)"
                                                 : "Sell Now (remove from cart)");
        mbox->buttonText.push_back("Cancel");
        mbox->cancelOptionIndex = 2;
        mbox->isCancellable = true;
        mbox->QueueMessage();
        DbgLog("Cart guard: popup for '{}' (in cart)", info.name);
    }
}

void Hooks::Install() {
    REL::Relocation<std::uintptr_t> barterVtbl{ RE::VTABLE_BarterMenu[0] };

    _AcceptBart = barterVtbl.write_vfunc(0x1, &AcceptBart);
    _PostCreateBart = barterVtbl.write_vfunc(0x2, &PostCreateBart);
    _ProcessMessageBart = barterVtbl.write_vfunc(0x4, &ProcessMessageBart);
    _AdvanceMovieBart = barterVtbl.write_vfunc(0x5, &AdvanceMovieBart);

    auto* ui = RE::UI::GetSingleton();
    if (ui) {
        ui->AddEventSink(BarterMenuEventSink::GetSingleton());
    }

    logger::info("Hooks installed (Accept + PostCreate + ProcessMessage + AdvanceMovie)");
}

void Hooks::AcceptBart(RE::BarterMenu* menu, RE::FxDelegateHandler::CallbackProcessor* a_cbReg) {
    ItemSelectProxy proxy;
    proxy.real = a_cbReg;
    _AcceptBart(menu, &proxy);
}

void Hooks::ItemSelectInterceptor(const RE::FxDelegateArgs& a_params) {
    // If WE triggered this (replaying a negotiated deal from TransferCart/TransferItemAndGold
    // or the quick-buy popup), forward straight to vanilla without re-intercepting.
    if (replayingItemSelect) {
        if (originalItemSelect) originalItemSelect(a_params);
        return;
    }

    auto* settings = Settings::GetSingleton();
    auto* mgr = BarterManager::GetSingleton();
    auto* cart = CartManager::GetSingleton();

    // Cart guard: activating (insta buy/sell) an item that's already in the cart is
    // ambiguous, so intercept and ask the player what they meant: open the cart
    // offer, quick buy/sell just this item (removing it from the cart), or cancel.
    if (settings->modEnabled && mgr->GetState() == BarterState::Idle && !cart->IsEmpty()) {
        auto* ui = RE::UI::GetSingleton();
        auto barterMenu = ui ? ui->GetMenu(RE::BarterMenu::MENU_NAME) : nullptr;
        if (barterMenu) {
            auto* menu = static_cast<RE::BarterMenu*>(barterMenu.get());
            HighlightedItemInfo info;
            if (ReadHighlightedItem(menu, info) && cart->Contains(info.formID)) {
                ShowCartItemPopup(info);
                return;  // block the vanilla insta-transaction until the player chooses
            }
        }
    }

    // Normal insta-buy/sell for items not in the cart. Apply the same chance-based
    // standing change a market-price deal grants in the offer window, and let it
    // surface via the vanilla notification system.
    if (mgr->GetState() == BarterState::Idle) {
        mgr->ApplyQuickDealRelationship();
    }
    if (originalItemSelect) originalItemSelect(a_params);
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

    // While our offer UI is active, block player INPUT messages, but always let
    // rendering/visibility AND inventory-refresh messages reach the vanilla menu.
    // (The per-frame overlay + cart-input work lives in AdvanceMovieBart now.)
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
                logger::info("ProcessMessageBart: passing kInventoryUpdate through (state={})",
                    static_cast<int>(state));
            }
            return _ProcessMessageBart(menu, a_message);
        }
        return RE::UI_MESSAGE_RESULTS::kHandled;
    }

    // === IDLE STATE ===
    // Consume the gamepad 'YButton' UserEvent so vanilla doesn't act on it. The
    // actual cart toggle/hold is driven by the raw InputDeviceSink path in
    // AdvanceMovieBart (single path -> no double-toggle).
    auto msgType = a_message.type.get();
    if (msgType == RE::UI_MESSAGE_TYPE::kUserEvent) {
        auto* strData = static_cast<RE::BSUIMessageData*>(a_message.data);
        if (strData) {
            auto eventStr = strData->fixedStr;
            DbgLog("ProcessMessageBart idle kUserEvent: '{}'", eventStr.c_str());
            if (eventStr == "YButton") {
                return RE::UI_MESSAGE_RESULTS::kHandled;
            }
        }
    }

    // Let everything else through to vanilla (scrolling, tab swap, activate=buy, etc.)
    return _ProcessMessageBart(menu, a_message);
}

void Hooks::AdvanceMovieBart(RE::BarterMenu* menu, float a_interval, std::uint32_t a_currentTime) {
    auto* settings = Settings::GetSingleton();
    if (!settings->modEnabled || !menu || !menu->uiMovie) {
        _AdvanceMovieBart(menu, a_interval, a_currentTime);
        return;
    }

    auto* mgr = BarterManager::GetSingleton();
    const bool idle = mgr->GetState() == BarterState::Idle;

    if (!idle) {
        // Negotiating: hide the prompt (offer menu is on top), keep the cart panel
        // synced. No cart input while negotiating.
        Hooks::itemHighlighted = false;
        Hooks::promptShow = false;
        Hooks::cartHoldActive = false;
        Hooks::cartHoldTimer = 0.0f;
        Hooks::cartPendingTap = false;
        BarterCartMenu::Update(menu->uiMovie.get());
        _AdvanceMovieBart(menu, a_interval, a_currentTime);
        return;
    }

    // --- Idle: poll the highlighted item every frame (drives the prompt) ---
    {
        HighlightedItemInfo hinfo;
        Hooks::itemHighlighted = ReadHighlightedItem(menu, hinfo);
    }
    // Compute prompt visibility + placement (mouse hover vs gamepad row anchor).
    ComputePromptState(menu);

    // --- Cart input -----------------------------------------------------------
    // A press STARTS a hold (no toggle yet). Releasing before the threshold is a
    // TAP -> toggle the highlighted item. Holding past the threshold OPENS the
    // cart offer WITHOUT toggling, so holding-to-open never accidentally adds the
    // currently-highlighted item.
    auto* inputSink = InputDeviceSink::GetSingleton();
    const bool yPressed = inputSink->ConsumeY();
    const bool bPressed = inputSink->ConsumeB();

    if ((yPressed || bPressed) && !cartHoldActive) {
        DbgLog("Cart input press: Y={} B={}", yPressed, bPressed);
        cartHoldActive = true;
        cartHoldTimer = 0.0f;
        Hooks::cartPendingTap = true;  // becomes a tap if released before threshold
    }

    if (cartHoldActive) {
        const bool stillHeld = inputSink->IsUsingGamepad() ? inputSink->IsYHeld() : inputSink->IsBHeld();
        if (stillHeld) {
            cartHoldTimer += (a_interval > 0.0f && a_interval < 1.0f) ? a_interval : (1.0f / 60.0f);
            if (cartHoldTimer >= settings->cartHoldThreshold && Hooks::cartPendingTap) {
                // HOLD recognised -> open the cart offer (do NOT toggle on hold).
                Hooks::cartPendingTap = false;
                cartHoldActive = false;
                cartHoldTimer = 0.0f;
                auto* cart = CartManager::GetSingleton();
                // Convenience: holding directly on an item with an empty cart barters
                // just that item. With items already in the cart, hold opens the cart
                // without adding the highlighted item.
                if (cart->IsEmpty()) {
                    HighlightedItemInfo info;
                    if (ReadHighlightedItem(menu, info)) {
                        cart->Toggle(info.formID, info.count, info.isBuying,
                            info.marketUnitPrice, info.name, info.stolen);
                    }
                }
                if (!cart->IsEmpty()) {
                    logger::info("Cart hold complete: starting cart offer ({} items)", cart->Count());
                    mgr->StartCartOffer();
                }
            }
        } else {
            // Released. If it was quick (still pending), treat as a TAP -> toggle.
            if (Hooks::cartPendingTap) {
                HighlightedItemInfo info;
                if (ReadHighlightedItem(menu, info)) {
                    DbgLog("Cart tap: toggling '{}' ({})", info.name, info.isBuying ? "buy" : "sell");
                    CartManager::GetSingleton()->Toggle(
                        info.formID, info.count, info.isBuying,
                        info.marketUnitPrice, info.name, info.stolen);
                } else {
                    DbgLog("Cart tap: ReadHighlightedItem failed (no item highlighted?)");
                }
            }
            Hooks::cartPendingTap = false;
            cartHoldActive = false;
            cartHoldTimer = 0.0f;
        }
    }

    // Drive the injected overlay (prompt + cart panel + hold meter) every frame.
    BarterCartMenu::Update(menu->uiMovie.get());

    _AdvanceMovieBart(menu, a_interval, a_currentTime);
}

bool Hooks::CurrentSideIsBuying(RE::BarterMenu* menu) {
    return DetermineIsBuying(menu);
}

void Hooks::SwitchBarterSide(RE::BarterMenu* menu, bool buying) {
    if (!menu) return;
    auto& rd = menu->GetRuntimeData();
    RE::GFxValue root = rd.root;
    if (!root.IsObject()) return;

    RE::GFxValue invLists;
    if (!root.GetMember("inventoryLists", &invLists) || !invLists.IsObject()) {
        if (!root.GetMember("InventoryLists_mc", &invLists) || !invLists.IsObject()) return;
    }

    auto* movie = menu->uiMovie.get();

    // The buy/sell side in SkyUI is the category list's "segment" (LEFT=0 buy,
    // RIGHT=1 sell), NOT a plain selectedIndex. isViewingVendorItems() reads
    // categoryList.activeSegment, and items from the other side are filtered out
    // of the enumeration entirely, so a row can't even be selected until the
    // segment is switched. InventoryLists.onTabPress({index}) is exactly what the
    // vanilla "switch tab" input runs: it flips the tab bar, sets activeSegment,
    // and rebuilds the item list for that side.
    bool switched = false;
    if (movie) {
        RE::GFxValue evt;
        movie->CreateObject(&evt);
        if (evt.IsObject()) {
            RE::GFxValue idx;
            idx.SetNumber(buying ? 0.0 : 1.0);  // TabBar LEFT_TAB=buy, RIGHT_TAB=sell
            evt.SetMember("index", idx);
            RE::GFxValue res;
            switched = invLists.Invoke("onTabPress", &res, &evt, 1);
        }
    }

    // Fallback for skins without onTabPress: drive the segment setter directly.
    if (!switched) {
        RE::GFxValue catList;
        if (!invLists.GetMember("categoryList", &catList) || !catList.IsObject()) {
            invLists.GetMember("CategoriesList", &catList);
        }
        if (catList.IsObject()) {
            RE::GFxValue seg; seg.SetNumber(buying ? 0.0 : 1.0);
            catList.SetMember("activeSegment", seg);
            invLists.Invoke("showItemsList", nullptr, nullptr, 0);
        }
    }

    // Resync the RE item array (entryList spans both sides; this is harmless).
    if (rd.itemList) rd.itemList->Update();
    DbgLog("SwitchBarterSide -> {} (onTabPress={})", buying ? "buy" : "sell", switched);
}

bool Hooks::SelectCartItem(RE::BarterMenu* menu, RE::FormID formID, bool isBuying) {
    if (!menu) return false;
    auto& rd = menu->GetRuntimeData();
    auto* il = rd.itemList;
    if (!il) return false;

    // Make sure we're on the right side first: items from the other side are
    // filtered out of the list enumeration, so doSetSelectedIndex() will silently
    // reject selecting them (the selection simply won't "take").
    if (DetermineIsBuying(menu) != isBuying) return false;

    // entryList spans BOTH sides, so the same form can appear twice (e.g. both the
    // merchant and the player carry iron arrows). selectedIndex indexes entryList
    // directly (BSList.selectedEntry == entryList[selectedIndex]), so we try every
    // matching row and keep the one whose selection actually commits on this side.
    for (std::size_t i = 0; i < il->items.size(); ++i) {
        auto* it = il->items[i];
        if (!it || !it->data.objDesc || !it->data.objDesc->object) continue;
        if (it->data.objDesc->object->GetFormID() != formID) continue;

        RE::GFxValue sv; sv.SetNumber(static_cast<double>(i));
        il->root.SetMember("selectedIndex", sv);

        // Verify the selection committed to this exact row. doSetSelectedIndex
        // rejects rows that aren't in the current (this-side) enumeration, so a
        // matching read-back here also confirms we're transacting the right side.
        auto* sel = il->GetSelectedItem();
        if (sel && sel->data.objDesc && sel->data.objDesc->object &&
            sel->data.objDesc->object->GetFormID() == formID) {
            return true;
        }
    }
    return false;
}

RE::BSEventNotifyControl BarterMenuEventSink::ProcessEvent(
    const RE::MenuOpenCloseEvent* a_event,
    RE::BSTEventSource<RE::MenuOpenCloseEvent>*) {

    if (!a_event) return RE::BSEventNotifyControl::kContinue;
    if (!Settings::GetSingleton()->modEnabled) return RE::BSEventNotifyControl::kContinue;

    if (a_event->menuName == RE::BarterMenu::MENU_NAME) {
        if (a_event->opening) {
            BarterManager::GetSingleton()->OnBarterOpen();
            CartManager::GetSingleton()->Clear();
            // Cart overlay is injected into the BarterMenu's own movie on first
            // kUpdate (it cannot composite as a separate menu). Just reset state.
            BarterCartMenu::OnBarterOpen();
        } else {
            Hooks::cartHoldActive = false;
            Hooks::cartHoldTimer = 0.0f;
            Hooks::cartPendingTap = false;
            Hooks::itemHighlighted = false;
            Hooks::promptShow = false;
            CartManager::GetSingleton()->Clear();
            BarterCartMenu::OnBarterClose();
            BarterManager::GetSingleton()->OnBarterClose();
        }
    }

    return RE::BSEventNotifyControl::kContinue;
}
