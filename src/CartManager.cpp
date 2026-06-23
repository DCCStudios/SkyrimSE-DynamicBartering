#include "PCH.h"
#include "CartManager.h"
#include "BarterSounds.h"
#include "Settings.h"
#include "DebugLog.h"

namespace {
    // Cart add/remove feedback. With "use vanilla cart sounds" on, play the item's own
    // pickup/putdown cue (what a vanilla quick buy/sell would play): pickup when the item
    // is being acquired (buying+add or selling+remove), putdown otherwise. Falls back to
    // the mod's generic AddToCart/RemoveFromCart WAV when no item sound resolves.
    void PlayCartSound(bool add, RE::FormID formID, bool isBuying) {
        auto* s = Settings::GetSingleton();
        if (s && s->useVanillaCartSounds) {
            const bool pickup = (isBuying == add);
            if (BarterSounds::PlayVanillaItemSound(formID, pickup)) {
                return;
            }
        }
        BarterSounds::Play(add ? BarterSounds::Event::AddToCart
                               : BarterSounds::Event::RemoveFromCart);
    }
}

bool CartManager::Toggle(RE::FormID formID, int count, bool isBuying, int unitPrice,
                         const std::string& name, bool stolen) {
    auto it = std::find_if(entries.begin(), entries.end(), [&](const CartEntry& e) {
        return e.formID == formID && e.isBuying == isBuying;
    });

    if (it != entries.end()) {
        DbgLog("CartManager: Removed {} from cart ({})", it->name, isBuying ? "buy" : "sell");
        entries.erase(it);
        PlayCartSound(false, formID, isBuying);
        return true;
    }

    // New add: refuse once the cart is full and prompt the player to clear it.
    if (entries.size() >= kMaxItems) {
        DbgLog("CartManager: cart full ({} items), refused to add {}", kMaxItems, name);
        RE::DebugNotification(
            "Barter cart is full - complete the offer or remove items before adding more.");
        return false;
    }

    CartEntry entry;
    entry.formID = formID;
    entry.count = count;
    entry.isBuying = isBuying;
    entry.marketUnitPrice = unitPrice;
    entry.name = name;
    entry.stolen = stolen;
    entries.push_back(std::move(entry));
    DbgLog("CartManager: Added {} x{} to cart ({}, {}g each)",
        name, count, isBuying ? "buy" : "sell", unitPrice);
    PlayCartSound(true, formID, isBuying);
    return true;
}

bool CartManager::ApplyQuantity(RE::FormID formID, int amount, bool isBuying, int unitPrice,
                                const std::string& name, bool stolen) {
    if (amount < 1) amount = 1;
    auto it = std::find_if(entries.begin(), entries.end(), [&](const CartEntry& e) {
        return e.formID == formID && e.isBuying == isBuying;
    });

    if (it != entries.end()) {
        // Re-selecting an item already in the cart subtracts the chosen amount.
        if (amount >= it->count) {
            DbgLog("CartManager: Removed all of {} (selected {} >= {} in cart)",
                it->name, amount, it->count);
            entries.erase(it);
        } else {
            it->count -= amount;
            DbgLog("CartManager: Reduced {} by {} -> {} left in cart",
                it->name, amount, it->count);
        }
        PlayCartSound(false, formID, isBuying);
        return true;
    }

    // New add: refuse once the cart is full and prompt the player to clear it.
    if (entries.size() >= kMaxItems) {
        DbgLog("CartManager: cart full ({} items), refused to add {}", kMaxItems, name);
        RE::DebugNotification(
            "Barter cart is full - complete the offer or remove items before adding more.");
        return false;
    }

    CartEntry entry;
    entry.formID = formID;
    entry.count = amount;
    entry.isBuying = isBuying;
    entry.marketUnitPrice = unitPrice;
    entry.name = name;
    entry.stolen = stolen;
    entries.push_back(std::move(entry));
    DbgLog("CartManager: Added {} x{} to cart ({}, {}g each)",
        name, amount, isBuying ? "buy" : "sell", unitPrice);
    PlayCartSound(true, formID, isBuying);
    return true;
}

bool CartManager::AddUnit(RE::FormID formID, bool isBuying, int unitPrice,
                          const std::string& name, bool stolen, int stackMax) {
    if (stackMax < 1) stackMax = 1;
    auto it = std::find_if(entries.begin(), entries.end(), [&](const CartEntry& e) {
        return e.formID == formID && e.isBuying == isBuying;
    });

    if (it != entries.end()) {
        if (it->count < stackMax) {
            ++it->count;
            DbgLog("CartManager: Incremented {} -> {}/{} in cart ({})",
                it->name, it->count, stackMax, isBuying ? "buy" : "sell");
            PlayCartSound(true, formID, isBuying);
        } else {
            // Already holding the whole stack: the next tap clears it so the same
            // button can cycle the item back off without a quantity slider.
            DbgLog("CartManager: {} at stack max ({}), removed from cart", it->name, stackMax);
            entries.erase(it);
            PlayCartSound(false, formID, isBuying);
        }
        return true;
    }

    // New add: refuse once the cart is full and prompt the player to clear it.
    if (entries.size() >= kMaxItems) {
        DbgLog("CartManager: cart full ({} items), refused to add {}", kMaxItems, name);
        RE::DebugNotification(
            "Barter cart is full - complete the offer or remove items before adding more.");
        return false;
    }

    CartEntry entry;
    entry.formID = formID;
    entry.count = 1;
    entry.isBuying = isBuying;
    entry.marketUnitPrice = unitPrice;
    entry.name = name;
    entry.stolen = stolen;
    entries.push_back(std::move(entry));
    DbgLog("CartManager: Added 1/{} of {} to cart ({}, {}g each)",
        stackMax, name, isBuying ? "buy" : "sell", unitPrice);
    PlayCartSound(true, formID, isBuying);
    return true;
}

void CartManager::Remove(RE::FormID formID, bool isBuying) {
    auto it = std::find_if(entries.begin(), entries.end(), [&](const CartEntry& e) {
        return e.formID == formID && e.isBuying == isBuying;
    });
    if (it != entries.end()) {
        DbgLog("CartManager: Removed {} from cart", it->name);
        entries.erase(it);
    }
}

void CartManager::Clear() {
    if (!entries.empty()) {
        DbgLog("CartManager: Cleared {} entries", entries.size());
    }
    entries.clear();
}

int CartManager::GetBuySubtotal() const {
    int total = 0;
    for (const auto& e : entries) {
        if (e.isBuying) {
            total += e.count * e.marketUnitPrice;
        }
    }
    return total;
}

int CartManager::GetSellSubtotal() const {
    int total = 0;
    for (const auto& e : entries) {
        if (!e.isBuying) {
            total += e.count * e.marketUnitPrice;
        }
    }
    return total;
}

int CartManager::GetNetAmount() const {
    return GetBuySubtotal() - GetSellSubtotal();
}
