#include "PCH.h"
#include "Integration/ChimBridge.h"
#include "Settings.h"

#include <Windows.h>
#include <winhttp.h>

#include <array>
#include <atomic>
#include <chrono>
#include <cstring>
#include <mutex>
#include <thread>
#include <vector>

#pragma comment(lib, "winhttp.lib")

namespace ChimBridge {
namespace {

    // --- Availability state (set by the async probe) ----------------------------
    std::atomic<bool> g_available{ false };
    std::atomic<bool> g_probing{ false };
    std::atomic<long long> g_lastProbeMs{ 0 };

    constexpr long long kReprobeIntervalMs = 60'000;  // retry detection at most once a minute

    // Whether SkyrimSouls(RE) is loaded (decided once at Initialize). When true the
    // barter menu doesn't pause the game, so reactions can be voiced live.
    std::atomic<bool> g_soulsActive{ false };

    // Events buffered for a single barter session, used only when SkyrimSouls is
    // absent. Touched exclusively on the game/main thread (BarterManager outcomes
    // and barter open/close), so no locking is required.
    std::vector<BarterEvent> g_session;
    constexpr std::size_t kMaxSessionTx = 16;  // cap so the summary URL stays sane

    // Set when the barter menu closed but the dialogue menu is still up: the summary is
    // held until dialogue also closes so the reaction isn't muted by the open menu.
    bool g_pendingFlush = false;

    long long NowMs() {
        return std::chrono::duration_cast<std::chrono::milliseconds>(
                   std::chrono::steady_clock::now().time_since_epoch())
            .count();
    }

    // --- Base64 (standard alphabet; raw, never URL-escaped) ----------------------
    std::string Base64Encode(const std::string& in) {
        static const char* tbl = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
        std::string out;
        out.reserve(((in.size() + 2) / 3) * 4);
        std::size_t i = 0;
        const auto* data = reinterpret_cast<const unsigned char*>(in.data());
        for (; i + 2 < in.size(); i += 3) {
            const std::uint32_t n = (data[i] << 16) | (data[i + 1] << 8) | data[i + 2];
            out.push_back(tbl[(n >> 18) & 0x3F]);
            out.push_back(tbl[(n >> 12) & 0x3F]);
            out.push_back(tbl[(n >> 6) & 0x3F]);
            out.push_back(tbl[n & 0x3F]);
        }
        if (i < in.size()) {
            std::uint32_t n = data[i] << 16;
            const bool two = (i + 1 < in.size());
            if (two) n |= data[i + 1] << 8;
            out.push_back(tbl[(n >> 18) & 0x3F]);
            out.push_back(tbl[(n >> 12) & 0x3F]);
            out.push_back(two ? tbl[(n >> 6) & 0x3F] : '=');
            out.push_back('=');
        }
        return out;
    }

    // --- MD5 (compact, public-domain style) --------------------------------------
    // CHIM keys an NPC "profile" by the md5 of its name. We send a best-effort
    // profile param; the server plugin authoritatively re-attributes by the name in
    // the payload, so exact parity is not safety-critical.
    struct MD5 {
        std::uint32_t a0 = 0x67452301, b0 = 0xefcdab89, c0 = 0x98badcfe, d0 = 0x10325476;

        static std::uint32_t LeftRotate(std::uint32_t x, std::uint32_t c) {
            return (x << c) | (x >> (32 - c));
        }

        std::string Hex(const std::string& msg) {
            static const std::uint32_t s[64] = {
                7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22,
                5, 9, 14, 20, 5, 9, 14, 20, 5, 9, 14, 20, 5, 9, 14, 20,
                4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23,
                6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21 };
            static const std::uint32_t K[64] = {
                0xd76aa478, 0xe8c7b756, 0x242070db, 0xc1bdceee, 0xf57c0faf, 0x4787c62a, 0xa8304613, 0xfd469501,
                0x698098d8, 0x8b44f7af, 0xffff5bb1, 0x895cd7be, 0x6b901122, 0xfd987193, 0xa679438e, 0x49b40821,
                0xf61e2562, 0xc040b340, 0x265e5a51, 0xe9b6c7aa, 0xd62f105d, 0x02441453, 0xd8a1e681, 0xe7d3fbc8,
                0x21e1cde6, 0xc33707d6, 0xf4d50d87, 0x455a14ed, 0xa9e3e905, 0xfcefa3f8, 0x676f02d9, 0x8d2a4c8a,
                0xfffa3942, 0x8771f681, 0x6d9d6122, 0xfde5380c, 0xa4beea44, 0x4bdecfa9, 0xf6bb4b60, 0xbebfbc70,
                0x289b7ec6, 0xeaa127fa, 0xd4ef3085, 0x04881d05, 0xd9d4d039, 0xe6db99e5, 0x1fa27cf8, 0xc4ac5665,
                0xf4292244, 0x432aff97, 0xab9423a7, 0xfc93a039, 0x655b59c3, 0x8f0ccc92, 0xffeff47d, 0x85845dd1,
                0x6fa87e4f, 0xfe2ce6e0, 0xa3014314, 0x4e0811a1, 0xf7537e82, 0xbd3af235, 0x2ad7d2bb, 0xeb86d391 };

            std::vector<std::uint8_t> data(msg.begin(), msg.end());
            const std::uint64_t bitLen = static_cast<std::uint64_t>(data.size()) * 8ULL;
            data.push_back(0x80);
            while (data.size() % 64 != 56) data.push_back(0x00);
            for (int i = 0; i < 8; ++i) data.push_back(static_cast<std::uint8_t>((bitLen >> (8 * i)) & 0xFF));

            for (std::size_t off = 0; off < data.size(); off += 64) {
                std::uint32_t M[16];
                for (int i = 0; i < 16; ++i) {
                    M[i] = data[off + i * 4] | (data[off + i * 4 + 1] << 8) |
                           (data[off + i * 4 + 2] << 16) | (data[off + i * 4 + 3] << 24);
                }
                std::uint32_t A = a0, B = b0, C = c0, D = d0;
                for (std::uint32_t i = 0; i < 64; ++i) {
                    std::uint32_t F, g;
                    if (i < 16) { F = (B & C) | (~B & D); g = i; }
                    else if (i < 32) { F = (D & B) | (~D & C); g = (5 * i + 1) % 16; }
                    else if (i < 48) { F = B ^ C ^ D; g = (3 * i + 5) % 16; }
                    else { F = C ^ (B | ~D); g = (7 * i) % 16; }
                    F = F + A + K[i] + M[g];
                    A = D; D = C; C = B;
                    B = B + LeftRotate(F, s[i]);
                }
                a0 += A; b0 += B; c0 += C; d0 += D;
            }

            const std::uint32_t parts[4] = { a0, b0, c0, d0 };
            char hex[33];
            for (int i = 0; i < 4; ++i) {
                for (int j = 0; j < 4; ++j) {
                    const std::uint8_t byte = (parts[i] >> (8 * j)) & 0xFF;
                    std::snprintf(hex + (i * 8 + j * 2), 3, "%02x", byte);
                }
            }
            hex[32] = '\0';
            return std::string(hex);
        }
    };

    std::string Md5Hex(const std::string& s) {
        return MD5{}.Hex(s);
    }

    // --- WinHTTP send ------------------------------------------------------------
    // Performs one blocking request (this always runs on a detached worker thread).
    // Returns true if the server produced *any* HTTP response (i.e. it is reachable),
    // regardless of status code. `bodyOnly` controls whether we wait for the body.
    bool HttpGet(const std::wstring& wideUrl, DWORD timeoutMs) {
        URL_COMPONENTS uc{};
        uc.dwStructSize = sizeof(uc);
        wchar_t host[256] = {};
        wchar_t path[4096] = {};
        wchar_t extra[8192] = {};
        uc.lpszHostName = host; uc.dwHostNameLength = static_cast<DWORD>(std::size(host));
        uc.lpszUrlPath = path; uc.dwUrlPathLength = static_cast<DWORD>(std::size(path));
        uc.lpszExtraInfo = extra; uc.dwExtraInfoLength = static_cast<DWORD>(std::size(extra));

        if (!WinHttpCrackUrl(wideUrl.c_str(), 0, 0, &uc)) {
            return false;
        }

        const bool https = (uc.nScheme == INTERNET_SCHEME_HTTPS);
        INTERNET_PORT port = uc.nPort ? uc.nPort : (https ? INTERNET_DEFAULT_HTTPS_PORT : INTERNET_DEFAULT_HTTP_PORT);

        HINTERNET hSession = WinHttpOpen(L"DynamicBarteringSKSE/1.0",
                                         WINHTTP_ACCESS_TYPE_NO_PROXY,
                                         WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
        if (!hSession) return false;

        WinHttpSetTimeouts(hSession, static_cast<int>(timeoutMs), static_cast<int>(timeoutMs),
                           static_cast<int>(timeoutMs), static_cast<int>(timeoutMs));

        bool ok = false;
        if (HINTERNET hConnect = WinHttpConnect(hSession, host, port, 0)) {
            std::wstring object(path);
            object.append(extra);  // path already includes "?..." via ExtraInfo

            const DWORD flags = (https ? WINHTTP_FLAG_SECURE : 0) | WINHTTP_FLAG_ESCAPE_DISABLE;
            HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"GET", object.c_str(),
                                                    nullptr, WINHTTP_NO_REFERER,
                                                    WINHTTP_DEFAULT_ACCEPT_TYPES, flags);
            if (hRequest) {
                if (WinHttpSendRequest(hRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                                       WINHTTP_NO_REQUEST_DATA, 0, 0, 0) &&
                    WinHttpReceiveResponse(hRequest, nullptr)) {
                    ok = true;  // got an HTTP response => server is alive
                }
                WinHttpCloseHandle(hRequest);
            }
            WinHttpCloseHandle(hConnect);
        }
        WinHttpCloseHandle(hSession);
        return ok;
    }

    std::wstring Widen(const std::string& s) {
        if (s.empty()) return {};
        const int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()), nullptr, 0);
        std::wstring w(n, L'\0');
        MultiByteToWideChar(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()), w.data(), n);
        return w;
    }

    // Normalises the configured base URL into ".../HerikaServer/comm.php".
    std::string CommEndpoint() {
        std::string base = Settings::GetSingleton()->chimServerUrl;
        if (base.empty()) base = "http://localhost:8081";
        while (!base.empty() && base.back() == '/') base.pop_back();
        return base + "/HerikaServer/comm.php";
    }

    void DoProbe() {
        const std::string url = CommEndpoint();  // a bare GET; server replies even with no data
        const bool ok = HttpGet(Widen(url), 2000);
        g_available.store(ok, std::memory_order_relaxed);
        g_lastProbeMs.store(NowMs(), std::memory_order_relaxed);
        g_probing.store(false, std::memory_order_relaxed);
        logger::info("ChimBridge: probe of {} -> {}", url, ok ? "reachable" : "unreachable");
    }

    void MaybeProbeAsync() {
        bool expected = false;
        if (!g_probing.compare_exchange_strong(expected, true, std::memory_order_acq_rel)) {
            return;  // a probe is already in flight
        }
        std::thread([]() { DoProbe(); }).detach();
    }

}  // namespace

const char* ActionToString(Action a) {
    switch (a) {
    case Action::Lowball:           return "lowball";
    case Action::Fair:              return "fair";
    case Action::Generous:          return "generous";
    case Action::DealClose:         return "deal_close";
    case Action::IntimidateSuccess: return "intimidate_success";
    case Action::IntimidateFail:    return "intimidate_fail";
    case Action::Counter:           return "counter";
    case Action::CounterAccept:     return "counter_accept";
    case Action::CounterReject:     return "counter_reject";
    case Action::WalkAway:          return "walk_away";
    default:                        return "unknown";
    }
}

void Initialize() {
    // SkyrimSouls(RE) keeps the world running while menus are open. We detect it once;
    // it no longer gates *when* we speak (Skyrim mutes NPC voice while a menu is topmost
    // regardless), but it lets the custom offer window avoid re-pausing the game.
    const bool souls = (GetModuleHandleA("SkyrimSoulsRE.dll") != nullptr);
    g_soulsActive.store(souls, std::memory_order_relaxed);

    auto* s = Settings::GetSingleton();
    if (!s || !s->enableChim) {
        logger::info("ChimBridge: disabled in settings (SkyrimSouls {})",
                     souls ? "detected" : "not detected");
        return;
    }
    logger::info("ChimBridge: enabled, server={}, SkyrimSouls {}, send delay {}s after barter close",
                 s->chimServerUrl, souls ? "detected" : "not detected", s->chimSendDelaySec);
    MaybeProbeAsync();
}

bool SkyrimSoulsActive() {
    return g_soulsActive.load(std::memory_order_relaxed);
}

bool IsAvailable() {
    return g_available.load(std::memory_order_relaxed);
}

namespace {

    // True if we should proceed to contact CHIM. Triggers an occasional re-probe when
    // the server looked unreachable, but never blocks the caller.
    bool ShouldSend() {
        auto* s = Settings::GetSingleton();
        if (!s || !s->enableChim) return false;
        if (!g_available.load(std::memory_order_relaxed)) {
            const long long since = NowMs() - g_lastProbeMs.load(std::memory_order_relaxed);
            if (since > kReprobeIntervalMs) MaybeProbeAsync();
            return false;
        }
        return true;
    }

    // The per-transaction fields shared by single events and session entries.
    nlohmann::json TxJson(const BarterEvent& e) {
        nlohmann::json j;
        j["action"] = ActionToString(e.action);
        j["item"] = e.itemName;
        j["market_price"] = e.marketPrice;
        j["offered_price"] = e.offeredPrice;
        j["counter_price"] = e.counterPrice;
        j["gold_delta"] = e.goldDelta;
        j["is_buying"] = e.isBuying;
        j["is_stolen"] = e.isStolen;
        return j;
    }

    // base64(`barter_event|<ts>|0|<json>`) -> comm.php on a detached thread. `delayMs`
    // optionally holds the send back so the barter menu has fully closed and the world
    // has unpaused before CHIM is asked to voice a reaction (Skyrim mutes NPC speech
    // while a menu is topmost). The delay runs on the worker, never the game thread.
    void DispatchAsync(const nlohmann::json& j, const std::string& merchantName, long long delayMs = 0) {
        auto* s = Settings::GetSingleton();
        if (!s) return;
        const long long unixTs = std::chrono::duration_cast<std::chrono::seconds>(
                                     std::chrono::system_clock::now().time_since_epoch())
                                     .count();
        const std::string pipe = std::format("barter_event|{}|0|{}", unixTs, j.dump());
        const std::string data = Base64Encode(pipe);
        const std::string profile = Md5Hex(merchantName);
        const std::string url = CommEndpoint() + "?data=" + data + "&profile=" + profile;

        const DWORD timeout = static_cast<DWORD>(std::clamp(s->chimTimeoutMs, 500, 15000));
        const std::wstring wideUrl = Widen(url);
        const bool debug = s->debugLogging;
        const long long delay = std::max(0LL, delayMs);

        std::thread([wideUrl, timeout, debug, delay]() {
            if (delay > 0) {
                std::this_thread::sleep_for(std::chrono::milliseconds(delay));
            }
            const bool ok = HttpGet(wideUrl, timeout);
            if (!ok) {
                // Lost the server mid-session - mark unavailable until the next probe.
                g_available.store(false, std::memory_order_relaxed);
                g_lastProbeMs.store(NowMs(), std::memory_order_relaxed);
            }
            if (debug) {
                logger::info("ChimBridge: sent barter_event (delay {}ms) -> {}", delay, ok ? "ok" : "failed");
            }
        }).detach();
    }

}  // namespace

void Emit(const BarterEvent& evt, bool allowBark) {
    auto* s = Settings::GetSingleton();
    if (!ShouldSend()) return;

    nlohmann::json j = TxJson(evt);
    j["merchant"] = evt.merchantName;
    j["merchant_id"] = std::format("0x{:08X}", evt.merchantFormID);
    j["personality"] = evt.personality;
    j["relationship"] = evt.relationship;
    j["big_moment"] = allowBark && evt.isBigMoment && s->chimImmediateReactions;
    // Cooldowns are configured client-side (SKSE menu) and honoured by the server,
    // which falls back to its own globals if these are absent.
    j["reaction_cooldown"] = std::max(0, s->chimReactionCooldownSec);
    j["counter_cooldown"] = std::max(0, s->chimCounterCooldownSec);
    DispatchAsync(j, evt.merchantName);
}

void Submit(const BarterEvent& evt) {
    auto* s = Settings::GetSingleton();
    if (!s || !s->enableChim) return;

    // Skyrim won't voice (or subtitle) NPC lines while a menu is topmost, even unpaused,
    // so the audible reaction is always deferred: buffer every outcome and fire one
    // consolidated summary when the barter menu closes (see FlushSession).
    g_session.push_back(evt);

    // Optionally also log each outcome to the merchant's memory right now (no bark), so
    // it tracks the blow-by-blow if you talk to it later. Silent during the menu by design.
    if (s->chimLiveContextLogging) {
        Emit(evt, /*allowBark=*/false);
    }
}

void ResetSession() {
    // Keep accumulating while a flush is pending so multiple barters in one conversation
    // collapse into a single summary, sent once the dialogue finally closes.
    if (g_pendingFlush) return;
    g_session.clear();
}

void OnBarterClosed(bool dialogueStillOpen) {
    if (g_session.empty()) {
        g_pendingFlush = false;
        return;
    }
    if (dialogueStillOpen) {
        g_pendingFlush = true;  // hold until the dialogue menu closes too
        return;
    }
    g_pendingFlush = false;
    FlushSession();
}

void OnDialogueClosed() {
    if (!g_pendingFlush) return;
    g_pendingFlush = false;
    FlushSession();
}

void FlushSession() {
    g_pendingFlush = false;
    if (g_session.empty()) return;

    auto* s = Settings::GetSingleton();
    if (!s || !s->enableChim || !ShouldSend()) {
        g_session.clear();  // drop rather than accumulate across sessions
        return;
    }

    const std::size_t total = g_session.size();
    const std::size_t start = (total > kMaxSessionTx) ? (total - kMaxSessionTx) : 0;
    const BarterEvent& last = g_session.back();

    nlohmann::json tx = nlohmann::json::array();
    long long netGold = 0;
    bool anyBig = false;
    for (std::size_t i = start; i < total; ++i) {
        tx.push_back(TxJson(g_session[i]));
        netGold += g_session[i].goldDelta;
        anyBig = anyBig || g_session[i].isBigMoment;
    }

    nlohmann::json j;
    j["action"] = "session";
    j["merchant"] = last.merchantName;
    j["merchant_id"] = std::format("0x{:08X}", last.merchantFormID);
    j["personality"] = last.personality;
    j["relationship"] = last.relationship;
    j["transactions"] = std::move(tx);
    j["tx_count"] = static_cast<int>(total);
    j["net_gold"] = static_cast<int>(netGold);
    // big_moment is recorded for analytics; the after-close closing remark itself is
    // NOT gated on it (the server decides the line from the net gold / intimidation),
    // so a normal haggle still earns a spoken comment once the menus close.
    j["big_moment"] = anyBig;
    j["reaction_cooldown"] = std::max(0, s->chimReactionCooldownSec);
    j["counter_cooldown"] = std::max(0, s->chimCounterCooldownSec);

    const long long delayMs = static_cast<long long>(std::clamp(s->chimSendDelaySec, 0, 60)) * 1000LL;
    DispatchAsync(j, last.merchantName, delayMs);
    g_session.clear();
}

}  // namespace ChimBridge
