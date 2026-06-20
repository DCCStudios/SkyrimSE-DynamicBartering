#include "PCH.h"
#include "Settings.h"
#include "Hooks.h"
#include "BarterManager.h"
#include "RelationshipManager.h"
#include "UI/UIBridge.h"
#include "UI/ScaleformUI.h"
#include "Menu/ConfigMenu.h"

namespace {
    constexpr std::uint32_t kSerializationID = 'DBRT';
    constexpr std::uint32_t kRelationshipRecord = 'RELS';
    constexpr std::uint32_t kSerializationVersion = 1;

    void SaveCallback(SKSE::SerializationInterface* a_intfc) {
        RelationshipManager::GetSingleton()->SerializeSave(a_intfc, kRelationshipRecord, kSerializationVersion);
    }

    void LoadCallback(SKSE::SerializationInterface* a_intfc) {
        std::uint32_t type = 0, version = 0, length = 0;
        while (a_intfc->GetNextRecordInfo(type, version, length)) {
            if (type == kRelationshipRecord) {
                RelationshipManager::GetSingleton()->SerializeLoad(a_intfc, version);
            } else {
                logger::warn("LoadCallback: unknown co-save record type {:08X}", type);
            }
        }
    }

    void RevertCallback(SKSE::SerializationInterface*) {
        RelationshipManager::GetSingleton()->Revert();
    }

    void InitializeLog() {
        auto path = logger::log_directory();
        if (!path) return;

        *path /= "DynamicBarteringSKSE.log"sv;
        auto sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(path->string(), true);
        auto log = std::make_shared<spdlog::logger>("global log"s, std::move(sink));

        log->set_level(spdlog::level::info);
        log->flush_on(spdlog::level::info);

        spdlog::set_default_logger(std::move(log));
        spdlog::set_pattern("[%H:%M:%S.%e] [%l] %v"s);

        logger::info("DynamicBarteringSKSE v{}.{}.{} loaded", 1, 0, 0);
    }

    void MessageHandler(SKSE::MessagingInterface::Message* a_msg) {
        switch (a_msg->type) {
            case SKSE::MessagingInterface::kDataLoaded:
                Settings::GetSingleton()->Load();
                RelationshipManager::GetSingleton()->LoadData();
                Hooks::Install();
                UIBridge::GetSingleton()->Initialize();
                ConfigMenu::Register();
                logger::info("DynamicBarteringSKSE initialized");
                break;
            case SKSE::MessagingInterface::kInputLoaded:
                InputDeviceSink::Register();
                break;
            case SKSE::MessagingInterface::kPreLoadGame:
                RelationshipManager::GetSingleton()->LoadData();
                break;
            case SKSE::MessagingInterface::kSaveGame:
                RelationshipManager::GetSingleton()->SaveData();
                break;
        }
    }
}

SKSEPluginLoad(const SKSE::LoadInterface* a_skse) {
    SKSE::Init(a_skse);
    InitializeLog();

    auto messaging = SKSE::GetMessagingInterface();
    messaging->RegisterListener("SKSE", MessageHandler);

    auto serialization = SKSE::GetSerializationInterface();
    if (serialization) {
        serialization->SetUniqueID(kSerializationID);
        serialization->SetSaveCallback(SaveCallback);
        serialization->SetLoadCallback(LoadCallback);
        serialization->SetRevertCallback(RevertCallback);
        logger::info("DynamicBarteringSKSE serialization callbacks registered");
    } else {
        logger::error("Failed to get serialization interface");
    }

    logger::info("DynamicBarteringSKSE registered");
    return true;
}
