#include <new>
#include <string>
#include <vector>
#include <cmath>
#include <algorithm>
#include "utils.hpp"
#include "ISpTTSEngineImpl.hpp"
#include "lucent_log.h"
#include "lucent_settings.h"

namespace Lucent {
namespace sapi {

namespace {

constexpr WORD AUDIO_CHANNELS = 1;
constexpr WORD AUDIO_BITS_PER_SAMPLE = 16;
constexpr ULONG WRITE_SLICE_BYTES = 4410;     // 200 ms at 11025 Hz: the granularity at which SAPI aborts are honoured
constexpr size_t CHUNK_TARGET_BYTES = 1500;   // utterance chunk size sent to the engine

lucent::LucentEngine* g_engine = nullptr;
CRITICAL_SECTION g_engineCs;
bool g_engineCsInit = false;
HINSTANCE g_dll = nullptr;
lucent::Settings g_settings;
FILETIME g_settingsTime = {};
bool g_settingsLoaded = false;

void reload_settings_if_changed()
{
    FILETIME ft = lucent::settingsFileTime();
    if (!g_settingsLoaded || CompareFileTime(&ft, &g_settingsTime) != 0) {
        lucent::loadSettings(g_settings);
        g_settingsTime = ft;
        g_settingsLoaded = true;
        lucent::Logger::instance().configure(g_settings.logging, L"sapi");
        LLOG("settings: %ls / %ls, %d Hz, email %d, speed %d%%, pitch %d/%d/%d%%, breath %d%%, tilt %d%%, vt %d/%d%%, vol %d%%, sapiProsody %d",
             g_settings.language.c_str(), g_settings.speaker.c_str(), g_settings.sampleRate, g_settings.emailPreprocessing ? 1 : 0,
             g_settings.speed, g_settings.topPitch, g_settings.referenceLine, g_settings.bottomPitch, g_settings.breathiness,
             g_settings.spectralTilt, g_settings.frontVocalTract, g_settings.backVocalTract, g_settings.volume, g_settings.applySapiProsody ? 1 : 0);
    }
}

float rate_to_speed_factor(long rate)
{
    rate = std::clamp(rate, -10L, 10L);
    if (rate >= 0) {
        return std::pow(0.4f, rate / 10.0f);      // +10 -> 0.4 (fastest the engine honours)
    }
    return std::pow(3.0f, -rate / 10.0f);         // -10 -> 3.0 (slowest)
}

float pitch_to_factor(int pitch)
{
    pitch = std::clamp(pitch, -10, 10);
    return std::pow(2.0f, pitch / 10.0f);          // one octave either way
}

float clamp_hz(float hz)
{
    return std::clamp(hz, lucent::kRangePitchHz.min, lucent::kRangePitchHz.max);
}

// A marker inserted into the engine text as \Mrk=n\ .
struct Marker {
    enum Kind { Bookmark, Word, Sentence };
    Kind kind;
    ULONG textOffset;
    ULONG textLen;
    std::wstring bookmark;
};

struct SpeakContext {
    ISpTTSEngineSite* site = nullptr;
    ULONGLONG bytes_written = 0;
    ULONGLONG chunk_base = 0;
    std::vector<std::vector<size_t>> marker_groups;   // engine marker id -> marker indices
    std::vector<Marker> markers;
    bool aborted = false;
};

bool check_actions(SpeakContext& ctx)
{
    const DWORD actions = ctx.site->GetActions();
    if (actions & SPVES_ABORT) {
        LLOG("sapi: abort requested");
        ctx.aborted = true;
        return false;
    }
    if (actions & SPVES_SKIP) {
        LLOG("sapi: skip requested");
        ctx.site->CompleteSkip(0);
        ctx.aborted = true;
        return false;
    }
    return true;
}

bool write_audio(SpeakContext& ctx, const uint8_t* data, size_t size)
{
    const BYTE* ptr = data;
    size_t remaining = size;
    while (remaining > 0) {
        if (!check_actions(ctx)) {
            return false;
        }
        const ULONG slice = static_cast<ULONG>((std::min)(remaining, static_cast<size_t>(WRITE_SLICE_BYTES)));
        ULONG written = 0;
        HRESULT hr = ctx.site->Write(ptr, slice, &written);
        if (FAILED(hr)) {
            LLOG("sapi: Write failed 0x%08lx", hr);
            return false;
        }
        // SAPI does not reliably report pcbWritten; a successful Write consumed the slice.
        ctx.bytes_written += slice;
        remaining -= slice;
        ptr += slice;
    }
    return true;
}

void fire_marker(SpeakContext& ctx, uint32_t markerId, uint32_t offsetInCall)
{
    if (markerId == 0 || markerId > ctx.marker_groups.size()) {
        return;
    }
    const ULONGLONG stream_offset = ctx.chunk_base + offsetInCall;
    for (size_t idx : ctx.marker_groups[markerId - 1]) {
        const Marker& m = ctx.markers[idx];
        SPEVENT ev = {};
        ev.ullAudioStreamOffset = stream_offset;
        ev.ulStreamNum = 0;
        switch (m.kind) {
        case Marker::Bookmark: {
            ev.eEventId = SPEI_TTS_BOOKMARK;
            ev.elParamType = SPET_LPARAM_IS_STRING;
            ev.lParam = reinterpret_cast<LPARAM>(m.bookmark.c_str());
            long id = 0;
            try { id = std::stol(m.bookmark); } catch (...) {}
            ev.wParam = id;
            break;
        }
        case Marker::Word:
            ev.eEventId = SPEI_WORD_BOUNDARY;
            ev.elParamType = SPET_LPARAM_IS_UNDEFINED;
            ev.lParam = m.textOffset;
            ev.wParam = m.textLen;
            break;
        case Marker::Sentence:
            ev.eEventId = SPEI_SENTENCE_BOUNDARY;
            ev.elParamType = SPET_LPARAM_IS_UNDEFINED;
            ev.lParam = m.textOffset;
            ev.wParam = m.textLen;
            break;
        }
        ctx.site->AddEvents(&ev, 1);
    }
}

bool is_word_char(wchar_t c)
{
    return iswalnum(c) || c == L'\'' || c == L'-' || c > 0x7f;
}

// Escapes a piece of user text so the engine never sees a control tag in it.
void append_escaped(std::wstring& out, const wchar_t* s, ULONG n)
{
    for (ULONG i = 0; i < n; ++i) {
        wchar_t c = s[i];
        if (c == L'\\') {
            out += L"\\\\";
        } else if (c < 0x20) {
            out += L' ';
        } else {
            out += c;
        }
    }
}

std::string marker_tag(uint32_t id)
{
    char buf[32];
    snprintf(buf, sizeof(buf), " \\Mrk=%u\\ ", id);
    return buf;
}

// Splits the engine text into chunks at sentence boundaries, never inside a tag.
std::vector<std::string> split_chunks(const std::string& text)
{
    std::vector<std::string> chunks;
    size_t start = 0;
    while (start < text.size()) {
        if (text.size() - start <= CHUNK_TARGET_BYTES) {
            chunks.push_back(text.substr(start));
            break;
        }
        // look for a sentence end after CHUNK_TARGET_BYTES/2
        size_t cut = std::string::npos;
        for (size_t i = start + CHUNK_TARGET_BYTES; i > start + CHUNK_TARGET_BYTES / 2; --i) {
            const char c = text[i - 1];
            if ((c == '.' || c == '!' || c == '?' || c == ';' || c == ':') && text[i] == ' ') {
                cut = i;
                break;
            }
        }
        if (cut == std::string::npos) {
            for (size_t i = start + CHUNK_TARGET_BYTES; i > start + CHUNK_TARGET_BYTES / 2; --i) {
                if (text[i] == ' ' && text[i - 1] != '\\') {
                    cut = i;
                    break;
                }
            }
        }
        if (cut == std::string::npos) {
            cut = start + CHUNK_TARGET_BYTES;
        }
        chunks.push_back(text.substr(start, cut - start));
        start = cut;
    }
    return chunks;
}

}  // namespace

void InitEngine(HINSTANCE dll)
{
    g_dll = dll;
    if (!g_engineCsInit) {
        InitializeCriticalSection(&g_engineCs);
        g_engineCsInit = true;
    }
    reload_settings_if_changed();
}

void CleanupEngine()
{
    if (g_engineCsInit) {
        EnterCriticalSection(&g_engineCs);
        if (g_engine) g_engine->setDetaching();   // DllMain holds the loader lock: never join our thread here
        delete g_engine;
        g_engine = nullptr;
        LeaveCriticalSection(&g_engineCs);
        DeleteCriticalSection(&g_engineCs);
        g_engineCsInit = false;
    }
}

ISpTTSEngineImpl::ISpTTSEngineImpl()
    : voice_(0)
    , sample_rate_(11025)
{
}

ISpTTSEngineImpl::~ISpTTSEngineImpl() = default;

STDMETHODIMP ISpTTSEngineImpl::SetObjectToken(ISpObjectToken* pToken)
{
    if (!pToken) {
        return E_INVALIDARG;
    }

    try {
        reload_settings_if_changed();

        DWORD index = 0;
        if (FAILED(pToken->GetDWORD(L"VoiceIndex", &index))) {
            // Fall back to matching the display name.
            ISpDataKeyPtr attr;
            utils::out_ptr<wchar_t> name(CoTaskMemFree);
            if (SUCCEEDED(pToken->OpenKey(L"Attributes", &attr)) && SUCCEEDED(attr->GetStringValue(L"Name", name.address()))) {
                for (int i = 0; i < voice_count(); ++i) {
                    if (_wcsicmp(voice_attributes(i).get_name().c_str(), name.get()) == 0) {
                        index = static_cast<DWORD>(i);
                        break;
                    }
                }
            }
        }
        voice_ = voice_attributes(static_cast<int>(index));

        if (voice_.is_custom()) {
            sample_rate_ = g_settings.sampleRate;
            const lucent::LanguageInfo* lang = lucent::findLanguage(g_settings.language);
            if (lang && !lang->has11k) sample_rate_ = 8000;
        } else {
            const lucent::LanguageInfo* lang = lucent::findLanguage(voice_.speaker()->language);
            sample_rate_ = (lang && !lang->has11k) ? 8000 : 11025;
        }

        token_ = pToken;
        LLOG("sapi: voice %d (%ls) selected, %d Hz", voice_.get_index(), voice_.get_name().c_str(), sample_rate_);
        return S_OK;
    }
    catch (const std::bad_alloc&) {
        return E_OUTOFMEMORY;
    }
    catch (...) {
        return E_UNEXPECTED;
    }
}

STDMETHODIMP ISpTTSEngineImpl::GetObjectToken(ISpObjectToken** ppToken)
{
    if (!ppToken) {
        return E_POINTER;
    }
    *ppToken = nullptr;

    if (token_) {
        token_.AddRef();
        *ppToken = token_.GetInterfacePtr();
        return S_OK;
    }
    return E_UNEXPECTED;
}

STDMETHODIMP ISpTTSEngineImpl::GetOutputFormat(
    const GUID* /*pTargetFmtId*/,
    const WAVEFORMATEX* /*pTargetWaveFormatEx*/,
    GUID* pOutputFormatId,
    WAVEFORMATEX** ppCoMemOutputWaveFormatEx)
{
    if (!pOutputFormatId || !ppCoMemOutputWaveFormatEx) {
        return E_POINTER;
    }

    *pOutputFormatId = SPDFID_WaveFormatEx;
    *ppCoMemOutputWaveFormatEx = nullptr;

    auto* pwfex = static_cast<WAVEFORMATEX*>(CoTaskMemAlloc(sizeof(WAVEFORMATEX)));
    if (!pwfex) {
        return E_OUTOFMEMORY;
    }

    pwfex->wFormatTag = WAVE_FORMAT_PCM;
    pwfex->nChannels = AUDIO_CHANNELS;
    pwfex->nSamplesPerSec = static_cast<DWORD>(sample_rate_);
    pwfex->wBitsPerSample = AUDIO_BITS_PER_SAMPLE;
    pwfex->nBlockAlign = pwfex->nChannels * pwfex->wBitsPerSample / 8;
    pwfex->nAvgBytesPerSec = pwfex->nSamplesPerSec * pwfex->nBlockAlign;
    pwfex->cbSize = 0;

    *ppCoMemOutputWaveFormatEx = pwfex;
    LLOG("sapi: output format %lu Hz", pwfex->nSamplesPerSec);
    return S_OK;
}

bool ISpTTSEngineImpl::build_request(lucent::VoiceRequest& req, long sapi_rate, USHORT sapi_volume, int sapi_pitch)
{
    const lucent::SpeakerInfo* sp = nullptr;
    if (voice_.is_custom()) {
        reload_settings_if_changed();
        sp = lucent::findSpeaker(g_settings.language, g_settings.speaker);
        req.language = lucent::findLanguage(g_settings.language);
        if (!req.language || !sp) {
            LLOG("sapi: custom voice has no valid language/speaker");
            return false;
        }
        req.female = sp->female;
        req.channelSuffix = sp->channelSuffix;
        req.sampleRate = sample_rate_;
        req.email = g_settings.emailPreprocessing && req.language->hasEmail;
        req.topPitch = g_settings.topPitchHz();
        req.referenceLine = g_settings.referenceLineHz();
        req.bottomPitch = g_settings.bottomPitchHz();
        req.frontVocalTract = g_settings.frontScale();
        req.backVocalTract = g_settings.backScale();
        req.speedFactor = g_settings.speedFactor();
        req.volume = g_settings.volumeScale();
        req.aspiration = g_settings.aspiration();
        req.spectralTilt = g_settings.tilt();
        if (g_settings.applySapiProsody) {
            req.speedFactor *= rate_to_speed_factor(sapi_rate);
            const float pf = pitch_to_factor(sapi_pitch);
            req.topPitch = clamp_hz(req.topPitch * pf);
            req.referenceLine = clamp_hz(req.referenceLine * pf);
            req.bottomPitch = clamp_hz(req.bottomPitch * pf);
            req.volume *= sapi_volume / 100.0f;
        }
    } else {
        sp = voice_.speaker();
        req.language = lucent::findLanguage(sp->language);
        if (!req.language) return false;
        req.female = sp->female;
        req.channelSuffix = sp->channelSuffix;
        req.sampleRate = sample_rate_;
        req.email = false;
        const float pf = pitch_to_factor(sapi_pitch);
        req.topPitch = clamp_hz(sp->topPitch * pf);
        req.referenceLine = clamp_hz(sp->referenceLine * pf);
        req.bottomPitch = clamp_hz(sp->bottomPitch * pf);
        req.frontVocalTract = sp->frontVocalTract;
        req.backVocalTract = sp->backVocalTract;
        req.speedFactor = rate_to_speed_factor(sapi_rate);
        req.volume = sapi_volume / 100.0f;
        req.aspiration = 0.0f;
        req.spectralTilt = 0.0f;
    }
    req.speedFactor = std::clamp(req.speedFactor, 0.3f, 4.0f);
    req.volume = std::clamp(req.volume, 0.0f, 2.0f);
    // keep the pitch triple ordered
    if (req.bottomPitch > req.referenceLine) req.bottomPitch = req.referenceLine;
    if (req.topPitch < req.referenceLine) req.topPitch = req.referenceLine;
    return true;
}

STDMETHODIMP ISpTTSEngineImpl::Speak(
    DWORD dwSpeakFlags,
    REFGUID /*rguidFormatId*/,
    const WAVEFORMATEX* /*pWaveFormatEx*/,
    const SPVTEXTFRAG* pTextFragList,
    ISpTTSEngineSite* pOutputSite)
{
    if (!pTextFragList || !pOutputSite) {
        return E_INVALIDARG;
    }
    if (!g_engineCsInit) {
        return E_UNEXPECTED;
    }

    try {
        long sapi_rate = 0;
        pOutputSite->GetRate(&sapi_rate);
        USHORT sapi_volume = 100;
        pOutputSite->GetVolume(&sapi_volume);

        ULONGLONG event_interest = 0;
        pOutputSite->GetEventInterest(&event_interest);
        const bool want_sentence = (event_interest & (1ULL << SPEI_SENTENCE_BOUNDARY)) != 0;
        const bool want_word = (event_interest & (1ULL << SPEI_WORD_BOUNDARY)) != 0;

        LLOG("sapi: Speak flags 0x%08lx rate %ld volume %u events 0x%llx", dwSpeakFlags, sapi_rate, sapi_volume, event_interest);

        // --- Build the tagged text -------------------------------------------------
        SpeakContext ctx;
        ctx.site = pOutputSite;
        std::wstring text;
        int sapi_pitch = 0;
        USHORT frag_volume = 100;
        bool first_speak_frag = true;
        bool have_text = false;
        uint32_t last_marker_id = 0;      // marker id with no text appended after it
        auto add_marker = [&](Marker m) {
            if (last_marker_id == 0) {
                ctx.marker_groups.emplace_back();
                last_marker_id = static_cast<uint32_t>(ctx.marker_groups.size());
                std::string tag = marker_tag(last_marker_id);
                text.append(tag.begin(), tag.end());
            }
            ctx.markers.push_back(std::move(m));
            ctx.marker_groups[last_marker_id - 1].push_back(ctx.markers.size() - 1);
        };

        for (const SPVTEXTFRAG* frag = pTextFragList; frag; frag = frag->pNext) {
            if (!check_actions(ctx)) {
                return S_OK;
            }
            switch (frag->State.eAction) {
            case SPVA_Bookmark: {
                Marker m;
                m.kind = Marker::Bookmark;
                m.textOffset = frag->ulTextSrcOffset;
                m.textLen = frag->ulTextLen;
                m.bookmark.assign(frag->pTextStart ? frag->pTextStart : L"", frag->ulTextLen);
                add_marker(std::move(m));
                break;
            }
            case SPVA_Silence: {
                wchar_t buf[48];
                swprintf_s(buf, L" \\Pau=%lu\\ ", static_cast<unsigned long>(frag->State.SilenceMSecs));
                text += buf;
                break;
            }
            case SPVA_Speak:
            case SPVA_SpellOut:
            case SPVA_Pronounce: {
                if (frag->ulTextLen == 0 || !frag->pTextStart) break;
                if (first_speak_frag) {
                    sapi_pitch = frag->State.PitchAdj.MiddleAdj;
                    frag_volume = static_cast<USHORT>((std::min)(100UL, frag->State.Volume));
                    first_speak_frag = false;
                }
                if (want_sentence) {
                    Marker m;
                    m.kind = Marker::Sentence;
                    m.textOffset = frag->ulTextSrcOffset;
                    m.textLen = frag->ulTextLen;
                    add_marker(std::move(m));
                }
                const wchar_t* s = frag->pTextStart;
                const ULONG n = frag->ulTextLen;
                if (frag->State.eAction == SPVA_SpellOut) {
                    for (ULONG i = 0; i < n; ++i) {
                        if (iswspace(s[i])) continue;
                        if (want_word) {
                            Marker m;
                            m.kind = Marker::Word;
                            m.textOffset = frag->ulTextSrcOffset + i;
                            m.textLen = 1;
                            add_marker(std::move(m));
                        }
                        append_escaped(text, s + i, 1);
                        text += L' ';
                        last_marker_id = 0;
                        have_text = true;
                    }
                } else {
                    ULONG i = 0;
                    while (i < n) {
                        if (is_word_char(s[i])) {
                            ULONG j = i;
                            while (j < n && is_word_char(s[j])) ++j;
                            if (want_word) {
                                Marker m;
                                m.kind = Marker::Word;
                                m.textOffset = frag->ulTextSrcOffset + i;
                                m.textLen = j - i;
                                add_marker(std::move(m));
                            }
                            append_escaped(text, s + i, j - i);
                            last_marker_id = 0;
                            have_text = true;
                            i = j;
                        } else {
                            append_escaped(text, s + i, 1);
                            if (!iswspace(s[i])) {
                                last_marker_id = 0;
                                have_text = true;
                            }
                            ++i;
                        }
                    }
                }
                text += L' ';
                break;
            }
            default:
                break;
            }
        }

        if (!have_text) {
            LLOG("sapi: nothing speakable");
            return S_OK;
        }

        lucent::VoiceRequest req;
        const USHORT combined_volume = static_cast<USHORT>((std::min)(100, static_cast<int>(sapi_volume) * frag_volume / 100));
        if (!build_request(req, sapi_rate, combined_volume, sapi_pitch)) {
            return E_FAIL;
        }

        UINT cp = req.language->codePage;
        if (req.language->engineIndex == lucent::LANG_ChineseMandarin && req.channelSuffix && strcmp(req.channelSuffix, "m") == 0) {
            cp = 950;  // Ming Big5
        }
        const std::string engine_text = lucent::toCodePage(text, cp);
        const std::vector<std::string> chunks = split_chunks(engine_text);
        LLOG("sapi: %zu chunks, %zu markers, speed %.3f pitch %.0f/%.0f/%.0f volume %.2f", chunks.size(), ctx.markers.size(),
             req.speedFactor, req.topPitch, req.referenceLine, req.bottomPitch, req.volume);

        // --- Render --------------------------------------------------------------
        EnterCriticalSection(&g_engineCs);
        struct Unlock { ~Unlock() { LeaveCriticalSection(&g_engineCs); } } unlock;

        if (!g_engine) {
            const std::wstring dir = lucent::engineDirectory(g_dll);
            if (dir.empty()) {
                LLOG("sapi: engine directory not found");
                return E_FAIL;
            }
            g_engine = new lucent::LucentEngine(dir);
        }

        lucent::SpeakSink sink;
        sink.onAudio = [&](const uint8_t* pcm, size_t bytes) { return write_audio(ctx, pcm, bytes); };
        sink.onBookmark = [&](uint32_t id, uint32_t off) { fire_marker(ctx, id, off); };

        for (size_t ci = 0; ci < chunks.size(); ++ci) {
            if (!check_actions(ctx)) break;
            const DWORD actions = pOutputSite->GetActions();
            if (actions & (SPVES_RATE | SPVES_VOLUME)) {
                pOutputSite->GetRate(&sapi_rate);
                pOutputSite->GetVolume(&sapi_volume);
                build_request(req, sapi_rate, static_cast<USHORT>((std::min)(100, static_cast<int>(sapi_volume) * frag_volume / 100)), sapi_pitch);
            }
            ctx.chunk_base = ctx.bytes_written;
            bool aborted = false;
            bool ok = g_engine->speak(req, chunks[ci], sink, &aborted);
            if (!ok) {
                // One retry with a fresh engine process.
                LLOG("sapi: engine transport failed, retrying once");
                ok = g_engine->speak(req, chunks[ci], sink, &aborted);
                if (!ok) {
                    return E_FAIL;
                }
            }
            if (aborted || ctx.aborted) break;
        }

        LLOG("sapi: Speak done, %llu bytes%s", ctx.bytes_written, ctx.aborted ? " (aborted)" : "");
        return S_OK;
    }
    catch (const std::bad_alloc&) {
        return E_OUTOFMEMORY;
    }
    catch (...) {
        LLOG("sapi: unexpected exception in Speak");
        return E_UNEXPECTED;
    }
}
}
}
