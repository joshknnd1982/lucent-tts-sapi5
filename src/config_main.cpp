// Lucent TTS Configuration utility.
//
// Edits %APPDATA%\LucentSAPI\settings.ini, which the "Lucent Custom Voice" SAPI token
// re-reads on every utterance, so changes reach running screen readers immediately.
// Every control is labelled and in the tab order; values are 0..100 percent of the
// engine's real range.  The Test button renders through the engine directly, so it works
// even when the SAPI DLL is not registered.
#include <windows.h>
#include <commctrl.h>
#include <shellapi.h>
#include <mmsystem.h>
#include <string>
#include <vector>
#include <thread>
#include <atomic>
#include "config_resource.h"
#include "lucent_settings.h"
#include "lucent_engine.h"
#include "lucent_log.h"

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "winmm.lib")

using namespace lucent;

namespace {

HINSTANCE g_instance = nullptr;
Settings g_settings;
bool g_loading = true;          // TRUE from static init: WM_COMMAND can arrive before WM_INITDIALOG
LucentEngine* g_engine = nullptr;
std::thread g_testThread;
std::atomic<bool> g_testRunning{ false };
std::atomic<bool> g_stopRequested{ false };
std::vector<uint8_t> g_wav;     // in-memory WAV played by PlaySound
constexpr UINT WM_APP_STATUS = WM_APP + 1;
constexpr UINT WM_APP_PLAY = WM_APP + 2;

struct SpinField {
    int edit, spin, label;
    int Settings::* member;
    const Range* range;
    const wchar_t* unit;
};

const SpinField kFields[] = {
    { IDC_SPEED, IDC_SPEED_SPIN, IDC_SPEED_LABEL, &Settings::speed, &kRangeSpeed, L"x slower" },
    { IDC_TOP, IDC_TOP_SPIN, IDC_TOP_LABEL, &Settings::topPitch, &kRangePitchHz, L"Hz" },
    { IDC_REF, IDC_REF_SPIN, IDC_REF_LABEL, &Settings::referenceLine, &kRangePitchHz, L"Hz" },
    { IDC_BOTTOM, IDC_BOTTOM_SPIN, IDC_BOTTOM_LABEL, &Settings::bottomPitch, &kRangePitchHz, L"Hz" },
    { IDC_BREATH, IDC_BREATH_SPIN, IDC_BREATH_LABEL, &Settings::breathiness, &kRangeBreathiness, L"" },
    { IDC_TILT, IDC_TILT_SPIN, IDC_TILT_LABEL, &Settings::spectralTilt, &kRangeSpectralTilt, L"" },
    { IDC_FRONT, IDC_FRONT_SPIN, IDC_FRONT_LABEL, &Settings::frontVocalTract, &kRangeVocalTract, L"x" },
    { IDC_BACK, IDC_BACK_SPIN, IDC_BACK_LABEL, &Settings::backVocalTract, &kRangeVocalTract, L"x" },
    { IDC_VOLUME, IDC_VOLUME_SPIN, IDC_VOLUME_LABEL, &Settings::volume, &kRangeVolume, L"" },
};

const wchar_t* kBaseLabels[] = {
    L"S&peaking speed (%%)", L"&Top pitch (%%)", L"Re&ference line pitch (%%)", L"&Bottom pitch (%%)",
    L"Breat&hiness (%%)", L"Spectral t&ilt (%%)", L"Fro&nt vocal tract (%%)", L"Bac&k vocal tract (%%)", L"Volu&me (%%)",
};

std::wstring engineDir() {
    return engineDirectory(nullptr);
}

void setStatus(HWND dlg, const wchar_t* text) {
    SetDlgItemTextW(dlg, IDC_STATUS, text);
}

void updateLabel(HWND dlg, size_t i) {
    const SpinField& f = kFields[i];
    const float v = percentToValue(*f.range, g_settings.*f.member);
    wchar_t buf[128];
    if (f.range == &kRangeSpeed) {
        swprintf_s(buf, L"S&peaking speed (%%), now %.2f:", v);
    } else if (f.range == &kRangePitchHz) {
        swprintf_s(buf, kBaseLabels[i], 0);
        wcscat_s(buf, L", now ");
        wchar_t hz[32];
        swprintf_s(hz, L"%.0f Hz:", v);
        wcscat_s(buf, hz);
    } else if (f.range == &kRangeVocalTract) {
        swprintf_s(buf, kBaseLabels[i], 0);
        wchar_t x[32];
        swprintf_s(x, L", now %.2f:", v);
        wcscat_s(buf, x);
    } else {
        swprintf_s(buf, kBaseLabels[i], 0);
        wcscat_s(buf, L":");
    }
    SetDlgItemTextW(dlg, f.label, buf);
}

void fillSpeakers(HWND dlg) {
    HWND combo = GetDlgItem(dlg, IDC_SPEAKER);
    SendMessageW(combo, CB_RESETCONTENT, 0, 0);
    size_t n = 0;
    const SpeakerInfo* sp = speakers(&n);
    int sel = 0;
    for (size_t i = 0; i < n; ++i) {
        if (_wcsicmp(sp[i].language, g_settings.language.c_str()) != 0) continue;
        std::wstring label = sp[i].name;
        label += sp[i].female ? L" (female)" : L" (male)";
        int idx = static_cast<int>(SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(label.c_str())));
        SendMessageW(combo, CB_SETITEMDATA, idx, static_cast<LPARAM>(i));
        if (_wcsicmp(sp[i].name, g_settings.speaker.c_str()) == 0) sel = idx;
    }
    SendMessageW(combo, CB_SETCURSEL, sel, 0);
    int data = static_cast<int>(SendMessageW(combo, CB_GETITEMDATA, sel, 0));
    if (data >= 0 && data < static_cast<int>(n)) g_settings.speaker = sp[data].name;
}

void fillSampleRates(HWND dlg) {
    HWND combo = GetDlgItem(dlg, IDC_SAMPLERATE);
    SendMessageW(combo, CB_RESETCONTENT, 0, 0);
    const LanguageInfo* lang = findLanguage(g_settings.language);
    int sel = 0;
    if (!lang || lang->has11k) {
        SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"11025 Hz (standard quality)"));
    }
    if (!lang || lang->has8k) {
        int idx = static_cast<int>(SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"8000 Hz (telephone quality)")));
        if (g_settings.sampleRate == 8000 || (lang && !lang->has11k)) sel = idx;
    }
    SendMessageW(combo, CB_SETCURSEL, sel, 0);
    wchar_t text[64] = L"";
    SendMessageW(combo, CB_GETLBTEXT, sel, reinterpret_cast<LPARAM>(text));
    g_settings.sampleRate = wcsncmp(text, L"8000", 4) == 0 ? 8000 : 11025;
    EnableWindow(GetDlgItem(dlg, IDC_EMAIL), !lang || lang->hasEmail);
}

void loadIntoDialog(HWND dlg) {
    g_loading = true;
    HWND lc = GetDlgItem(dlg, IDC_LANGUAGE);
    SendMessageW(lc, CB_RESETCONTENT, 0, 0);
    size_t n = 0;
    const LanguageInfo* langs = languages(&n);
    int sel = 0;
    for (size_t i = 0; i < n; ++i) {
        int idx = static_cast<int>(SendMessageW(lc, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(langs[i].display)));
        SendMessageW(lc, CB_SETITEMDATA, idx, static_cast<LPARAM>(i));
        if (_wcsicmp(langs[i].key, g_settings.language.c_str()) == 0) sel = idx;
    }
    SendMessageW(lc, CB_SETCURSEL, sel, 0);
    fillSpeakers(dlg);
    fillSampleRates(dlg);
    CheckDlgButton(dlg, IDC_EMAIL, g_settings.emailPreprocessing ? BST_CHECKED : BST_UNCHECKED);
    for (size_t i = 0; i < sizeof(kFields) / sizeof(kFields[0]); ++i) {
        const SpinField& f = kFields[i];
        SendDlgItemMessageW(dlg, f.spin, UDM_SETRANGE32, 0, 100);
        SendDlgItemMessageW(dlg, f.spin, UDM_SETPOS32, 0, g_settings.*f.member);
        SetDlgItemInt(dlg, f.edit, static_cast<UINT>(g_settings.*f.member), FALSE);
        updateLabel(dlg, i);
    }
    CheckDlgButton(dlg, IDC_SAPIPROSODY, g_settings.applySapiProsody ? BST_CHECKED : BST_UNCHECKED);
    CheckDlgButton(dlg, IDC_LOGGING, g_settings.logging ? BST_CHECKED : BST_UNCHECKED);
    CheckDlgButton(dlg, IDC_ENGINELOG, g_settings.loggingEngine ? BST_CHECKED : BST_UNCHECKED);
    g_loading = false;
}

void readFromDialog(HWND dlg) {
    size_t n = 0;
    const LanguageInfo* langs = languages(&n);
    int sel = static_cast<int>(SendDlgItemMessageW(dlg, IDC_LANGUAGE, CB_GETCURSEL, 0, 0));
    if (sel >= 0) {
        int data = static_cast<int>(SendDlgItemMessageW(dlg, IDC_LANGUAGE, CB_GETITEMDATA, sel, 0));
        if (data >= 0 && data < static_cast<int>(n)) g_settings.language = langs[data].key;
    }
    size_t ns = 0;
    const SpeakerInfo* sp = speakers(&ns);
    sel = static_cast<int>(SendDlgItemMessageW(dlg, IDC_SPEAKER, CB_GETCURSEL, 0, 0));
    if (sel >= 0) {
        int data = static_cast<int>(SendDlgItemMessageW(dlg, IDC_SPEAKER, CB_GETITEMDATA, sel, 0));
        if (data >= 0 && data < static_cast<int>(ns)) g_settings.speaker = sp[data].name;
    }
    wchar_t text[64] = L"";
    sel = static_cast<int>(SendDlgItemMessageW(dlg, IDC_SAMPLERATE, CB_GETCURSEL, 0, 0));
    if (sel >= 0) {
        SendDlgItemMessageW(dlg, IDC_SAMPLERATE, CB_GETLBTEXT, sel, reinterpret_cast<LPARAM>(text));
        g_settings.sampleRate = wcsncmp(text, L"8000", 4) == 0 ? 8000 : 11025;
    }
    g_settings.emailPreprocessing = IsDlgButtonChecked(dlg, IDC_EMAIL) == BST_CHECKED;
    for (const SpinField& f : kFields) {
        BOOL ok = FALSE;
        int v = static_cast<int>(GetDlgItemInt(dlg, f.edit, &ok, FALSE));
        if (ok) g_settings.*f.member = (std::max)(0, (std::min)(100, v));
    }
    g_settings.applySapiProsody = IsDlgButtonChecked(dlg, IDC_SAPIPROSODY) == BST_CHECKED;
    g_settings.logging = IsDlgButtonChecked(dlg, IDC_LOGGING) == BST_CHECKED;
    g_settings.loggingEngine = IsDlgButtonChecked(dlg, IDC_ENGINELOG) == BST_CHECKED;
}

void saveNow(HWND dlg) {
    if (g_loading) return;
    readFromDialog(dlg);
    if (saveSettings(g_settings)) {
        setStatus(dlg, L"Saved.");
    } else {
        setStatus(dlg, L"Could not save settings.ini!");
    }
    for (size_t i = 0; i < sizeof(kFields) / sizeof(kFields[0]); ++i) updateLabel(dlg, i);
    LLOG("config: saved %ls/%ls speed %d pitch %d/%d/%d", g_settings.language.c_str(), g_settings.speaker.c_str(), g_settings.speed, g_settings.topPitch, g_settings.referenceLine, g_settings.bottomPitch);
}

void applySpeakerDefaults(HWND dlg) {
    const SpeakerInfo* sp = findSpeaker(g_settings.language, g_settings.speaker);
    if (!sp) return;
    g_settings.applySpeakerDefaults(*sp);
    g_loading = true;
    for (size_t i = 0; i < sizeof(kFields) / sizeof(kFields[0]); ++i) {
        const SpinField& f = kFields[i];
        SendDlgItemMessageW(dlg, f.spin, UDM_SETPOS32, 0, g_settings.*f.member);
        SetDlgItemInt(dlg, f.edit, static_cast<UINT>(g_settings.*f.member), FALSE);
        updateLabel(dlg, i);
    }
    g_loading = false;
}

void stopTest() {
    g_stopRequested = true;
    if (g_engine) g_engine->cancel();
    PlaySoundW(nullptr, nullptr, 0);
    if (g_testThread.joinable()) g_testThread.join();
    g_testRunning = false;
}

void runTest(HWND dlg) {
    stopTest();
    readFromDialog(dlg);
    saveSettings(g_settings);
    wchar_t text[1024];
    GetDlgItemTextW(dlg, IDC_TESTTEXT, text, 1024);
    std::wstring wtext = text;
    if (wtext.empty()) wtext = L"Hello, this is the Lucent custom voice.";
    const std::wstring dir = engineDir();
    if (dir.empty()) {
        setStatus(dlg, L"Engine not found (is the wrapper installed?)");
        return;
    }
    if (!g_engine) g_engine = new LucentEngine(dir);
    g_stopRequested = false;
    g_testRunning = true;
    setStatus(dlg, L"Rendering...");
    Settings s = g_settings;
    g_testThread = std::thread([dlg, s, wtext] {
        VoiceRequest req;
        const SpeakerInfo* sp = findSpeaker(s.language, s.speaker);
        req.language = findLanguage(s.language);
        if (!req.language || !sp) {
            PostMessageW(dlg, WM_APP_STATUS, 0, reinterpret_cast<LPARAM>(L"Invalid language or voice."));
            g_testRunning = false;
            return;
        }
        req.female = sp->female;
        req.channelSuffix = sp->channelSuffix;
        req.sampleRate = req.language->has11k ? s.sampleRate : 8000;
        req.email = s.emailPreprocessing && req.language->hasEmail;
        req.topPitch = s.topPitchHz();
        req.referenceLine = s.referenceLineHz();
        req.bottomPitch = s.bottomPitchHz();
        if (req.bottomPitch > req.referenceLine) req.bottomPitch = req.referenceLine;
        if (req.topPitch < req.referenceLine) req.topPitch = req.referenceLine;
        req.frontVocalTract = s.frontScale();
        req.backVocalTract = s.backScale();
        req.speedFactor = s.speedFactor();
        req.volume = s.volumeScale();
        req.aspiration = s.aspiration();
        req.spectralTilt = s.tilt();
        UINT cp = req.language->codePage;
        if (req.language->engineIndex == LANG_ChineseMandarin && strcmp(sp->channelSuffix, "m") == 0) cp = 950;
        std::wstring escaped;
        for (wchar_t c : wtext) {
            if (c == L'\\') escaped += L"\\\\";
            else if (c < 0x20) escaped += L' ';
            else escaped += c;
        }
        const std::string text8 = toCodePage(escaped, cp);
        std::vector<uint8_t> pcm;
        SpeakSink sink;
        sink.onAudio = [&](const uint8_t* p, size_t n) { pcm.insert(pcm.end(), p, p + n); return !g_stopRequested; };
        sink.onBookmark = [](uint32_t, uint32_t) {};
        bool aborted = false;
        const ULONGLONG t0 = GetTickCount64();
        const bool ok = g_engine->speak(req, text8, sink, &aborted);
        const ULONGLONG ms = GetTickCount64() - t0;
        if (!ok) {
            PostMessageW(dlg, WM_APP_STATUS, 0, reinterpret_cast<LPARAM>(L"Engine failed - see the log folder."));
            g_testRunning = false;
            return;
        }
        if (g_stopRequested || pcm.empty()) {
            PostMessageW(dlg, WM_APP_STATUS, 0, reinterpret_cast<LPARAM>(L"Stopped."));
            g_testRunning = false;
            return;
        }
        // Build a WAV in memory and hand it to the UI thread to play.
        const uint32_t rate = static_cast<uint32_t>(req.sampleRate);
        const uint32_t dataLen = static_cast<uint32_t>(pcm.size());
        std::vector<uint8_t> wav(44 + pcm.size());
        auto put32 = [&](size_t off, uint32_t v) { memcpy(&wav[off], &v, 4); };
        auto put16 = [&](size_t off, uint16_t v) { memcpy(&wav[off], &v, 2); };
        memcpy(&wav[0], "RIFF", 4); put32(4, 36 + dataLen); memcpy(&wav[8], "WAVEfmt ", 8);
        put32(16, 16); put16(20, 1); put16(22, 1); put32(24, rate); put32(28, rate * 2); put16(32, 2); put16(34, 16);
        memcpy(&wav[36], "data", 4); put32(40, dataLen); memcpy(&wav[44], pcm.data(), pcm.size());
        g_wav.swap(wav);
        static wchar_t status[128];
        swprintf_s(status, L"Rendered %.1f s of audio in %llu ms.", pcm.size() / 2.0 / rate, ms);
        PostMessageW(dlg, WM_APP_PLAY, 0, reinterpret_cast<LPARAM>(status));
        g_testRunning = false;
    });
}

INT_PTR CALLBACK DialogProc(HWND dlg, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_INITDIALOG: {
        HICON icon = LoadIconW(g_instance, MAKEINTRESOURCEW(IDI_APP));
        if (icon) {
            SendMessageW(dlg, WM_SETICON, ICON_BIG, reinterpret_cast<LPARAM>(icon));
            SendMessageW(dlg, WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(icon));
        }
        loadSettings(g_settings);
        loadIntoDialog(dlg);
        SetDlgItemTextW(dlg, IDC_TESTTEXT, L"Hello, this is the Lucent custom voice speaking through SAPI 5.");
        setStatus(dlg, engineDir().empty() ? L"Engine not found: run the installer." : L"Ready. Changes are saved immediately.");
        return TRUE;
    }
    case WM_APP_STATUS:
        setStatus(dlg, reinterpret_cast<const wchar_t*>(lParam));
        return TRUE;
    case WM_APP_PLAY:
        setStatus(dlg, reinterpret_cast<const wchar_t*>(lParam));
        PlaySoundW(reinterpret_cast<LPCWSTR>(g_wav.data()), nullptr, SND_MEMORY | SND_ASYNC | SND_NODEFAULT);
        return TRUE;
    case WM_COMMAND: {
        const int id = LOWORD(wParam);
        const int code = HIWORD(wParam);
        if (g_loading) return FALSE;
        switch (id) {
        case IDCANCEL:
            stopTest();
            EndDialog(dlg, 0);
            return TRUE;
        case IDC_TEST:
            if (code == BN_CLICKED) runTest(dlg);
            return TRUE;
        case IDC_STOP:
            if (code == BN_CLICKED) { stopTest(); setStatus(dlg, L"Stopped."); }
            return TRUE;
        case IDC_DEFAULTS:
            if (code == BN_CLICKED) {
                Settings d;
                d.language = g_settings.language;
                d.speaker = g_settings.speaker;
                g_settings = d;
                const SpeakerInfo* sp = findSpeaker(g_settings.language, g_settings.speaker);
                if (sp) g_settings.applySpeakerDefaults(*sp);
                loadIntoDialog(dlg);
                saveNow(dlg);
                setStatus(dlg, L"Defaults restored and saved.");
            }
            return TRUE;
        case IDC_OPENLOGS:
            if (code == BN_CLICKED) {
                ShellExecuteW(dlg, L"open", Logger::defaultLogDirectory().c_str(), nullptr, nullptr, SW_SHOWNORMAL);
            }
            return TRUE;
        case IDC_LANGUAGE:
            if (code == CBN_SELCHANGE) {
                readFromDialog(dlg);
                g_loading = true;
                fillSpeakers(dlg);
                fillSampleRates(dlg);
                g_loading = false;
                applySpeakerDefaults(dlg);
                saveNow(dlg);
            }
            return TRUE;
        case IDC_SPEAKER:
            if (code == CBN_SELCHANGE) {
                readFromDialog(dlg);
                applySpeakerDefaults(dlg);
                saveNow(dlg);
            }
            return TRUE;
        case IDC_SAMPLERATE:
            if (code == CBN_SELCHANGE) saveNow(dlg);
            return TRUE;
        case IDC_EMAIL:
        case IDC_SAPIPROSODY:
        case IDC_LOGGING:
        case IDC_ENGINELOG:
            if (code == BN_CLICKED) saveNow(dlg);
            return TRUE;
        default:
            for (const SpinField& f : kFields) {
                if (id == f.edit && code == EN_CHANGE) {
                    saveNow(dlg);
                    return TRUE;
                }
            }
            break;
        }
        break;
    }
    case WM_CLOSE:
        stopTest();
        EndDialog(dlg, 0);
        return TRUE;
    }
    return FALSE;
}

}  // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, LPWSTR, int) {
    g_instance = instance;
    INITCOMMONCONTROLSEX icc = { sizeof(icc), ICC_UPDOWN_CLASS | ICC_STANDARD_CLASSES };
    InitCommonControlsEx(&icc);
    loadSettings(g_settings);
    Logger::instance().configure(g_settings.logging, L"config");
    LLOG("config: started");
    DialogBoxParamW(instance, MAKEINTRESOURCEW(IDD_MAIN), nullptr, DialogProc, 0);
    stopTest();
    delete g_engine;
    return 0;
}
