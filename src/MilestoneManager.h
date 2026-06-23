#pragma once

// Widespread, category-wide reputation from major story milestones. Becoming
// Arch-Mage warms every magic trader to you; taking over the Thieves Guild wins over
// the fences; and so on. Each milestone is applied at most once (tracked in the
// co-save).
//
// Two detection paths:
//   * Register() hooks TESQuestStageEvent so a milestone is granted the moment its
//     questline completes - that's when the player gets the on-screen notification.
//   * Evaluate() is a SILENT retroactive catch-up run on barter open, so standing is
//     still applied for milestones completed before the sink was active (existing
//     saves) without popping a notification just for talking to a merchant.
//
// Covered milestones:
//   * Faction questline completions -> a whole merchant category (Arch-Mage, Guild
//     Master, Harbinger, Bards College).
//   * Becoming Thane of a hold -> that hold's merchants (per-hold Favor quests).
//   * Civil-war standing -> a LIVE per-hold offset (holds your side controls warm to
//     you, enemy-held holds chill), recomputed on every barter open via RefreshCivilWar.
//     This one is silent (the hold-flip moment isn't cleanly hookable for a popup).
namespace MilestoneManager {
    void Register();  // hook quest-completion events; called once at kDataLoaded
    void Evaluate();  // civil-war refresh + silent milestone catch-up; from OnBarterOpen
}
