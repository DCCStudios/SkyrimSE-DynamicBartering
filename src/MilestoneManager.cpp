#include "PCH.h"
#include "MilestoneManager.h"
#include "MerchantCategory.h"
#include "Hold.h"
#include "RelationshipManager.h"
#include "Settings.h"
#include "DebugLog.h"

namespace {
    // A milestone grants either a category-wide bonus (questline completions) or a
    // hold-wide bonus (becoming Thane). The civil-war standing is handled separately
    // (RefreshCivilWar) because it's a live, two-directional value rather than a
    // one-shot grant.
    enum class Target { Category, Hold };

    struct Milestone {
        std::uint32_t questFormID;  // doubles as the one-shot milestone id
        Target target;
        MerchantCategory category;  // used when target == Category
        Hold hold;                  // used when target == Hold
        int delta;
        const char* label;
        const char* group;  // friendly merchant-group noun for the on-screen notification
    };

    // Vanilla Skyrim.esm quest base FormIDs (load index 0x00), all editor IDs verified
    // with tools/esm_index.py. Completing the quest grants the listed standing once.
    constexpr Milestone kMilestones[] = {
        // --- Faction questlines -> whole merchant category ---
        // College of Winterhold - The Eye of Magnus (MG08): become Arch-Mage.
        { 0x0001F258, Target::Category, MerchantCategory::CourtWizardMagic, Hold::None, 15, "Arch-Mage", "Magic merchants" },
        // Thieves Guild - Under New Management (TGLeadership): become Guild Master.
        { 0x000D7D69, Target::Category, MerchantCategory::Fence, Hold::None, 15, "Thieves Guild Master", "Fences" },
        // Companions - Glory of the Dead (C06): become Harbinger.
        { 0x0001CEF6, Target::Category, MerchantCategory::Blacksmith, Hold::None, 12, "Companions Harbinger", "Smiths and warrior traders" },
        // Bards College - Tending the Flames (MS05): join the Bards College.
        { 0x00053511, Target::Category, MerchantCategory::Innkeeper, Hold::None, 10, "Bards College", "Innkeepers" },

        // --- Thane of a hold -> that hold's merchants ---
        // Per-hold Thane quests (Favor25x) + the Rift's umbrella quest (FreeformRiftenThane).
        { 0x000A2C86, Target::Hold, MerchantCategory::Generalist, Hold::Reach,      10, "Thane of the Reach",  "Markarth's merchants" },
        { 0x000A2C9B, Target::Hold, MerchantCategory::Generalist, Hold::Haafingar,  10, "Thane of Haafingar",  "Solitude's merchants" },
        { 0x000A2C9E, Target::Hold, MerchantCategory::Generalist, Hold::Whiterun,   10, "Thane of Whiterun",   "Whiterun's merchants" },
        { 0x000A2CA6, Target::Hold, MerchantCategory::Generalist, Hold::Eastmarch,  10, "Thane of Eastmarch",  "Windhelm's merchants" },
        { 0x000A34CE, Target::Hold, MerchantCategory::Generalist, Hold::Hjaalmarch, 10, "Thane of Hjaalmarch", "Morthal's merchants" },
        { 0x000A34D4, Target::Hold, MerchantCategory::Generalist, Hold::Pale,       10, "Thane of the Pale",   "Dawnstar's merchants" },
        { 0x000A34D7, Target::Hold, MerchantCategory::Generalist, Hold::Winterhold, 10, "Thane of Winterhold", "Winterhold's merchants" },
        { 0x000A34DE, Target::Hold, MerchantCategory::Generalist, Hold::Falkreath,  10, "Thane of Falkreath",  "Falkreath's merchants" },
        { 0x00065BDF, Target::Hold, MerchantCategory::Generalist, Hold::Rift,       10, "Thane of the Rift",   "Riften's merchants" },
    };

    bool QuestCompleted(std::uint32_t formID) {
        auto* quest = RE::TESForm::LookupByID<RE::TESQuest>(formID);
        return quest && quest->IsCompleted();
    }

    const char* TargetName(const Milestone& m) {
        return m.target == Target::Category
            ? Merchants::MerchantCategoryToString(m.category)
            : Holds::HoldName(m.hold);
    }

    // Passive corner notification (same channel as the cart/quick-buy messages).
    void NotifyMilestone(const Milestone& m) {
        const char* verb = m.delta >= 0 ? "now regard you more favorably"
                                         : "now regard you with suspicion";
        std::string msg = std::format("{} - {} {} ({:+d} standing).",
            m.label, m.group, verb, m.delta);
        RE::DebugNotification(msg.c_str());
    }

    // Grant a milestone if its quest is complete and it hasn't fired yet. Returns true
    // only when applied this call. notify is suppressed for the silent barter-open
    // catch-up so it never pops on a mere merchant interaction.
    bool TryApply(const Milestone& m, bool notify) {
        auto* rel = RelationshipManager::GetSingleton();
        if (rel->HasMilestone(m.questFormID)) return false;
        if (!QuestCompleted(m.questFormID)) return false;

        if (m.target == Target::Category) {
            rel->AddCategoryReputation(m.category, m.delta);
        } else {
            rel->AddHoldReputation(m.hold, m.delta);
        }
        rel->MarkMilestone(m.questFormID);
        logger::info("Milestone '{}' reached: {} {:+d} reputation", m.label, TargetName(m), m.delta);
        if (notify) NotifyMilestone(m);
        return true;
    }

    // Live, two-directional civil-war standing: holds controlled by the player's side
    // warm their merchants, enemy-held holds chill theirs. Recomputed every barter open
    // (SET, not accumulated) so a hold flipping hands moves the standing with it, and
    // disabling the feature clears it. No notification (the flip moment isn't hookable).
    void RefreshCivilWar() {
        auto* s = Settings::GetSingleton();
        auto* rel = RelationshipManager::GetSingleton();

        const bool enabled = s->milestoneReputation && s->civilWarReputation && s->civilWarRepBonus > 0;
        const Allegiance side = enabled ? Holds::GetPlayerSide() : Allegiance::None;

        for (int hi = static_cast<int>(Hold::None) + 1; hi < static_cast<int>(Hold::kTotal); ++hi) {
            const Hold h = static_cast<Hold>(hi);
            int value = 0;
            if (enabled && side != Allegiance::None) {
                const Allegiance owner = Holds::GetHoldOwner(h);
                if (owner != Allegiance::None) {
                    value = (owner == side) ? s->civilWarRepBonus : -s->civilWarRepBonus;
                }
            }
            rel->SetCivilWarStanding(h, value);
        }
    }

    // Real-time completion detector: fires the moment one of the milestone questlines
    // (faction finale or per-hold Thane quest) completes, which is where the
    // notification comes from. The apply is deferred one frame so the quest's
    // "completed" flag is guaranteed set, and runs on the main thread.
    class QuestStageSink : public RE::BSTEventSink<RE::TESQuestStageEvent> {
    public:
        static QuestStageSink* GetSingleton() {
            static QuestStageSink instance;
            return &instance;
        }

        RE::BSEventNotifyControl ProcessEvent(const RE::TESQuestStageEvent* a_event,
            RE::BSTEventSource<RE::TESQuestStageEvent>*) override {
            if (!a_event) return RE::BSEventNotifyControl::kContinue;
            if (!Settings::GetSingleton()->milestoneReputation) return RE::BSEventNotifyControl::kContinue;

            bool isMilestoneQuest = false;
            for (const auto& m : kMilestones) {
                if (m.questFormID == a_event->formID) { isMilestoneQuest = true; break; }
            }
            if (!isMilestoneQuest) return RE::BSEventNotifyControl::kContinue;

            const std::uint32_t formID = a_event->formID;
            if (auto* task = SKSE::GetTaskInterface()) {
                task->AddTask([formID]() {
                    for (const auto& m : kMilestones) {
                        if (m.questFormID != formID) continue;
                        if (TryApply(m, /*notify*/ true)) {
                            RelationshipManager::GetSingleton()->SaveData();
                        }
                        break;
                    }
                });
            }
            return RE::BSEventNotifyControl::kContinue;
        }
    };
}

void MilestoneManager::Register() {
    if (auto* holder = RE::ScriptEventSourceHolder::GetSingleton()) {
        holder->AddEventSink<RE::TESQuestStageEvent>(QuestStageSink::GetSingleton());
        logger::info("MilestoneManager: quest-completion sink registered");
    } else {
        logger::warn("MilestoneManager: no ScriptEventSourceHolder; completion notifications disabled");
    }
}

void MilestoneManager::Evaluate() {
    // Civil-war standing is recomputed every barter open regardless of the milestone
    // toggle, so flipping the feature (or a hold changing hands) takes effect at once.
    RefreshCivilWar();

    auto* settings = Settings::GetSingleton();
    if (!settings->milestoneReputation) return;

    auto* rel = RelationshipManager::GetSingleton();
    bool applied = false;

    // Silent catch-up only: applies standing for milestones already completed before
    // the live sink existed (e.g. an existing save), without a notification. New
    // completions are announced by QuestStageSink at the moment they happen.
    for (const auto& m : kMilestones) {
        if (TryApply(m, /*notify*/ false)) applied = true;
    }

    if (applied) rel->SaveData();
}
