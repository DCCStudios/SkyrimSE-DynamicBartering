#pragma once

#include <functional>

// First-run, two-step in-game tutorial. Popup 1 explains the cart workflow the first
// time a barter menu opens; popup 2 explains the offer window the first time it opens.
// Both are simple click-to-dismiss message boxes. Once both have been seen the
// tutorial auto-disables; the SKSE menu can re-arm it (which replays both).
namespace Tutorial {
    void OnBarterOpened();       // popup 1: cart basics

    // Popup 2: offer window mechanics. The offer window draws on top of the message
    // box, so opening it is DEFERRED: `onProceed` runs after the player dismisses the
    // popup (or immediately if the popup is suppressed/already seen). It is always
    // invoked exactly once.
    void OnOfferWindowOpened(std::function<void()> onProceed);

    void Rearm();                // re-enable + clear seen flags (from the config menu)
}
