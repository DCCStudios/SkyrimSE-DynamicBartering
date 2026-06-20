#pragma once

class Hooks {
public:
    static void Install();

    static void InvokeVanillaConfirm();

    static inline bool interceptingTransaction = false;
    static inline bool transactionApproved = false;
    static inline RE::ItemList::Item* lastSelectedItem = nullptr;

    using ProcessMessageFunc = RE::UI_MESSAGE_RESULTS(RE::BarterMenu*, RE::UIMessage&);
    static inline REL::Relocation<ProcessMessageFunc> _ProcessMessageBart;

private:
    static void PostCreateBart(RE::BarterMenu* menu);
    static inline REL::Relocation<decltype(&PostCreateBart)> _PostCreateBart;

    static RE::UI_MESSAGE_RESULTS ProcessMessageBart(RE::BarterMenu* menu, RE::UIMessage& a_message);
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
