#include "lucent_engine.h"
#include "lucent_log.h"
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>

namespace lucent {

namespace {

constexpr DWORD kOpenTimeoutMs = 15000;
constexpr DWORD kSpeakIdleTimeoutMs = 6000;    // no packet at all for this long -> transport dead (engine renders ~300x real time)

std::string toUtf8(const std::wstring& s) {
    if (s.empty()) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()), nullptr, 0, nullptr, nullptr);
    std::string out(static_cast<size_t>(n), '\0');
    WideCharToMultiByte(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()), &out[0], n, nullptr, nullptr);
    return out;
}

uint32_t fnv1a(const std::string& s) {
    uint32_t h = 2166136261u;
    for (unsigned char c : s) {
        h ^= c;
        h *= 16777619u;
    }
    return h;
}

}  // namespace

std::string toCodePage(const std::wstring& s, UINT codePage) {
    if (s.empty()) return {};
    int n = WideCharToMultiByte(codePage, 0, s.c_str(), static_cast<int>(s.size()), nullptr, 0, nullptr, nullptr);
    if (n <= 0) return {};
    std::string out(static_cast<size_t>(n), '\0');
    WideCharToMultiByte(codePage, 0, s.c_str(), static_cast<int>(s.size()), &out[0], n, nullptr, nullptr);
    return out;
}

LucentEngine::LucentEngine(const std::wstring& engineDir)
    : engineDir_(engineDir)
{
    InitializeCriticalSection(&cs_);
    InitializeCriticalSection(&writeCs_);
    packetEvent_ = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    channelDir_ = Logger::defaultLogDirectory();
    size_t slash = channelDir_.rfind(L'\\');
    if (slash != std::wstring::npos) channelDir_.resize(slash);
    channelDir_ += L"\\channels";
    CreateDirectoryW(channelDir_.c_str(), nullptr);
}

LucentEngine::~LucentEngine() {
    shutdown();
    if (packetEvent_) CloseHandle(packetEvent_);
    DeleteCriticalSection(&writeCs_);
    DeleteCriticalSection(&cs_);
}

bool LucentEngine::isRunning() {
    if (!pi_.hProcess) return false;
    DWORD code = 0;
    if (!GetExitCodeProcess(pi_.hProcess, &code) || code != STILL_ACTIVE) return false;
    return !readerFailed_;
}

void LucentEngine::closeHandles() {
    if (stdinWrite_ != INVALID_HANDLE_VALUE) { CloseHandle(stdinWrite_); stdinWrite_ = INVALID_HANDLE_VALUE; }
    if (pi_.hProcess) {
        // Make sure the child is gone so the reader thread's ReadFile returns.
        if (WaitForSingleObject(pi_.hProcess, detaching_ ? 0 : 1500) != WAIT_OBJECT_0) {
            TerminateProcess(pi_.hProcess, 0);
            WaitForSingleObject(pi_.hProcess, detaching_ ? 200 : 2000);
        }
    }
    if (readerThread_) {
        // Never block on our own thread while DllMain holds the loader lock.
        if (!detaching_) WaitForSingleObject(readerThread_, 3000);
        CloseHandle(readerThread_);
        readerThread_ = nullptr;
    }
    if (stdoutRead_ != INVALID_HANDLE_VALUE) { CloseHandle(stdoutRead_); stdoutRead_ = INVALID_HANDLE_VALUE; }
    if (stderrFile_ != INVALID_HANDLE_VALUE) { CloseHandle(stderrFile_); stderrFile_ = INVALID_HANDLE_VALUE; }
    if (pi_.hProcess) {
        CloseHandle(pi_.hProcess);
        pi_.hProcess = nullptr;
    }
    if (pi_.hThread) {
        CloseHandle(pi_.hThread);
        pi_.hThread = nullptr;
    }
    pi_ = PROCESS_INFORMATION{};
    EnterCriticalSection(&cs_);
    queue_.clear();
    channels_.clear();
    activeChannel_ = 0;
    LeaveCriticalSection(&cs_);
}

void LucentEngine::shutdown() {
    if (pi_.hProcess) {
        LLOG("engine: shutting down child pid %lu", pi_.dwProcessId);
        // Closing stdin makes the engine's pipe reader exit; closeHandles() kills it as a
        // fallback and (outside DLL detach) waits for our reader thread.
        if (stdinWrite_ != INVALID_HANDLE_VALUE) { CloseHandle(stdinWrite_); stdinWrite_ = INVALID_HANDLE_VALUE; }
    }
    closeHandles();
}

bool LucentEngine::launch() {
    closeHandles();
    readerFailed_ = false;
    nextPacketId_ = 1;

    const std::wstring exe = engineDir_ + L"\\ttsserver.exe";
    if (GetFileAttributesW(exe.c_str()) == INVALID_FILE_ATTRIBUTES) {
        LLOG("engine: %ls not found", exe.c_str());
        return false;
    }

    SECURITY_ATTRIBUTES sa = { sizeof(sa), nullptr, TRUE };
    HANDLE childStdinRead = INVALID_HANDLE_VALUE, childStdoutWrite = INVALID_HANDLE_VALUE;
    if (!CreatePipe(&childStdinRead, &stdinWrite_, &sa, 0)) return false;
    if (!CreatePipe(&stdoutRead_, &childStdoutWrite, &sa, 1 << 20)) { CloseHandle(childStdinRead); return false; }
    SetHandleInformation(stdinWrite_, HANDLE_FLAG_INHERIT, 0);
    SetHandleInformation(stdoutRead_, HANDLE_FLAG_INHERIT, 0);

    // Engine diagnostics go to a per-process log next to ours.
    std::wstring errPath = Logger::defaultLogDirectory() + L"\\engine_" + std::to_wstring(GetCurrentProcessId()) + L".log";
    stderrFile_ = CreateFileW(errPath.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE, &sa, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (stderrFile_ == INVALID_HANDLE_VALUE) {
        stderrFile_ = CreateFileW(L"NUL", GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, &sa, OPEN_EXISTING, 0, nullptr);
    }

    // Environment: inherit ours plus LTTS_ROOT.  The engine looks for its data under
    // ${LTTS_ROOT}/data/languages.
    std::wstring envBlock;
    {
        LPWCH env = GetEnvironmentStringsW();
        for (LPWCH p = env; *p; ) {
            std::wstring entry(p);
            p += entry.size() + 1;
            if (_wcsnicmp(entry.c_str(), L"LTTS_", 5) == 0) continue;
            envBlock += entry;
            envBlock.push_back(L'\0');
        }
        FreeEnvironmentStringsW(env);
        envBlock += L"LTTS_ROOT=" + engineDir_;
        envBlock.push_back(L'\0');
        envBlock.push_back(L'\0');
    }

    std::wstring cmd = L"\"" + exe + L"\" -dir \"" + engineDir_ + L"\\data\\chfiles\"";
    std::vector<wchar_t> cmdBuf(cmd.begin(), cmd.end());
    cmdBuf.push_back(L'\0');

    STARTUPINFOW si = {};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    si.hStdInput = childStdinRead;
    si.hStdOutput = childStdoutWrite;
    si.hStdError = stderrFile_;

    BOOL ok = CreateProcessW(exe.c_str(), cmdBuf.data(), nullptr, nullptr, TRUE,
                             CREATE_NO_WINDOW | CREATE_UNICODE_ENVIRONMENT,
                             &envBlock[0], engineDir_.c_str(), &si, &pi_);
    DWORD err = GetLastError();
    CloseHandle(childStdinRead);
    CloseHandle(childStdoutWrite);
    if (!ok) {
        LLOG("engine: CreateProcess failed (%lu) for %ls", err, exe.c_str());
        closeHandles();
        return false;
    }
    LLOG("engine: launched pid %lu: %ls", pi_.dwProcessId, cmd.c_str());
    readerThread_ = CreateThread(nullptr, 0, readerThreadThunk, this, 0, nullptr);
    if (!readerThread_) {
        LLOG("engine: CreateThread failed");
        shutdown();
        return false;
    }
    return true;
}

bool LucentEngine::ensureRunning() {
    if (isRunning()) return true;
    return launch();
}

DWORD WINAPI LucentEngine::readerThreadThunk(LPVOID p) {
    static_cast<LucentEngine*>(p)->readerThread();
    return 0;
}

bool LucentEngine::readExact(void* buf, size_t len) {
    uint8_t* p = static_cast<uint8_t*>(buf);
    while (len > 0) {
        DWORD got = 0;
        if (!ReadFile(stdoutRead_, p, static_cast<DWORD>(len), &got, nullptr) || got == 0) return false;
        p += got;
        len -= got;
    }
    return true;
}

bool LucentEngine::readPacket(Packet& pkt) {
    if (!readExact(&pkt.header, sizeof(pkt.header))) return false;
    if (pkt.header.cookie != kCookie) {
        LLOG("engine: bad cookie %08x (type %u)", pkt.header.cookie, pkt.header.type);
        return false;
    }
    const uint32_t bsz = bodySize(pkt.header.type);
    if (bsz == 0) {
        LLOG("engine: unknown packet type %u", pkt.header.type);
        return false;
    }
    pkt.body.resize(bsz);
    if (!readExact(pkt.body.data(), bsz)) return false;
    pkt.extra.clear();
    const int off = extraLengthOffset(pkt.header.type);
    if (off >= 0) {
        uint32_t n = 0;
        memcpy(&n, pkt.body.data() + off, 4);
        if (n > (64u << 20)) {
            LLOG("engine: absurd extra length %u", n);
            return false;
        }
        if (n) {
            pkt.extra.resize(n);
            if (!readExact(pkt.extra.data(), n)) return false;
        }
    }
    return true;
}

void LucentEngine::readerThread() {
    for (;;) {
        Packet pkt;
        if (!readPacket(pkt)) break;
        EnterCriticalSection(&cs_);
        queue_.push_back(std::move(pkt));
        LeaveCriticalSection(&cs_);
        SetEvent(packetEvent_);
    }
    readerFailed_ = true;
    SetEvent(packetEvent_);
    LLOG("engine: reader thread finished (child gone or pipe closed)");
}

bool LucentEngine::waitPacket(Packet& pkt, DWORD timeoutMs) {
    const ULONGLONG deadline = GetTickCount64() + timeoutMs;
    for (;;) {
        EnterCriticalSection(&cs_);
        if (!queue_.empty()) {
            pkt = std::move(queue_.front());
            queue_.pop_front();
            LeaveCriticalSection(&cs_);
            return true;
        }
        const bool failed = readerFailed_;
        LeaveCriticalSection(&cs_);
        if (failed) return false;
        const ULONGLONG now = GetTickCount64();
        if (now >= deadline) return false;
        WaitForSingleObject(packetEvent_, static_cast<DWORD>(deadline - now));
    }
}

bool LucentEngine::writeAll(const void* data, size_t len) {
    const uint8_t* p = static_cast<const uint8_t*>(data);
    while (len > 0) {
        DWORD written = 0;
        if (!WriteFile(stdinWrite_, p, static_cast<DWORD>(len), &written, nullptr)) {
            LLOG("engine: WriteFile failed (%lu)", GetLastError());
            return false;
        }
        p += written;
        len -= written;
    }
    return true;
}

bool LucentEngine::sendPacket(uint16_t channel, uint16_t type, const void* body, size_t bodyLen, const void* extra, size_t extraLen, uint16_t* packetIdOut) {
    EnterCriticalSection(&writeCs_);
    PacketHeader h = { kCookie, channel, 0, nextPacketId_++, type };
    if (nextPacketId_ == 0) nextPacketId_ = 1;
    if (packetIdOut) *packetIdOut = h.packetId;
    std::vector<uint8_t> buf(sizeof(h) + bodyLen + extraLen);
    memcpy(buf.data(), &h, sizeof(h));
    if (bodyLen) memcpy(buf.data() + sizeof(h), body, bodyLen);
    if (extraLen) memcpy(buf.data() + sizeof(h) + bodyLen, extra, extraLen);
    const bool ok = writeAll(buf.data(), buf.size());
    LeaveCriticalSection(&writeCs_);
    return ok;
}

std::string LucentEngine::channelKey(const VoiceRequest& req) const {
    // Channels are shared by every voice that resolves to the same channel-file content
    // and sample rate: male/female variants of most languages differ only in the pitch
    // sent with the speaker parameters, and the Canadian French front end cannot even
    // host two channels of its language in one process.
    std::string templateName;
    const std::string content = buildChannelFile(engineDir_, req, &templateName);
    char buf[256];
    snprintf(buf, sizeof(buf), "%s|%d|%08x", req.language ? req.language->subdir : "?", req.sampleRate,
             content.empty() ? 0u : fnv1a(content));
    return buf;
}

bool LucentEngine::ensureChannelFile(const VoiceRequest& req, std::string& pathOut) {
    std::string templateName;
    const std::string content = buildChannelFile(engineDir_, req, &templateName);
    if (content.empty()) {
        LLOG("engine: no channel template for %s", req.language ? req.language->subdir : "?");
        return false;
    }
    char name[64];
    snprintf(name, sizeof(name), "\\%08x_%s", fnv1a(content), templateName.c_str());
    std::wstring wpath = channelDir_ + std::wstring(name, name + strlen(name));
    // Write only when missing or different.
    bool same = false;
    {
        std::ifstream in(wpath, std::ios::binary);
        if (in) {
            std::stringstream ss;
            ss << in.rdbuf();
            same = ss.str() == content;
        }
    }
    if (!same) {
        std::ofstream out(wpath, std::ios::binary | std::ios::trunc);
        if (!out) {
            LLOG("engine: cannot write channel file %ls", wpath.c_str());
            return false;
        }
        out << content;
    }
    // The engine is an ANSI program; hand it an ANSI path.
    int n = WideCharToMultiByte(CP_ACP, 0, wpath.c_str(), -1, nullptr, 0, nullptr, nullptr);
    pathOut.assign(static_cast<size_t>(n > 0 ? n - 1 : 0), '\0');
    if (n > 1) WideCharToMultiByte(CP_ACP, 0, wpath.c_str(), -1, &pathOut[0], n - 1, nullptr, nullptr);
    return !pathOut.empty();
}

LucentEngine::Channel* LucentEngine::openChannel(const VoiceRequest& req) {
    const std::string key = channelKey(req);
    auto it = channels_.find(key);
    if (it != channels_.end()) return &it->second;

    if (req.language && req.language->exclusive) {
        // A second, different channel of this language would hang the engine; start a
        // fresh process instead (channel setup costs a few hundred milliseconds).
        const std::string prefix = std::string(req.language->subdir) + "|";
        for (const auto& kv : channels_) {
            if (kv.first.compare(0, prefix.size(), prefix) == 0) {
                LLOG("engine: %s needs an exclusive front end; relaunching the engine", req.language->subdir);
                shutdown();
                if (!launch()) return nullptr;
                break;
            }
        }
    }

    std::string chnPath;
    if (!ensureChannelFile(req, chnPath)) return nullptr;

    OpenChannelBody body = {};
    body.audioFormat = req.sampleRate == 8000 ? kFormatPcm16_8000 : kFormatPcm16_11025;
    body.topPitch = req.topPitch;
    body.referenceLine = req.referenceLine;
    body.bottomPitch = req.bottomPitch;
    body.breathiness = 0.0f;
    body.frontVocalTract = req.frontVocalTract;
    body.backVocalTract = req.backVocalTract;
    body.speakingRate = req.speedFactor;
    body.gender = req.female ? 1 : 2;
    body.language = req.language->engineIndex;
    body.threadingModel = 1;
    body.emailSupport = req.email ? 1 : 2;
    body.bookmarkRules = 0;
    body.initFileLength = static_cast<uint32_t>(chnPath.size());

    uint16_t pid = 0;
    const ULONGLONG t0 = GetTickCount64();
    if (!sendPacket(0, PT_OpenChannel, &body, sizeof(body), chnPath.data(), chnPath.size(), &pid)) return nullptr;
    LLOG("engine: open channel %s using %s", key.c_str(), chnPath.c_str());

    // Wait for the matching OpenNotify; other packets (late audio from a previous
    // utterance) are discarded.
    for (;;) {
        Packet pkt;
        if (!waitPacket(pkt, kOpenTimeoutMs)) {
            LLOG("engine: open channel timed out");
            return nullptr;
        }
        if (pkt.header.type != PT_OpenNotify) continue;
        OpenNotifyBody on;
        memcpy(&on, pkt.body.data(), sizeof(on));
        if (on.requestPacketId != pid) continue;
        if (on.result != 0 || on.channelId == 0) {
            LLOG("engine: open channel failed, result %u", on.result);
            return nullptr;
        }
        Channel ch;
        ch.id = on.channelId;
        ch.key = key;
        ch.channelFile = chnPath;
        LLOG("engine: channel %u ready in %llu ms", ch.id, GetTickCount64() - t0);
        auto ins = channels_.emplace(key, ch);
        return &ins.first->second;
    }
}

bool LucentEngine::sendSpeakerParams(Channel& ch, const VoiceRequest& req) {
    SpeakerParamsBody sp;
    sp.topPitch = req.topPitch;
    sp.referenceLine = req.referenceLine;
    sp.bottomPitch = req.bottomPitch;
    sp.breathiness = 0.0f;
    sp.frontVocalTract = req.frontVocalTract;
    sp.backVocalTract = req.backVocalTract;
    sp.speakingRate = req.speedFactor;
    sp.gender = req.female ? 1 : 2;
    return sendPacket(static_cast<uint16_t>(ch.id), PT_SpeakerParams, &sp, sizeof(sp), nullptr, 0, nullptr);
}

bool LucentEngine::sendSetValue(Channel& ch, const char* name, const char* value) {
    SetValueBody sv = {};
    strncpy_s(sv.name, name, _TRUNCATE);
    const size_t vlen = strlen(value);
    sv.valueLength = static_cast<uint32_t>(vlen);
    return sendPacket(static_cast<uint16_t>(ch.id), PT_SetValue, &sv, sizeof(sv), value, vlen, nullptr);
}

void LucentEngine::cancel() {
    InterlockedExchange(&cancelRequested_, 1);
    const uint32_t chan = activeChannel_;
    if (chan && stdinWrite_ != INVALID_HANDLE_VALUE) {
        CommandBody cb = { CMD_DiscardSpeech };
        sendPacket(static_cast<uint16_t>(chan), PT_Command, &cb, sizeof(cb), nullptr, 0, nullptr);
    }
    SetEvent(packetEvent_);
}

bool LucentEngine::speak(const VoiceRequest& req, const std::string& text, SpeakSink& sink, bool* aborted) {
    if (aborted) *aborted = false;
    if (!ensureRunning()) return false;
    Channel* ch = openChannel(req);
    if (!ch) return false;

    InterlockedExchange(&cancelRequested_, 0);
    activeChannel_ = ch->id;

    // Drop anything left over from an earlier, aborted utterance.
    EnterCriticalSection(&cs_);
    queue_.clear();
    LeaveCriticalSection(&cs_);

    const bool paramsChanged = !ch->paramsSent ||
        ch->last.topPitch != req.topPitch || ch->last.referenceLine != req.referenceLine ||
        ch->last.bottomPitch != req.bottomPitch || ch->last.frontVocalTract != req.frontVocalTract ||
        ch->last.backVocalTract != req.backVocalTract || ch->last.speedFactor != req.speedFactor ||
        ch->last.female != req.female;
    if (paramsChanged) {
        if (!sendSpeakerParams(*ch, req)) return false;
        ch->paramsSent = true;
        ch->last = req;
    }
    if (ch->lastVolume != req.volume) {
        char v[32];
        snprintf(v, sizeof(v), "%.4f", req.volume);
        if (!sendSetValue(*ch, "Volume", v)) return false;
        ch->lastVolume = req.volume;
    }

    TextBody tb = { 2, 0xffffffffu, static_cast<uint32_t>(text.size()) };
    uint16_t textPid = 0;
    const ULONGLONG t0 = GetTickCount64();
    if (!sendPacket(static_cast<uint16_t>(ch->id), PT_Text, &tb, sizeof(tb), text.data(), text.size(), &textPid)) return false;
    LLOG("engine: text pid %u chan %u %u bytes", textPid, ch->id, static_cast<unsigned>(text.size()));

    struct PendingMark { uint32_t id; uint32_t offset; };
    std::vector<PendingMark> pending;
    uint64_t emitted = 0;
    bool discarding = false;
    bool sinkAborted = false;
    bool firstAudio = true;

    bool textConsumed = false;   // engine reported the whole text as processed
    for (;;) {
        if (!discarding && InterlockedCompareExchange(&cancelRequested_, 0, 0)) {
            discarding = true;
        }
        Packet pkt;
        if (!waitPacket(pkt, textConsumed ? 1500 : kSpeakIdleTimeoutMs)) {
            if (textConsumed && !readerFailed_) {
                // Text with nothing speakable produces no audio packets at all.
                LLOG("engine: text consumed without audio; treating as end of stream");
                if (aborted) *aborted = discarding;
                activeChannel_ = 0;
                return true;
            }
            LLOG("engine: no end-of-stream (timeout or transport failure); restarting child");
            shutdown();
            return false;
        }
        if (pkt.header.channel != ch->id && pkt.header.channel != 0 && pkt.header.type == PT_Audio) continue;
        if (pkt.header.type == PT_Notify) {
            NotifyBody nb;
            memcpy(&nb, pkt.body.data(), sizeof(nb));
            if (nb.notifyId == NI_TextRange && pkt.extra.size() >= 8) {
                uint32_t endPos = 0;
                memcpy(&endPos, pkt.extra.data() + 4, 4);
                if (endPos >= text.size()) textConsumed = true;
            }
            continue;
        }
        switch (pkt.header.type) {
        case PT_BookMark: {
            BookMarkBody bm;
            memcpy(&bm, pkt.body.data(), sizeof(bm));
            pending.push_back({ bm.id, bm.byteOffset });
            break;
        }
        case PT_Audio: {
            AudioBody ab;
            memcpy(&ab, pkt.body.data(), sizeof(ab));
            if (firstAudio && !pkt.extra.empty()) {
                firstAudio = false;
                LLOG("engine: first audio after %llu ms (%u bytes)", GetTickCount64() - t0, ab.size);
            }
            if (!discarding && !sinkAborted) {
                for (const PendingMark& m : pending) {
                    if (sink.onBookmark) sink.onBookmark(m.id, static_cast<uint32_t>(emitted + m.offset));
                }
                pending.clear();
                if (!pkt.extra.empty()) {
                    if (!sink.onAudio(pkt.extra.data(), pkt.extra.size())) {
                        sinkAborted = true;
                        LLOG("engine: sink aborted, discarding rest of utterance");
                        cancel();
                        discarding = true;
                    }
                    emitted += pkt.extra.size();
                }
            } else {
                pending.clear();
            }
            if (ab.flags & AF_End) {
                LLOG("engine: end of stream, %llu bytes in %llu ms%s", emitted, GetTickCount64() - t0, discarding ? " (discarded)" : "");
                if (aborted) *aborted = discarding;
                activeChannel_ = 0;
                return true;
            }
            break;
        }
        case PT_Notify:
        case PT_ValueNotify:
        case PT_OpenNotify:
        default:
            break;
        }
    }
}

}  // namespace lucent
