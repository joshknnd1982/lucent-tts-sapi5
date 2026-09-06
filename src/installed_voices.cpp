#include "installed_voices.h"
#include "lucent_settings.h"

#include <windows.h>

namespace lucent {

namespace {

// The manifest key for a voice: language key, a dot, and the speaker name with
// its spaces removed - "ChineseMandarin.MingBig5".  Setup writes exactly this.
std::wstring voiceKey(const SpeakerInfo& sp) {
    std::wstring key = sp.language;
    key += L'.';
    for (const wchar_t* p = sp.name; *p; ++p) {
        if (*p != L' ') key += *p;
    }
    return key;
}

// This module, whether we are the SAPI DLL loaded into someone else's process
// or the configuration utility.  GetModuleHandleW(nullptr) would give the host
// application, whose directory has nothing to do with ours.
HMODULE thisModule() {
    HMODULE h = nullptr;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       reinterpret_cast<LPCWSTR>(&thisModule), &h);
    return h;
}

struct Manifest {
    bool found = false;
    std::vector<bool> languages;
    std::vector<bool> speakers;
    std::vector<std::size_t> languageList;
    std::vector<std::size_t> speakerList;
};

Manifest loadManifest() {
    Manifest m;
    size_t nl = 0, ns = 0;
    const LanguageInfo* langs = lucent::languages(&nl);
    const SpeakerInfo* spks = lucent::speakers(&ns);
    m.languages.assign(nl, true);
    m.speakers.assign(ns, true);

    const std::wstring dir = installDirectory(thisModule());
    const std::wstring path = dir.empty() ? std::wstring() : dir + L"\\voices.ini";
    if (!path.empty() && GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES) {
        m.found = true;
        for (size_t i = 0; i < nl; ++i) {
            m.languages[i] = GetPrivateProfileIntW(L"languages", langs[i].key, 0, path.c_str()) != 0;
        }
        for (size_t i = 0; i < ns; ++i) {
            m.speakers[i] = GetPrivateProfileIntW(L"voices", voiceKey(spks[i]).c_str(), 0, path.c_str()) != 0;
        }
        // A voice whose language was dropped is not usable, however the file reads.
        for (size_t i = 0; i < ns; ++i) {
            const LanguageInfo* lang = findLanguage(spks[i].language);
            if (!lang || !m.languages[static_cast<size_t>(lang - langs)]) m.speakers[i] = false;
        }
    }

    for (size_t i = 0; i < nl; ++i) {
        if (m.languages[i]) m.languageList.push_back(i);
    }
    for (size_t i = 0; i < ns; ++i) {
        if (m.speakers[i]) m.speakerList.push_back(i);
    }

    // A manifest that leaves nothing would hide the engine completely, and the
    // user would have a silent installation with no way to see why.  Report
    // everything instead; the missing data files then fail one voice at a time.
    if (m.languageList.empty() || m.speakerList.empty()) {
        m.languages.assign(nl, true);
        m.speakers.assign(ns, true);
        m.languageList.clear();
        m.speakerList.clear();
        for (size_t i = 0; i < nl; ++i) m.languageList.push_back(i);
        for (size_t i = 0; i < ns; ++i) m.speakerList.push_back(i);
    }
    return m;
}

const Manifest& manifest() {
    static const Manifest m = loadManifest();
    return m;
}

}

bool isLanguageInstalled(const std::wstring& key) {
    size_t n = 0;
    const LanguageInfo* langs = languages(&n);
    const LanguageInfo* lang = findLanguage(key);
    if (!lang) return false;
    return manifest().languages[static_cast<size_t>(lang - langs)];
}

bool isSpeakerInstalled(std::size_t speakerIndex) {
    const Manifest& m = manifest();
    return speakerIndex < m.speakers.size() && m.speakers[speakerIndex];
}

const std::vector<std::size_t>& installedLanguages() { return manifest().languageList; }

const std::vector<std::size_t>& installedSpeakers() { return manifest().speakerList; }

bool haveVoiceManifest() { return manifest().found; }

}
