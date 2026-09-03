// Speaks arbitrary lines of text with one voice and reports how many PCM bytes each
// produced.  Built to investigate inputs that make a front end emit nothing at all: the
// engine reports a clean end-of-stream after zero bytes, so a byte-count check is the
// only way to see the failure.
//
//   text_test <engineDir> <language> <speaker> <textFile> [outDir]
//
// <textFile> is UTF-8, one utterance per line; blank lines and lines starting with '#'
// are skipped.  Exit code is the number of lines that produced no audio.
#include <windows.h>
#include <cstdio>
#include <string>
#include <vector>
#include <fstream>
#include "lucent_engine.h"
#include "lucent_settings.h"
#include "lucent_log.h"

using namespace lucent;

static std::wstring fromUtf8(const std::string& s) {
    if (s.empty()) return L"";
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
    std::wstring w(n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), &w[0], n);
    return w;
}

// Same mapping engine_test uses: a speaker's defaults into an engine request.
static VoiceRequest requestFor(const SpeakerInfo& sp, int rate) {
    VoiceRequest req;
    req.language = findLanguage(sp.language);
    req.female = sp.female;
    req.channelSuffix = sp.channelSuffix;
    req.sampleRate = (req.language && !req.language->has11k) ? 8000 : rate;
    req.topPitch = sp.topPitch;
    req.referenceLine = sp.referenceLine;
    req.bottomPitch = sp.bottomPitch;
    req.frontVocalTract = sp.frontVocalTract;
    req.backVocalTract = sp.backVocalTract;
    return req;
}

static void writeWav(const std::wstring& path, const std::vector<uint8_t>& pcm, int rate) {
    FILE* f = _wfopen(path.c_str(), L"wb");
    if (!f) return;
    const uint32_t dataLen = static_cast<uint32_t>(pcm.size());
    const uint32_t riffLen = 36 + dataLen;
    const uint32_t byteRate = static_cast<uint32_t>(rate) * 2;
    const uint16_t one = 1, two = 2, sixteen = 16;
    const uint32_t fmtLen = 16;
    fwrite("RIFF", 1, 4, f); fwrite(&riffLen, 4, 1, f); fwrite("WAVEfmt ", 1, 8, f);
    fwrite(&fmtLen, 4, 1, f); fwrite(&one, 2, 1, f); fwrite(&one, 2, 1, f);
    fwrite(&rate, 4, 1, f); fwrite(&byteRate, 4, 1, f); fwrite(&two, 2, 1, f); fwrite(&sixteen, 2, 1, f);
    fwrite("data", 1, 4, f); fwrite(&dataLen, 4, 1, f);
    if (dataLen) fwrite(pcm.data(), 1, dataLen, f);
    fclose(f);
}

int wmain(int argc, wchar_t** argv) {
    if (argc < 5) {
        fwprintf(stderr, L"usage: text_test <engineDir> <language> <speaker> <textFile> [outDir]\n");
        fwprintf(stderr, L"  e.g. text_test bin\\engine French Pierre test\\french_corpus.txt out\n");
        return 2;
    }
    const std::wstring engineDir = argv[1];
    const std::wstring language = argv[2];
    const std::wstring speakerName = argv[3];
    const std::wstring textFile = argv[4];
    const std::wstring outDir = argc > 5 ? argv[5] : L"";

    Logger::instance().configure(true, L"texttest");

    const SpeakerInfo* sp = findSpeaker(language, speakerName);
    if (!sp) {
        fwprintf(stderr, L"unknown voice %s / %s\n", language.c_str(), speakerName.c_str());
        return 2;
    }
    if (!outDir.empty()) CreateDirectoryW(outDir.c_str(), nullptr);

    std::ifstream in(textFile);
    if (!in) {
        fwprintf(stderr, L"cannot open %s\n", textFile.c_str());
        return 2;
    }

    LucentEngine engine(engineDir);
    if (!engine.ensureRunning()) {
        fwprintf(stderr, L"engine failed to start from %s\n", engineDir.c_str());
        return 1;
    }

    VoiceRequest req = requestFor(*sp, 11025);
    const UINT cp = req.language->codePage;

    int silent = 0, spoken = 0, line = 0;
    std::string raw;
    while (std::getline(in, raw)) {
        ++line;
        if (!raw.empty() && raw.back() == '\r') raw.pop_back();
        if (raw.empty() || raw[0] == '#') continue;

        const std::wstring wtext = fromUtf8(raw);
        const std::string text = toCodePage(wtext, cp);

        std::vector<uint8_t> pcm;
        std::vector<std::pair<uint32_t, uint32_t>> marks;
        SpeakSink sink;
        sink.onAudio = [&](const uint8_t* p, size_t len) { pcm.insert(pcm.end(), p, p + len); return true; };
        sink.onBookmark = [&](uint32_t id, uint32_t offset) { marks.emplace_back(id, offset); };
        bool aborted = false;
        const bool ok = engine.speak(req, text, sink, &aborted);

        const bool quiet = pcm.empty();
        if (quiet) ++silent; else ++spoken;
        wprintf(L"%-4d %-8s %7zu bytes  %s\n", line, ok ? L"ok" : L"FAIL", pcm.size(),
                quiet ? L"*** NO AUDIO ***  " : L"                  ");
        wprintf(L"       %s\n", wtext.c_str());

        // Bookmark offsets must climb with the audio and stay inside it. A retry that
        // re-spoke the utterance piecewise would otherwise restart them at zero.
        if (!marks.empty()) {
            wprintf(L"       marks:");
            bool bad = false;
            uint32_t prev = 0;
            for (size_t m = 0; m < marks.size(); ++m) {
                wprintf(L" %u@%u", marks[m].first, marks[m].second);
                if (marks[m].second < prev || marks[m].second > pcm.size()) bad = true;
                prev = marks[m].second;
            }
            if (bad) {
                wprintf(L"  *** OUT OF ORDER / PAST END OF AUDIO ***");
                ++silent;   // count as a failure so the exit code reflects it
            }
            wprintf(L"\n");
        }

        if (!outDir.empty() && !pcm.empty()) {
            wchar_t name[64];
            swprintf(name, 64, L"\\line%03d.wav", line);
            writeWav(outDir + name, pcm, req.sampleRate);
        }
    }

    wprintf(L"\n%d spoken, %d silent\n", spoken, silent);
    return silent;
}
