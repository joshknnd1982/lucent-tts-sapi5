#include "lucent_textfix.h"

#include <cctype>
#include <cstring>

namespace lucent {
namespace {

// The engine text uses '\' to delimit control tags (" \Mrk=7\ "), and a literal backslash
// from the user is escaped as "\\". Everything in this file walks the text with this
// helper so a repair never corrupts a tag.
//
// Given the index of a '\', reports whether it opens a tag and where the run ends.
// Returns the index just past the tag (or past the escaped backslash).
size_t skipTagOrEscape(const std::string& s, size_t i, bool* wasTag) {
    *wasTag = false;
    if (i >= s.size() || s[i] != '\\') return i;
    if (i + 1 < s.size() && s[i + 1] == '\\') return i + 2;   // escaped backslash: content
    *wasTag = true;
    size_t j = i + 1;
    while (j < s.size() && s[j] != '\\') ++j;
    return j < s.size() ? j + 1 : s.size();                   // unterminated tag: consume rest
}

bool isSentenceEnd(char c) { return c == '.' || c == '!' || c == '?'; }

// Rejected wherever they appear, across the languages measured. Dropping them loses the
// symbol but keeps the utterance, which is the right trade once the engine has already
// refused the text.
bool isAlwaysRejected(unsigned char c) {
    return c == '%' || c == '^' || c == '|' || c == '`' || c == '~';
}

// Pass 2 keeps only what every front end handled in the sweep.
bool isSafeInPass2(unsigned char c) {
    return std::isalnum(c) || std::isspace(c) || c == '\'' || c == '-' ||
           c == ',' || c == ';' || c == ':' || isSentenceEnd(static_cast<char>(c));
}

// Appends '.' unless the text already ends in sentence punctuation. A doubled terminator
// ("un deux ..") is itself rejected, hence the check rather than an unconditional append.
void terminate(std::string& s) {
    size_t last = std::string::npos;
    for (size_t i = 0; i < s.size();) {
        if (s[i] == '\\') {
            bool wasTag = false;
            const size_t next = skipTagOrEscape(s, i, &wasTag);
            if (!wasTag) last = i;                            // escaped backslash is content
            i = next;
            continue;
        }
        if (!std::isspace(static_cast<unsigned char>(s[i]))) last = i;
        ++i;
    }
    if (last == std::string::npos) return;                    // tags and whitespace only
    if (isSentenceEnd(s[last])) return;
    s.insert(last + 1, ".");
}

}  // namespace

bool hasSpeakableContent(const std::string& engineText) {
    for (size_t i = 0; i < engineText.size();) {
        if (engineText[i] == '\\') {
            bool wasTag = false;
            i = skipTagOrEscape(engineText, i, &wasTag);
            continue;
        }
        if (std::isalnum(static_cast<unsigned char>(engineText[i]))) return true;
        ++i;
    }
    return false;
}

std::string repairText(const std::string& engineText, int pass, unsigned int codePage) {
    std::string out;
    out.reserve(engineText.size() + 2);

    // Under a DBCS code page a trail byte can look like an ASCII letter or symbol, so
    // per-byte rewriting is unsafe; terminate the utterance and change nothing else.
    const bool doubleByte = codePage == 932 || codePage == 936 || codePage == 949 || codePage == 950;

    for (size_t i = 0; i < engineText.size();) {
        if (engineText[i] == '\\') {
            bool wasTag = false;
            const size_t next = skipTagOrEscape(engineText, i, &wasTag);
            out.append(engineText, i, next - i);              // tags pass through verbatim
            i = next;
            continue;
        }
        if (doubleByte) {
            out += engineText[i++];
            continue;
        }
        const unsigned char c = static_cast<unsigned char>(engineText[i]);
        if (pass >= 2 ? !isSafeInPass2(c) : isAlwaysRejected(c)) {
            out += ' ';
        } else if (pass >= 3 && c >= 'A' && c <= 'Z') {
            out += static_cast<char>(c - 'A' + 'a');
        } else {
            out += engineText[i];
        }
        ++i;
    }

    terminate(out);
    return out == engineText ? std::string() : out;
}

std::vector<std::string> splitIntoWords(const std::string& engineText) {
    std::vector<std::string> words;
    std::string current;
    bool currentHasWord = false;

    auto flush = [&]() {
        if (currentHasWord) {
            terminate(current);
            words.push_back(current);
        } else if (!current.empty() && !words.empty()) {
            // Trailing tags with no word of their own belong to the previous piece, so
            // their bookmarks still fire.
            words.back().insert(words.back().size(), current);
        }
        current.clear();
        currentHasWord = false;
    };

    for (size_t i = 0; i < engineText.size();) {
        if (engineText[i] == '\\') {
            bool wasTag = false;
            const size_t next = skipTagOrEscape(engineText, i, &wasTag);
            if (wasTag && currentHasWord) flush();             // a tag starts the next piece
            current.append(engineText, i, next - i);
            if (!wasTag) currentHasWord = true;
            i = next;
            continue;
        }
        if (std::isspace(static_cast<unsigned char>(engineText[i]))) {
            if (currentHasWord) flush();
            else current += ' ';
            ++i;
            continue;
        }
        current += engineText[i];
        if (std::isalnum(static_cast<unsigned char>(engineText[i]))) currentHasWord = true;
        ++i;
    }
    flush();
    return words;
}

}  // namespace lucent
