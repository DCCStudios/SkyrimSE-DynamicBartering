#pragma once

// Merchant specialization + item categorization.
//
// Merchants "care more" about the goods they actually deal in: a blacksmith
// haggles more readily over weapons/armor, an apothecary over potions and
// ingredients, a court wizard over spell tomes and enchanted gear, etc. The
// SpecialtyFactor() this produces feeds the acceptance roll (see PriceCalculator),
// nudging the merchant to accept better deals on in-specialty goods and resist on
// off-specialty ones.
//
// Item categories are derived purely from form type + a few engine queries (no
// fragile keyword FormID guessing). Merchant categories are resolved from the same
// name-keyed JSON used for personalities (so vanilla coverage is hand-authored and
// mod authors extend it identically), with a Thieves Guild fence faction fallback.

enum class ItemCategory {
    Weapon,
    Armor,
    Clothing,
    Jewelry,
    Potion,        // potions + poisons
    Ingredient,
    Food,
    Book,
    SpellTome,     // spell tomes + scrolls
    SoulGemStaff,  // soul gems + staves (magic implements)
    EnchantedGear, // enchanted weapons/armor
    Misc,          // gems, ore, ingots, clutter, ammo, etc.
    Unknown
};

enum class MerchantCategory {
    Generalist,       // no meaningful specialty (neutral)
    Blacksmith,       // weapons + armor
    GeneralGoods,     // everyday goods, buys most things
    Apothecary,       // potions + ingredients
    CourtWizardMagic, // spell tomes, staves, soul gems, enchanted gear
    Innkeeper,        // food + drink
    Clothier,         // clothing
    Jeweler,          // jewelry + gems
    Fence,            // stolen goods (Thieves Guild)
    Caravan           // Khajiit travelling traders (buy anything)
};

namespace Merchants {
    // Classify a bound object into a broad item category for specialty matching.
    ItemCategory CategorizeItem(RE::TESBoundObject* obj);

    // Resolve a merchant's specialty: JSON override (by name) -> known factions ->
    // Generalist. Mirrors the personality lookup so the same JSON files drive both.
    MerchantCategory DetectCategory(RE::Actor* merchant);

    // How much a merchant's specialty helps (>0) or hurts (<0) acceptance for an
    // item of the given category. ~[-1, +1]; scaled by Settings::specialtyWeight.
    float SpecialtyFactor(MerchantCategory merchant, ItemCategory item);

    const char* ItemCategoryToString(ItemCategory c);
    const char* MerchantCategoryToString(MerchantCategory c);
    MerchantCategory MerchantCategoryFromString(const std::string& s);
}
