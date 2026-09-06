#pragma once
//
// SAPI voice list: token 0 is the "Lucent Custom Voice" whose parameters live in
// settings.ini; the rest are the named speakers shipped with the engine.
//
#include <cstddef>
#include <string>
#include <vector>
#include "utils.hpp"
#include "installed_voices.h"
#include "lucent_settings.h"

namespace Lucent {
namespace sapi {

constexpr int kCustomVoiceIndex = 0;

class voice_attributes
{
public:
    explicit voice_attributes(int voice_index = 0) noexcept
        : index_(voice_index)
    {
        size_t n = 0;
        lucent::speakers(&n);
        if (index_ < 0 || index_ > static_cast<int>(n)) {
            index_ = 0;
        }
    }

    [[nodiscard]] int get_index() const noexcept { return index_; }

    [[nodiscard]] bool is_custom() const noexcept { return index_ == kCustomVoiceIndex; }

    // Speaker behind a named voice (null for the custom voice).
    [[nodiscard]] const lucent::SpeakerInfo* speaker() const noexcept
    {
        if (is_custom()) return nullptr;
        size_t n = 0;
        const lucent::SpeakerInfo* sp = lucent::speakers(&n);
        return sp + (index_ - 1);
    }

    // Token id fragment, e.g. "LucentCustom" or "LucentJohnEnglishUS".
    [[nodiscard]] std::wstring get_token_name() const
    {
        if (is_custom()) return L"LucentCustom";
        const lucent::SpeakerInfo* sp = speaker();
        std::wstring s = L"Lucent";
        for (const wchar_t* p = sp->name; *p; ++p) if (*p != L' ') s += *p;
        s += sp->language;
        return s;
    }

    // Display name shown by SAPI applications.
    [[nodiscard]] std::wstring get_name() const
    {
        if (is_custom()) return L"Lucent Custom Voice";
        const lucent::SpeakerInfo* sp = speaker();
        const lucent::LanguageInfo* lang = lucent::findLanguage(sp->language);
        std::wstring s = L"Lucent ";
        s += sp->name;
        s += L" (";
        s += lang ? lang->display : sp->language;
        s += L")";
        return s;
    }

    [[nodiscard]] std::wstring get_age() const { return L"Adult"; }

    [[nodiscard]] std::wstring get_gender() const
    {
        if (is_custom()) {
            lucent::Settings s;
            lucent::loadSettings(s);
            const lucent::SpeakerInfo* sp = lucent::findSpeaker(s.language, s.speaker);
            return (sp && sp->female) ? L"Female" : L"Male";
        }
        return speaker()->female ? L"Female" : L"Male";
    }

    // SAPI language attribute: hexadecimal LANGID without a prefix.
    [[nodiscard]] std::wstring get_language() const
    {
        const lucent::LanguageInfo* lang = nullptr;
        if (is_custom()) {
            lucent::Settings s;
            lucent::loadSettings(s);
            lang = lucent::findLanguage(s.language);
        } else {
            lang = lucent::findLanguage(speaker()->language);
        }
        wchar_t buf[16];
        swprintf_s(buf, L"%X", lang ? lang->langId : 0x409);
        return buf;
    }

private:
    int index_;
};

// Every voice the engine knows about, installed or not.  A voice index stays the
// same on every machine, so it is safe to publish as the token's VoiceIndex.
inline int voice_count() noexcept
{
    size_t n = 0;
    lucent::speakers(&n);
    return static_cast<int>(n) + 1;
}

// The voice indices this installation actually has data for: the custom voice,
// then one per installed named speaker.  This is what SAPI is shown.
inline std::vector<int> installed_voice_indices()
{
    std::vector<int> indices;
    const std::vector<std::size_t>& speakers = lucent::installedSpeakers();
    indices.reserve(speakers.size() + 1);
    indices.push_back(kCustomVoiceIndex);
    for (std::size_t i : speakers) {
        indices.push_back(static_cast<int>(i) + 1);
    }
    return indices;
}

}
}
