#pragma once

struct CartEntry {
    RE::FormID formID;
    int count;
    bool isBuying;
    int marketUnitPrice;
    std::string name;
    bool stolen;
};

class CartManager {
public:
    static CartManager* GetSingleton() {
        static CartManager instance;
        return &instance;
    }

    // Cart capacity. The cart panel shrinks its row text to fit, but only down to a
    // minimum readable size; this is the number of rows that still fit at that floor.
    // Adding past this is refused (the player is prompted to finish/empty the cart).
    static constexpr std::size_t kMaxItems = 18;

    // Add if absent (match by FormID + direction), remove if present.
    // Returns true if the cart changed; false if a new add was refused (cart full).
    bool Toggle(RE::FormID formID, int count, bool isBuying, int unitPrice,
                const std::string& name, bool stolen);

    // Apply a quantity-menu selection. If the (formID, direction) isn't in the cart yet
    // it's added with `amount`. If it IS already in the cart, the selection SUBTRACTS:
    // amount >= the cart count removes the whole entry, otherwise the count is reduced.
    // Returns true if the cart changed; false if a new add was refused (cart full).
    bool ApplyQuantity(RE::FormID formID, int amount, bool isBuying, int unitPrice,
                       const std::string& name, bool stolen);

    // Build a stack one unit at a time. Used for small stacks (e.g. exactly 2) where the
    // vanilla quantity slider refuses to appear, so a single tap can't pick a partial
    // amount. Each call adds one unit until `stackMax` is reached; the next call past the
    // max removes the entry, so one button cycles 0 -> 1 -> ... -> stackMax -> 0.
    // Returns true if the cart changed; false if a new add was refused (cart full).
    bool AddUnit(RE::FormID formID, bool isBuying, int unitPrice,
                 const std::string& name, bool stolen, int stackMax);

    // True when a new item of this (formID, direction) can be added OR it is already
    // present (so a toggle-off is allowed). False only when the cart is full of others.
    bool CanAccept(RE::FormID formID, bool isBuying) const {
        if (entries.size() < kMaxItems) return true;
        for (const auto& e : entries) {
            if (e.formID == formID && e.isBuying == isBuying) return true;
        }
        return false;
    }
    bool IsFull() const { return entries.size() >= kMaxItems; }

    void Remove(RE::FormID formID, bool isBuying);
    void Clear();

    // First cart entry matching this FormID (either direction), or nullptr.
    const CartEntry* Find(RE::FormID formID) const {
        for (const auto& e : entries) {
            if (e.formID == formID) return &e;
        }
        return nullptr;
    }
    bool Contains(RE::FormID formID) const { return Find(formID) != nullptr; }

    const std::vector<CartEntry>& GetEntries() const { return entries; }
    int GetBuySubtotal() const;
    int GetSellSubtotal() const;
    int GetNetAmount() const;  // positive = player pays, negative = player receives
    bool IsEmpty() const { return entries.empty(); }
    std::size_t Count() const { return entries.size(); }

private:
    CartManager() = default;
    std::vector<CartEntry> entries;
};
