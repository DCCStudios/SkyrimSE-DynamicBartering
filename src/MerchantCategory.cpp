#include "PCH.h"
#include "MerchantCategory.h"
#include "DebugLog.h"
#include <nlohmann/json.hpp>
#include <fstream>
#include <filesystem>

namespace {
    std::unordered_map<std::string, MerchantCategory> g_categoryMap;
    bool g_categoriesLoaded = false;

    std::string ToLower(std::string s) {
        std::transform(s.begin(), s.end(), s.begin(), ::tolower);
        return s;
    }

    void LoadCategoryFile(const std::filesystem::path& path) {
        try {
            std::ifstream f(path);
            if (!f.is_open()) return;
            auto j = nlohmann::json::parse(f);
            if (!j.contains("merchants") || !j["merchants"].is_object()) return;
            int added = 0;
            for (auto& [name, data] : j["merchants"].items()) {
                if (data.contains("category") && data["category"].is_string()) {
                    g_categoryMap[ToLower(name)] =
                        Merchants::MerchantCategoryFromString(data["category"].get<std::string>());
                    ++added;
                }
            }
            logger::info("MerchantCategory: Loaded {} category entries from {}", added, path.filename().string());
        } catch (const std::exception& e) {
            logger::error("MerchantCategory: Failed to parse {}: {}", path.string(), e.what());
        }
    }

    void EnsureCategoriesLoaded() {
        if (g_categoriesLoaded) return;
        g_categoriesLoaded = true;

        auto pluginPath = std::filesystem::path("Data/SKSE/Plugins");
        auto defaultFile = pluginPath / "DynamicBartering" / "merchant_dispositions.json";
        if (std::filesystem::exists(defaultFile)) {
            LoadCategoryFile(defaultFile);
        }

        auto addonsDir = pluginPath / "DynamicBartering" / "Dispositions";
        if (std::filesystem::exists(addonsDir) && std::filesystem::is_directory(addonsDir)) {
            for (auto& entry : std::filesystem::directory_iterator(addonsDir)) {
                if (entry.path().extension() == ".json") {
                    LoadCategoryFile(entry.path());
                }
            }
        }

        logger::info("MerchantCategory: Total category entries loaded: {}", g_categoryMap.size());
    }

    bool IsEnchanted(RE::TESBoundObject* obj) {
        if (auto* ench = obj->As<RE::TESEnchantableForm>()) {
            return ench->formEnchanting != nullptr;
        }
        return false;
    }
}

namespace Merchants {

    ItemCategory CategorizeItem(RE::TESBoundObject* obj) {
        if (!obj) return ItemCategory::Unknown;

        switch (obj->GetFormType()) {
            case RE::FormType::Weapon: {
                auto* w = obj->As<RE::TESObjectWEAP>();
                if (w && w->IsStaff()) return ItemCategory::SoulGemStaff;
                if (IsEnchanted(obj)) return ItemCategory::EnchantedGear;
                return ItemCategory::Weapon;
            }
            case RE::FormType::Armor: {
                auto* a = obj->As<RE::TESObjectARMO>();
                if (a) {
                    using Slot = RE::BGSBipedObjectForm::BipedObjectSlot;
                    if (a->HasPartOf(Slot::kAmulet) || a->HasPartOf(Slot::kRing)) {
                        return ItemCategory::Jewelry;
                    }
                    if (IsEnchanted(obj)) return ItemCategory::EnchantedGear;
                    if (a->IsClothing()) return ItemCategory::Clothing;
                }
                return ItemCategory::Armor;
            }
            case RE::FormType::AlchemyItem: {
                auto* al = obj->As<RE::AlchemyItem>();
                if (al && al->IsFood()) return ItemCategory::Food;
                return ItemCategory::Potion;  // potions + poisons
            }
            case RE::FormType::Ingredient:
                return ItemCategory::Ingredient;
            case RE::FormType::Book: {
                auto* b = obj->As<RE::TESObjectBOOK>();
                if (b && b->TeachesSpell()) return ItemCategory::SpellTome;
                return ItemCategory::Book;
            }
            case RE::FormType::Scroll:
                return ItemCategory::SpellTome;
            case RE::FormType::SoulGem:
                return ItemCategory::SoulGemStaff;
            case RE::FormType::Misc:
            case RE::FormType::Ammo:
                return ItemCategory::Misc;
            default:
                return ItemCategory::Misc;
        }
    }

    MerchantCategory DetectCategory(RE::Actor* merchant) {
        if (!merchant) return MerchantCategory::Generalist;
        EnsureCategoriesLoaded();

        auto* npc = merchant->GetActorBase();
        if (npc) {
            const char* name = npc->GetName();
            if (name && name[0]) {
                std::string nameLower = ToLower(name);
                auto it = g_categoryMap.find(nameLower);
                if (it != g_categoryMap.end()) {
                    return it->second;
                }
                // Longest substring match (handles mod-added last names).
                MerchantCategory best = MerchantCategory::Generalist;
                std::size_t bestLen = 0;
                bool found = false;
                for (auto& [key, cat] : g_categoryMap) {
                    if (key.length() > bestLen &&
                        (nameLower.starts_with(key) || nameLower.find(key) != std::string::npos)) {
                        best = cat;
                        bestLen = key.length();
                        found = true;
                    }
                }
                if (found) return best;
            }
        }

        // Faction fallback: Thieves Guild fence faction -> Fence.
        auto tgFence = RE::TESForm::LookupByID<RE::TESFaction>(0x0001F3B5);
        if (tgFence && merchant->IsInFaction(tgFence)) {
            return MerchantCategory::Fence;
        }

        return MerchantCategory::Generalist;
    }

    float SpecialtyFactor(MerchantCategory m, ItemCategory i) {
        switch (m) {
            case MerchantCategory::Blacksmith:
                if (i == ItemCategory::Weapon || i == ItemCategory::Armor) return 1.0f;
                if (i == ItemCategory::SoulGemStaff || i == ItemCategory::SpellTome ||
                    i == ItemCategory::Potion || i == ItemCategory::Ingredient) return -0.6f;
                return 0.0f;
            case MerchantCategory::Apothecary:
                if (i == ItemCategory::Potion || i == ItemCategory::Ingredient) return 1.0f;
                if (i == ItemCategory::Food) return 0.3f;
                if (i == ItemCategory::Weapon || i == ItemCategory::Armor ||
                    i == ItemCategory::Jewelry) return -0.6f;
                return 0.0f;
            case MerchantCategory::CourtWizardMagic:
                if (i == ItemCategory::SpellTome || i == ItemCategory::SoulGemStaff ||
                    i == ItemCategory::EnchantedGear) return 1.0f;
                if (i == ItemCategory::Weapon || i == ItemCategory::Armor ||
                    i == ItemCategory::Food) return -0.5f;
                return 0.0f;
            case MerchantCategory::Innkeeper:
                if (i == ItemCategory::Food) return 1.0f;
                if (i == ItemCategory::Potion) return 0.3f;
                if (i == ItemCategory::Weapon || i == ItemCategory::Armor ||
                    i == ItemCategory::Jewelry || i == ItemCategory::SpellTome) return -0.5f;
                return 0.0f;
            case MerchantCategory::Clothier:
                if (i == ItemCategory::Clothing) return 1.0f;
                if (i == ItemCategory::Jewelry) return 0.3f;
                if (i == ItemCategory::Weapon || i == ItemCategory::SpellTome) return -0.5f;
                return 0.0f;
            case MerchantCategory::Jeweler:
                if (i == ItemCategory::Jewelry) return 1.0f;
                if (i == ItemCategory::Misc) return 0.3f;  // gems / ore / ingots
                if (i == ItemCategory::Food || i == ItemCategory::Potion ||
                    i == ItemCategory::SpellTome) return -0.5f;
                return 0.0f;
            case MerchantCategory::GeneralGoods:
                if (i == ItemCategory::Misc || i == ItemCategory::Food ||
                    i == ItemCategory::Ingredient) return 0.3f;
                return 0.1f;  // happy to deal in most things
            case MerchantCategory::Caravan:
                return 0.2f;  // Khajiit buy anything; mild bonus everywhere
            case MerchantCategory::Fence:
            case MerchantCategory::Generalist:
            default:
                return 0.0f;
        }
    }

    const char* ItemCategoryToString(ItemCategory c) {
        switch (c) {
            case ItemCategory::Weapon: return "Weapon";
            case ItemCategory::Armor: return "Armor";
            case ItemCategory::Clothing: return "Clothing";
            case ItemCategory::Jewelry: return "Jewelry";
            case ItemCategory::Potion: return "Potion";
            case ItemCategory::Ingredient: return "Ingredient";
            case ItemCategory::Food: return "Food";
            case ItemCategory::Book: return "Book";
            case ItemCategory::SpellTome: return "Spell Tome";
            case ItemCategory::SoulGemStaff: return "Soul Gem / Staff";
            case ItemCategory::EnchantedGear: return "Enchanted Gear";
            case ItemCategory::Misc: return "Misc";
            default: return "Unknown";
        }
    }

    const char* MerchantCategoryToString(MerchantCategory c) {
        switch (c) {
            case MerchantCategory::Blacksmith: return "Blacksmith";
            case MerchantCategory::GeneralGoods: return "General Goods";
            case MerchantCategory::Apothecary: return "Apothecary";
            case MerchantCategory::CourtWizardMagic: return "Magic Trader";
            case MerchantCategory::Innkeeper: return "Innkeeper";
            case MerchantCategory::Clothier: return "Clothier";
            case MerchantCategory::Jeweler: return "Jeweler";
            case MerchantCategory::Fence: return "Fence";
            case MerchantCategory::Caravan: return "Caravan Trader";
            case MerchantCategory::Generalist:
            default: return "Generalist";
        }
    }

    MerchantCategory MerchantCategoryFromString(const std::string& s) {
        std::string l = ToLower(s);
        if (l == "blacksmith" || l == "smith" || l == "weaponarmor") return MerchantCategory::Blacksmith;
        if (l == "generalgoods" || l == "general" || l == "trader") return MerchantCategory::GeneralGoods;
        if (l == "apothecary" || l == "alchemist" || l == "alchemy") return MerchantCategory::Apothecary;
        if (l == "courtwizard" || l == "wizard" || l == "magic" || l == "mage" ||
            l == "magictrader") return MerchantCategory::CourtWizardMagic;
        if (l == "innkeeper" || l == "inn" || l == "food" || l == "tavern") return MerchantCategory::Innkeeper;
        if (l == "clothier" || l == "clothing" || l == "tailor") return MerchantCategory::Clothier;
        if (l == "jeweler" || l == "jeweller" || l == "jewelry" || l == "silversmith") return MerchantCategory::Jeweler;
        if (l == "fence" || l == "thievesguild") return MerchantCategory::Fence;
        if (l == "caravan" || l == "khajiit") return MerchantCategory::Caravan;
        return MerchantCategory::Generalist;
    }
}
