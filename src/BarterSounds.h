#pragma once

// Lightweight UI sound player for the bartering mod. Plays WAV files placed in
// Data/SKSE/Plugins/DynamicBartering/Sounds/ using Windows PlaySound from memory, scaled by
// the master sound-volume setting. Supports random variants: a base file like
// "MoveSlider.wav" can be accompanied by "MoveSlider_1.wav", "MoveSlider_2.wav" ...
// and a random one is chosen each play.
namespace BarterSounds {
    enum class Event {
        AddToCart,
        RemoveFromCart,
        OpenOffer,
        MoveSlider,
        SliderStep5,
        AcceptOffer,
        Intimidate,
        CancelOffer,
        CounterOffer,
        ReOffer,
        OfferRejected,
        OfferAccepted,
        IntimidateSuccess,
    };

    // Play the sound for an event (async, volume-scaled, random variant). No-op when
    // sounds are disabled or the master volume is 0. Safe to call from any thread.
    void Play(Event e);

    // Play after a short delay. Used so a result jingle doesn't immediately cut off the
    // click that triggered it (PlaySound is single-channel).
    void PlayDelayed(Event e, int delayMs);

    // Play an item's own vanilla pickup (a_pickup=true) or putdown (false) sound via the
    // game's audio manager - the same cue Skyrim plays when you buy/sell that item type.
    // Falls back to the default pickup/putdown sound for the item's form type when the
    // item defines none. Returns false if no sound could be resolved/played (caller can
    // then fall back to a custom WAV cue). Must be called on the main game thread.
    bool PlayVanillaItemSound(RE::FormID a_formID, bool a_pickup);
}
