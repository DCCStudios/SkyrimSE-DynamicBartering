#include "PCH.h"
#include "Menu/ConfigMenu.h"
#include "Settings.h"
#include "RelationshipManager.h"
#include "MerchantPersonality.h"
#include "UI/UIBridge.h"

// Minimal header for SKSEMenuFramework - only what we need
namespace SKSEMenuFramework {
    namespace Model {
        typedef void(__stdcall* RenderFunction)();
        using AddSectionItemFunction = void (*)(const char* path, RenderFunction rendererFunction);
    }
    namespace Internal {
        template <class T>
        T GetFunction(LPCSTR name) {
            static auto menuFramework = GetModuleHandle(L"SKSEMenuFramework");
            if (!menuFramework) return nullptr;
            return reinterpret_cast<T>(GetProcAddress(menuFramework, name));
        }
        inline std::string key;
    }
    inline void SetSection(std::string key) { Internal::key = key; }
    inline void AddSectionItem(std::string menu, Model::RenderFunction rendererFunction) {
        static auto func = Internal::GetFunction<Model::AddSectionItemFunction>("AddSectionItem");
        if (func) func((Internal::key + "/" + menu).c_str(), rendererFunction);
    }
    inline bool IsInstalled() {
        return GetModuleHandle(L"SKSEMenuFramework") != nullptr;
    }
}

// Forward declare ImGui functions we use (provided by SKSEMenuFramework)
struct ImGuiContext;
typedef unsigned int ImGuiID;

extern "C" {
    typedef bool (*ImGui_Begin_t)(const char*, bool*, int);
    typedef void (*ImGui_End_t)();
    typedef bool (*ImGui_Checkbox_t)(const char*, bool*);
    typedef bool (*ImGui_SliderFloat_t)(const char*, float*, float, float, const char*, int);
    typedef bool (*ImGui_SliderInt_t)(const char*, int*, int, int, const char*, int);
    typedef void (*ImGui_Text_t)(const char*, ...);
    typedef void (*ImGui_Separator_t)();
    typedef bool (*ImGui_Button_t)(const char*, ...);
    typedef bool (*ImGui_BeginTabBar_t)(const char*, int);
    typedef void (*ImGui_EndTabBar_t)();
    typedef bool (*ImGui_BeginTabItem_t)(const char*, bool*, int);
    typedef void (*ImGui_EndTabItem_t)();
    typedef bool (*ImGui_Combo_t)(const char*, int*, const char* const*, int, int);
}

void ConfigMenu::Register() {
    if (!SKSEMenuFramework::IsInstalled()) {
        logger::info("SKSEMenuFramework not installed, config menu unavailable");
        return;
    }

    SKSEMenuFramework::SetSection("DynamicBartering");
    SKSEMenuFramework::AddSectionItem("Settings", RenderMenu);
    logger::info("ConfigMenu registered with SKSEMenuFramework");
}

void ConfigMenu::RenderMenu() {
    auto menuFramework = GetModuleHandle(L"SKSEMenuFramework");
    if (!menuFramework) return;

    // Reset relationship tab tracking each frame; RenderRelationshipsTab sets it true
    relationshipsTabActive = false;

    auto ImGui_BeginTabBar = reinterpret_cast<ImGui_BeginTabBar_t>(GetProcAddress(menuFramework, "igBeginTabBar"));
    auto ImGui_EndTabBar = reinterpret_cast<ImGui_EndTabBar_t>(GetProcAddress(menuFramework, "igEndTabBar"));
    auto ImGui_BeginTabItem = reinterpret_cast<ImGui_BeginTabItem_t>(GetProcAddress(menuFramework, "igBeginTabItem"));
    auto ImGui_EndTabItem = reinterpret_cast<ImGui_EndTabItem_t>(GetProcAddress(menuFramework, "igEndTabItem"));

    if (!ImGui_BeginTabBar || !ImGui_EndTabBar) {
        RenderGeneralTab();
        return;
    }

    if (ImGui_BeginTabBar("DynBarterTabs", 0)) {
        if (ImGui_BeginTabItem && ImGui_BeginTabItem("General", nullptr, 0)) {
            RenderGeneralTab();
            if (ImGui_EndTabItem) ImGui_EndTabItem();
        }
        if (ImGui_BeginTabItem && ImGui_BeginTabItem("Cart", nullptr, 0)) {
            RenderCartTab();
            if (ImGui_EndTabItem) ImGui_EndTabItem();
        }
        if (ImGui_BeginTabItem && ImGui_BeginTabItem("Pricing", nullptr, 0)) {
            RenderPricingTab();
            if (ImGui_EndTabItem) ImGui_EndTabItem();
        }
        if (ImGui_BeginTabItem && ImGui_BeginTabItem("Relationships", nullptr, 0)) {
            RenderRelationshipsTab();
            if (ImGui_EndTabItem) ImGui_EndTabItem();
        }
        if (ImGui_BeginTabItem && ImGui_BeginTabItem("Personalities", nullptr, 0)) {
            RenderPersonalitiesTab();
            if (ImGui_EndTabItem) ImGui_EndTabItem();
        }
        if (ImGui_BeginTabItem && ImGui_BeginTabItem("Debug", nullptr, 0)) {
            RenderDebugTab();
            if (ImGui_EndTabItem) ImGui_EndTabItem();
        }
        ImGui_EndTabBar();
    }
}

void ConfigMenu::RenderGeneralTab() {
    auto menuFramework = GetModuleHandle(L"SKSEMenuFramework");
    if (!menuFramework) return;

    auto ImGui_Checkbox = reinterpret_cast<ImGui_Checkbox_t>(GetProcAddress(menuFramework, "igCheckbox"));
    auto ImGui_SliderInt = reinterpret_cast<ImGui_SliderInt_t>(GetProcAddress(menuFramework, "igSliderInt"));
    auto ImGui_Combo = reinterpret_cast<ImGui_Combo_t>(GetProcAddress(menuFramework, "igCombo"));
    auto ImGui_Text = reinterpret_cast<ImGui_Text_t>(GetProcAddress(menuFramework, "igText"));
    auto ImGui_Separator = reinterpret_cast<ImGui_Separator_t>(GetProcAddress(menuFramework, "igSeparator"));
    auto ImGui_Button = reinterpret_cast<ImGui_Button_t>(GetProcAddress(menuFramework, "igButton"));

    auto* s = Settings::GetSingleton();
    if (!s) return;

    if (ImGui_Checkbox) {
        ImGui_Checkbox("Enable Mod", &s->modEnabled);
        ImGui_Checkbox("Show Acceptance Hint", &s->showAcceptanceHint);
        ImGui_Checkbox("Show Relationship Preview", &s->showRelationshipPreview);
        ImGui_Checkbox("Skip Items Below Threshold", &s->skipBelowThreshold);
    }
    if (ImGui_SliderInt) {
        ImGui_SliderInt("Popup Delay (ms)", &s->popupDelayMs, 0, 1000, "%d", 0);
        ImGui_SliderInt("Value Threshold", &s->valueThreshold, 0, 500, "%d", 0);
    }

    if (ImGui_Separator) ImGui_Separator();
    if (ImGui_Text) ImGui_Text("UI Backend");

    if (ImGui_Combo) {
        static int uiModeIdx = static_cast<int>(s->uiMode);
        static const char* uiModeItems[] = { "Auto", "Scaleform (SWF)", "PrismaUI (HTML)" };
        if (ImGui_Combo("UI Mode", &uiModeIdx, uiModeItems, 3, 3)) {
            UIMode newMode = static_cast<UIMode>(uiModeIdx);
            s->uiMode = newMode;
            UIBridge::GetSingleton()->SwitchMode(newMode);
        }
    } else if (ImGui_Button) {
        // Fallback: use buttons when combo isn't available
        auto currentMode = s->uiMode;
        if (ImGui_Text) {
            const char* modeStr = currentMode == UIMode::Auto ? "Auto" :
                                  currentMode == UIMode::ScaleformSWF ? "Scaleform (SWF)" : "PrismaUI (HTML)";
            ImGui_Text("Current: %s", modeStr);
        }
        if (ImGui_Button("Use SWF", 0) && currentMode != UIMode::ScaleformSWF) {
            s->uiMode = UIMode::ScaleformSWF;
            UIBridge::GetSingleton()->SwitchMode(UIMode::ScaleformSWF);
        }
        if (ImGui_Button("Use PrismaUI", 0) && currentMode != UIMode::PrismaUI) {
            s->uiMode = UIMode::PrismaUI;
            UIBridge::GetSingleton()->SwitchMode(UIMode::PrismaUI);
        }
        if (ImGui_Button("Auto", 0) && currentMode != UIMode::Auto) {
            s->uiMode = UIMode::Auto;
            UIBridge::GetSingleton()->SwitchMode(UIMode::Auto);
        }
    } else if (ImGui_Text) {
        ImGui_Text("UI Mode: %s (change in INI)",
            s->uiMode == UIMode::ScaleformSWF ? "SWF" :
            s->uiMode == UIMode::PrismaUI ? "PrismaUI" : "Auto");
    }

    if (ImGui_Separator) ImGui_Separator();
    if (ImGui_Text) ImGui_Text("Controller Glyphs");

    if (ImGui_Combo) {
        static int iconStyleIdx = static_cast<int>(s->gamepadIconStyle);
        static const char* iconStyleItems[] = { "Xbox", "PlayStation" };
        if (ImGui_Combo("Gamepad Icon Style", &iconStyleIdx, iconStyleItems, 2, 2)) {
            s->gamepadIconStyle = static_cast<GamepadIconStyle>(iconStyleIdx);
            s->Save();
        }
    } else if (ImGui_Button) {
        if (ImGui_Text) {
            ImGui_Text("Gamepad Icons: %s",
                s->gamepadIconStyle == GamepadIconStyle::PlayStation ? "PlayStation" : "Xbox");
        }
        if (ImGui_Button("Use Xbox", 0)) {
            s->gamepadIconStyle = GamepadIconStyle::Xbox;
            s->Save();
        }
        if (ImGui_Button("Use PlayStation", 0)) {
            s->gamepadIconStyle = GamepadIconStyle::PlayStation;
            s->Save();
        }
    }
}

void ConfigMenu::RenderCartTab() {
    auto menuFramework = GetModuleHandle(L"SKSEMenuFramework");
    if (!menuFramework) return;

    auto ImGui_SliderFloat = reinterpret_cast<ImGui_SliderFloat_t>(GetProcAddress(menuFramework, "igSliderFloat"));
    auto ImGui_Text = reinterpret_cast<ImGui_Text_t>(GetProcAddress(menuFramework, "igText"));
    auto ImGui_Separator = reinterpret_cast<ImGui_Separator_t>(GetProcAddress(menuFramework, "igSeparator"));
    auto ImGui_Button = reinterpret_cast<ImGui_Button_t>(GetProcAddress(menuFramework, "igButton"));
    auto ImGui_Checkbox = reinterpret_cast<ImGui_Checkbox_t>(GetProcAddress(menuFramework, "igCheckbox"));

    auto* s = Settings::GetSingleton();
    if (!s) return;

    if (ImGui_Text) {
        ImGui_Text("Hold the Barter key/button to negotiate the whole cart.");
        ImGui_Text("Tap it to add/remove the highlighted item.");
    }
    if (ImGui_Separator) ImGui_Separator();

    if (ImGui_SliderFloat) {
        ImGui_SliderFloat("Hold Threshold (sec)", &s->cartHoldThreshold, 0.2f, 1.5f, "%.2f", 0);
    }

    if (ImGui_Separator) ImGui_Separator();
    if (ImGui_Text) {
        ImGui_Text("Cart Window Position (stage is ~1280 x 720)");
        ImGui_Text("Changes apply live while the barter menu is open.");
    }

    if (ImGui_SliderFloat) {
        // Allow a little overscan past the stage so the panel can be tucked anywhere.
        ImGui_SliderFloat("Position X", &s->cartPanelX, -100.0f, 1280.0f, "%.0f", 0);
        ImGui_SliderFloat("Position Y", &s->cartPanelY, -100.0f, 720.0f, "%.0f", 0);
        ImGui_SliderFloat("Scale", &s->cartPanelScale, 0.5f, 1.5f, "%.2f", 0);
    }

    if (ImGui_Separator) ImGui_Separator();
    if (ImGui_Button) {
        if (ImGui_Button("Reset Position", 0)) {
            s->cartPanelX = 596.0f;
            s->cartPanelY = 110.0f;
            s->cartPanelScale = 1.0f;
        }
        if (ImGui_Button("Save Cart Settings", 0)) {
            s->Save();
        }
    }
    if (ImGui_Text) ImGui_Text("Tip: use Save to keep position across sessions.");
}

void ConfigMenu::RenderPricingTab() {
    auto menuFramework = GetModuleHandle(L"SKSEMenuFramework");
    if (!menuFramework) return;

    auto ImGui_SliderFloat = reinterpret_cast<ImGui_SliderFloat_t>(GetProcAddress(menuFramework, "igSliderFloat"));
    auto ImGui_Checkbox = reinterpret_cast<ImGui_Checkbox_t>(GetProcAddress(menuFramework, "igCheckbox"));

    auto* s = Settings::GetSingleton();
    if (!s) return;

    if (ImGui_SliderFloat) {
        ImGui_SliderFloat("Base Acceptance %", &s->baseAcceptanceChance, 0.0f, 100.0f, "%.1f", 0);
        ImGui_SliderFloat("Speech Weight", &s->speechWeight, 0.0f, 50.0f, "%.1f", 0);
        ImGui_SliderFloat("Haggling Perk Bonus/Rank", &s->hagglingPerkBonus, 0.0f, 10.0f, "%.1f", 0);
        ImGui_SliderFloat("Persuasion Perk Bonus", &s->persuasionPerkBonus, 0.0f, 30.0f, "%.1f", 0);
        ImGui_SliderFloat("Allure Bonus", &s->allureBonus, 0.0f, 20.0f, "%.1f", 0);
        ImGui_SliderFloat("Relationship Weight", &s->relationshipWeight, 0.0f, 30.0f, "%.1f", 0);
        ImGui_SliderFloat("Personality Weight", &s->personalityWeight, 0.0f, 25.0f, "%.1f", 0);
        ImGui_SliderFloat("Deal History Weight", &s->dealHistoryWeight, 0.0f, 15.0f, "%.1f", 0);
        ImGui_SliderFloat("Greed Factor", &s->greedFactor, 0.5f, 3.0f, "%.2f", 0);
        ImGui_SliderFloat("Stolen Item Penalty", &s->stolenItemPenalty, 0.0f, 50.0f, "%.1f", 0);
        ImGui_SliderFloat("Fence Perk Reduction %", &s->fencePerkReduction, 0.0f, 100.0f, "%.0f", 0);
    }
    if (ImGui_Checkbox) {
        ImGui_Checkbox("Use Vanilla Base Price", &s->useVanillaBasePrice);
    }
}

void ConfigMenu::RenderRelationshipsTab() {
    auto menuFramework = GetModuleHandle(L"SKSEMenuFramework");
    if (!menuFramework) return;

    auto ImGui_SliderFloat = reinterpret_cast<ImGui_SliderFloat_t>(GetProcAddress(menuFramework, "igSliderFloat"));
    auto ImGui_SliderInt = reinterpret_cast<ImGui_SliderInt_t>(GetProcAddress(menuFramework, "igSliderInt"));
    auto ImGui_Text = reinterpret_cast<ImGui_Text_t>(GetProcAddress(menuFramework, "igText"));
    auto ImGui_Separator = reinterpret_cast<ImGui_Separator_t>(GetProcAddress(menuFramework, "igSeparator"));
    auto ImGui_Button = reinterpret_cast<ImGui_Button_t>(GetProcAddress(menuFramework, "igButton"));
    auto ImGui_Checkbox = reinterpret_cast<ImGui_Checkbox_t>(GetProcAddress(menuFramework, "igCheckbox"));

    auto* s = Settings::GetSingleton();
    if (!s) return;

    // Reload data once when tab becomes active (transition from inactive -> active)
    bool justOpened = !relationshipsTabActive;
    relationshipsTabActive = true;
    if (justOpened) {
        RelationshipManager::GetSingleton()->LoadData();
    }

    // --- Relationship Settings ---
    if (ImGui_Text) ImGui_Text("== Relationship Settings ==");
    if (ImGui_Checkbox) {
        ImGui_Checkbox("Relationship Affects Prices", &s->relationshipPricing);
    }
    if (ImGui_Text) {
        ImGui_Text("When on, low relationships raise prices and high");
        ImGui_Text("relationships improve haggling success.");
    }
    if (ImGui_Separator) ImGui_Separator();

    if (ImGui_SliderFloat) {
        ImGui_SliderFloat("Rel Gain on Fair Deal", &s->relGainFairDeal, 1.0f, 10.0f, "%.1f", 0);
        ImGui_SliderFloat("Rel Loss on Insult", &s->relLossInsult, 1.0f, 20.0f, "%.1f", 0);
        ImGui_SliderFloat("Decay Rate/Day", &s->relDecayRate, 0.0f, 1.0f, "%.2f", 0);
        ImGui_SliderFloat("Price Jack Intensity", &s->priceJackIntensity, 0.5f, 3.0f, "%.2f", 0);
    }
    if (ImGui_SliderInt) {
        ImGui_SliderInt("Price Jack Threshold", &s->priceJackThreshold, -50, 0, "%d", 0);
    }

    if (ImGui_Separator) ImGui_Separator();
    if (ImGui_Text) ImGui_Text("== Merchant Relationships (from co-save) ==");
    if (ImGui_Text) ImGui_Text("Adjust relationships below. Changes auto-save.");

    auto* relMgr = RelationshipManager::GetSingleton();
    auto& allData = relMgr->GetAllData();

    if (allData.empty()) {
        if (ImGui_Text) ImGui_Text("  (No merchants interacted with yet)");
    } else {
        // Track changes so we can save at the end
        bool anyChanged = false;

        // Sort merchants by name for display stability
        std::vector<std::pair<RE::FormID, const MerchantMemory*>> sorted;
        sorted.reserve(allData.size());
        for (const auto& [id, mem] : allData) {
            sorted.push_back({id, &mem});
        }
        std::sort(sorted.begin(), sorted.end(), [](const auto& a, const auto& b) {
            return a.second->merchantName < b.second->merchantName;
        });

        for (const auto& [id, memPtr] : sorted) {
            const auto& mem = *memPtr;
            if (ImGui_Separator) ImGui_Separator();

            // Label with name, FormID, and stats
            if (ImGui_Text) {
                const char* relLabel =
                    mem.relationship >= 50 ? "Trusted" :
                    mem.relationship >= 20 ? "Warm" :
                    mem.relationship >= 5  ? "Friendly" :
                    mem.relationship >= -5 ? "Neutral" :
                    mem.relationship >= -20 ? "Cool" :
                    mem.relationship >= -50 ? "Hostile" : "Despised";

                ImGui_Text("%s [0x%08X] - %s (%d)",
                    mem.merchantName.c_str(), id, relLabel, mem.relationship);
                ImGui_Text("  Deals: %d | Accepted: %d | Lowballs: %d",
                    mem.totalDeals, mem.acceptedDeals, mem.lowballCount);
            }

            // Editable relationship slider
            if (ImGui_SliderInt) {
                // Need a unique label per slider for ImGui
                char sliderLabel[128];
                snprintf(sliderLabel, sizeof(sliderLabel), "##rel_%08X", id);
                int relValue = mem.relationship;
                int relMin = s->relMin;
                int relMax = s->relMax;
                if (ImGui_SliderInt(sliderLabel, &relValue, relMin, relMax, "%d", 0)) {
                    relMgr->SetRelationship(id, relValue);
                    anyChanged = true;
                }
            }

            // Reset button per merchant
            if (ImGui_Button) {
                char btnLabel[128];
                snprintf(btnLabel, sizeof(btnLabel), "Reset##%08X", id);
                if (ImGui_Button(btnLabel, 0)) {
                    relMgr->ResetMerchant(id);
                    anyChanged = true;
                }
            }
        }

        if (anyChanged) {
            relMgr->SaveData();
        }
    }

    if (ImGui_Separator) ImGui_Separator();

    if (ImGui_Button) {
        if (ImGui_Button("Reset All Relationships", 0)) {
            relMgr->ResetAll();
            relMgr->SaveData();
        }
        if (ImGui_Button("Reload from Co-Save", 0)) {
            relMgr->LoadData();
        }
        if (ImGui_Button("Save Settings", 0)) {
            s->Save();
        }
    }
}

void ConfigMenu::RenderPersonalitiesTab() {
    auto menuFramework = GetModuleHandle(L"SKSEMenuFramework");
    if (!menuFramework) return;

    auto ImGui_Text = reinterpret_cast<ImGui_Text_t>(GetProcAddress(menuFramework, "igText"));
    auto ImGui_SliderFloat = reinterpret_cast<ImGui_SliderFloat_t>(GetProcAddress(menuFramework, "igSliderFloat"));
    auto ImGui_SliderInt = reinterpret_cast<ImGui_SliderInt_t>(GetProcAddress(menuFramework, "igSliderInt"));

    auto* s = Settings::GetSingleton();
    if (!s) return;

    if (ImGui_Text) {
        ImGui_Text("Greedy: -15%% accept, 8 offense, 20%% counter, 30%% split, 1.5x jack");
        ImGui_Text("Fair: 0%% accept, 4 offense, 40%% counter, 50%% split, 1.0x jack");
        ImGui_Text("Generous: +10%% accept, 2 offense, 60%% counter, 70%% split, 0.5x jack");
        ImGui_Text("Sleazy: +5%% accept, 0 offense, 80%% counter, 40-60%% split, 0.8x jack");
    }
    if (ImGui_SliderFloat) {
        ImGui_SliderFloat("Counter Base Chance", &s->counterOfferBaseChance, 0.0f, 100.0f, "%.0f%%", 0);
    }
    if (ImGui_SliderInt) {
        ImGui_SliderInt("Patience Rounds", &s->counterOfferPatience, 1, 5, "%d", 0);
    }
}

void ConfigMenu::RenderDebugTab() {
    auto menuFramework = GetModuleHandle(L"SKSEMenuFramework");
    if (!menuFramework) return;

    auto ImGui_Checkbox = reinterpret_cast<ImGui_Checkbox_t>(GetProcAddress(menuFramework, "igCheckbox"));
    auto ImGui_Text = reinterpret_cast<ImGui_Text_t>(GetProcAddress(menuFramework, "igText"));
    auto ImGui_Separator = reinterpret_cast<ImGui_Separator_t>(GetProcAddress(menuFramework, "igSeparator"));
    auto ImGui_Button = reinterpret_cast<ImGui_Button_t>(GetProcAddress(menuFramework, "igButton"));

    auto* s = Settings::GetSingleton();
    if (!s) return;

    if (ImGui_Checkbox) {
        ImGui_Checkbox("Debug Logging (verbose)", &s->debugLogging);
        ImGui_Checkbox("Show Roll in Console", &s->showRollInConsole);
        ImGui_Checkbox("Force Next Accept", &s->forceAccept);
        ImGui_Checkbox("Force Next Reject", &s->forceReject);
        ImGui_Checkbox("Force Next Counter", &s->forceCounter);
    }

    if (ImGui_Separator) ImGui_Separator();
    if (ImGui_Text) ImGui_Text("Debug Tools:");

    if (ImGui_Button) {
        if (ImGui_Button("Dump Merchant Data to Log", 0)) {
            auto& allData = RelationshipManager::GetSingleton()->GetAllData();
            for (const auto& [id, mem] : allData) {
                logger::info("Merchant 0x{:08X} '{}': rel={} deals={} accepted={} lowballs={}",
                    id, mem.merchantName, mem.relationship, mem.totalDeals,
                    mem.acceptedDeals, mem.lowballCount);
            }
        }
        if (ImGui_Button("Save Data Now", 0)) {
            RelationshipManager::GetSingleton()->SaveData();
            Settings::GetSingleton()->Save();
        }
    }
}
