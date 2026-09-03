#pragma once
//
// LucentEngine: owns one ttsserver.exe child process, talks the packet protocol over its
// stdin/stdout, keeps one engine channel per distinct voice configuration and renders
// utterances into a caller-supplied sink.  Works identically from x86 and x64 callers
// because the engine is a separate process.
//
#include <windows.h>
#include <stdint.h>
#include <string>
#include <vector>
#include <map>
#include <deque>
#include <functional>
#include "lucent_protocol.h"
#include "lucent_settings.h"

namespace lucent {

struct Packet {
    PacketHeader header{};
    std::vector<uint8_t> body;
    std::vector<uint8_t> extra;
};

// Callbacks used while an utterance is rendered.  Return false from onAudio to abort.
struct SpeakSink {
    std::function<bool(const uint8_t* pcm, size_t bytes)> onAudio;
    std::function<void(uint32_t id, uint32_t byteOffset)> onBookmark;
};

class LucentEngine {
public:
    explicit LucentEngine(const std::wstring& engineDir);
    ~LucentEngine();

    LucentEngine(const LucentEngine&) = delete;
    LucentEngine& operator=(const LucentEngine&) = delete;

    // Starts the child if needed.  Returns false when ttsserver.exe cannot be launched.
    bool ensureRunning();
    bool isRunning();
    void shutdown();

    // Call before destroying the engine from DllMain(DLL_PROCESS_DETACH): the loader lock
    // is held there, so the reader thread must not be waited for.
    void setDetaching() { detaching_ = true; }

    // Renders `text` (already converted to the language code page, control tags allowed)
    // with the given voice.  Blocks until the engine reports end of stream, the sink
    // aborts, or a deadline passes.  Returns false on transport failure (the child is
    // restarted on the next call).
    bool speak(const VoiceRequest& req, const std::string& text, SpeakSink& sink, bool* aborted);

    // Asks the engine to drop everything queued on the current channel.  Safe to call
    // from another thread while speak() runs.
    void cancel();

    std::wstring engineDirectory() const { return engineDir_; }
    DWORD childPid() const { return pi_.dwProcessId; }

private:
    struct Channel {
        uint32_t id = 0;
        std::string key;
        std::string channelFile;
        VoiceRequest last{};
        bool paramsSent = false;
        float lastVolume = -1.0f;
        float lastSpeed = -1.0f;
    };

    bool launch();
    void closeHandles();
    bool writeAll(const void* data, size_t len);
    bool sendPacket(uint16_t channel, uint16_t type, const void* body, size_t bodyLen, const void* extra, size_t extraLen, uint16_t* packetIdOut);
    bool readExact(void* buf, size_t len);
    bool readPacket(Packet& pkt);
    bool waitPacket(Packet& pkt, DWORD timeoutMs);
    Channel* openChannel(const VoiceRequest& req);
    std::string channelKey(const VoiceRequest& req) const;
    bool ensureChannelFile(const VoiceRequest& req, std::string& pathOut);
    bool sendSpeakerParams(Channel& ch, const VoiceRequest& req);
    bool sendSetValue(Channel& ch, const char* name, const char* value);
    void readerThread();
    static DWORD WINAPI readerThreadThunk(LPVOID p);

    std::wstring engineDir_;
    std::wstring channelDir_;
    PROCESS_INFORMATION pi_{};
    HANDLE stdinWrite_ = INVALID_HANDLE_VALUE;
    HANDLE stdoutRead_ = INVALID_HANDLE_VALUE;
    HANDLE stderrFile_ = INVALID_HANDLE_VALUE;
    HANDLE readerThread_ = nullptr;
    HANDLE packetEvent_ = nullptr;
    CRITICAL_SECTION cs_;          // guards queue_ and the write side
    CRITICAL_SECTION writeCs_;
    std::deque<Packet> queue_;
    bool readerFailed_ = false;
    uint16_t nextPacketId_ = 1;
    std::map<std::string, Channel> channels_;
    volatile LONG cancelRequested_ = 0;
    uint32_t activeChannel_ = 0;
    bool detaching_ = false;
};

// Converts a wide string to the engine code page.
std::string toCodePage(const std::wstring& s, UINT codePage);

}  // namespace lucent
