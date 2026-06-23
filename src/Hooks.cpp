#include "PCH.h"
#include "Hooks.h"
#include "BarterManager.h"
#include "CartManager.h"
#include "Settings.h"
#include "UI/ScaleformUI.h"
#include "UI/BarterCartMenu.h"
#include "Integration/ChimBridge.h"
#include "DebugLog.h"

namespace {
    class ItemSelectProxy : public RE::FxDelegateHandler::CallbackProcessor {
    public:
        RE::FxDelegateHandler::CallbackProcessor* real = nullptr;

        void Process(const RE::GString& a_methodName, RE::FxDelegateHandler::CallbackFn* a_method) override {
            if (a_methodName == "ItemSelect") {
                Hooks::originalItemSelect = a_method;
                real->Process(a_methodName, &Hooks::ItemSelectInterceptor);
                DbgLog("Captured vanilla ItemSelect callback; installed interceptor");
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

    // A cart add/remove that the vanilla item-select kicked off but that we haven't
    // committed yet. Only a TAP of activate/accept commits it; holding activate/mouse
    // past the tap window discards it (so a hold never adds/removes). Driven per-frame
    // from AdvanceMovieBart.
    struct PendingCartAdd {
        bool       active = false;
        float      elapsed = 0.0f;
        RE::FormID formID = 0;
        int        amount = 1;
        int        availCount = 1;  // how many are available in the source stack
        bool       isBuying = true;
        int        unitPrice = 0;
        std::string name;
        bool       stolen = false;
    };
    PendingCartAdd g_pendingAdd;

    bool DetermineIsBuying(RE::BarterMenu* menu);  // defined below

    // Force the vanilla stackable quantity slider for the highlighted item, bypassing
    // SkyUI's quantityMenu.minCount threshold. On confirm the itemCard dispatches
    // "quantitySelect" -> BarterMenu.onQuantityMenuSelect -> doTransaction -> ItemSelect,
    // which ItemSelectInterceptor reroutes into the cart. Returns false if the menu
    // couldn't be driven (caller should fall back to a direct full-stack add).
    bool TriggerCartQuantityMenu(RE::BarterMenu* menu, int count) {
        if (!menu || !menu->uiMovie || count < 2) { return false; }
        auto* movie = menu->uiMovie.get();
        RE::GFxValue itemCard;
        if (!movie->GetVariable(&itemCard, "_root.Menu_mc.ItemCard_mc") || !itemCard.IsObject()) {
            return false;
        }
        RE::GFxValue arg; arg.SetNumber(static_cast<double>(count));
        RE::GFxValue res;
        return itemCard.Invoke("ShowQuantityMenu", &res, &arg, 1);
    }

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

    // Pure-DLL wrapper around the BarterMenu's Scaleform "UpdateItemCardInfo" function.
    // It multiplies the item card's displayed `value` by the merchant's current
    // relationship/personality price multiplier (buy vs sell), then chains to the
    // original function - so the vanilla price number reflects standing and still
    // composes with other price mods (DPF / DynamicPrices) that wrap the same call.
    // Display-only: the mod's own negotiation reads the raw list-row value and applies
    // the multiplier itself, so prices are never double-counted.
    class PriceCardWrapper : public RE::GFxFunctionHandler {
    public:
        explicit PriceCardWrapper(RE::GFxValue&& a_old) : oldFunc(std::move(a_old)) {}

        void Call(Params& a_params) override {
            auto* settings = Settings::GetSingleton();
            auto* mgr = BarterManager::GetSingleton();
            if (a_params.argCount >= 1 && settings->modEnabled &&
                settings->showRelationshipInVanillaPrices && mgr->GetCurrentMerchant()) {
                bool isBuying = true;
                if (a_params.thisPtr) {
                    RE::GFxValue res;
                    if (a_params.thisPtr->Invoke("isViewingVendorItems", &res)) {
                        if (res.IsBool()) isBuying = res.GetBool();
                        else if (res.IsNumber()) isBuying = res.GetNumber() != 0.0;
                    }
                }
                float mult = mgr->GetCurrentPriceMult(isBuying);
                if (mult != 1.0f) {
                    RE::GFxValue& updateObj = a_params.args[0];
                    if (updateObj.IsObject()) {
                        RE::GFxValue value;
                        if (updateObj.GetMember("value", &value) && value.IsNumber()) {
                            value.SetNumber(value.GetNumber() * static_cast<double>(mult));
                            updateObj.SetMember("value", value);
                        }
                    }
                }
            }
            // Chain to the original (or the next mod's wrapper) so other price mods keep working.
            oldFunc.Invoke("call", a_params.retVal, a_params.argsWithThisRef, a_params.argCount + 1);
        }

    private:
        RE::GFxValue oldFunc;
    };
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

    // Block Quick Buy: the vanilla item-select flow is allowed to run (so the
    // stackable quantity menu can appear), but instead of transacting we reroute it
    // into the cart using the amount the player chose (args = [amount, value, isVendor]).
    if (settings->modEnabled && settings->blockQuickBuy && mgr->GetState() == BarterState::Idle) {
        int amount = 1;
        if (a_params.GetArgCount() >= 1 && a_params[0].IsNumber()) {
            amount = static_cast<int>(a_params[0].GetNumber());
            if (amount < 1) amount = 1;
        }
        auto* ui = RE::UI::GetSingleton();
        auto bm = ui ? ui->GetMenu(RE::BarterMenu::MENU_NAME) : nullptr;
        if (bm) {
            auto* bMenu = static_cast<RE::BarterMenu*>(bm.get());
            HighlightedItemInfo info;
            if (ReadHighlightedItem(bMenu, info)) {
                // Defer the toggle: a quick tap commits it, a hold discards it (handled
                // per-frame in AdvanceMovieBart), so holding activate/mouse never
                // adds/removes from the cart.
                g_pendingAdd.active    = true;
                g_pendingAdd.elapsed   = 0.0f;
                g_pendingAdd.formID    = info.formID;
                g_pendingAdd.amount    = amount;
                g_pendingAdd.availCount = info.count;
                g_pendingAdd.isBuying  = info.isBuying;
                g_pendingAdd.unitPrice = info.marketUnitPrice;
                g_pendingAdd.name      = info.name;
                g_pendingAdd.stolen    = info.stolen;
                DbgLog("ItemSelectInterceptor: pending cart toggle '{}' x{} ({})",
                       info.name, amount, info.isBuying ? "buy" : "sell");
            } else {
                DbgLog("ItemSelectInterceptor: blockQuickBuy add failed (no highlight)");
            }
        }
        return;  // never transact while blocking quick buy
    }

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

    // Install the relationship-aware item-card price wrapper (display only; chains with
    // any other price mod that wraps UpdateItemCardInfo). The Call() body no-ops unless
    // the feature is enabled, so it's always safe to leave installed.
    if (menu && menu->uiMovie) {
        auto& root = menu->GetRuntimeData().root;
        if (root.IsObject()) {
            RE::GFxValue oldFunc;
            if (root.GetMember("UpdateItemCardInfo", &oldFunc) && oldFunc.IsObject()) {
                auto impl = RE::make_gptr<PriceCardWrapper>(std::move(oldFunc));
                RE::GFxValue newFunc;
                menu->uiMovie->CreateFunction(&newFunc, impl.get());
                root.SetMember("UpdateItemCardInfo", newFunc);
                DbgLog("Installed UpdateItemCardInfo price wrapper");
            }
        }
    }
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
                DbgLog("ProcessMessageBart: passing kInventoryUpdate through (state={})",
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
            // NB: under blockQuickBuy we deliberately let Accept/Activate flow to
            // vanilla so SkyUI's item-press (and the stackable quantity slider) runs
            // normally; the resulting ItemSelect is rerouted into the cart by
            // ItemSelectInterceptor instead of transacting.
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
        // synced. No cart input while negotiating. Drain any latched presses so they
        // can't fire a stray cart action the moment we return to Idle.
        Hooks::itemHighlighted = false;
        Hooks::promptShow = false;
        Hooks::cartHoldActive = false;
        Hooks::cartHoldTimer = 0.0f;
        Hooks::cartPendingTap = false;
        g_pendingAdd.active = false;  // drop any uncommitted add when leaving Idle
        auto* sink = InputDeviceSink::GetSingleton();
        sink->ConsumeY();
        sink->ConsumeB();
        sink->ConsumeActivatePress();
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
    const bool gamepad = inputSink->IsUsingGamepad();
    // Barter key (Y on gamepad, B on keyboard) always drives the cart.
    bool triggerPressed = gamepad ? inputSink->ConsumeY() : inputSink->ConsumeB();
    bool triggerHeld    = gamepad ? inputSink->IsYHeld() : inputSink->IsBHeld();

    // Under blockQuickBuy the Activate input (A / E / left-mouse) is left to the
    // vanilla item-select path so the stackable quantity slider can appear; the
    // resulting ItemSelect is rerouted into the cart by ItemSelectInterceptor. Drain
    // our latched copy so it can't double as a barter-key cart trigger.
    inputSink->ConsumeActivatePress();

    // Commit-or-discard a deferred cart add (queued by ItemSelectInterceptor). A quick
    // tap of activate/accept commits the toggle; holding activate/mouse past the tap
    // window discards it, so a HOLD never adds/removes from the cart.
    if (g_pendingAdd.active) {
        const float dt = (a_interval > 0.0f && a_interval < 1.0f) ? a_interval : (1.0f / 60.0f);
        g_pendingAdd.elapsed += dt;
        if (!inputSink->IsActivateHeld()) {
            auto* c = CartManager::GetSingleton();
            if (g_pendingAdd.availCount >= 2 && g_pendingAdd.availCount <= 5) {
                // SkyUI only opens the quantity slider for stacks larger than 5, so a
                // stack of 2-5 always commits amount=1 per tap. Build it one unit at a
                // time instead of letting ApplyQuantity subtract on the next tap
                // (0 -> 1 -> ... -> max -> 0).
                c->AddUnit(g_pendingAdd.formID, g_pendingAdd.isBuying, g_pendingAdd.unitPrice,
                    g_pendingAdd.name, g_pendingAdd.stolen, g_pendingAdd.availCount);
                DbgLog("Pending cart commit (tap, incremental): '{}'", g_pendingAdd.name);
            } else {
                // Quantity-aware commit: a fresh item is added with the chosen amount; an
                // item already in the cart has the chosen amount subtracted (selecting >=
                // what's in the cart removes it entirely).
                c->ApplyQuantity(g_pendingAdd.formID, g_pendingAdd.amount,
                    g_pendingAdd.isBuying, g_pendingAdd.unitPrice, g_pendingAdd.name, g_pendingAdd.stolen);
                DbgLog("Pending cart commit (tap): '{}' x{}", g_pendingAdd.name, g_pendingAdd.amount);
            }
            g_pendingAdd.active = false;
        } else if (g_pendingAdd.elapsed > settings->cartHoldThreshold) {
            DbgLog("Pending cart toggle discarded (hold): '{}'", g_pendingAdd.name);
            g_pendingAdd.active = false;
        }
    }

    // When "Block quick buy/sell" is on, the barter key is HOLD-ONLY: it never taps to
    // add/remove (the activate key owns that), and the hold meter fills immediately with
    // no tap-window delay. When the option is off, the barter key keeps its tap-to-toggle
    // behavior and the meter only starts after the tap window.
    const bool  barterAdds = !settings->blockQuickBuy;
    const float tapWindow  = barterAdds ? settings->cartHoldThreshold : 0.0f;

    if (triggerPressed && !cartHoldActive) {
        DbgLog("Cart input press (gamepad={}, barterAdds={})", gamepad, barterAdds);
        cartHoldActive = true;
        cartHoldTimer = 0.0f;
        Hooks::cartPendingTap = barterAdds;  // only a tap candidate when allowed to add
    }

    if (cartHoldActive) {
        const bool stillHeld = triggerHeld;
        if (stillHeld) {
            cartHoldTimer += (a_interval > 0.0f && a_interval < 1.0f) ? a_interval : (1.0f / 60.0f);
            const float fillTime  = settings->cartHoldFillTime;
            // Once we hold past the tap window, this is no longer a tap; the fill meter
            // (drawn from cartHoldTimer - tapWindow) now starts climbing. Releasing during
            // the fill phase simply cancels (no tap, no open).
            if (Hooks::cartPendingTap && cartHoldTimer >= tapWindow) {
                Hooks::cartPendingTap = false;
            }
            if (!Hooks::cartPendingTap && cartHoldTimer >= tapWindow + fillTime) {
                // HOLD complete -> open the cart offer (do NOT toggle on hold).
                cartHoldActive = false;
                cartHoldTimer = 0.0f;
                auto* cart = CartManager::GetSingleton();
                // Convenience: holding directly on an item with an empty cart barters
                // just that item. With items already in the cart, hold opens the cart
                // without adding the highlighted item. Skipped under Block quick buy/sell
                // since the barter key must not add anything there.
                if (barterAdds && cart->IsEmpty()) {
                    HighlightedItemInfo info;
                    if (ReadHighlightedItem(menu, info)) {
                        cart->Toggle(info.formID, info.count, info.isBuying,
                            info.marketUnitPrice, info.name, info.stolen);
                    }
                }
                if (!cart->IsEmpty()) {
                    DbgLog("Cart hold complete: starting cart offer ({} items)", cart->Count());
                    mgr->StartCartOffer();
                }
            }
        } else {
            // Released. If it was quick (still pending), treat as a TAP -> toggle.
            if (Hooks::cartPendingTap) {
                HighlightedItemInfo info;
                if (ReadHighlightedItem(menu, info)) {
                    auto* c = CartManager::GetSingleton();
                    // For a stackable item that isn't in the cart yet, force the vanilla
                    // quantity slider so the player can pick how many to add (the
                    // confirmed amount is rerouted into the cart by ItemSelectInterceptor).
                    // Already-in-cart taps just toggle it back off (no pointless slider).
                    // Skip the slider when the cart is full so Toggle below shows the
                    // "cart is full" prompt instead of opening a slider we'd then refuse.
                    if (settings->blockQuickBuy && info.count > 5 && !c->Contains(info.formID) &&
                        c->CanAccept(info.formID, info.isBuying) &&
                        TriggerCartQuantityMenu(menu, info.count)) {
                        DbgLog("Cart tap: opened quantity slider for '{}' (x{} avail)",
                               info.name, info.count);
                    } else if (info.count >= 2 && info.count <= 5 && c->CanAccept(info.formID, info.isBuying)) {
                        // SkyUI only opens the quantity slider for stacks larger than 5, so a
                        // stack of 2-5 builds one unit per tap instead of toggling the whole
                        // stack (0 -> 1 -> ... -> max -> 0).
                        DbgLog("Cart tap: incremental add for '{}' (stack of {})", info.name, info.count);
                        c->AddUnit(info.formID, info.isBuying, info.marketUnitPrice,
                                   info.name, info.stolen, info.count);
                    } else {
                        DbgLog("Cart tap: toggling '{}' ({})", info.name, info.isBuying ? "buy" : "sell");
                        c->Toggle(info.formID, info.count, info.isBuying,
                                  info.marketUnitPrice, info.name, info.stolen);
                    }
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
    } else if (a_event->menuName == RE::DialogueMenu::MENU_NAME && !a_event->opening) {
        // Leaving the conversation: now the world is fully back, so release any barter
        // summary that was held back while the dialogue menu was still open.
        ChimBridge::OnDialogueClosed();
    }

    return RE::BSEventNotifyControl::kContinue;
}
