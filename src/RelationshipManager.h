#pragma once
#include "DealMemory.h"
#include "MerchantPersonality.h"
#include "MerchantCategory.h"
#include <unordered_set>

class RelationshipManager {
public:
    static RelationshipManager* GetSingleton() {
        static RelationshipManager instance;
        return &instance;
    }

    void LoadData();
    void SaveData();

    // SKSE co-save serialization (authoritative per-save state)
    void SerializeSave(SKSE::SerializationInterface* a_intfc, std::uint32_t a_type, std::uint32_t a_version);
    void SerializeLoad(SKSE::SerializationInterface* a_intfc, std::uint32_t a_version);
    void Revert();

    MerchantMemory& GetOrCreate(RE::FormID merchantRefID, const std::string& name);
    int GetRelationship(RE::FormID merchantRefID) const;
    void ModifyRelationship(RE::FormID merchantRefID, int delta);
    void SetRelationship(RE::FormID merchantRefID, int value);
    void RecordDeal(RE::FormID merchantRefID, const DealRecord& deal);
    void ResetMerchant(RE::FormID merchantRefID);
    void ResetAll();

    MerchantPersonality GetPersonality(RE::Actor* merchant);
    void SetPersonalityOverride(RE::FormID merchantRefID, MerchantPersonality::Trait trait);

    // --- Category-wide reputation (milestone bonuses) ---------------------------
    // A standing offset applied to EVERY merchant of a category, so milestones like
    // becoming Archmage lift all magic traders at once. Effective relationship for a
    // merchant = their per-merchant standing + their category offset (clamped).
    int GetCategoryReputation(MerchantCategory cat) const;
    void AddCategoryReputation(MerchantCategory cat, int delta);
    int GetEffectiveRelationship(RE::FormID merchantRefID, MerchantCategory cat) const;

    // One-shot milestone bookkeeping so each milestone applies its bonus only once.
    bool HasMilestone(std::uint32_t id) const;
    void MarkMilestone(std::uint32_t id);

    const std::unordered_map<RE::FormID, MerchantMemory>& GetAllData() const { return merchantData; }
    std::unordered_map<int, int> GetCategoryReputationSnapshot() const;

private:
    RelationshipManager() = default;
    std::unordered_map<RE::FormID, MerchantMemory> merchantData;
    std::unordered_map<RE::FormID, MerchantPersonality::Trait> personalityOverrides;
    std::unordered_map<int, int> categoryReputation;       // key = int(MerchantCategory)
    std::unordered_set<std::uint32_t> appliedMilestones;   // milestone ids already granted
    mutable std::mutex dataMutex;

    std::string GetSavePath() const;

    // Build/apply the full data set as JSON (caller must hold dataMutex)
    nlohmann::json BuildJsonRootLocked() const;
    void ApplyJsonRootLocked(const nlohmann::json& a_root);
};
