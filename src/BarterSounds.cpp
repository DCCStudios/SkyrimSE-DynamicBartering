#include "PCH.h"
#include "BarterSounds.h"
#include "Settings.h"

#include <Windows.h>
#include <mmsystem.h>
#include <array>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstring>
#include <deque>
#include <filesystem>
#include <fstream>
#include <map>
#include <mutex>
#include <random>
#include <thread>
#include <vector>

#pragma comment(lib, "winmm.lib")

#ifdef PlaySound
#	undef PlaySound
#endif

// <Windows.h> defines GetObject -> GetObjectW, which collides with
// BGSDefaultObjectManager::GetObject. We don't use the GDI call here.
#ifdef GetObject
#	undef GetObject
#endif

namespace BarterSounds {
namespace {
#pragma pack(push, 1)
    struct WAVHeader {
        char          riff[4];
        std::uint32_t fileSize;
        char          wave[4];
    };
    struct WAVChunkHeader {
        char          id[4];
        std::uint32_t size;
    };
    struct WAVFmtChunk {
        std::uint16_t audioFormat;
        std::uint16_t numChannels;
        std::uint32_t sampleRate;
        std::uint32_t byteRate;
        std::uint16_t blockAlign;
        std::uint16_t bitsPerSample;
    };
#pragma pack(pop)

    std::mutex g_mutex;

    // Cached raw (unscaled) file bytes keyed by absolute path, so slider spam doesn't
    // re-read from disk every frame. Volume scaling is applied to a fresh copy each play.
    std::map<std::string, std::shared_ptr<std::vector<std::uint8_t>>> g_rawCache;

    // Cached per-base list of existing variant filenames (base + base_1, base_2, ...).
    std::map<std::string, std::vector<std::string>> g_variantCache;

    // Keep a few recently played buffers alive while PlaySound reads them asynchronously.
    std::array<std::shared_ptr<std::vector<std::uint8_t>>, 4> g_keepAlive;
    std::size_t g_keepAliveIdx = 0;

    // Per-event throttle so rapid retriggers (e.g. slider held down) don't machine-gun.
    std::map<Event, std::chrono::steady_clock::time_point> g_lastPlay;

    const char* BaseFileFor(Event e) {
        switch (e) {
        case Event::AddToCart:      return "AddToCart.wav";
        case Event::RemoveFromCart: return "RemoveFromCart.wav";
        case Event::OpenOffer:      return "OpenBarterOffer.wav";
        case Event::MoveSlider:     return "MoveSlider.wav";
        case Event::SliderStep5:    return "MoveSlider5.wav";
        case Event::AcceptOffer:    return "AcceptOffer.wav";
        case Event::Intimidate:     return "IntimidateOffer.wav";
        case Event::CancelOffer:    return "CancelOffer.wav";
        case Event::CounterOffer:   return "CounterOffer.wav";
        case Event::ReOffer:        return "ReOffer.wav";
        case Event::OfferRejected:  return "OfferRejected.wav";
        case Event::OfferAccepted:  return "OfferAccepted.wav";
        default:                    return nullptr;
        }
    }

    int ThrottleMsFor(Event e) {
        // Slider feedback fires very frequently; cap its rate. Everything else just
        // needs a small guard against double-fire from overlapping input paths.
        return (e == Event::MoveSlider || e == Event::SliderStep5) ? 45 : 60;
    }

    std::filesystem::path SoundDir() {
        return std::filesystem::current_path() / "Data" / "SKSE" / "Plugins" / "DynamicBartering" / "Sounds";
    }

    // Returns the list of existing variant filenames for a base file (cached). Includes
    // the base itself if present, plus base_1.wav, base_2.wav, ... until a gap.
    const std::vector<std::string>& VariantsFor(const std::string& baseFile) {
        auto it = g_variantCache.find(baseFile);
        if (it != g_variantCache.end()) {
            return it->second;
        }

        namespace fs = std::filesystem;
        const fs::path dir = SoundDir();
        std::vector<std::string> found;

        if (fs::exists(dir / baseFile)) {
            found.push_back(baseFile);
        }
        if (baseFile.size() > 4) {
            const std::string stem = baseFile.substr(0, baseFile.size() - 4);  // strip ".wav"
            for (int i = 1; i <= 30; ++i) {
                const std::string variant = stem + "_" + std::to_string(i) + ".wav";
                if (fs::exists(dir / variant)) {
                    found.push_back(variant);
                } else {
                    break;
                }
            }
        }
        return g_variantCache.emplace(baseFile, std::move(found)).first->second;
    }

    std::shared_ptr<std::vector<std::uint8_t>> LoadRaw(const std::filesystem::path& path) {
        const std::string key = path.string();
        auto it = g_rawCache.find(key);
        if (it != g_rawCache.end()) {
            return it->second;
        }
        std::ifstream file(path, std::ios::binary | std::ios::ate);
        if (!file.is_open()) {
            return nullptr;
        }
        const std::streamsize size = file.tellg();
        if (size <= 0) {
            return nullptr;
        }
        file.seekg(0, std::ios::beg);
        auto buf = std::make_shared<std::vector<std::uint8_t>>(static_cast<std::size_t>(size));
        if (!file.read(reinterpret_cast<char*>(buf->data()), size)) {
            return nullptr;
        }
        g_rawCache.emplace(key, buf);
        return buf;
    }

    // Apply a 0..1 volume scale in-place to the PCM samples of a WAV byte buffer.
    void ApplyVolume(std::vector<std::uint8_t>& buf, float volume) {
        if (volume >= 0.99f || buf.size() < sizeof(WAVHeader)) {
            return;
        }
        auto* header = reinterpret_cast<WAVHeader*>(buf.data());
        if (std::memcmp(header->riff, "RIFF", 4) != 0 || std::memcmp(header->wave, "WAVE", 4) != 0) {
            return;
        }

        std::size_t pos = sizeof(WAVHeader);
        WAVFmtChunk* fmt = nullptr;
        std::uint8_t* audio = nullptr;
        std::uint32_t audioSize = 0;
        while (pos + sizeof(WAVChunkHeader) <= buf.size()) {
            auto* chunk = reinterpret_cast<WAVChunkHeader*>(buf.data() + pos);
            const std::size_t dataStart = pos + sizeof(WAVChunkHeader);
            const std::size_t dataEnd = dataStart + chunk->size;
            if (dataEnd > buf.size() || dataEnd < dataStart) {
                break;
            }
            if (std::memcmp(chunk->id, "fmt ", 4) == 0 && chunk->size >= sizeof(WAVFmtChunk)) {
                fmt = reinterpret_cast<WAVFmtChunk*>(buf.data() + dataStart);
            } else if (std::memcmp(chunk->id, "data", 4) == 0) {
                audio = buf.data() + dataStart;
                audioSize = chunk->size;
                break;
            }
            pos = dataEnd + (dataEnd % 2);
        }
        if (!fmt || !audio || audioSize == 0 || fmt->audioFormat != 1) {
            return;
        }

        if (fmt->bitsPerSample == 16) {
            auto* s = reinterpret_cast<std::int16_t*>(audio);
            const std::size_t n = audioSize / sizeof(std::int16_t);
            for (std::size_t i = 0; i < n; ++i) {
                s[i] = static_cast<std::int16_t>(std::clamp(static_cast<float>(s[i]) * volume, -32768.0f, 32767.0f));
            }
        } else if (fmt->bitsPerSample == 8) {
            for (std::uint32_t i = 0; i < audioSize; ++i) {
                const float v = (static_cast<float>(audio[i]) - 128.0f) * volume;
                audio[i] = static_cast<std::uint8_t>(std::clamp(v + 128.0f, 0.0f, 255.0f));
            }
        } else if (fmt->bitsPerSample == 24) {
            const std::size_t n = audioSize / 3;
            for (std::size_t i = 0; i < n; ++i) {
                std::int32_t v = audio[i * 3] | (audio[i * 3 + 1] << 8) | (audio[i * 3 + 2] << 16);
                if (v & 0x800000) v |= 0xFF000000;
                const std::int32_t r = static_cast<std::int32_t>(
                    std::clamp(static_cast<float>(v) * volume, -8388608.0f, 8388607.0f));
                audio[i * 3] = r & 0xFF;
                audio[i * 3 + 1] = (r >> 8) & 0xFF;
                audio[i * 3 + 2] = (r >> 16) & 0xFF;
            }
        } else if (fmt->bitsPerSample == 32) {
            auto* s = reinterpret_cast<std::int32_t*>(audio);
            const std::size_t n = audioSize / sizeof(std::int32_t);
            for (std::size_t i = 0; i < n; ++i) {
                s[i] = static_cast<std::int32_t>(
                    std::clamp(static_cast<double>(s[i]) * static_cast<double>(volume), -2147483648.0, 2147483647.0));
            }
        }
    }

    // Cheap throttle gate (game-thread safe). Returns false if this event fired too
    // recently; otherwise records the time and returns true. Done before spawning the
    // worker thread so rapid slider movement doesn't spam short-lived threads.
    bool PassThrottle(Event e) {
        std::lock_guard lock(g_mutex);
        const auto now = std::chrono::steady_clock::now();
        auto last = g_lastPlay.find(e);
        if (last != g_lastPlay.end()) {
            const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - last->second).count();
            if (elapsed < ThrottleMsFor(e)) {
                return false;
            }
        }
        g_lastPlay[e] = now;
        return true;
    }

    void PlayNow(Event e) {
        auto* settings = Settings::GetSingleton();
        if (!settings || !settings->enableSounds) {
            return;
        }
        const float vol = std::clamp(settings->soundVolume, 0.0f, 1.0f);
        if (vol <= 0.0f) {
            return;
        }
        const char* base = BaseFileFor(e);
        if (!base) {
            return;
        }

        std::lock_guard lock(g_mutex);

        const auto& variants = VariantsFor(base);
        if (variants.empty()) {
            return;  // no asset installed for this event; silently skip
        }

        std::string chosen = variants.front();
        if (variants.size() > 1) {
            static std::mt19937 rng{ std::random_device{}() };
            std::uniform_int_distribution<std::size_t> dist(0, variants.size() - 1);
            chosen = variants[dist(rng)];
        }

        auto raw = LoadRaw(SoundDir() / chosen);
        if (!raw) {
            logger::warn("BarterSounds: failed to load {}", chosen);
            return;
        }

        // Work on a fresh copy so the cached raw stays unscaled and volume changes apply live.
        auto playable = std::make_shared<std::vector<std::uint8_t>>(*raw);
        ApplyVolume(*playable, vol);

        const BOOL ok = PlaySoundA(reinterpret_cast<LPCSTR>(playable->data()), nullptr,
            SND_MEMORY | SND_ASYNC | SND_NODEFAULT);
        if (!ok) {
            logger::error("BarterSounds: PlaySoundA failed for {} (err={})", chosen, GetLastError());
            return;
        }

        g_keepAlive[g_keepAliveIdx] = playable;
        g_keepAliveIdx = (g_keepAliveIdx + 1) % g_keepAlive.size();
    }

    // --- Persistent playback worker --------------------------------------------------
    // A single long-lived thread drains a small queue. This avoids spawning a brand-new
    // OS thread per cue (the previous approach), whose creation + scheduling latency was
    // the main source of the audible lag between a button press and its click.
    std::mutex                    g_qMutex;
    std::condition_variable       g_qCv;
    std::deque<Event>             g_queue;
    bool                          g_workerStarted = false;

    void WorkerLoop() {
        for (;;) {
            Event e;
            {
                std::unique_lock<std::mutex> lk(g_qMutex);
                g_qCv.wait(lk, [] { return !g_queue.empty(); });
                e = g_queue.front();
                g_queue.pop_front();
            }
            PlayNow(e);
        }
    }

    void EnsureWorker() {
        // Started lazily under g_qMutex (callers already hold it).
        if (!g_workerStarted) {
            g_workerStarted = true;
            std::thread(WorkerLoop).detach();
        }
    }
}  // namespace

void Play(Event e) {
    if (!PassThrottle(e)) {
        return;
    }
    // Hand off to the persistent worker (file I/O + PCM scaling stay off the game thread).
    std::lock_guard<std::mutex> lk(g_qMutex);
    EnsureWorker();
    // Cap the backlog so a stuck/slow cue can't let the queue grow unbounded under spam.
    if (g_queue.size() < 8) {
        g_queue.push_back(e);
        g_qCv.notify_one();
    }
}

void PlayDelayed(Event e, int delayMs) {
    std::thread([e, delayMs]() {
        if (delayMs > 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(delayMs));
        }
        if (PassThrottle(e)) {
            PlayNow(e);
        }
    }).detach();
}

namespace {
    // Default pickup/putdown sound for an item's form type, used when the item itself
    // defines no pickup/putdown sound (mirrors how the game falls back).
    RE::BGSSoundDescriptorForm* DefaultSoundForType(RE::FormType type, bool pickup) {
        auto* dom = RE::BGSDefaultObjectManager::GetSingleton();
        if (!dom) {
            return nullptr;
        }
        using DO = RE::DEFAULT_OBJECT;
        DO obj;
        switch (type) {
        case RE::FormType::Weapon:
            obj = pickup ? DO::kPickupSoundWeapon : DO::kPutdownSoundWeapon;
            break;
        case RE::FormType::Armor:
            obj = pickup ? DO::kPickupSoundArmor : DO::kPutdownSoundArmor;
            break;
        case RE::FormType::Book:
            obj = pickup ? DO::kPickupSoundBook : DO::kPutdownSoundBook;
            break;
        case RE::FormType::Ingredient:
            obj = pickup ? DO::kPickupSoundIngredient : DO::kPutdownSoundIngredient;
            break;
        default:
            obj = pickup ? DO::kPickupSoundGeneric : DO::kPutdownSoundGeneric;
            break;
        }
        return dom->GetObject<RE::BGSSoundDescriptorForm>(obj);
    }
}  // namespace

bool PlayVanillaItemSound(RE::FormID a_formID, bool a_pickup) {
    auto* settings = Settings::GetSingleton();
    if (!settings || !settings->enableSounds || settings->soundVolume <= 0.0f) {
        return false;
    }

    auto* obj = RE::TESForm::LookupByID<RE::TESBoundObject>(a_formID);
    if (!obj) {
        return false;
    }

    RE::BGSSoundDescriptorForm* desc = nullptr;
    if (auto* sounds = obj->As<RE::BGSPickupPutdownSounds>()) {
        desc = a_pickup ? sounds->pickupSound : sounds->putdownSound;
    }
    if (!desc) {
        desc = DefaultSoundForType(obj->GetFormType(), a_pickup);
    }
    if (!desc) {
        return false;
    }

    auto* audioManager = RE::BSAudioManager::GetSingleton();
    if (!audioManager) {
        return false;
    }

    RE::BSSoundHandle handle;
    if (!audioManager->BuildSoundDataFromDescriptor(handle, desc) || !handle.IsValid()) {
        return false;
    }
    if (auto* player = RE::PlayerCharacter::GetSingleton()) {
        handle.SetPosition(player->GetPosition());
    }
    handle.SetVolume(std::clamp(settings->soundVolume, 0.0f, 1.0f));
    return handle.Play();
}

}  // namespace BarterSounds
