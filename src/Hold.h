#pragma once

// The nine vanilla holds, used for hold-scoped reputation (Thane bonuses + civil-war
// standing). A merchant is mapped to a hold via their assigned crime faction (every
// hold NPC carries CrimeFaction<Hold>), with a location-walk fallback. All FormIDs
// are Skyrim.esm base IDs verified with tools/esm_index.py.
enum class Hold : int {
    None = 0,
    Eastmarch,    // Windhelm
    Falkreath,    // Falkreath
    Haafingar,    // Solitude
    Hjaalmarch,   // Morthal
    Pale,         // Dawnstar
    Reach,        // Markarth
    Rift,         // Riften
    Whiterun,     // Whiterun
    Winterhold,   // Winterhold
    kTotal
};

// Civil-war allegiance of a hold's current owner, or the player's chosen side.
enum class Allegiance : int { None = 0, Imperial, Stormcloak };

namespace Holds {
    // Resolve the merchant's home hold. Returns Hold::None when it can't be determined
    // (e.g. Khajiit caravans, mod NPCs with no hold crime faction or hold location).
    Hold DetectHold(RE::Actor* merchant);

    // Display name of the hold (e.g. "Whiterun", "the Rift").
    const char* HoldName(Hold h);
    // Friendly merchant-group label for notifications (e.g. "Whiterun merchants").
    const char* HoldMerchantsLabel(Hold h);

    // --- Civil war --------------------------------------------------------------
    // The player's committed military side (Imperial Legion / Stormcloaks), or None
    // if they haven't joined either army.
    Allegiance GetPlayerSide();

    // Current civil-war owner of a hold, read from the hold location's CWOwner keyword
    // data. Returns Allegiance::None if ownership can't be resolved.
    Allegiance GetHoldOwner(Hold h);
}
