#include "lucent_settings.h"
#include <cmath>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <algorithm>

namespace lucent {

namespace {

const LanguageInfo kLanguages[] = {
    //  key                 display               subdir    engine idx              langid  cp    11k    8k     email  female excl
    { L"EnglishUS",        L"US English",        "engusg", LANG_EnglishUS,        0x0409, 1252, true,  true,  true,  true,  false },
    { L"German",           L"German",            "deudes", LANG_German,           0x0407, 1252, true,  true,  true,  true,  false },
    { L"French",           L"French",            "frafrs", LANG_French,           0x040c, 1252, true,  true,  true,  true,  false },
    { L"FrenchCanadian",   L"Canadian French",   "fracaq", LANG_FrenchCanadian,   0x0c0c, 1252, true,  true,  true,  true,  true  },
    { L"Italian",          L"Italian",           "itaits", LANG_Italian,          0x0410, 1252, true,  true,  true,  true,  false },
    { L"SpanishCastilian", L"Castilian Spanish", "esless", LANG_SpanishCastilian, 0x0c0a, 1252, false, true,  true,  true,  false },
    { L"SpanishMexican",   L"Mexican Spanish",   "eslmxm", LANG_SpanishMexican,   0x080a, 1252, true,  true,  true,  true,  false },
    { L"ChineseMandarin",  L"Mandarin Chinese",  "chixxm", LANG_ChineseMandarin,  0x0804, 936,  true,  true,  false, false, false },
};

const SpeakerInfo kSpeakers[] = {
    //  name           language              female  top   ref   bottom front  back   suffix
    { L"John",        L"EnglishUS",         false,  149,  89,   65,   1.00f, 1.00f, "m" },
    { L"Grace",       L"EnglishUS",         true,   215,  145,  120,  1.00f, 1.00f, "f" },
    { L"Rainer",      L"German",            false,  142,  82,   70,   1.00f, 1.00f, "m" },
    { L"Monika",      L"German",            true,   250,  195,  155,  1.20f, 1.09f, "f" },
    { L"Pierre",      L"French",            false,  150,  110,  85,   1.00f, 1.00f, "m" },
    { L"Madeleine",   L"French",            true,   255,  200,  165,  1.20f, 1.05f, "f" },
    { L"Jacques",     L"FrenchCanadian",    false,  195,  120,  100,  1.00f, 1.00f, "m" },
    { L"Yvette",      L"FrenchCanadian",    true,   255,  195,  170,  1.20f, 1.09f, "f" },
    { L"Carlo",       L"Italian",           false,  161,  111,  75,   1.00f, 1.00f, "m" },
    { L"Giulia",      L"Italian",           true,   228,  185,  145,  1.20f, 1.06f, "f" },
    { L"Pedro",       L"SpanishCastilian",  false,  160,  110,  75,   1.00f, 1.00f, "m" },
    { L"Juanita",     L"SpanishCastilian",  true,   235,  186,  152,  1.20f, 1.08f, "f" },
    { L"Pablo",       L"SpanishMexican",    false,  150,  105,  78,   1.00f, 1.00f, "m" },
    { L"Carmen",      L"SpanishMexican",    true,   220,  185,  150,  1.10f, 1.07f, "f" },
    { L"Ming",        L"ChineseMandarin",   false,  142,  105,  80,   1.00f, 1.00f, "mg" },
    { L"Ming Big5",   L"ChineseMandarin",   false,  142,  105,  80,   1.00f, 1.00f, "m" },
};

std::wstring appDataDirectory() {
    wchar_t buf[MAX_PATH];
    DWORD n = GetEnvironmentVariableW(L"APPDATA", buf, MAX_PATH);
    std::wstring dir = (n > 0 && n < MAX_PATH) ? std::wstring(buf) : L"C:\\";
    dir += L"\\LucentSAPI";
    CreateDirectoryW(dir.c_str(), nullptr);
    return dir;
}

int readInt(const wchar_t* section, const wchar_t* key, int def, const std::wstring& path) {
    return static_cast<int>(GetPrivateProfileIntW(section, key, def, path.c_str()));
}

std::wstring readStr(const wchar_t* section, const wchar_t* key, const wchar_t* def, const std::wstring& path) {
    wchar_t buf[256];
    GetPrivateProfileStringW(section, key, def, buf, 256, path.c_str());
    return buf;
}

bool writeInt(const wchar_t* section, const wchar_t* key, int value, const std::wstring& path) {
    wchar_t buf[32];
    swprintf_s(buf, L"%d", value);
    return WritePrivateProfileStringW(section, key, buf, path.c_str()) != 0;
}

bool writeStr(const wchar_t* section, const wchar_t* key, const std::wstring& value, const std::wstring& path) {
    return WritePrivateProfileStringW(section, key, value.c_str(), path.c_str()) != 0;
}

int clampPercent(int v) { return (std::max)(0, (std::min)(100, v)); }

std::string narrow(const std::wstring& s) {
    if (s.empty()) return {};
    int n = WideCharToMultiByte(CP_ACP, 0, s.c_str(), static_cast<int>(s.size()), nullptr, 0, nullptr, nullptr);
    std::string out(static_cast<size_t>(n), '\0');
    WideCharToMultiByte(CP_ACP, 0, s.c_str(), static_cast<int>(s.size()), &out[0], n, nullptr, nullptr);
    return out;
}

std::string replaceAll(std::string s, const std::string& from, const std::string& to) {
    size_t pos = 0;
    while ((pos = s.find(from, pos)) != std::string::npos) {
        s.replace(pos, from.size(), to);
        pos += to.size();
    }
    return s;
}

}  // namespace

const Range kRangeSpeed        = { 3.0f, 0.4f, 1.0f };
const Range kRangePitchHz      = { 40.0f, 400.0f, 89.0f };
const Range kRangeBreathiness  = { 0.0f, 4.0f, 0.0f };
const Range kRangeSpectralTilt = { 0.0f, 10.0f, 0.0f };
const Range kRangeVocalTract   = { 0.5f, 1.5f, 1.0f };
const Range kRangeVolume       = { 0.0f, 1.0f, 1.0f };

float percentToValue(const Range& r, int percent) {
    const float p = clampPercent(percent) / 100.0f;
    if (&r == &kRangeSpeed) {
        // logarithmic so that 1.0 (normal) sits near the middle of the scale
        return r.min * std::pow(r.max / r.min, p);
    }
    return r.min + (r.max - r.min) * p;
}

int valueToPercent(const Range& r, float value) {
    float p;
    if (&r == &kRangeSpeed) {
        const float lo = (std::min)(r.min, r.max), hi = (std::max)(r.min, r.max);
        value = (std::max)(lo, (std::min)(hi, value));
        p = std::log(value / r.min) / std::log(r.max / r.min);
    } else {
        p = (value - r.min) / (r.max - r.min);
    }
    return clampPercent(static_cast<int>(std::lround(p * 100.0f)));
}

const LanguageInfo* languages(size_t* count) {
    if (count) *count = sizeof(kLanguages) / sizeof(kLanguages[0]);
    return kLanguages;
}

const SpeakerInfo* speakers(size_t* count) {
    if (count) *count = sizeof(kSpeakers) / sizeof(kSpeakers[0]);
    return kSpeakers;
}

const LanguageInfo* findLanguage(const std::wstring& key) {
    for (const auto& l : kLanguages) {
        if (_wcsicmp(l.key, key.c_str()) == 0) return &l;
    }
    return nullptr;
}

const SpeakerInfo* findSpeaker(const std::wstring& language, const std::wstring& name) {
    for (const auto& s : kSpeakers) {
        if (_wcsicmp(s.language, language.c_str()) == 0 && _wcsicmp(s.name, name.c_str()) == 0) return &s;
    }
    return nullptr;
}

void Settings::applySpeakerDefaults(const SpeakerInfo& sp) {
    topPitch = valueToPercent(kRangePitchHz, sp.topPitch);
    referenceLine = valueToPercent(kRangePitchHz, sp.referenceLine);
    bottomPitch = valueToPercent(kRangePitchHz, sp.bottomPitch);
    frontVocalTract = valueToPercent(kRangeVocalTract, sp.frontVocalTract);
    backVocalTract = valueToPercent(kRangeVocalTract, sp.backVocalTract);
}

std::wstring settingsDirectory() { return appDataDirectory(); }
std::wstring settingsPath() { return appDataDirectory() + L"\\settings.ini"; }

bool loadSettings(Settings& s) {
    const std::wstring path = settingsPath();
    const bool exists = GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES;
    Settings d;  // defaults
    s.language = readStr(L"voice", L"language", d.language.c_str(), path);
    s.speaker = readStr(L"voice", L"speaker", d.speaker.c_str(), path);
    s.sampleRate = readInt(L"voice", L"samplerate", d.sampleRate, path) == 8000 ? 8000 : 11025;
    s.emailPreprocessing = readInt(L"voice", L"email", d.emailPreprocessing ? 1 : 0, path) != 0;
    s.speed = clampPercent(readInt(L"prosody", L"speed", d.speed, path));
    s.topPitch = clampPercent(readInt(L"prosody", L"toppitch", d.topPitch, path));
    s.referenceLine = clampPercent(readInt(L"prosody", L"referenceline", d.referenceLine, path));
    s.bottomPitch = clampPercent(readInt(L"prosody", L"bottompitch", d.bottomPitch, path));
    s.breathiness = clampPercent(readInt(L"timbre", L"breathiness", d.breathiness, path));
    s.spectralTilt = clampPercent(readInt(L"timbre", L"spectraltilt", d.spectralTilt, path));
    s.frontVocalTract = clampPercent(readInt(L"timbre", L"frontvocaltract", d.frontVocalTract, path));
    s.backVocalTract = clampPercent(readInt(L"timbre", L"backvocaltract", d.backVocalTract, path));
    s.volume = clampPercent(readInt(L"audio", L"volume", d.volume, path));
    s.applySapiProsody = readInt(L"audio", L"applysapiprosody", d.applySapiProsody ? 1 : 0, path) != 0;
    s.logging = readInt(L"diagnostics", L"logging", d.logging ? 1 : 0, path) != 0;
    s.loggingEngine = readInt(L"diagnostics", L"enginelog", d.loggingEngine ? 1 : 0, path) != 0;
    if (!findLanguage(s.language)) s.language = d.language;
    if (!findSpeaker(s.language, s.speaker)) {
        // pick the first speaker of the language
        size_t n = 0;
        const SpeakerInfo* sp = speakers(&n);
        for (size_t i = 0; i < n; ++i) {
            if (_wcsicmp(sp[i].language, s.language.c_str()) == 0) { s.speaker = sp[i].name; break; }
        }
    }
    return exists;
}

bool saveSettings(const Settings& s) {
    const std::wstring path = settingsPath();
    bool ok = true;
    ok &= writeStr(L"voice", L"language", s.language, path);
    ok &= writeStr(L"voice", L"speaker", s.speaker, path);
    ok &= writeInt(L"voice", L"samplerate", s.sampleRate, path);
    ok &= writeInt(L"voice", L"email", s.emailPreprocessing ? 1 : 0, path);
    ok &= writeInt(L"prosody", L"speed", s.speed, path);
    ok &= writeInt(L"prosody", L"toppitch", s.topPitch, path);
    ok &= writeInt(L"prosody", L"referenceline", s.referenceLine, path);
    ok &= writeInt(L"prosody", L"bottompitch", s.bottomPitch, path);
    ok &= writeInt(L"timbre", L"breathiness", s.breathiness, path);
    ok &= writeInt(L"timbre", L"spectraltilt", s.spectralTilt, path);
    ok &= writeInt(L"timbre", L"frontvocaltract", s.frontVocalTract, path);
    ok &= writeInt(L"timbre", L"backvocaltract", s.backVocalTract, path);
    ok &= writeInt(L"audio", L"volume", s.volume, path);
    ok &= writeInt(L"audio", L"applysapiprosody", s.applySapiProsody ? 1 : 0, path);
    ok &= writeInt(L"diagnostics", L"logging", s.logging ? 1 : 0, path);
    ok &= writeInt(L"diagnostics", L"enginelog", s.loggingEngine ? 1 : 0, path);
    return ok;
}

FILETIME settingsFileTime() {
    FILETIME ft = {};
    WIN32_FILE_ATTRIBUTE_DATA fad;
    if (GetFileAttributesExW(settingsPath().c_str(), GetFileExInfoStandard, &fad)) {
        ft = fad.ftLastWriteTime;
    }
    return ft;
}

std::wstring installDirectory(HMODULE thisModule) {
    HKEY key;
    wchar_t buf[MAX_PATH];
    for (REGSAM view : { REGSAM(0), REGSAM(KEY_WOW64_64KEY), REGSAM(KEY_WOW64_32KEY) }) {
        if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"Software\\LucentSAPI", 0, KEY_READ | view, &key) == ERROR_SUCCESS) {
            DWORD size = sizeof(buf), type = 0;
            LONG r = RegQueryValueExW(key, L"InstallDir", nullptr, &type, reinterpret_cast<BYTE*>(buf), &size);
            RegCloseKey(key);
            if (r == ERROR_SUCCESS && type == REG_SZ && buf[0]) {
                std::wstring dir(buf);
                while (!dir.empty() && dir.back() == L'\\') dir.pop_back();
                if (GetFileAttributesW((dir + L"\\engine\\ttsserver.exe").c_str()) != INVALID_FILE_ATTRIBUTES) return dir;
            }
        }
    }
    // Fall back to the module's directory (or its parent when running from an x64 subfolder
    // or a build tree).
    if (GetModuleFileNameW(thisModule, buf, MAX_PATH)) {
        std::wstring dir(buf);
        size_t slash = dir.rfind(L'\\');
        if (slash != std::wstring::npos) dir.resize(slash);
        for (int up = 0; up < 5; ++up) {
            if (GetFileAttributesW((dir + L"\\engine\\ttsserver.exe").c_str()) != INVALID_FILE_ATTRIBUTES) return dir;
            // build tree: <repo>\bin\engine
            if (GetFileAttributesW((dir + L"\\bin\\engine\\ttsserver.exe").c_str()) != INVALID_FILE_ATTRIBUTES) return dir + L"\\bin";
            slash = dir.rfind(L'\\');
            if (slash == std::wstring::npos) break;
            dir.resize(slash);
        }
    }
    return L"";
}

std::wstring engineDirectory(HMODULE thisModule) {
    std::wstring dir = installDirectory(thisModule);
    return dir.empty() ? dir : dir + L"\\engine";
}

std::string buildChannelFile(const std::wstring& engineDir, const VoiceRequest& req, std::string* templateName) {
    if (!req.language) return {};
    const LanguageInfo& lang = *req.language;
    std::string subdir = lang.subdir;
    // Pick the closest shipped template.
    bool want11k = req.sampleRate != 8000 && lang.has11k;
    bool female = req.female && lang.hasFemale;
    bool email = req.email && lang.hasEmail;
    std::string suffix = req.channelSuffix ? req.channelSuffix : (female ? "f" : "m");
    if (female && suffix == "m") suffix = "f";
    std::string name = "x" + subdir + "." + (want11k ? "11" : "08") + suffix + (email ? "e" : "") + ".chn";
    std::wstring wname(name.begin(), name.end());
    std::wstring path = engineDir + L"\\data\\chfiles\\" + wname;
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        // Mandarin has no female inventory and no email variant; degrade gracefully.
        suffix = (suffix == "mg") ? "mg" : "m";
        name = "x" + subdir + "." + (want11k ? "11" : "08") + suffix + ".chn";
        wname.assign(name.begin(), name.end());
        path = engineDir + L"\\data\\chfiles\\" + wname;
        in.open(path, std::ios::binary);
        if (!in) return {};
    }
    if (templateName) *templateName = name;
    // The engine's module option parser splits on whitespace and cannot take a quoted
    // path, so an absolute install path such as "C:\Program Files\..." breaks every
    // module.  The engine sets its working directory to the folder of ttsserver.exe at
    // startup, so paths relative to that folder work for any install location.
    std::string target = "data/languages/" + subdir;
    std::string common = "data/languages/common";
    std::stringstream out;
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty()) continue;
        line = replaceAll(line, "${LTTS_DATADIR_common}/${LTTS_MODULE_SUBDIR}", common);
        line = replaceAll(line, "${LTTS_TARGET_DIR}", target);
        line = replaceAll(line, "${LTTS_ENDIANNESS}", "l");
        // The intonation module keys its databank by joining -directory and -parameters;
        // an absolute parameters path breaks that join on modern Windows, so refer to the
        // copy shipped next to ttsserver.exe by bare name.
        size_t p = line.find("-parameters ");
        if (p != std::string::npos) {
            size_t start = p + 12;
            size_t end = line.find(' ', start);
            std::string arg = line.substr(start, end == std::string::npos ? std::string::npos : end - start);
            size_t slash = arg.find_last_of("/\\");
            if (slash != std::string::npos) {
                line.replace(start, arg.size(), arg.substr(slash + 1));
            }
        }
        if (line.compare(0, 8, "wavesynt") == 0) {
            char extra[128];
            extra[0] = '\0';
            if (req.aspiration > 0.0f || req.spectralTilt > 0.0f) {
                snprintf(extra, sizeof(extra), " -aspirampl %.3f -spectilt %.3f", req.aspiration, req.spectralTilt);
            }
            line += extra;
        }
        out << line << "\n";
    }
    return out.str();
}

}  // namespace lucent
