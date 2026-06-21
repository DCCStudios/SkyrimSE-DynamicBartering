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

    // Add if absent (match by FormID + direction), remove if present
    void Toggle(RE::FormID formID, int count, bool isBuying, int unitPrice,
                const std::string& name, bool stolen);

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
