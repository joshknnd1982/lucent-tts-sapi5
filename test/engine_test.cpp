// Direct test of the Lucent engine client (no SAPI involved).
//
//   engine_test <engineDir> <outDir> [voice-filter]
//
// Renders a sample with every shipped speaker into <outDir>\<name>.wav, then checks
// bookmarks, cancellation, a long text and a text without speakable content.  Exit code
// 0 means every check passed.
#include <windows.h>
#include <cstdio>
#include <string>
#include <vector>
#include <thread>
#include "../src/lucent_engine.h"
#include "../src/lucent_log.h"

using namespace lucent;

static bool writeWav(const std::wstring& path, const std::vector<uint8_t>& pcm, int rate) {
    FILE* f = _wfopen(path.c_str(), L"wb");
    if (!f) return false;
    uint32_t dataLen = static_cast<uint32_t>(pcm.size());
    uint32_t riffLen = 36 + dataLen;
    uint16_t channels = 1, bits = 16, blockAlign = 2;
    uint32_t byteRate = rate * blockAlign;
    fwrite("RIFF", 1, 4, f); fwrite(&riffLen, 4, 1, f); fwrite("WAVEfmt ", 1, 8, f);
    uint32_t fmtLen = 16; uint16_t fmtTag = 1;
    fwrite(&fmtLen, 4, 1, f); fwrite(&fmtTag, 2, 1, f); fwrite(&channels, 2, 1, f);
    uint32_t r = rate; fwrite(&r, 4, 1, f); fwrite(&byteRate, 4, 1, f); fwrite(&blockAlign, 2, 1, f); fwrite(&bits, 2, 1, f);
    fwrite("data", 1, 4, f); fwrite(&dataLen, 4, 1, f); fwrite(pcm.data(), 1, pcm.size(), f);
    fclose(f);
    return true;
}

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

static const wchar_t* sampleText(const wchar_t* language) {
    if (!wcscmp(language, L"German")) return L"Guten Tag. Dies ist das Lucent Sprachsystem. Wie spät ist es? Es ist 15 Uhr 45.";
    if (!wcscmp(language, L"French")) return L"Bonjour. Ceci est le système de synthèse vocale Lucent. Quelle heure est-il ?";
    if (!wcscmp(language, L"FrenchCanadian")) return L"Bonjour. Ceci est la voix canadienne du système Lucent.";
    if (!wcscmp(language, L"Italian")) return L"Buongiorno. Questo è il sistema di sintesi vocale Lucent. Che ore sono?";
    if (!wcscmp(language, L"SpanishCastilian")) return L"Buenos días. Este es el sistema de síntesis de voz Lucent. ¿Qué hora es?";
    if (!wcscmp(language, L"SpanishMexican")) return L"Buenos días. Este es el sistema Lucent en español mexicano.";
    if (!wcscmp(language, L"ChineseMandarin")) return L"你好，这是朗讯文字转语音系统。";
    return L"Hello, this is the Lucent text to speech system. The quick brown fox jumps over the lazy dog.";
}

int wmain(int argc, wchar_t** argv) {
    if (argc < 3) {
        fwprintf(stderr, L"usage: engine_test <engineDir> <outDir> [voice-filter]\n");
        return 2;
    }
    Logger::instance().configure(true, L"enginetest");
    std::wstring engineDir = argv[1], outDir = argv[2];
    std::wstring filter = argc > 3 ? argv[3] : L"";
    CreateDirectoryW(outDir.c_str(), nullptr);
    LucentEngine engine(engineDir);
    if (!engine.ensureRunning()) {
        fwprintf(stderr, L"engine failed to start from %s\n", engineDir.c_str());
        return 1;
    }
    int failures = 0;
    size_t n = 0;
    const SpeakerInfo* sps = speakers(&n);
    for (size_t i = 0; i < n; ++i) {
        const SpeakerInfo& sp = sps[i];
        if (!filter.empty() && !wcsstr(sp.name, filter.c_str()) && !wcsstr(sp.language, filter.c_str())) continue;
        VoiceRequest req = requestFor(sp, 11025);
        UINT cp = req.language->codePage;
        if (!strcmp(sp.channelSuffix, "m") && req.language->engineIndex == LANG_ChineseMandarin) cp = 950;
        std::wstring wtext = sampleText(sp.language);
        if (cp == 950) wtext = L"你好，這是朗訊文字轉語音系統。";
        std::string text = toCodePage(wtext, cp);
        std::vector<uint8_t> pcm;
        SpeakSink sink;
        sink.onAudio = [&](const uint8_t* p, size_t len) { pcm.insert(pcm.end(), p, p + len); return true; };
        sink.onBookmark = [](uint32_t, uint32_t) {};
        ULONGLONG t0 = GetTickCount64();
        bool aborted = false;
        bool ok = engine.speak(req, text, sink, &aborted);
        ULONGLONG ms = GetTickCount64() - t0;
        std::wstring name = std::wstring(sp.name) + L"_" + sp.language;
        for (auto& c : name) if (c == L' ') c = L'_';
        wprintf(L"%-28s %s %7zu bytes (%.2f s audio) in %llu ms\n", name.c_str(), ok ? L"ok  " : L"FAIL", pcm.size(), pcm.size() / 2.0 / req.sampleRate, ms);
        if (!ok || pcm.size() < 8000) failures++;
        writeWav(outDir + L"\\" + name + L".wav", pcm, req.sampleRate);
    }

    // Bookmarks: three markers must arrive in order with increasing offsets.
    {
        VoiceRequest req = requestFor(sps[0], 11025);
        std::vector<std::pair<uint32_t, uint32_t>> marks;
        std::vector<uint8_t> pcm;
        SpeakSink sink;
        sink.onAudio = [&](const uint8_t* p, size_t len) { pcm.insert(pcm.end(), p, p + len); return true; };
        sink.onBookmark = [&](uint32_t id, uint32_t off) { marks.emplace_back(id, off); };
        bool aborted = false;
        bool ok = engine.speak(req, " \\Mrk=1\\ Hello world \\Mrk=2\\ this is a test. \\Mrk=3\\ Second sentence here.", sink, &aborted);
        bool good = ok && marks.size() == 3 && marks[0].first == 1 && marks[1].first == 2 && marks[2].first == 3 &&
                    marks[0].second < marks[1].second && marks[1].second < marks[2].second && marks[2].second < pcm.size();
        wprintf(L"bookmarks: %s (%zu marks", good ? L"ok" : L"FAIL", marks.size());
        for (auto& m : marks) wprintf(L" %u@%u", m.first, m.second);
        wprintf(L", %zu bytes)\n", pcm.size());
        if (!good) failures++;
    }

    // Cancellation: abort from the sink after the first packet and from another thread.
    {
        VoiceRequest req = requestFor(sps[0], 11025);
        std::string longText;
        for (int i = 0; i < 12; ++i) longText += "This is sentence number " + std::to_string(i + 1) + " of a fairly long paragraph used to test cancellation. ";
        size_t got = 0;
        SpeakSink sink;
        sink.onAudio = [&](const uint8_t*, size_t len) { got += len; return got < 40000; };
        sink.onBookmark = [](uint32_t, uint32_t) {};
        bool aborted = false;
        ULONGLONG t0 = GetTickCount64();
        bool ok = engine.speak(req, longText, sink, &aborted);
        wprintf(L"sink abort: %s (aborted=%d, %zu bytes, %llu ms)\n", (ok && aborted) ? L"ok" : L"FAIL", aborted ? 1 : 0, got, GetTickCount64() - t0);
        if (!(ok && aborted)) failures++;

        got = 0;
        SpeakSink sink2;
        sink2.onAudio = [&](const uint8_t*, size_t len) { got += len; Sleep(50); return true; };
        sink2.onBookmark = [](uint32_t, uint32_t) {};
        std::thread canceller([&] { Sleep(120); engine.cancel(); });
        t0 = GetTickCount64();
        ok = engine.speak(req, longText, sink2, &aborted);
        canceller.join();
        wprintf(L"thread cancel: %s (aborted=%d, %zu bytes, %llu ms)\n", (ok && aborted) ? L"ok" : L"FAIL", aborted ? 1 : 0, got, GetTickCount64() - t0);
        if (!(ok && aborted)) failures++;

        // The engine must still work after cancelling.
        std::vector<uint8_t> pcm;
        SpeakSink sink3;
        sink3.onAudio = [&](const uint8_t* p, size_t len) { pcm.insert(pcm.end(), p, p + len); return true; };
        sink3.onBookmark = [](uint32_t, uint32_t) {};
        ok = engine.speak(req, "Still working after cancel.", sink3, &aborted);
        wprintf(L"after cancel: %s (%zu bytes)\n", (ok && !aborted && pcm.size() > 8000) ? L"ok" : L"FAIL", pcm.size());
        if (!(ok && !aborted && pcm.size() > 8000)) failures++;
    }

    // Long text: make sure nothing is truncated (audio grows with text).
    {
        VoiceRequest req = requestFor(sps[0], 11025);
        std::string t1, t2;
        for (int i = 0; i < 20; ++i) t1 += "Sentence " + std::to_string(i) + " is here. ";
        for (int i = 0; i < 60; ++i) t2 += "Sentence " + std::to_string(i) + " is here. ";
        size_t n1 = 0, n2 = 0;
        SpeakSink s1; s1.onAudio = [&](const uint8_t*, size_t len) { n1 += len; return true; }; s1.onBookmark = [](uint32_t, uint32_t) {};
        SpeakSink s2; s2.onAudio = [&](const uint8_t*, size_t len) { n2 += len; return true; }; s2.onBookmark = [](uint32_t, uint32_t) {};
        bool aborted = false;
        bool ok = engine.speak(req, t1, s1, &aborted) && engine.speak(req, t2, s2, &aborted);
        bool good = ok && n2 > n1 * 2;
        wprintf(L"long text: %s (%zu chars -> %zu bytes, %zu chars -> %zu bytes)\n", good ? L"ok" : L"FAIL", t1.size(), n1, t2.size(), n2);
        if (!good) failures++;
    }

    // Unspeakable text must return promptly.
    {
        VoiceRequest req = requestFor(sps[0], 11025);
        size_t got = 0;
        SpeakSink sink; sink.onAudio = [&](const uint8_t*, size_t len) { got += len; return true; }; sink.onBookmark = [](uint32_t, uint32_t) {};
        bool aborted = false;
        ULONGLONG t0 = GetTickCount64();
        bool ok = engine.speak(req, "   ", sink, &aborted);
        ULONGLONG ms = GetTickCount64() - t0;
        wprintf(L"empty text: %s (%zu bytes, %llu ms)\n", (ok && ms < 5000) ? L"ok" : L"FAIL", got, ms);
        if (!(ok && ms < 5000)) failures++;
    }

    wprintf(L"%d failure(s)\n", failures);
    return failures ? 1 : 0;
}
