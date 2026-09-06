#pragma once
//
// Which languages and which named voices setup actually put on this machine.
//
// The wizard lets an installation be anything from all eight languages down to
// a single voice, so the voice tables in lucent_settings.cpp describe what the
// engine *can* do rather than what is present.  Setup records the choice in
// voices.ini next to LucentSAPI.dll; everything reads it from here so the
// Windows voice list, the configuration utility and the settings fallback only
// ever offer combinations whose data files exist.
//
// With no voices.ini - a build tree, or an installation made by 1.0.x - every
// language and voice is reported as installed, which is the previous behaviour.
//
#include <cstddef>
#include <string>
#include <vector>

namespace lucent {

// key is LanguageInfo::key, e.g. L"EnglishUS".
bool isLanguageInstalled(const std::wstring& key);

// speakerIndex indexes speakers().
bool isSpeakerInstalled(std::size_t speakerIndex);

// Indices into languages() / speakers(), in table order, of what is installed.
const std::vector<std::size_t>& installedLanguages();
const std::vector<std::size_t>& installedSpeakers();

// False when no manifest was found and everything is being reported.
bool haveVoiceManifest();

}
