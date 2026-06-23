#pragma once

#include "Settings.h"

// Verbose logging gated behind Settings::debugLogging (the "Debug Logging" toggle in the
// SKSE menu / bDebugLogging in the INI). Routine per-event / per-frame chatter goes through
// this so a normal session keeps a quiet log; only warnings, errors and one-time lifecycle
// messages stay at plain logger:: level.
//
// Implemented as a macro because the SKSE logger captures std::source_location via deduction
// guides and cannot be forwarded through a wrapper template. `logger` is the SKSE::log alias
// from PCH.h, which is always included first.
#define DbgLog(...)                                   \
    do {                                              \
        if (Settings::GetSingleton()->debugLogging) { \
            logger::info(__VA_ARGS__);                \
        }                                             \
    } while (0)
