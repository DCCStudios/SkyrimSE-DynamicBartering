#include "PCH.h"
#include "UI/PrismaUI.h"
#include "BarterManager.h"
#include "UI/ScaleformUI.h"
#include "Settings.h"
#include "DebugLog.h"

bool PrismaUIImpl::Initialize() {
    auto pluginHandle = GetModuleHandle(L"PrismaUI.dll");
    if (!pluginHandle) {
        logger::warn("PrismaUI.dll not loaded in process - PrismaUI unavailable");
        return false;
    }
    logger::info("PrismaUI: Found PrismaUI.dll module");

    auto requestAPI = reinterpret_cast<PRISMA_UI_API::_RequestPluginAPI>(
        GetProcAddress(pluginHandle, "RequestPluginAPI"));

    if (!requestAPI) {
        logger::error("PrismaUI: RequestPluginAPI export not found in PrismaUI.dll");
        return false;
    }

    api = static_cast<PRISMA_UI_API::IVPrismaUI1*>(
        requestAPI(PRISMA_UI_API::InterfaceVersion::V1));

    if (!api) {
        logger::error("PrismaUI: Failed to get API interface");
        return false;
    }

    view = api->CreateView(
        "DynamicBartering/barter_offer.html",
        OnDomReady);

    if (!view) {
        logger::error("PrismaUI: Failed to create view (file not found or rendering error)");
        return false;
    }

    api->RegisterJSListener(view, "BarterResult", OnBarterResult);
    api->RegisterJSListener(view, "CounterResponse", OnCounterResponse);
    api->RegisterJSListener(view, "IntimidateAttempt", OnIntimidateAttempt);

    api->Hide(view);
    initialized = true;
    logger::info("PrismaUI initialized successfully (view handle: {:x})", static_cast<uint64_t>(view));
    return true;
}

bool PrismaUIImpl::IsAvailable() const {
    return initialized && api != nullptr;
}

void PrismaUIImpl::ShowOffer(const OfferData& data) {
    if (!api || !view) {
        logger::error("PrismaUI::ShowOffer - api or view is null");
        return;
    }

    if (!api->IsValid(view)) {
        logger::error("PrismaUI::ShowOffer - view is no longer valid");
        return;
    }

    nlohmann::json j = {
        {"itemName", data.itemName},
        {"basePrice", data.basePrice},
        {"effectivePrice", data.effectivePrice},
        {"merchantName", data.merchantName},
        {"personality", data.personalityName},
        {"relationship", data.relationship},
        {"speechBonus", data.speechBonus},
        {"perkBonuses", data.perkSummary},
        {"dealHistory", data.recentDealsJson},
        {"hasIntimidation", data.hasIntimidationPerk},
        {"sliderMin", data.sliderMin},
        {"sliderMax", data.sliderMax},
        {"acceptanceChance", data.acceptanceChance},
        {"priceJackMult", data.priceJackMult},
        {"gamepad", InputDeviceSink::GetSingleton()->IsUsingGamepad()},
        {"iconStyle", Settings::GetSingleton()->gamepadIconStyle == GamepadIconStyle::PlayStation ? "ps" : "xbox"}
    };

    std::string payload = j.dump();
    DbgLog("PrismaUI::ShowOffer - '{}' (payload {} bytes)", data.itemName, payload.size());

    api->SetOrder(view, 100);
    api->Show(view);

    bool focusOk = api->Focus(view, true, true);
    if (!focusOk) {
        logger::warn("PrismaUI: Focus() returned false - DOM may not be ready (HTML file not loaded?)");
        api->Show(view);
    }

    api->InteropCall(view, "ShowOffer", payload.c_str());

    DbgLog("PrismaUI::ShowOffer - done (hidden={}, hasFocus={}, focusOk={})",
        api->IsHidden(view) ? 1 : 0, api->HasFocus(view) ? 1 : 0, focusOk ? 1 : 0);
}

void PrismaUIImpl::ShowCounterOffer(int counterAmount, int patience) {
    if (!api || !view) return;

    nlohmann::json j = {{"counter", counterAmount}, {"patience", patience}};
    api->InteropCall(view, "ShowCounter", j.dump().c_str());
}

void PrismaUIImpl::ShowResult(bool accepted, int goldAmount, int relDelta) {
    if (!api || !view) return;

    nlohmann::json j = {{"accepted", accepted}, {"goldAmount", goldAmount}, {"relDelta", relDelta}};
    api->InteropCall(view, "ShowResult", j.dump().c_str());
}

void PrismaUIImpl::Hide() {
    if (!api || !view) return;
    api->Unfocus(view);
    api->Hide(view);
}

void PrismaUIImpl::OnDomReady(PrismaView) {
    DbgLog("PrismaUI: DOM ready - JS functions should now be available");
}

void PrismaUIImpl::OnBarterResult(const char* argument) {
    if (!argument) return;
    try {
        auto j = nlohmann::json::parse(argument);
        int offeredPrice = j.value("offeredPrice", 0);
        SKSE::GetTaskInterface()->AddTask([offeredPrice]() {
            BarterManager::GetSingleton()->OnPlayerOffer(offeredPrice);
        });
    } catch (...) {
        logger::error("PrismaUI: Failed to parse BarterResult");
    }
}

void PrismaUIImpl::OnCounterResponse(const char* argument) {
    if (!argument) return;
    try {
        auto j = nlohmann::json::parse(argument);
        int response = j.value("response", 2);
        SKSE::GetTaskInterface()->AddTask([response]() {
            BarterManager::GetSingleton()->OnCounterResponse(response);
        });
    } catch (...) {
        logger::error("PrismaUI: Failed to parse CounterResponse");
    }
}

void PrismaUIImpl::OnIntimidateAttempt(const char*) {
    SKSE::GetTaskInterface()->AddTask([]() {
        BarterManager::GetSingleton()->OnIntimidateAttempt();
    });
}
