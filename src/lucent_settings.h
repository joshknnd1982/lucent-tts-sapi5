#pragma once
//
// Voice table, user settings (settings.ini) and the parameter mappings shared by the
// SAPI engine DLL, the configuration utility and the test tools.
//
#include <windows.h>
#include <string>
#include <vector>
#include "lucent_protocol.h"

namespace lucent {

// One Lucent language as shipped in data\languages\<subdir>.
struct LanguageInfo {
    const wchar_t* key;        // settings value, e.g. L"EnglishUS"
    const wchar_t* display;    // e.g. L"US English"
    const char*    subdir;     // e.g. "engusg"
    uint16_t       engineIndex;// LanguageIndex
    WORD           langId;     // Windows LANGID for the SAPI token
    UINT           codePage;   // code page the engine expects for text
    bool           has11k;     // 11025 Hz inventory available
    bool           has8k;      // 8000 Hz inventory available
    bool           hasEmail;   // email preprocessor channel files exist
    bool           hasFemale;  // a female channel file exists
    bool           exclusive;  // front end cannot host two channels in one process (Canadian French)
};

// One named speaker (voice) of a language, with the pitch/vocal tract defaults from
// Lucent's own registry files.
struct SpeakerInfo {
    const wchar_t* name;       // e.g. L"John"
    const wchar_t* language;   // LanguageInfo::key
    bool           female;
    float          topPitch;
    float          referenceLine;
    float          bottomPitch;
    float          frontVocalTract;
    float          backVocalTract;
    const char*    channelSuffix; // "m", "f" or "mg" (Mandarin GB code page)
};

const LanguageInfo* languages(size_t* count);
const SpeakerInfo* speakers(size_t* count);
const LanguageInfo* findLanguage(const std::wstring& key);
const SpeakerInfo* findSpeaker(const std::wstring& language, const std::wstring& name);

// Parameter ranges.  The configuration utility exposes every value as 0..100 percent;
// percentToValue()/valueToPercent() map that onto the engine's real range.
struct Range {
    float min, max, def;
};
extern const Range kRangeSpeed;          // duration multiplier, 3.0 (slowest) .. 0.4 (fastest); note min > max
extern const Range kRangePitchHz;        // 40 .. 400 Hz
extern const Range kRangeBreathiness;    // wavesynt -aspirampl 0 .. 4
extern const Range kRangeSpectralTilt;   // wavesynt -spectilt 0 .. 10
extern const Range kRangeVocalTract;     // 0.5 .. 1.5
extern const Range kRangeVolume;         // 0 .. 1

float percentToValue(const Range& r, int percent);
int valueToPercent(const Range& r, float value);

// User settings for the "Lucent Custom Voice", stored in %APPDATA%\LucentSAPI\settings.ini.
struct Settings {
    std::wstring language = L"EnglishUS";
    std::wstring speaker = L"John";
    int sampleRate = 11025;      // 11025 or 8000
    bool emailPreprocessing = false;
    int speed = 55;              // percent: 0 slowest .. 100 fastest (55 = engine default 1.0 on the log scale)
    int topPitch = 30;           // percent of 40..400 Hz; filled from the speaker defaults on Restore
    int referenceLine = 14;
    int bottomPitch = 7;
    int breathiness = 0;
    int spectralTilt = 0;
    int frontVocalTract = 50;
    int backVocalTract = 50;
    int volume = 100;
    bool applySapiProsody = false; // let SAPI rate/pitch/volume modify the custom voice
    bool logging = true;
    bool loggingEngine = false;    // keep the engine's stderr log as well

    // Resolved engine values.
    float speedFactor() const { return percentToValue(kRangeSpeed, speed); }
    float topPitchHz() const { return percentToValue(kRangePitchHz, topPitch); }
    float referenceLineHz() const { return percentToValue(kRangePitchHz, referenceLine); }
    float bottomPitchHz() const { return percentToValue(kRangePitchHz, bottomPitch); }
    float aspiration() const { return percentToValue(kRangeBreathiness, breathiness); }
    float tilt() const { return percentToValue(kRangeSpectralTilt, spectralTilt); }
    float frontScale() const { return percentToValue(kRangeVocalTract, frontVocalTract); }
    float backScale() const { return percentToValue(kRangeVocalTract, backVocalTract); }
    float volumeScale() const { return percentToValue(kRangeVolume, volume); }

    void applySpeakerDefaults(const SpeakerInfo& sp);
};

std::wstring settingsDirectory();          // %APPDATA%\LucentSAPI (created)
std::wstring settingsPath();               // ...\settings.ini
bool loadSettings(Settings& s);            // returns false when the file did not exist (defaults kept)
bool saveSettings(const Settings& s);
FILETIME settingsFileTime();               // zero when missing

// Installation layout helpers.  The engine lives in <install>\engine\ttsserver.exe with
// its data in <install>\engine\data\{languages,chfiles}.  The install directory is taken
// from the registry (HKLM\Software\LucentSAPI\InstallDir) or, failing that, from the
// directory of the calling module.
std::wstring installDirectory(HMODULE thisModule);
std::wstring engineDirectory(HMODULE thisModule);

// Voice request: everything the engine needs to render one utterance.
struct VoiceRequest {
    const LanguageInfo* language = nullptr;
    bool female = false;
    int sampleRate = 11025;
    bool email = false;
    float topPitch = 149, referenceLine = 89, bottomPitch = 65;
    float frontVocalTract = 1.0f, backVocalTract = 1.0f;
    float speedFactor = 1.0f;     // duration multiplier
    float volume = 1.0f;          // 0..2
    float aspiration = 0.0f;      // wavesynt -aspirampl
    float spectralTilt = 0.0f;    // wavesynt -spectilt
    const char* channelSuffix = "m";
};

// Builds the channel file text for a request from the shipped template
// <engineDir>\data\chfiles\x<lang>.<08|11><m|f>[e].chn, substituting absolute paths and
// appending the wavesynt options.  Returns an empty string when the template is missing.
std::string buildChannelFile(const std::wstring& engineDir, const VoiceRequest& req, std::string* templateName);

}  // namespace lucent
