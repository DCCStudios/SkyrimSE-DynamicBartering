#include "PCH.h"
#include "RelationshipManager.h"
#include "Settings.h"

std::string RelationshipManager::GetSavePath() const {
    return "Data/SKSE/Plugins/DynamicBartering/DynamicBartering_Save.json";
}

nlohmann::json RelationshipManager::BuildJsonRootLocked() const {
    nlohmann::json root;
    root["merchants"] = nlohmann::json::array();
    for (const auto& [id, mem] : merchantData) {
        root["merchants"].push_back(mem.ToJson());
    }

    root["personalityOverrides"] = nlohmann::json::object();
    for (const auto& [id, trait] : personalityOverrides) {
        char hexStr[16];
        snprintf(hexStr, sizeof(hexStr), "%08X", id);
        root["personalityOverrides"][hexStr] = MerchantPersonality::TraitToString(trait);
    }

    root["categoryReputation"] = nlohmann::json::object();
    for (const auto& [cat, value] : categoryReputation) {
        root["categoryReputation"][std::to_string(cat)] = value;
    }

    root["appliedMilestones"] = nlohmann::json::array();
    for (auto id : appliedMilestones) {
        root["appliedMilestones"].push_back(id);
    }
    return root;
}

void RelationshipManager::ApplyJsonRootLocked(const nlohmann::json& root) {
    merchantData.clear();
    personalityOverrides.clear();
    categoryReputation.clear();
    appliedMilestones.clear();

    if (root.contains("merchants")) {
        for (const auto& m : root["merchants"]) {
            auto mem = MerchantMemory::FromJson(m);
            merchantData[mem.merchantRefID] = mem;
        }
    }

    if (root.contains("personalityOverrides")) {
        for (auto& [key, val] : root["personalityOverrides"].items()) {
            RE::FormID id = std::stoul(key, nullptr, 16);
            personalityOverrides[id] = MerchantPersonality::StringToTrait(val.get<std::string>());
        }
    }

    // New keys read with defaults so older saves load cleanly (no co-save bump).
    if (root.contains("categoryReputation") && root["categoryReputation"].is_object()) {
        for (auto& [key, val] : root["categoryReputation"].items()) {
            try {
                categoryReputation[std::stoi(key)] = val.get<int>();
            } catch (...) {}
        }
    }
    if (root.contains("appliedMilestones") && root["appliedMilestones"].is_array()) {
        for (auto& v : root["appliedMilestones"]) {
            if (v.is_number_unsigned() || v.is_number_integer()) {
                appliedMilestones.insert(v.get<std::uint32_t>());
            }
        }
    }
}

void RelationshipManager::LoadData() {
    std::lock_guard lock(dataMutex);

    auto path = GetSavePath();
    std::ifstream file(path);
    if (!file.is_open()) {
        merchantData.clear();
        personalityOverrides.clear();
        logger::info("No save data found at {}", path);
        return;
    }

    try {
        nlohmann::json root;
        file >> root;
        ApplyJsonRootLocked(root);
        logger::info("Loaded {} merchant records (JSON)", merchantData.size());
    } catch (const std::exception& e) {
        logger::error("Failed to load save data: {}", e.what());
    }
}

void RelationshipManager::SaveData() {
    std::lock_guard lock(dataMutex);

    auto root = BuildJsonRootLocked();

    auto path = GetSavePath();
    std::filesystem::create_directories(std::filesystem::path(path).parent_path());
    std::ofstream file(path);
    if (file.is_open()) {
        file << root.dump(2);
        logger::info("Saved {} merchant records (JSON)", merchantData.size());
    } else {
        logger::error("Failed to save data to {}", path);
    }
}

void RelationshipManager::SerializeSave(SKSE::SerializationInterface* a_intfc, std::uint32_t a_type, std::uint32_t a_version) {
    std::string blob;
    {
        std::lock_guard lock(dataMutex);
        blob = BuildJsonRootLocked().dump();
    }

    if (!a_intfc->OpenRecord(a_type, a_version)) {
        logger::error("SerializeSave: failed to open record");
        return;
    }

    auto size = static_cast<std::uint32_t>(blob.size());
    if (!a_intfc->WriteRecordData(&size, sizeof(size))) {
        logger::error("SerializeSave: failed to write size");
        return;
    }
    if (size > 0 && !a_intfc->WriteRecordData(blob.data(), size)) {
        logger::error("SerializeSave: failed to write blob");
        return;
    }

    logger::info("SerializeSave: wrote relationship data to co-save ({} bytes)", size);
}

void RelationshipManager::SerializeLoad(SKSE::SerializationInterface* a_intfc, std::uint32_t) {
    std::uint32_t size = 0;
    if (a_intfc->ReadRecordData(&size, sizeof(size)) == 0) {
        logger::error("SerializeLoad: failed to read size");
        return;
    }

    if (size == 0) {
        logger::info("SerializeLoad: co-save record empty");
        return;
    }

    std::string blob;
    blob.resize(size);

    // Read in chunks to be robust against large blobs
    std::uint32_t totalRead = 0;
    while (totalRead < size) {
        auto read = a_intfc->ReadRecordData(blob.data() + totalRead, size - totalRead);
        if (read == 0) break;
        totalRead += read;
    }

    if (totalRead != size) {
        logger::error("SerializeLoad: incomplete read ({}/{})", totalRead, size);
        return;
    }

    try {
        auto root = nlohmann::json::parse(blob);
        std::lock_guard lock(dataMutex);
        // Co-save is the source of truth for this save: override JSON-loaded values
        ApplyJsonRootLocked(root);
        logger::info("SerializeLoad: restored {} merchant records from co-save", merchantData.size());
    } catch (const std::exception& e) {
        logger::error("SerializeLoad: failed to parse co-save data: {}", e.what());
    }
}

void RelationshipManager::Revert() {
    // Reset to the JSON-backed baseline; a co-save load (if present) overrides this afterward
    LoadData();
    logger::info("RelationshipManager: reverted to JSON baseline");
}

MerchantMemory& RelationshipManager::GetOrCreate(RE::FormID merchantRefID, const std::string& name) {
    std::lock_guard lock(dataMutex);
    auto it = merchantData.find(merchantRefID);
    if (it == merchantData.end()) {
        MerchantMemory mem;
        mem.merchantRefID = merchantRefID;
        mem.merchantName = name;
        mem.relationship = 0;
        merchantData[merchantRefID] = mem;
        return merchantData[merchantRefID];
    }
    return it->second;
}

int RelationshipManager::GetRelationship(RE::FormID merchantRefID) const {
    std::lock_guard lock(dataMutex);
    auto it = merchantData.find(merchantRefID);
    if (it != merchantData.end()) return it->second.relationship;
    return 0;
}

void RelationshipManager::ModifyRelationship(RE::FormID merchantRefID, int delta) {
    std::lock_guard lock(dataMutex);
    auto it = merchantData.find(merchantRefID);
    if (it != merchantData.end()) {
        auto* settings = Settings::GetSingleton();
        it->second.relationship = std::clamp(
            it->second.relationship + delta,
            settings->relMin,
            settings->relMax
        );
    }
}

void RelationshipManager::SetRelationship(RE::FormID merchantRefID, int value) {
    std::lock_guard lock(dataMutex);
    auto it = merchantData.find(merchantRefID);
    if (it != merchantData.end()) {
        auto* settings = Settings::GetSingleton();
        it->second.relationship = std::clamp(value, settings->relMin, settings->relMax);
    }
}

void RelationshipManager::RecordDeal(RE::FormID merchantRefID, const DealRecord& deal) {
    std::lock_guard lock(dataMutex);
    auto it = merchantData.find(merchantRefID);
    if (it != merchantData.end()) {
        it->second.RecordDeal(deal);
    }
}

void RelationshipManager::ResetMerchant(RE::FormID merchantRefID) {
    std::lock_guard lock(dataMutex);
    merchantData.erase(merchantRefID);
}

void RelationshipManager::ResetAll() {
    std::lock_guard lock(dataMutex);
    merchantData.clear();
}

MerchantPersonality RelationshipManager::GetPersonality(RE::Actor* merchant) {
    if (!merchant) return MerchantPersonality::FromTrait(MerchantPersonality::Trait::Fair);

    auto refID = merchant->GetFormID();
    {
        std::lock_guard lock(dataMutex);
        auto it = personalityOverrides.find(refID);
        if (it != personalityOverrides.end()) {
            return MerchantPersonality::FromTrait(it->second);
        }
    }
    return MerchantPersonality::DetectFromActor(merchant);
}

void RelationshipManager::SetPersonalityOverride(RE::FormID merchantRefID, MerchantPersonality::Trait trait) {
    std::lock_guard lock(dataMutex);
    personalityOverrides[merchantRefID] = trait;
}

int RelationshipManager::GetCategoryReputation(MerchantCategory cat) const {
    std::lock_guard lock(dataMutex);
    auto it = categoryReputation.find(static_cast<int>(cat));
    return (it != categoryReputation.end()) ? it->second : 0;
}

void RelationshipManager::AddCategoryReputation(MerchantCategory cat, int delta) {
    if (delta == 0) return;
    std::lock_guard lock(dataMutex);
    auto* settings = Settings::GetSingleton();
    int& v = categoryReputation[static_cast<int>(cat)];
    v = std::clamp(v + delta, settings->relMin, settings->relMax);
}

int RelationshipManager::GetEffectiveRelationship(RE::FormID merchantRefID, MerchantCategory cat) const {
    std::lock_guard lock(dataMutex);
    int base = 0;
    auto it = merchantData.find(merchantRefID);
    if (it != merchantData.end()) base = it->second.relationship;

    int catOffset = 0;
    auto cit = categoryReputation.find(static_cast<int>(cat));
    if (cit != categoryReputation.end()) catOffset = cit->second;

    auto* settings = Settings::GetSingleton();
    return std::clamp(base + catOffset, settings->relMin, settings->relMax);
}

bool RelationshipManager::HasMilestone(std::uint32_t id) const {
    std::lock_guard lock(dataMutex);
    return appliedMilestones.find(id) != appliedMilestones.end();
}

void RelationshipManager::MarkMilestone(std::uint32_t id) {
    std::lock_guard lock(dataMutex);
    appliedMilestones.insert(id);
}

std::unordered_map<int, int> RelationshipManager::GetCategoryReputationSnapshot() const {
    std::lock_guard lock(dataMutex);
    return categoryReputation;
}
