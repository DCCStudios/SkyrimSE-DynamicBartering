#pragma once
#include "DealMemory.h"
#include "MerchantPersonality.h"
#include "MerchantCategory.h"
#include "Hold.h"
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

    // --- Per-hold reputation (Thane bonuses + civil-war standing) ----------------
    // holdReputation is an accumulated one-shot bonus (e.g. becoming Thane lifts every
    // merchant in that hold). civilWarStanding is a LIVE value recomputed from the
    // current war state (hold owner vs the player's side), so it tracks holds flipping
    // hands; it is set, not accumulated.
    int GetHoldReputation(Hold hold) const;
    void AddHoldReputation(Hold hold, int delta);
    int GetCivilWarStanding(Hold hold) const;
    void SetCivilWarStanding(Hold hold, int value);

    // Effective relationship = per-merchant standing + category offset + hold bonus +
    // civil-war standing, clamped. Pass Hold::None to skip the hold contribution.
    int GetEffectiveRelationship(RE::FormID merchantRefID, MerchantCategory cat,
                                 Hold hold = Hold::None) const;

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
    std::unordered_map<int, int> holdReputation;           // key = int(Hold); accumulated bonus
    std::unordered_map<int, int> civilWarStanding;         // key = int(Hold); live war-derived
    std::unordered_set<std::uint32_t> appliedMilestones;   // milestone ids already granted
    mutable std::mutex dataMutex;

    std::string GetSavePath() const;

    // Build/apply the full data set as JSON (caller must hold dataMutex)
    nlohmann::json BuildJsonRootLocked() const;
    void ApplyJsonRootLocked(const nlohmann::json& a_root);
};
