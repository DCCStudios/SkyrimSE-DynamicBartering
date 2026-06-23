#pragma once

#include <cstdint>
#include <string>

// Fire-and-forget bridge that pushes bartering events to a CHIM / HerikaServer
// instance so AI NPCs can detect and react (with voice lines) to haggling,
// intimidation, counter-offers and closed deals.
//
// The DLL never waits on or reads CHIM's response: it base64-encodes a single
// `barter_event` and HTTP-GETs it to CHIM's existing comm.php endpoint on a
// detached thread. The server-side ext plugin (CHIM-DynamicBartering) logs it to
// the merchant's memory and, for "big moments", queues an immediate spoken line
// that CHIM's own AIAgent client picks up and voices.
//
// Everything is gated behind a master toggle AND an availability probe, so the
// mod stays completely inert (no stalls, no spam) when CHIM is not installed or
// its server is not running.
namespace ChimBridge {
    enum class Action {
        Lowball,            // accepted a deal struck in the player's favour
        Fair,               // accepted a roughly fair deal
        Generous,           // accepted a deal generous to the merchant
        DealClose,          // generic close (fallback)
        IntimidateSuccess,  // big moment
        IntimidateFail,     // big moment
        Counter,            // merchant proposes a counter-offer (big moment - they justify it)
        CounterAccept,      // player accepted the merchant's counter
        CounterReject,      // merchant rejected the player's offer (no counter)
        WalkAway,           // player walked away from a counter
    };

    struct BarterEvent {
        std::string   merchantName;
        std::uint32_t merchantFormID = 0;
        std::string   personality;       // e.g. "Greedy", "Sleazy"
        int           relationship = 0;  // -100..100
        Action        action = Action::Fair;
        std::string   itemName;          // item name, or "the goods" for a cart
        int           marketPrice = 0;   // vanilla/effective market total
        int           offeredPrice = 0;  // final agreed/offered total (for a counter: the player's offer)
        int           counterPrice = 0;  // the merchant's counter amount (Counter action only)
        int           goldDelta = 0;     // signed gold that changed hands (+player pays / -player gets)
        bool          isBuying = true;   // player buying from merchant (vs selling)
        bool          isStolen = false;
        bool          isBigMoment = false;
    };

    // Called once at kDataLoaded. Reads settings and kicks off an async probe to
    // detect whether CHIM is installed and its server is reachable.
    void Initialize();

    // Fire-and-forget. If CHIM is disabled in settings, or was not reachable on the
    // last probe, this is a cheap no-op. Returns immediately; all I/O is async.
    // `allowBark` lets the server queue a spoken line for big moments; pass false for
    // memory-only logging (e.g. mid-menu, where Skyrim mutes voice/subtitles anyway).
    void Emit(const BarterEvent& evt, bool allowBark = true);

    // Outcome entry point used by the game. Skyrim won't voice (or even subtitle) NPC
    // lines while a menu is topmost, so the audible reaction is always deferred:
    //   - Every outcome is buffered and sent as one consolidated "session" summary on
    //     FlushSession(), i.e. once the barter menu closes (optionally after a delay).
    //   - If live context logging is enabled, each outcome is ALSO pushed immediately as
    //     memory only (no bark), so the merchant's memory tracks the blow-by-blow.
    void Submit(const BarterEvent& evt);

    // Clear the buffered session (call when a barter session opens). No-op while a flush
    // is pending, so consecutive barters within one conversation combine into one summary.
    void ResetSession();

    // Call when the barter menu closes. Barter is normally entered from dialogue, and
    // Skyrim mutes NPC voice while ANY menu (including the dialogue menu) is topmost, so
    // if `dialogueStillOpen` the consolidated summary is held until the dialogue menu also
    // closes (see OnDialogueClosed). Otherwise it is flushed right away.
    void OnBarterClosed(bool dialogueStillOpen);

    // Call when the dialogue menu closes; flushes a held barter summary if one is pending.
    void OnDialogueClosed();

    // If any events were buffered this session, send one consolidated "session"
    // event summarising them, then clear the buffer.
    void FlushSession();

    // Whether the SkyrimSouls(RE) unpaused-menus plugin is loaded. When true, the
    // barter menu does not pause the game, so live in-menu reactions are possible.
    bool SkyrimSoulsActive();

    // Whether CHIM responded on the most recent probe.
    bool IsAvailable();

    // Maps an Action to its stable lowercase wire token (used in the payload).
    const char* ActionToString(Action a);
}
