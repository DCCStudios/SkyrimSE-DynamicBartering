#include "PCH.h"
#include "MerchantPersonality.h"
#include "DebugLog.h"
#include <nlohmann/json.hpp>
#include <fstream>
#include <filesystem>

namespace {
    std::unordered_map<std::string, MerchantPersonality::Trait> g_dispositionMap;
    bool g_dispositionsLoaded = false;

    std::string ToLower(std::string s) {
        std::transform(s.begin(), s.end(), s.begin(), ::tolower);
        return s;
    }

    void LoadDispositionFile(const std::filesystem::path& path) {
        try {
            std::ifstream f(path);
            if (!f.is_open()) return;
            auto j = nlohmann::json::parse(f);
            if (!j.contains("merchants") || !j["merchants"].is_object()) return;
            for (auto& [name, data] : j["merchants"].items()) {
                if (data.contains("personality") && data["personality"].is_string()) {
                    auto trait = MerchantPersonality::StringToTrait(data["personality"].get<std::string>());
                    g_dispositionMap[ToLower(name)] = trait;
                }
            }
            logger::info("MerchantPersonality: Loaded {} entries from {}", j["merchants"].size(), path.filename().string());
        } catch (const std::exception& e) {
            logger::error("MerchantPersonality: Failed to parse {}: {}", path.string(), e.what());
        }
    }

    void EnsureDispositionsLoaded() {
        if (g_dispositionsLoaded) return;
        g_dispositionsLoaded = true;

        // Load from plugin data folder (next to DLL)
        auto pluginPath = std::filesystem::path("Data/SKSE/Plugins");
        auto defaultFile = pluginPath / "DynamicBartering" / "merchant_dispositions.json";
        if (std::filesystem::exists(defaultFile)) {
            LoadDispositionFile(defaultFile);
        }

        // Load addon files from Dispositions subfolder (mod-author extensions)
        auto addonsDir = pluginPath / "DynamicBartering" / "Dispositions";
        if (std::filesystem::exists(addonsDir) && std::filesystem::is_directory(addonsDir)) {
            for (auto& entry : std::filesystem::directory_iterator(addonsDir)) {
                if (entry.path().extension() == ".json") {
                    LoadDispositionFile(entry.path());
                }
            }
        }

        logger::info("MerchantPersonality: Total disposition entries loaded: {}", g_dispositionMap.size());
    }
}

MerchantPersonality MerchantPersonality::FromTrait(Trait t) {
    MerchantPersonality p;
    p.trait = t;
    switch (t) {
        case Trait::Greedy:
            p.acceptanceMod = -15.0f;
            p.offensePerInsult = 8.0f;
            p.counterChance = 0.20f;
            p.counterSplit = 0.30f;
            p.priceJackMult = 1.5f;
            p.patienceRounds = 2;
            p.enjoysHaggling = false;
            p.haggleRangeScale = 0.6f;  // tight-fisted: standing barely widens the range
            break;
        case Trait::Fair:
            p.acceptanceMod = 0.0f;
            p.offensePerInsult = 4.0f;
            p.counterChance = 0.40f;
            p.counterSplit = 0.50f;
            p.priceJackMult = 1.0f;
            p.patienceRounds = 3;
            p.enjoysHaggling = false;
            p.haggleRangeScale = 1.0f;
            break;
        case Trait::Generous:
            p.acceptanceMod = 10.0f;
            p.offensePerInsult = 2.0f;
            p.counterChance = 0.60f;
            p.counterSplit = 0.70f;
            p.priceJackMult = 0.5f;
            p.patienceRounds = 4;
            p.enjoysHaggling = false;
            p.haggleRangeScale = 1.4f;  // kind-hearted: standing opens up generous deals
            break;
        case Trait::Sleazy:
            p.acceptanceMod = 5.0f;
            p.offensePerInsult = 0.0f;
            p.counterChance = 0.80f;
            p.counterSplit = 0.50f;
            p.priceJackMult = 0.8f;
            p.patienceRounds = 4;
            p.enjoysHaggling = true;
            p.haggleRangeScale = 1.3f;  // loves a good haggle: lots of give either way
            break;
        case Trait::Stern:
            p.acceptanceMod = -10.0f;
            p.offensePerInsult = 10.0f;
            p.counterChance = 0.10f;
            p.counterSplit = 0.20f;
            p.priceJackMult = 1.3f;
            p.patienceRounds = 1;
            p.enjoysHaggling = false;
            p.haggleRangeScale = 0.6f;  // take-it-or-leave-it: standing barely matters
            break;
        case Trait::Timid:
            p.acceptanceMod = 15.0f;
            p.offensePerInsult = 1.0f;
            p.counterChance = 0.25f;
            p.counterSplit = 0.60f;
            p.priceJackMult = 0.7f;
            p.patienceRounds = 4;
            p.enjoysHaggling = false;
            p.haggleRangeScale = 1.2f;  // eager to please: standing opens the range up
            break;
    }
    return p;
}

MerchantPersonality MerchantPersonality::DetectFromActor(RE::Actor* merchant) {
    if (!merchant) return FromTrait(Trait::Fair);

    EnsureDispositionsLoaded();

    auto* npc = merchant->GetActorBase();
    if (!npc) return FromTrait(Trait::Fair);

    // Check disposition JSON by NPC name (case-insensitive)
    // Uses substring matching to handle mods that add last names (e.g., "Belethor Phileron")
    const char* name = npc->GetName();
    if (name && name[0]) {
        std::string nameLower = ToLower(name);
        // First try exact match
        auto it = g_dispositionMap.find(nameLower);
        if (it != g_dispositionMap.end()) {
            DbgLog("MerchantPersonality: Exact match for '{}' -> {}", name, TraitToString(it->second));
            return FromTrait(it->second);
        }
        // Then try: does the NPC name START WITH any known disposition key?
        // (handles "Belethor Phileron" matching "belethor")
        MerchantPersonality::Trait bestMatch = Trait::Fair;
        std::size_t bestLen = 0;
        bool found = false;
        for (auto& [key, trait] : g_dispositionMap) {
            if (key.length() > bestLen && nameLower.starts_with(key)) {
                // NPC name starts with this key (longest match wins)
                bestMatch = trait;
                bestLen = key.length();
                found = true;
            } else if (key.length() > bestLen && nameLower.find(key) != std::string::npos) {
                // NPC name contains this key somewhere
                bestMatch = trait;
                bestLen = key.length();
                found = true;
            }
        }
        if (found) {
            DbgLog("MerchantPersonality: Substring match for '{}' (matched '{}' len {}) -> {}",
                name, "", bestLen, TraitToString(bestMatch));
            return FromTrait(bestMatch);
        }
    }

    // Fallback: faction-based detection
    auto tgFence = RE::TESForm::LookupByID<RE::TESFaction>(0x0001F3B5);
    if (tgFence && merchant->IsInFaction(tgFence)) {
        return FromTrait(Trait::Sleazy);
    }

    return FromTrait(Trait::Fair);
}

const char* MerchantPersonality::TraitToString(Trait t) {
    switch (t) {
        case Trait::Greedy: return "Greedy";
        case Trait::Fair: return "Fair";
        case Trait::Generous: return "Generous";
        case Trait::Sleazy: return "Sleazy";
        case Trait::Stern: return "Stern";
        case Trait::Timid: return "Timid";
    }
    return "Fair";
}

MerchantPersonality::Trait MerchantPersonality::StringToTrait(const std::string& s) {
    std::string lower = ToLower(s);
    if (lower == "greedy") return Trait::Greedy;
    if (lower == "generous") return Trait::Generous;
    if (lower == "sleazy") return Trait::Sleazy;
    if (lower == "stern") return Trait::Stern;
    if (lower == "timid") return Trait::Timid;
    return Trait::Fair;
}
