#include "PCH.h"
#include "MilestoneManager.h"
#include "MerchantCategory.h"
#include "RelationshipManager.h"
#include "Settings.h"
#include "DebugLog.h"

namespace {
    struct Milestone {
        std::uint32_t questFormID;  // doubles as the one-shot milestone id
        MerchantCategory category;
        int delta;
        const char* label;
    };

    // Vanilla Skyrim.esm quest base FormIDs (load index 0x00). Completing the quest
    // grants the listed category-wide standing once. If a FormID can't be resolved to
    // a quest (e.g. a heavily-modified load order) the milestone simply never fires.
    constexpr Milestone kMilestones[] = {
        // College of Winterhold - The Eye of Magnus (become Arch-Mage).
        { 0x0001F258, MerchantCategory::CourtWizardMagic, 15, "Arch-Mage" },
        // Thieves Guild - Under New Management (become Guildmaster).
        { 0x000D7D69, MerchantCategory::Fence, 15, "Thieves Guild Master" },
        // Companions - Glory of the Dead (become Harbinger).
        { 0x0001CEF6, MerchantCategory::Blacksmith, 12, "Companions Harbinger" },
        // Bards College - Tending the Flames (join the Bards College).
        { 0x00053511, MerchantCategory::Innkeeper, 10, "Bards College" },
    };

    bool QuestCompleted(std::uint32_t formID) {
        auto* quest = RE::TESForm::LookupByID<RE::TESQuest>(formID);
        return quest && quest->IsCompleted();
    }
}

void MilestoneManager::Evaluate() {
    auto* settings = Settings::GetSingleton();
    if (!settings->milestoneReputation) return;

    auto* rel = RelationshipManager::GetSingleton();
    bool applied = false;

    for (const auto& m : kMilestones) {
        if (rel->HasMilestone(m.questFormID)) continue;
        if (!QuestCompleted(m.questFormID)) continue;

        rel->AddCategoryReputation(m.category, m.delta);
        rel->MarkMilestone(m.questFormID);
        applied = true;
        logger::info("Milestone '{}' reached: {} +{} reputation",
            m.label, Merchants::MerchantCategoryToString(m.category), m.delta);
    }

    if (applied) rel->SaveData();
}
