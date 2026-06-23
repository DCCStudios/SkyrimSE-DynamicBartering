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
                    if (btn->IsDown()) {
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
                                gamepadActivatePress = true;
                                break;
                            case 0x2000:  // B button - cancel
                                gamepadCancel = true;
                                break;
                            case 0x4000:  // X button - re-offer
                                gamepadX = true;
                                break;
                            case 0x8000:  // Y button - cart add
                                gamepadY = true;
                                logger::info("InputDeviceSink: Y button DOWN detected (scan=0x8000)");
                                break;
                        }
                    } else if (btn->IsUp()) {
                        if (scan == 0x0004 && gamepadDir == -1) gamepadDir = 0;
                        if (scan == 0x0008 && gamepadDir == 1) gamepadDir = 0;
                        if (scan == 0x0100 && gamepadDir == -5) gamepadDir = 0;
                        if (scan == 0x0200 && gamepadDir == 5) gamepadDir = 0;
                    }
                    // Track Y button held state for cart hold detection
                    if (scan == 0x8000) {
                        gamepadYHeld = btn->IsPressed();
                    }
                    // Track A button held state (activate-to-cart / hold-to-open).
                    if (scan == 0x1000) {
                        gamepadActivateHeld = btn->IsPressed();
                    }
                    // Track X button held state (intimidate hold-to-fill in the offer window).
                    if (scan == 0x4000) {
                        gamepadXHeld = btn->IsPressed();
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
                if (device == RE::INPUT_DEVICE::kKeyboard) {
                    if (auto* btn = evt->AsButtonEvent()) {
                        auto scan = btn->GetIDCode();
                        if (btn->IsDown()) {
                            if (scan == 19) keyboardR = true;   // Scan code 19 = R
                            if (scan == 48) {
                                keyboardB = true;   // Scan code 48 = B (cart add)
                                logger::info("InputDeviceSink: B key DOWN detected (scan=48)");
                            }
                            if (scan == 18) keyboardActivatePress = true;  // E = Activate
                        }
                        // Track held state for B key (cart hold detection)
                        if (scan == 48) {
                            keyboardBHeld = btn->IsPressed();
                        }
                        // Track held state for E (activate-to-cart / hold-to-open).
                        if (scan == 18) {
                            keyboardActivateHeld = btn->IsPressed();
                        }
                        // Track R held state (intimidate hold-to-fill in the offer window).
                        if (scan == 19) {
                            keyboardRHeld = btn->IsPressed();
                        }
                    }
                } else if (device == RE::INPUT_DEVICE::kMouse) {
                    // Left mouse button (idCode 0) doubles as an activate trigger so
                    // a click adds to the cart and a hold opens the offer window.
                    if (auto* btn = evt->AsButtonEvent()) {
                        if (btn->GetIDCode() == 0) {
                            if (btn->IsDown()) mouseLeftPress = true;
                            mouseLeftHeld = btn->IsPressed();
                        }
                    }
                }
            }
        }
        return RE::BSEventNotifyControl::kContinue;
    }

    bool IsUsingGamepad() const { return usingGamepad; }
    bool HasChanged() { bool c = changed; changed = false; return c; }

    // Gamepad directional input for slider. NOTE: gamepadDir is *held state* - it is set
    // on a D-pad/bumper DOWN and cleared on UP, so it must be READ (not consumed), or the
    // hold-to-repeat below would clear itself the very next frame (no XInput auto-repeat
    // fires while a button stays down).
    int GetGamepadDir() const { return gamepadDir.load(); }
    float GetThumbstickX() const { return thumbstickX; }
    bool ConsumeAccept() { return gamepadAccept.exchange(false); }
    bool ConsumeCancel() { return gamepadCancel.exchange(false); }
    bool ConsumeX() { return gamepadX.exchange(false); }
    bool ConsumeY() { return gamepadY.exchange(false); }
    bool ConsumeR() { return keyboardR.exchange(false); }
    bool ConsumeB() { return keyboardB.exchange(false); }

    // Activate trigger (A on gamepad, E or left-mouse on keyboard/mouse). Used when
    // "block quick buy" repurposes the activate input for cart add / hold-to-open.
    bool ConsumeActivatePress() {
        if (usingGamepad) return gamepadActivatePress.exchange(false);
        bool kb = keyboardActivatePress.exchange(false);
        bool ms = mouseLeftPress.exchange(false);
        return kb || ms;
    }
    bool IsActivateHeld() const {
        return usingGamepad ? gamepadActivateHeld.load()
                            : (keyboardActivateHeld.load() || mouseLeftHeld.load());
    }

    // Hold state queries (non-consuming; polled each frame for hold detection)
    bool IsYHeld() const { return gamepadYHeld; }
    bool IsBHeld() const { return keyboardBHeld; }

    // Confirm/Intimidate held state for the offer window's hold-to-fill buttons.
    // Confirm = A (gamepad) / E (keyboard); Intimidate = X (gamepad) / R (keyboard).
    // Mouse is handled in the menu via button hover + left-button-held.
    bool IsConfirmHeld() const {
        return usingGamepad ? gamepadActivateHeld.load() : keyboardActivateHeld.load();
    }
    bool IsIntimidateHeld() const {
        return usingGamepad ? gamepadXHeld.load() : keyboardRHeld.load();
    }

private:
    InputDeviceSink() = default;
    std::atomic<bool> usingGamepad{ false };
    std::atomic<bool> changed{ false };
    std::atomic<int> gamepadDir{ 0 };
    std::atomic<float> thumbstickX{ 0.0f };
    std::atomic<bool> gamepadAccept{ false };
    std::atomic<bool> gamepadCancel{ false };
    std::atomic<bool> gamepadX{ false };
    std::atomic<bool> gamepadXHeld{ false };
    std::atomic<bool> gamepadY{ false };
    std::atomic<bool> gamepadYHeld{ false };
    std::atomic<bool> keyboardR{ false };
    std::atomic<bool> keyboardRHeld{ false };
    std::atomic<bool> keyboardB{ false };
    std::atomic<bool> keyboardBHeld{ false };
    // Activate trigger state (A / E / left-mouse).
    std::atomic<bool> gamepadActivatePress{ false };
    std::atomic<bool> gamepadActivateHeld{ false };
    std::atomic<bool> keyboardActivatePress{ false };
    std::atomic<bool> keyboardActivateHeld{ false };
    std::atomic<bool> mouseLeftPress{ false };
    std::atomic<bool> mouseLeftHeld{ false };
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
    void SetResult(bool accepted, int goldAmount, int relDelta);
    // Theatrical "merchant yielded" screen for a successful intimidation (deal already
    // done). Themed via the active palette; the intimidation red stays fixed in every
    // theme, mirroring the Intimidate button.
    void SetIntimidationSuccess(int coercedPrice, int relDelta, bool buying);
    void RestoreOfferUI();
    void UpdateRelationshipMeter(int relationship);
    // Live-update the stored standing + meter (called when the relationship changes
    // mid-session, e.g. failed intimidation or an SKSE-menu debug edit).
    void UpdateRelationship(int relationship);
    // Toggle the placed keybind-glyph hint row for the given state
    // (0=offer, 1=counter, 2=result) based on the active input device.
    void ApplyHintCells(int state);
    // Reposition the standalone coin glyph so it hugs the (centered) price number.
    void PositionCoin();
    // Recolor the baked gold art (panel corners, ornament, slider, submit button) to
    // match the active UI theme via a multiply color-transform. Identity for Default.
    void ApplyTheme();

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
    bool sliderTouched{ false };  // player has moved the slider off its market start
    bool currentIsBuying{ true }; // direction of the active offer (affects standing preview)
    float inputCooldown{ 0.0f };  // Prevents activation key from immediately submitting
    // Relationship-meter marker animation (eased toward target, like the slider handle).
    float relMarkerTargetX{ 60.0f };
    float relMarkerCurX{ -1.0f };  // -1 = uninitialized (snaps to a centered intro start)
    int currentRelationship{ 0 };  // latest effective standing, kept in sync for live meter updates

    // --- Hold-to-fill confirm/intimidate ---------------------------------------
    // Elapsed hold time (seconds) accumulated while the button is held; -1 = idle.
    float submitHoldElapsed{ 0.0f };
    float intimidateHoldElapsed{ 0.0f };
    // "Armed" once the trigger has been seen released since opening / last commit, so a
    // key still held from opening the window can't instantly charge a confirm.
    bool submitHoldArmed{ false };
    bool intimidateHoldArmed{ false };
    // Drives the per-frame button fill + commit; resets both bars when idle.
    void UpdateHoldToConfirm(float interval, bool mouseHeld);
    void SetButtonFill(const char* buttonName, float progress);
};

class ScaleformUIImpl : public IBarterUI {
public:
    bool Initialize() override;
    void ShowOffer(const OfferData& data) override;
    void ShowCounterOffer(int counterAmount, int patience) override;
    void ShowResult(bool accepted, int goldAmount, int relDelta) override;
    void ShowIntimidationSuccess(int coercedPrice, int relDelta, bool buying) override;
    void UpdateRelationship(int effectiveRelationship) override;
    void Hide() override;
    bool IsAvailable() const override { return true; }
};
