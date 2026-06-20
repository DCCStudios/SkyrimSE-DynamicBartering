#include "PCH.h"
#include "DealMemory.h"

nlohmann::json DealRecord::ToJson() const {
    return {
        {"itemFormID", itemFormID},
        {"itemName", itemName},
        {"basePrice", basePrice},
        {"offeredPrice", offeredPrice},
        {"accepted", accepted},
        {"wasCounterOffer", wasCounterOffer},
        {"counterAmount", counterAmount},
        {"timestamp", timestamp}
    };
}

DealRecord DealRecord::FromJson(const nlohmann::json& j) {
    DealRecord r;
    r.itemFormID = j.value("itemFormID", (RE::FormID)0);
    r.itemName = j.value("itemName", "");
    r.basePrice = j.value("basePrice", 0);
    r.offeredPrice = j.value("offeredPrice", 0);
    r.accepted = j.value("accepted", false);
    r.wasCounterOffer = j.value("wasCounterOffer", false);
    r.counterAmount = j.value("counterAmount", 0);
    r.timestamp = j.value("timestamp", 0.0f);
    return r;
}

void MerchantMemory::RecordDeal(const DealRecord& deal) {
    recentDeals.push_back(deal);
    if (static_cast<int>(recentDeals.size()) > kMaxRecentDeals) {
        recentDeals.erase(recentDeals.begin());
    }
    totalDeals++;
    if (deal.accepted || deal.wasCounterOffer) {
        acceptedDeals++;
    } else {
        rejectedDeals++;
    }
    float ratio = (deal.basePrice > 0) ? static_cast<float>(deal.offeredPrice) / deal.basePrice : 1.0f;
    if (ratio < 0.6f) {
        lowballCount++;
    }
    lastInteractionDay = deal.timestamp;
}

float MerchantMemory::GetFairnessReputation() const {
    if (recentDeals.empty()) return 0.5f;
    int fairCount = 0;
    for (const auto& d : recentDeals) {
        float ratio = (d.basePrice > 0) ? static_cast<float>(d.offeredPrice) / d.basePrice : 1.0f;
        if (ratio >= 0.8f && d.accepted) {
            fairCount++;
        }
    }
    return static_cast<float>(fairCount) / static_cast<float>(recentDeals.size());
}

bool MerchantMemory::HasRecentLowball() const {
    int checkCount = std::min(3, static_cast<int>(recentDeals.size()));
    for (int i = static_cast<int>(recentDeals.size()) - 1;
         i >= static_cast<int>(recentDeals.size()) - checkCount; --i) {
        float ratio = (recentDeals[i].basePrice > 0)
            ? static_cast<float>(recentDeals[i].offeredPrice) / recentDeals[i].basePrice
            : 1.0f;
        if (ratio < 0.6f) return true;
    }
    return false;
}

int MerchantMemory::GetConsecutiveFairDeals() const {
    int streak = 0;
    for (int i = static_cast<int>(recentDeals.size()) - 1; i >= 0; --i) {
        if (recentDeals[i].accepted) {
            float ratio = (recentDeals[i].basePrice > 0)
                ? static_cast<float>(recentDeals[i].offeredPrice) / recentDeals[i].basePrice
                : 1.0f;
            if (ratio >= 0.85f) {
                streak++;
            } else {
                break;
            }
        } else {
            break;
        }
    }
    return streak;
}

int MerchantMemory::GetRecentLowballCount(int withinLast) const {
    int count = 0;
    int check = std::min(withinLast, static_cast<int>(recentDeals.size()));
    for (int i = static_cast<int>(recentDeals.size()) - 1;
         i >= static_cast<int>(recentDeals.size()) - check; --i) {
        float ratio = (recentDeals[i].basePrice > 0)
            ? static_cast<float>(recentDeals[i].offeredPrice) / recentDeals[i].basePrice
            : 1.0f;
        if (ratio < 0.6f) count++;
    }
    return count;
}

nlohmann::json MerchantMemory::ToJson() const {
    nlohmann::json j;
    j["merchantRefID"] = merchantRefID;
    j["merchantName"] = merchantName;
    j["relationship"] = relationship;
    j["totalDeals"] = totalDeals;
    j["acceptedDeals"] = acceptedDeals;
    j["rejectedDeals"] = rejectedDeals;
    j["lowballCount"] = lowballCount;
    j["lastInteractionDay"] = lastInteractionDay;
    j["recentDeals"] = nlohmann::json::array();
    for (const auto& d : recentDeals) {
        j["recentDeals"].push_back(d.ToJson());
    }
    return j;
}

MerchantMemory MerchantMemory::FromJson(const nlohmann::json& j) {
    MerchantMemory m;
    m.merchantRefID = j.value("merchantRefID", (RE::FormID)0);
    m.merchantName = j.value("merchantName", "");
    m.relationship = j.value("relationship", 0);
    m.totalDeals = j.value("totalDeals", 0);
    m.acceptedDeals = j.value("acceptedDeals", 0);
    m.rejectedDeals = j.value("rejectedDeals", 0);
    m.lowballCount = j.value("lowballCount", 0);
    m.lastInteractionDay = j.value("lastInteractionDay", 0.0f);
    if (j.contains("recentDeals")) {
        for (const auto& d : j["recentDeals"]) {
            m.recentDeals.push_back(DealRecord::FromJson(d));
        }
    }
    return m;
}
