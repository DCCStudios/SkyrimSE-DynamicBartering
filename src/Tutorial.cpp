#include "PCH.h"
#include "Tutorial.h"
#include "Settings.h"
#include "DebugLog.h"

namespace {
    // Fires a one-shot callback when the player dismisses the info popup, so the offer
    // window can be opened only AFTER the tutorial box is gone (otherwise it draws on top).
    class DismissCallback : public RE::IMessageBoxCallback {
    public:
        std::function<void()> fn;
        void Run(Message) override {
            if (fn) fn();
        }
    };

    // Simple single-button info popup (reuses Skyrim's native message box system,
    // same machinery as the cart-guard popup). Returns true if the popup was actually
    // queued; when an `onProceed` callback is supplied it runs once on dismissal.
    bool ShowInfo(const std::string& body, std::function<void()> onProceed = {}) {
        auto* factoryManager = RE::MessageDataFactoryManager::GetSingleton();
        auto* uiStr = RE::InterfaceStrings::GetSingleton();
        if (!factoryManager || !uiStr) return false;
        auto* factory = factoryManager->GetCreator<RE::MessageBoxData>(uiStr->messageBoxData);
        if (!factory) return false;
        auto* mbox = factory->Create();
        if (!mbox) return false;

        if (onProceed) {
            auto* cb = new DismissCallback();
            cb->fn = std::move(onProceed);
            mbox->callback = RE::BSTSmartPointer<RE::IMessageBoxCallback>(cb);
        }

        mbox->bodyText = body.c_str();
        mbox->buttonText.push_back("Got it");
        mbox->isCancellable = true;
        mbox->cancelOptionIndex = 0;
        mbox->QueueMessage();
        return true;
    }

    // When both popups have been seen, disable the tutorial so it doesn't replay.
    void MaybeFinish() {
        auto* s = Settings::GetSingleton();
        if (s->tutorialCartSeen && s->tutorialOfferSeen) {
            s->tutorialEnabled = false;
            DbgLog("Tutorial: both popups seen -> auto-disabled");
        }
    }
}

void Tutorial::OnBarterOpened() {
    auto* s = Settings::GetSingleton();
    if (!s->tutorialEnabled || s->tutorialCartSeen) return;

    // The controls differ depending on whether instant buy/sell is blocked:
    //  - blockQuickBuy ON  : the normal Activate/buy key ADDS to the cart (so a stray
    //                        click can't empty your purse); HOLD the barter key to open.
    //  - blockQuickBuy OFF : a regular click still buys/sells instantly; TAP the barter
    //                        key to add/remove, and HOLD the barter key to open.
    if (s->blockQuickBuy) {
        ShowInfo(
            "Dynamic Bartering\n\n"
            "Buying and selling now goes through a cart. Highlight an item and press the "
            "ACTIVATE / buy key to add or remove it from the cart (instant buy/sell is "
            "turned off, so a stray click can't empty your purse).\n\n"
            "When you're ready, HOLD the barter key to open the offer window and negotiate. "
            "You can stage several items and haggle over the whole cart at once.");
    } else {
        ShowInfo(
            "Dynamic Bartering\n\n"
            "Buying and selling can now go through a cart. Highlight an item and TAP the "
            "barter key to add or remove it from the cart (a normal click still buys or "
            "sells instantly).\n\n"
            "When you're ready, HOLD the barter key to open the offer window and negotiate. "
            "You can stage several items and haggle over the whole cart at once.");
    }

    s->tutorialCartSeen = true;
    MaybeFinish();
    s->Save();
}

void Tutorial::OnOfferWindowOpened(std::function<void()> onProceed) {
    auto* s = Settings::GetSingleton();
    if (!s->tutorialEnabled || s->tutorialOfferSeen) {
        if (onProceed) onProceed();
        return;
    }

    s->tutorialOfferSeen = true;
    MaybeFinish();
    s->Save();

    const bool shown = ShowInfo(
        "Making an Offer\n\n"
        "Use the slider to set your price, then submit. The merchant may accept, refuse, "
        "or make a counter-offer. The acceptance chance updates as you slide.\n\n"
        "Your RELATIONSHIP with a merchant (bounded by their personality) sets how good a "
        "deal you can reach: liked merchants give better prices, disliked ones hold firm. "
        "Merchants also haggle more readily on goods they SPECIALIZE in.\n\n"
        "With the Intimidation perk you can strong-arm a better price, but it can sour "
        "the relationship.",
        onProceed);  // pass a copy so the fallback below still has it on failure

    // If the popup couldn't be queued for any reason, don't strand the offer window.
    if (!shown && onProceed) {
        onProceed();
    }
}

void Tutorial::Rearm() {
    auto* s = Settings::GetSingleton();
    s->tutorialEnabled = true;
    s->tutorialCartSeen = false;
    s->tutorialOfferSeen = false;
    s->Save();
    DbgLog("Tutorial: re-armed (both popups will replay)");
}
