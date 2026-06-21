#include "PCH.h"
#include "CartManager.h"

void CartManager::Toggle(RE::FormID formID, int count, bool isBuying, int unitPrice,
                         const std::string& name, bool stolen) {
    auto it = std::find_if(entries.begin(), entries.end(), [&](const CartEntry& e) {
        return e.formID == formID && e.isBuying == isBuying;
    });

    if (it != entries.end()) {
        logger::info("CartManager: Removed {} from cart ({})", it->name, isBuying ? "buy" : "sell");
        entries.erase(it);
    } else {
        CartEntry entry;
        entry.formID = formID;
        entry.count = count;
        entry.isBuying = isBuying;
        entry.marketUnitPrice = unitPrice;
        entry.name = name;
        entry.stolen = stolen;
        entries.push_back(std::move(entry));
        logger::info("CartManager: Added {} x{} to cart ({}, {}g each)",
            name, count, isBuying ? "buy" : "sell", unitPrice);
    }
}

void CartManager::Remove(RE::FormID formID, bool isBuying) {
    auto it = std::find_if(entries.begin(), entries.end(), [&](const CartEntry& e) {
        return e.formID == formID && e.isBuying == isBuying;
    });
    if (it != entries.end()) {
        logger::info("CartManager: Removed {} from cart", it->name);
        entries.erase(it);
    }
}

void CartManager::Clear() {
    if (!entries.empty()) {
        logger::info("CartManager: Cleared {} entries", entries.size());
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
