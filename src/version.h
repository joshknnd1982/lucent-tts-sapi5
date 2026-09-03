// Single source of truth for the version stamped into every binary we ship.
//
// Every module needs a complete VERSIONINFO block.  A freshly compiled,
// unsigned DLL with no version resource that registers itself as an in-process
// COM server is exactly the shape Microsoft Defender's machine-learning models
// score as suspicious, so the metadata below is not cosmetic.
#ifndef LUCENT_VERSION_H
#define LUCENT_VERSION_H

#define LUCENT_VER_MAJOR 1
#define LUCENT_VER_MINOR 0
#define LUCENT_VER_PATCH 2
#define LUCENT_VER_BUILD 0

#define LUCENT_VERSION_COMMA 1, 0, 2, 0
#define LUCENT_VERSION_STR   "1.0.2.0"

#define LUCENT_COMPANY   "Lucent TTS SAPI 5 wrapper project"
#define LUCENT_PRODUCT   "Lucent TTS SAPI 5"
#define LUCENT_COPYRIGHT "Open source wrapper; see LICENSE. Lucent Technologies text-to-speech engine is the property of its owners."
#define LUCENT_URL       "https://github.com/joshknnd1982/lucent-tts-sapi5"

#endif  // LUCENT_VERSION_H
