#include "PCH.h"
#include "Hold.h"
#include "DebugLog.h"
#include <cstring>

namespace {
    // CrimeFaction<Hold> base FormIDs. Every vanilla hold NPC is assigned their hold's
    // crime faction, so this is the most reliable merchant->hold signal.
    struct HoldForms {
        Hold hold;
        RE::FormID crimeFaction;
        RE::FormID location;
    };

    constexpr HoldForms kHoldForms[] = {
        { Hold::Eastmarch,  0x000267E3, 0x0001676A },
        { Hold::Falkreath,  0x00028170, 0x0001676F },
        { Hold::Haafingar,  0x00029DB0, 0x00016770 },
        { Hold::Hjaalmarch, 0x0002816D, 0x0001676E },
        { Hold::Pale,       0x0002816E, 0x0001676D },
        { Hold::Reach,      0x0002816C, 0x00016769 },
        { Hold::Rift,       0x0002816B, 0x0001676C },
        { Hold::Whiterun,   0x000267EA, 0x00016772 },
        { Hold::Winterhold, 0x0002816F, 0x0001676B },
    };

    // Civil-war factions stored in a hold location's CWOwner keyword. These two are the
    // only valid owners, which lets us decode the keyword-data value robustly (below).
    constexpr RE::FormID kCWImperialFaction = 0x0002BF9A;
    constexpr RE::FormID kCWSonsFaction     = 0x0002BF9B;
    constexpr RE::FormID kCWOwnerKeyword    = 0x0002A456;

    Hold HoldFromCrimeFaction(RE::FormID id) {
        for (const auto& hf : kHoldForms) {
            if (hf.crimeFaction == id) return hf.hold;
        }
        return Hold::None;
    }

    Hold HoldFromLocation(RE::FormID id) {
        for (const auto& hf : kHoldForms) {
            if (hf.location == id) return hf.hold;
        }
        return Hold::None;
    }

    RE::FormID HoldLocationFormID(Hold h) {
        for (const auto& hf : kHoldForms) {
            if (hf.hold == h) return hf.location;
        }
        return 0;
    }

    // The CWOwner keyword stores the owning faction, but the keyword-data float encoding
    // isn't documented. Since only two factions can ever own a hold, we test the value
    // under both plausible interpretations (numeric and raw-bit reinterpretation) and
    // accept whichever yields one of the two known civil-war factions.
    Allegiance DecodeOwner(float data) {
        std::uint32_t cands[2] = { 0, 0 };
        if (data >= 0.0f && data < 4.2e9f) {
            cands[0] = static_cast<std::uint32_t>(std::llround(static_cast<double>(data)));
        }
        std::memcpy(&cands[1], &data, sizeof(float));
        for (auto c : cands) {
            if (c == kCWImperialFaction) return Allegiance::Imperial;
            if (c == kCWSonsFaction) return Allegiance::Stormcloak;
        }
        return Allegiance::None;
    }
}

namespace Holds {

    Hold DetectHold(RE::Actor* merchant) {
        if (!merchant) return Hold::None;

        // Primary: the merchant's assigned crime faction == their hold.
        if (auto* cf = merchant->GetCrimeFaction()) {
            Hold h = HoldFromCrimeFaction(cf->GetFormID());
            if (h != Hold::None) return h;
        }

        // Fallback: walk the current location's parent chain to a hold location.
        for (auto* loc = merchant->GetCurrentLocation(); loc; loc = loc->parentLoc) {
            Hold h = HoldFromLocation(loc->GetFormID());
            if (h != Hold::None) return h;
        }

        return Hold::None;
    }

    const char* HoldName(Hold h) {
        switch (h) {
            case Hold::Eastmarch:  return "Eastmarch";
            case Hold::Falkreath:  return "Falkreath";
            case Hold::Haafingar:  return "Haafingar";
            case Hold::Hjaalmarch: return "Hjaalmarch";
            case Hold::Pale:       return "the Pale";
            case Hold::Reach:      return "the Reach";
            case Hold::Rift:       return "the Rift";
            case Hold::Whiterun:   return "Whiterun";
            case Hold::Winterhold: return "Winterhold";
            default:               return "the hold";
        }
    }

    const char* HoldMerchantsLabel(Hold h) {
        switch (h) {
            case Hold::Eastmarch:  return "Windhelm's merchants";
            case Hold::Falkreath:  return "Falkreath's merchants";
            case Hold::Haafingar:  return "Solitude's merchants";
            case Hold::Hjaalmarch: return "Morthal's merchants";
            case Hold::Pale:       return "Dawnstar's merchants";
            case Hold::Reach:      return "Markarth's merchants";
            case Hold::Rift:       return "Riften's merchants";
            case Hold::Whiterun:   return "Whiterun's merchants";
            case Hold::Winterhold: return "Winterhold's merchants";
            default:               return "the hold's merchants";
        }
    }

    Allegiance GetPlayerSide() {
        auto* player = RE::PlayerCharacter::GetSingleton();
        if (!player) return Allegiance::None;

        if (auto* imp = RE::TESForm::LookupByID<RE::TESFaction>(kCWImperialFaction)) {
            if (player->IsInFaction(imp)) return Allegiance::Imperial;
        }
        if (auto* sons = RE::TESForm::LookupByID<RE::TESFaction>(kCWSonsFaction)) {
            if (player->IsInFaction(sons)) return Allegiance::Stormcloak;
        }
        return Allegiance::None;
    }

    Allegiance GetHoldOwner(Hold h) {
        RE::FormID locID = HoldLocationFormID(h);
        if (!locID) return Allegiance::None;

        auto* loc = RE::TESForm::LookupByID<RE::BGSLocation>(locID);
        auto* cwOwner = RE::TESForm::LookupByID<RE::BGSKeyword>(kCWOwnerKeyword);
        if (!loc || !cwOwner) return Allegiance::None;

        for (const auto& kd : loc->keywordData) {
            if (kd.keyword == cwOwner) {
                Allegiance a = DecodeOwner(kd.data);
                if (a == Allegiance::None) {
                    DbgLog("Holds: {} CWOwner raw value {} did not match a known faction",
                        HoldName(h), kd.data);
                }
                return a;
            }
        }
        return Allegiance::None;
    }
}
