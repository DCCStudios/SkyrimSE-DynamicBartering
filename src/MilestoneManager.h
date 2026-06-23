#pragma once

// Widespread, category-wide reputation from major story milestones. Becoming
// Arch-Mage warms every magic trader to you; taking over the Thieves Guild wins over
// the fences; and so on. Each milestone is applied at most once (tracked in the
// co-save) and is evaluated on every barter open, so it applies retroactively even if
// the player completed the milestone before installing the mod.
//
// Phase 1 (this pass) covers reliable, globally-detectable milestones: faction
// questline completions. Hold-based Thane / civil-war reputation needs a
// merchant->hold mapping and is a documented follow-up.
namespace MilestoneManager {
    void Evaluate();  // called from BarterManager::OnBarterOpen
}
