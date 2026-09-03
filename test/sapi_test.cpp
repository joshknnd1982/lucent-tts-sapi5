// End-to-end SAPI 5 test that does not need an elevated shell or ATL.
//
//   sapi_test <LucentSAPI.dll> <outDir> [voiceIndex...]
//
// Registers the engine CLSIDs under HKCU\Software\Classes (the current architecture's
// view), creates the tokens straight from the DLL's enumerator, and speaks through a
// real SpVoice into WAV files while collecting bookmark / word / sentence events.  The
// HKCU entries are removed again on exit.  Set SAPI_TEST_MSVOICE=1 to run the same
// checks against the default Microsoft voice as a control.
#include <windows.h>
#include <sapi.h>
#include <sapiddk.h>
#include <sperror.h>
#include <cstdio>
#include <string>
#include <vector>
#include <algorithm>

typedef HRESULT (STDAPICALLTYPE* DllGetClassObjectFn)(REFCLSID, REFIID, void**);

static const wchar_t* kEngineClsid = L"{8e4f2a91-5c3d-4b7e-a6f0-2d9c1b8e7a55}";
static const wchar_t* kEnumClsid = L"{3b9d0c62-6f7e-4a1b-9e2d-5c8f1a7b3d40}";

template <class T> struct Ptr {
    T* p = nullptr;
    ~Ptr() { if (p) p->Release(); }
    T** operator&() { return &p; }
    T* operator->() { return p; }
    operator T*() { return p; }
    void reset() { if (p) { p->Release(); p = nullptr; } }
};

static void registerHkcu(const std::wstring& dll) {
    for (const wchar_t* clsid : { kEngineClsid, kEnumClsid }) {
        std::wstring path = L"Software\\Classes\\CLSID\\" + std::wstring(clsid) + L"\\InProcServer32";
        HKEY k;
        if (RegCreateKeyExW(HKEY_CURRENT_USER, path.c_str(), 0, nullptr, 0, KEY_SET_VALUE, nullptr, &k, nullptr) == ERROR_SUCCESS) {
            RegSetValueExW(k, nullptr, 0, REG_SZ, reinterpret_cast<const BYTE*>(dll.c_str()), static_cast<DWORD>((dll.size() + 1) * 2));
            const wchar_t* tm = L"Both";
            RegSetValueExW(k, L"ThreadingModel", 0, REG_SZ, reinterpret_cast<const BYTE*>(tm), 10);
            RegCloseKey(k);
        }
    }
}

static void unregisterHkcu() {
    for (const wchar_t* clsid : { kEngineClsid, kEnumClsid }) {
        std::wstring path = L"Software\\Classes\\CLSID\\" + std::wstring(clsid);
        RegDeleteTreeW(HKEY_CURRENT_USER, path.c_str());
    }
}

static std::wstring tokenName(ISpObjectToken* token) {
    LPWSTR desc = nullptr;
    std::wstring name;
    if (SUCCEEDED(token->GetStringValue(nullptr, &desc)) && desc) { name = desc; CoTaskMemFree(desc); }
    return name;
}

static DWORD fileSize(const std::wstring& file) {
    WIN32_FILE_ATTRIBUTE_DATA fad = {};
    GetFileAttributesExW(file.c_str(), GetFileExInfoStandard, &fad);
    return fad.nFileSizeLow;
}

static void freeEvent(SPEVENT& ev) {
    if (ev.elParamType == SPET_LPARAM_IS_POINTER || ev.elParamType == SPET_LPARAM_IS_STRING) {
        if (ev.lParam) CoTaskMemFree(reinterpret_cast<void*>(ev.lParam));
    } else if (ev.elParamType == SPET_LPARAM_IS_TOKEN || ev.elParamType == SPET_LPARAM_IS_OBJECT) {
        if (ev.lParam) reinterpret_cast<IUnknown*>(ev.lParam)->Release();
    }
}

struct Counts { int words = 0, marks = 0, sentences = 0; bool ended = false; };

static void drainEvents(ISpVoice* voice, Counts& c, bool verbose) {
    SPEVENT ev;
    ULONG fetched = 0;
    for (;;) {
        memset(&ev, 0, sizeof(ev));
        if (FAILED(voice->GetEvents(1, &ev, &fetched)) || fetched != 1) break;
        switch (ev.eEventId) {
        case SPEI_WORD_BOUNDARY: c.words++; if (verbose) wprintf(L"    word     audio=%llu chars %lu+%lu\n", ev.ullAudioStreamOffset, (unsigned long)ev.lParam, (unsigned long)ev.wParam); break;
        case SPEI_SENTENCE_BOUNDARY: c.sentences++; if (verbose) wprintf(L"    sentence audio=%llu chars %lu+%lu\n", ev.ullAudioStreamOffset, (unsigned long)ev.lParam, (unsigned long)ev.wParam); break;
        case SPEI_TTS_BOOKMARK: c.marks++; if (verbose) wprintf(L"    bookmark audio=%llu \"%s\" (%lu)\n", ev.ullAudioStreamOffset, ev.elParamType == SPET_LPARAM_IS_STRING && ev.lParam ? reinterpret_cast<const wchar_t*>(ev.lParam) : L"", (unsigned long)ev.wParam); break;
        case SPEI_END_INPUT_STREAM: c.ended = true; break;
        default: break;
        }
        freeEvent(ev);
    }
}

// Speaks `text` with `token` into `file` and collects events.  Returns S_OK when SAPI
// reported the end of the stream.
static HRESULT speakToFile(ISpObjectToken* token, const std::wstring& file, const wchar_t* text, DWORD flags, Counts& counts, bool verbose, DWORD purgeAfterMs, DWORD* purgeMs) {
    const bool toAudio = file.empty();   // empty file name = default audio device (events are only delivered there)
    Ptr<ISpVoice> voice;
    HRESULT hr = CoCreateInstance(CLSID_SpVoice, nullptr, CLSCTX_ALL, IID_ISpVoice, reinterpret_cast<void**>(&voice));
    if (FAILED(hr)) { wprintf(L"SpVoice failed 0x%08lx\n", hr); return hr; }
    if (token) {
        hr = voice->SetVoice(token);
        if (FAILED(hr)) { wprintf(L"SetVoice failed 0x%08lx\n", hr); return hr; }
    }
    Ptr<ISpStream> stream;
    if (!toAudio) {
        hr = CoCreateInstance(CLSID_SpStream, nullptr, CLSCTX_ALL, IID_ISpStream, reinterpret_cast<void**>(&stream));
        if (FAILED(hr)) { wprintf(L"SpStream failed 0x%08lx\n", hr); return hr; }
        WAVEFORMATEX wfx = {};
        wfx.wFormatTag = WAVE_FORMAT_PCM;
        wfx.nChannels = 1;
        wfx.nSamplesPerSec = 11025;
        wfx.wBitsPerSample = 16;
        wfx.nBlockAlign = 2;
        wfx.nAvgBytesPerSec = 22050;
        hr = stream->BindToFile(file.c_str(), SPFM_CREATE_ALWAYS, &SPDFID_WaveFormatEx, &wfx, SPFEI_ALL_TTS_EVENTS);
        if (FAILED(hr)) { wprintf(L"BindToFile failed 0x%08lx for %s\n", hr, file.c_str()); return hr; }
        hr = voice->SetOutput(stream, TRUE);
    } else {
        hr = voice->SetOutput(nullptr, TRUE);
    }
    if (FAILED(hr)) { wprintf(L"SetOutput failed 0x%08lx\n", hr); return hr; }
    const ULONGLONG interest = SPFEI(SPEI_WORD_BOUNDARY) | SPFEI(SPEI_SENTENCE_BOUNDARY) | SPFEI(SPEI_TTS_BOOKMARK) | SPFEI(SPEI_END_INPUT_STREAM);
    voice->SetInterest(interest, interest);
    voice->SetNotifyWin32Event();
    ULONGLONG t0 = GetTickCount64();
    hr = voice->Speak(text, flags, nullptr);
    if (FAILED(hr)) { wprintf(L"Speak failed 0x%08lx\n", hr); return hr; }
    bool purged = false;
    for (;;) {
        HRESULT w = voice->WaitUntilDone(100);
        drainEvents(voice, counts, verbose);
        if (purgeAfterMs && !purged && GetTickCount64() - t0 >= purgeAfterMs) {
            ULONGLONG tp = GetTickCount64();
            voice->Speak(nullptr, SPF_PURGEBEFORESPEAK, nullptr);
            voice->WaitUntilDone(10000);
            if (purgeMs) *purgeMs = static_cast<DWORD>(GetTickCount64() - tp);
            purged = true;
            drainEvents(voice, counts, verbose);
            break;
        }
        if (w == S_OK) break;
        if (GetTickCount64() - t0 > 60000) { wprintf(L"  TIMEOUT\n"); break; }
    }
    drainEvents(voice, counts, verbose);
    if (!toAudio) stream->Close();
    return counts.ended || purged ? S_OK : S_FALSE;
}

int wmain(int argc, wchar_t** argv) {
    if (argc < 3) {
        fwprintf(stderr, L"usage: sapi_test <LucentSAPI.dll> <outDir> [voiceIndex...]\n");
        return 2;
    }
    std::wstring dll = argv[1], outDir = argv[2];
    setvbuf(stdout, nullptr, _IONBF, 0);
    CreateDirectoryW(outDir.c_str(), nullptr);
    const bool useMsVoice = GetEnvironmentVariableW(L"SAPI_TEST_MSVOICE", nullptr, 0) != 0;
    const bool verbose = GetEnvironmentVariableW(L"SAPI_TEST_VERBOSE", nullptr, 0) != 0;
    CoInitialize(nullptr);
    registerHkcu(dll);
    int failures = 0;
    {
        HMODULE mod = LoadLibraryW(dll.c_str());
        if (!mod) { fwprintf(stderr, L"cannot load %s (%lu)\n", dll.c_str(), GetLastError()); unregisterHkcu(); return 1; }
        auto getObj = reinterpret_cast<DllGetClassObjectFn>(GetProcAddress(mod, "DllGetClassObject"));
        CLSID enumClsid;
        CLSIDFromString(kEnumClsid, &enumClsid);
        Ptr<IClassFactory> factory;
        HRESULT hr = getObj(enumClsid, IID_IClassFactory, reinterpret_cast<void**>(&factory));
        Ptr<IEnumSpObjectTokens> tokens;
        if (SUCCEEDED(hr)) hr = factory->CreateInstance(nullptr, IID_IEnumSpObjectTokens, reinterpret_cast<void**>(&tokens));
        if (FAILED(hr)) { fwprintf(stderr, L"enumerator failed 0x%08lx\n", hr); unregisterHkcu(); return 1; }
        ULONG count = 0;
        tokens->GetCount(&count);
        wprintf(L"%lu voices\n", count);
        std::vector<int> wanted;
        for (int i = 3; i < argc; ++i) wanted.push_back(_wtoi(argv[i]));
        const wchar_t* text = L"<bookmark mark=\"start\"/>Hello from SAPI. This is a test with a bookmark<bookmark mark=\"42\"/> in the middle. Numbers like 42 and 3.5 are spoken.";
        for (ULONG i = 0; i < count; ++i) {
            if (!wanted.empty() && std::find(wanted.begin(), wanted.end(), static_cast<int>(i)) == wanted.end()) continue;
            Ptr<ISpObjectToken> token;
            if (FAILED(tokens->Item(i, &token))) { failures++; continue; }
            std::wstring name = useMsVoice ? L"(default Microsoft voice)" : tokenName(token);
            std::wstring file = outDir + L"\\voice" + std::to_wstring(i) + L".wav";
            Counts c;
            ULONGLONG t0 = GetTickCount64();
            // 1) render to a WAV file (format negotiation, audio correctness)
            hr = speakToFile(useMsVoice ? nullptr : static_cast<ISpObjectToken*>(token), file, text, SPF_ASYNC | SPF_IS_XML, c, verbose, 0, nullptr);
            DWORD size = fileSize(file);
            ULONGLONG tFile = GetTickCount64() - t0;
            // 2) speak through the default audio device, where SAPI delivers events
            Counts a;
            t0 = GetTickCount64();
            HRESULT ha = speakToFile(useMsVoice ? nullptr : static_cast<ISpObjectToken*>(token), L"", text, SPF_ASYNC | SPF_IS_XML, a, verbose, 0, nullptr);
            // File streams deliver no events (so hr is S_FALSE there); judge the file by its size.
            bool ok = SUCCEEDED(hr) && size > 20000 && ha == S_OK && a.marks == 2 && a.words > 5;
            wprintf(L"[%2lu] %-40s %s  file %lu bytes in %llu ms; audio: %d words, %d marks, %d sentences, end=%d, %llu ms\n", i, name.c_str(), ok ? L"ok  " : L"FAIL", size, tFile, a.words, a.marks, a.sentences, a.ended ? 1 : 0, GetTickCount64() - t0);
            if (!ok) failures++;
        }
        // Cancel test through the audio device: speak a long text and purge after 300 ms
        // (the screen-reader scenario); the purge must return well under a second.
        {
            Ptr<ISpObjectToken> token;
            if (SUCCEEDED(tokens->Item(count > 1 ? 1 : 0, &token))) {
                std::wstring longText;
                for (int i = 0; i < 40; ++i) longText += L"This is a long sentence number " + std::to_wstring(i) + L" used to test cancellation through SAPI. ";
                Counts c;
                DWORD purgeMs = 0;
                speakToFile(useMsVoice ? nullptr : static_cast<ISpObjectToken*>(token), L"", longText.c_str(), SPF_ASYNC, c, false, 300, &purgeMs);
                bool ok = purgeMs < 1000;
                wprintf(L"purge: %s (%lu ms to stop, %d words heard before purge)\n", ok ? L"ok" : L"FAIL", purgeMs, c.words);
                if (!ok) failures++;
            }
        }
        tokens.reset();
        factory.reset();
    }
    unregisterHkcu();
    CoUninitialize();
    wprintf(L"%d failure(s)\n", failures);
    return failures ? 1 : 0;
}
