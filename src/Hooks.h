#pragma once
#include <chrono>

class Hooks {
public:
    static void Install();

    // Set true while WE re-invoke the vanilla "ItemSelect" callback to perform a
    // negotiated transaction, so our interceptor forwards straight to vanilla.
    static inline bool replayingItemSelect = false;

    // Cooldown: after a negotiation finishes, used by BarterManager to timestamp.
    static inline std::chrono::steady_clock::time_point lastNegotiationEnd{};

    // The vanilla BarterMenu's own "ItemSelect" callback, captured during Accept.
    static inline RE::FxDelegateHandler::CallbackFn* originalItemSelect = nullptr;

    // Cart hold state: tracked from ProcessMessageBart kUpdate
    static inline float cartHoldTimer = 0.0f;
    static inline bool cartHoldActive = false;

    // Whether an inventory item is currently highlighted in the BarterMenu.
    // Updated each frame in AdvanceMovieBart; read by BarterCartMenu.
    static inline bool itemHighlighted = false;

    // Computed prompt state (set each frame in AdvanceMovieBart, applied by the
    // injected overlay). Position is in _root space.
    static inline bool  promptShow = false;
    static inline float promptX = 0.0f;
    static inline float promptY = 0.0f;
    static inline bool  promptGamepad = false;  // glyph + positioning mode

    // Cart-button state: a press starts a hold; releasing before the threshold is a
    // TAP (toggles the highlighted item), holding past the threshold OPENS the cart
    // offer (without toggling). Prevents accidental adds when holding to open.
    static inline bool cartPendingTap = false;

    using ProcessMessageFunc = RE::UI_MESSAGE_RESULTS(RE::BarterMenu*, RE::UIMessage&);
    static inline REL::Relocation<ProcessMessageFunc> _ProcessMessageBart;

    static void ItemSelectInterceptor(const RE::FxDelegateArgs& a_params);

    // --- Cart transfer support (drives the vanilla selection for ItemSelect) ----
    // True if the BarterMenu is currently showing the vendor's (buy) categories.
    static bool CurrentSideIsBuying(RE::BarterMenu* menu);
    // Switch the visible category to the vendor (buy) or player (sell) side. The
    // list rebuild is async, so callers should wait a frame before selecting.
    static void SwitchBarterSide(RE::BarterMenu* menu, bool buying);
    // Make the engine's selected ItemList entry == the given item on the correct
    // side, so a replayed ItemSelect acts on it. Returns false (caller should do a
    // direct fallback) if the selection can't be verified.
    static bool SelectCartItem(RE::BarterMenu* menu, RE::FormID formID, bool isBuying);

private:
    static void PostCreateBart(RE::BarterMenu* menu);
    static inline REL::Relocation<decltype(&PostCreateBart)> _PostCreateBart;

    static RE::UI_MESSAGE_RESULTS ProcessMessageBart(RE::BarterMenu* menu, RE::UIMessage& a_message);

    // Per-frame tick (IMenu vtable 0x05). This is the reliable place to poll the
    // highlighted item, accumulate the cart hold timer, and drive the injected
    // overlay — ProcessMessage(kUpdate) is event-driven, not per-frame, so the
    // prompt would only move on input events (e.g. a device switch).
    using AdvanceMovieFunc = void(RE::BarterMenu*, float, std::uint32_t);
    static void AdvanceMovieBart(RE::BarterMenu* menu, float a_interval, std::uint32_t a_currentTime);
    static inline REL::Relocation<AdvanceMovieFunc> _AdvanceMovieBart;

    using AcceptFunc = void(RE::BarterMenu*, RE::FxDelegateHandler::CallbackProcessor*);
    static void AcceptBart(RE::BarterMenu* menu, RE::FxDelegateHandler::CallbackProcessor* a_cbReg);
    static inline REL::Relocation<AcceptFunc> _AcceptBart;
};

class BarterMenuEventSink : public RE::BSTEventSink<RE::MenuOpenCloseEvent> {
public:
    static BarterMenuEventSink* GetSingleton() {
        static BarterMenuEventSink instance;
        return &instance;
    }

    RE::BSEventNotifyControl ProcessEvent(
        const RE::MenuOpenCloseEvent* a_event,
        RE::BSTEventSource<RE::MenuOpenCloseEvent>* a_eventSource) override;

private:
    BarterMenuEventSink() = default;
};
