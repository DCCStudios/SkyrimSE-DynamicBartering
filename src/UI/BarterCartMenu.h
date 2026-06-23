#pragma once

// Cart overlay rendered by INJECTING display objects directly into the vanilla
// BarterMenu's own GFx movie (uiMovie), rather than running as a separate IMenu.
//
// Why: a separate non-modal IMenu does not composite over a fullscreen menu like
// BarterMenu (only kModal menus draw on top, and those steal input). The vanilla
// BarterMenu movie is always rendered, so anything we createEmptyMovieClip /
// createTextField into it is guaranteed visible — and it never touches input
// routing, so item highlighting keeps working.
//
// Everything is built with native GFx ActionScript primitives (createEmptyMovieClip,
// createTextField with background/border for the panel + prompt styling), driven via
// path-based SetVariable/Invoke. No external SWF load, no resource import.
class BarterCartMenu {
public:
    // Reset cached state when a barter session opens/closes. The injected clips live
    // inside the BarterMenu movie and die with it, so we only clear our flags.
    static void OnBarterOpen();
    static void OnBarterClose();

    // Per-frame driver. Call from Hooks::ProcessMessageBart kUpdate (Idle state) with
    // the live BarterMenu movie. Lazily builds the clips on first call, then refreshes
    // the prompt (highlight) + cart panel (count) + hold meter.
    static void Update(RE::GFxMovieView* a_movie);

private:
    static void Build(RE::GFxMovieView* a_movie);
    static void UpdateCartPlacement(RE::GFxMovieView* a_movie);
    static void UpdatePromptAndMeter(RE::GFxMovieView* a_movie);
    static void UpdateCartPanel(RE::GFxMovieView* a_movie);

    static inline bool built{ false };

    // Cached state to avoid redundant (expensive) SetVariable churn each frame.
    static inline bool lastPromptVisible{ true };
    static inline int  lastGlyph{ -1 };          // 0=xbox Y, 1=ps triangle, 2=keyboard B
    static inline int  lastHintGlyph{ -2 };      // barter-key glyph drawn in the cart hint
    static inline bool lastPanelVisible{ true };
    static inline int  lastCartCount{ -1 };
    static inline int  lastNet{ -2147483647 };  // sentinel: force first panel rebuild
    static inline int  lastRelMilli{ -2147483647 };  // relationship price effect (per-mille)
    static inline bool lastMeterVisible{ true };
    static inline float lastMeterFrac{ -1.0f };

    // Live cart-panel placement, mirrored from Settings each frame so the
    // SKSE Menu Framework sliders reposition/resize the panel in real time.
    static inline float lastPanelX{ -1.0e9f };
    static inline float lastPanelY{ -1.0e9f };
    static inline float lastPanelScale{ -1.0f };
};
