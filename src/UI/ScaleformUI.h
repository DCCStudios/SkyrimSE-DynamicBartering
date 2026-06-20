#pragma once
#include "UI/UIBridge.h"
#include "BarterManager.h"
#include <optional>

class InputDeviceSink : public RE::BSTEventSink<RE::InputEvent*> {
public:
    static InputDeviceSink* GetSingleton() {
        static InputDeviceSink instance;
        return &instance;
    }

    static void Register() {
        if (auto* mgr = RE::BSInputDeviceManager::GetSingleton()) {
            mgr->AddEventSink(GetSingleton());
            logger::info("InputDeviceSink registered");
        }
    }

    RE::BSEventNotifyControl ProcessEvent(RE::InputEvent* const* a_event,
        RE::BSTEventSource<RE::InputEvent*>*) override {
        if (!a_event) return RE::BSEventNotifyControl::kContinue;

        for (auto* evt = *a_event; evt; evt = evt->next) {
            auto device = evt->GetDevice();
            if (device == RE::INPUT_DEVICE::kGamepad) {
                if (!usingGamepad) {
                    usingGamepad = true;
                    changed = true;
                }
                // Track gamepad directional input for slider
                if (auto* btn = evt->AsButtonEvent()) {
                    auto scan = btn->GetIDCode();
                    // XInput button masks used as IDCodes:
                    // DPad-Up=0x0001, Down=0x0002, Left=0x0004, Right=0x0008
                    // LB=0x0100, RB=0x0200, A=0x1000, B=0x2000
                    if (btn->IsPressed()) {
                        switch (scan) {
                            case 0x0004:  // D-pad Left
                                gamepadDir = -1;
                                break;
                            case 0x0008:  // D-pad Right
                                gamepadDir = 1;
                                break;
                            case 0x0100:  // LB
                                gamepadDir = -5;
                                break;
                            case 0x0200:  // RB
                                gamepadDir = 5;
                                break;
                            case 0x1000:  // A button - accept
                                gamepadAccept = true;
                                break;
                            case 0x2000:  // B button - cancel
                                gamepadCancel = true;
                                break;
                            case 0x4000:  // X button - re-offer
                                gamepadX = true;
                                break;
                            case 0x8000:  // Y button - alternate action
                                gamepadY = true;
                                break;
                        }
                    } else if (btn->IsUp()) {
                        if (scan == 0x0004 && gamepadDir == -1) gamepadDir = 0;
                        if (scan == 0x0008 && gamepadDir == 1) gamepadDir = 0;
                        if (scan == 0x0100 && gamepadDir == -5) gamepadDir = 0;
                        if (scan == 0x0200 && gamepadDir == 5) gamepadDir = 0;
                    }
                }
                // Track thumbstick axis for continuous slider movement
                if (auto* thumbEvt = evt->AsThumbstickEvent()) {
                    float xVal = thumbEvt->xValue;
                    if (std::abs(xVal) > 0.25f) {
                        thumbstickX = xVal;
                    } else {
                        thumbstickX = 0.0f;
                    }
                }
            } else if (device == RE::INPUT_DEVICE::kKeyboard || device == RE::INPUT_DEVICE::kMouse) {
                if (usingGamepad) {
                    usingGamepad = false;
                    changed = true;
                }
                // Track keyboard R key for re-offer in counter state
                if (device == RE::INPUT_DEVICE::kKeyboard) {
                    if (auto* btn = evt->AsButtonEvent()) {
                        if (btn->IsPressed() && btn->GetIDCode() == 19) {  // Scan code 19 = R
                            keyboardR = true;
                        }
                    }
                }
            }
        }
        return RE::BSEventNotifyControl::kContinue;
    }

    bool IsUsingGamepad() const { return usingGamepad; }
    bool HasChanged() { bool c = changed; changed = false; return c; }

    // Gamepad directional input for slider
    int ConsumeGamepadDir() { int d = gamepadDir.exchange(0); return d; }
    float GetThumbstickX() const { return thumbstickX; }
    bool ConsumeAccept() { return gamepadAccept.exchange(false); }
    bool ConsumeCancel() { return gamepadCancel.exchange(false); }
    bool ConsumeX() { return gamepadX.exchange(false); }
    bool ConsumeY() { return gamepadY.exchange(false); }
    bool ConsumeR() { return keyboardR.exchange(false); }

private:
    InputDeviceSink() = default;
    std::atomic<bool> usingGamepad{ false };
    std::atomic<bool> changed{ false };
    std::atomic<int> gamepadDir{ 0 };
    std::atomic<float> thumbstickX{ 0.0f };
    std::atomic<bool> gamepadAccept{ false };
    std::atomic<bool> gamepadCancel{ false };
    std::atomic<bool> gamepadX{ false };
    std::atomic<bool> gamepadY{ false };
    std::atomic<bool> keyboardR{ false };
};

class BarterOfferMenu : public RE::IMenu {
public:
    static constexpr const char* MENU_NAME = "BarterOfferMenu";
    static constexpr std::string_view MENU_PATH = "DynamicBartering/BarterOffer";

    BarterOfferMenu();
    static void Register();
    static RE::IMenu* Creator();
    static void Show();
    static void Hide();

    void AdvanceMovie(float a_interval, std::uint32_t a_currentTime) override;
    RE::UI_MESSAGE_RESULTS ProcessMessage(RE::UIMessage& a_message) override;

    void SetOfferData(const struct OfferData& data);
    void SetCounterOffer(int amount, int patience);
    void SetResult(bool accepted, int amount);
    void RestoreOfferUI();

    static inline std::optional<OfferData> pendingOfferData;

private:
    class FxDelegateCallback : public RE::FxDelegateHandler {
    public:
        void Accept(CallbackProcessor* a_cbReg) override;
    };

    static void OnOfferSubmit(const RE::FxDelegateArgs& a_params);
    static void OnCounterResponse(const RE::FxDelegateArgs& a_params);
    static void OnIntimidateAttempt(const RE::FxDelegateArgs& a_params);
    static void OnClose(const RE::FxDelegateArgs& a_params);

    // Mouse drag tracking (C++ side - reliable)
    bool sliderDragging{ false };
    float gamepadHoldTimer{ 0.0f };
    int gamepadHoldDir{ 0 };
    bool gamepadGraceDone{ false };
    bool lastInputWasGamepad{ false };
    int hoveredButton{ -1 };
    bool showingResult{ false };
    bool showingCounter{ false };
    bool lastResultAccepted{ false };
    float inputCooldown{ 0.0f };  // Prevents activation key from immediately submitting
};

class ScaleformUIImpl : public IBarterUI {
public:
    bool Initialize() override;
    void ShowOffer(const OfferData& data) override;
    void ShowCounterOffer(int counterAmount, int patience) override;
    void ShowResult(bool accepted, int relDelta) override;
    void Hide() override;
    bool IsAvailable() const override { return true; }
};
