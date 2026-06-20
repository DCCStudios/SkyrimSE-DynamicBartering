#pragma once

struct DealRecord {
    RE::FormID itemFormID = 0;
    std::string itemName;
    int basePrice = 0;
    int offeredPrice = 0;
    bool accepted = false;
    bool wasCounterOffer = false;
    int counterAmount = 0;
    float timestamp = 0.0f;

    nlohmann::json ToJson() const;
    static DealRecord FromJson(const nlohmann::json& j);
};

struct MerchantMemory {
    RE::FormID merchantRefID = 0;
    std::string merchantName;
    int relationship = 0;
    std::vector<DealRecord> recentDeals;
    int totalDeals = 0;
    int acceptedDeals = 0;
    int rejectedDeals = 0;
    int lowballCount = 0;
    float lastInteractionDay = 0.0f;

    static constexpr int kMaxRecentDeals = 10;

    void RecordDeal(const DealRecord& deal);
    float GetFairnessReputation() const;
    bool HasRecentLowball() const;
    int GetConsecutiveFairDeals() const;
    int GetRecentLowballCount(int withinLast = 3) const;

    nlohmann::json ToJson() const;
    static MerchantMemory FromJson(const nlohmann::json& j);
};
