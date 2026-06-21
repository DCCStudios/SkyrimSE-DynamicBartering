#pragma once
#include <chrono>

class Hooks {
public:
    static void Install();

    static void InvokeVanillaConfirm();

    static inline bool interceptingTransaction = false;
    static inline bool transactionApproved = false;
    static inline RE::ItemList::Item* lastSelectedItem = nullptr;

    // Set true while WE re-invoke the vanilla "ItemSelect" callback to perform a
    // negotiated transaction, so our interceptor forwards straight to vanilla and
    // doesn't recursively re-open the barter window.
    static inline bool replayingItemSelect = false;
    // Cooldown: after a negotiation finishes, ignore ItemSelect for this many ms
    // to prevent the "quick open/close" glitch.
    static inline std::chrono::steady_clock::time_point lastNegotiationEnd{};
    // The vanilla BarterMenu's own "ItemSelect" callback, captured during Accept.
    static inline RE::FxDelegateHandler::CallbackFn* originalItemSelect = nullptr;

    using ProcessMessageFunc = RE::UI_MESSAGE_RESULTS(RE::BarterMenu*, RE::UIMessage&);
    static inline REL::Relocation<ProcessMessageFunc> _ProcessMessageBart;

    // Re-runs the captured vanilla ItemSelect with a custom amount/unit-price.
    static void ItemSelectInterceptor(const RE::FxDelegateArgs& a_params);

private:
    static void PostCreateBart(RE::BarterMenu* menu);
    static inline REL::Relocation<decltype(&PostCreateBart)> _PostCreateBart;

    static RE::UI_MESSAGE_RESULTS ProcessMessageBart(RE::BarterMenu* menu, RE::UIMessage& a_message);

    // Hook of BarterMenu::Accept (vtable 0x1): wraps the callback registrar so we can
    // substitute our own "ItemSelect" handler. ItemSelect is the single point where a
    // real buy/sell happens (after the Quantity Menu for stacks), and it never fires
    // for tab/inventory-swap clicks — so intercepting it fixes both bugs at once.
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
