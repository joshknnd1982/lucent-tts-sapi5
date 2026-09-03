#pragma once
//
// Wire protocol spoken by the Lucent Articulator 4.0 engine (ttsserver.exe) on its
// stdin/stdout.  Reverse engineered from ttsserv.exe (the original SAPI 4 broker)
// and ttsserver.exe itself; see docs/PROTOCOL.md for the field-by-field description.
//
// Every packet is a 12-byte header followed by a fixed-size body whose length depends
// on the packet type, followed by optional "extra" bytes whose length is stored in the
// body.  All integers are little-endian.
//
#include <stdint.h>

namespace lucent {

constexpr uint32_t kCookie = 0x56000101u;

enum PacketType : uint16_t {
    PT_OpenChannel      = 1,   // client -> engine, body 0x30 + init file name
    PT_GetValue         = 2,   // client -> engine, body 0x40 (symbol name)
    PT_Text             = 3,   // client -> engine, body 0x0c + text
    PT_GetTranscription = 4,   // client -> engine, body 0x0c + text
    PT_OpenNotify       = 5,   // engine -> client, body 8
    PT_ValueNotify      = 6,   // engine -> client, body 0x48 + extra
    PT_Audio            = 7,   // engine -> client, body 0x10 + PCM
    PT_Transcription    = 8,   // engine -> client, body 0x0c + extra
    PT_CommandString    = 9,   // body 4
    PT_Command          = 10,  // client -> engine, body 4 (server command id)
    PT_SetValue         = 11,  // client -> engine, body 0x44 + value string
    PT_SpeakerParams    = 12,  // client -> engine, body 0x20
    PT_Notify           = 13,  // engine -> client, body 0x0c + extra
    PT_DotR             = 14,
    PT_BookMark         = 15,  // engine -> client, body 0x10
};

// Server command ids carried by PT_Command.
enum CommandId : uint32_t {
    CMD_GetInfo       = 1,
    CMD_DiscardSpeech = 5,   // cancel: engine answers with an empty end-of-stream audio packet
    CMD_Channels      = 9,
};

// Audio packet flags (body +8).
enum AudioFlags : uint32_t {
    AF_Begin  = 1,
    AF_Middle = 2,
    AF_End    = 4,
};

// Notification ids carried by PT_Notify (body +4).
enum NotifyId : uint32_t {
    NI_ValueSet     = 1,
    NI_TextRange    = 2,   // extra = { uint32 start, uint32 end } characters consumed
    NI_TextAccepted = 3,
};

// Audio format words (body of PT_OpenChannel +0, PT_Audio +4).  The low 16 bits hold
// the sample rate; the top nibble is the encoding (1 = 16-bit signed PCM).
constexpr uint32_t kFormatPcm16_11025 = 0x12102B11u;
constexpr uint32_t kFormatPcm16_8000  = 0x12101F40u;

// Engine language indices (PT_OpenChannel body +0x24).
enum LanguageIndex : uint16_t {
    LANG_ChineseMandarin  = 2,
    LANG_EnglishUS        = 4,
    LANG_French           = 6,
    LANG_FrenchCanadian   = 7,
    LANG_German           = 8,
    LANG_Italian          = 9,
    LANG_SpanishCastilian = 16,
    LANG_SpanishMexican   = 17,
};

#pragma pack(push, 1)
struct PacketHeader {
    uint32_t cookie;
    uint16_t channel;
    uint16_t sender;
    uint16_t packetId;
    uint16_t type;
};

struct OpenChannelBody {
    uint32_t audioFormat;
    float    topPitch;
    float    referenceLine;
    float    bottomPitch;
    float    breathiness;
    float    frontVocalTract;
    float    backVocalTract;
    float    speakingRate;
    uint32_t gender;          // 1 = female, 2 = male
    uint16_t language;        // LanguageIndex
    uint8_t  threadingModel;  // 1
    uint8_t  emailSupport;    // 1 = on, 2 = off
    uint32_t bookmarkRules;   // 0
    uint32_t initFileLength;  // bytes of channel-file path that follow
};

struct OpenNotifyBody {
    uint16_t requestPacketId;
    uint16_t result;          // 0 = success
    uint32_t channelId;
};

struct TextBody {
    uint32_t encoding;        // 2 = code-page text
    uint32_t position;        // 0xffffffff
    uint32_t length;          // bytes of text that follow
};

struct AudioBody {
    uint32_t reserved;
    uint32_t audioFormat;
    uint32_t flags;           // AudioFlags
    uint32_t size;            // PCM bytes that follow
};

struct SetValueBody {
    char     name[0x40];
    uint32_t valueLength;     // bytes of value string that follow
};

struct SpeakerParamsBody {
    float    topPitch;
    float    referenceLine;
    float    bottomPitch;
    float    breathiness;
    float    frontVocalTract;
    float    backVocalTract;
    float    speakingRate;    // duration multiplier: 1.0 normal, 2.0 twice as slow
    uint32_t gender;
};

struct NotifyBody {
    uint32_t requestPacketId;
    uint32_t notifyId;        // NotifyId
    uint32_t extraLength;
};

struct BookMarkBody {
    uint32_t kind;
    uint32_t id;              // number from a \Mrk=n\ tag
    uint32_t reserved;
    uint32_t byteOffset;      // offset into the utterance's PCM stream
};

struct CommandBody {
    uint32_t commandId;
};
#pragma pack(pop)

static_assert(sizeof(PacketHeader) == 12, "header size");
static_assert(sizeof(OpenChannelBody) == 0x30, "open body size");
static_assert(sizeof(OpenNotifyBody) == 8, "open notify size");
static_assert(sizeof(TextBody) == 0x0c, "text body size");
static_assert(sizeof(AudioBody) == 0x10, "audio body size");
static_assert(sizeof(SetValueBody) == 0x44, "set value size");
static_assert(sizeof(SpeakerParamsBody) == 0x20, "speaker params size");
static_assert(sizeof(NotifyBody) == 0x0c, "notify size");
static_assert(sizeof(BookMarkBody) == 0x10, "bookmark size");

// Fixed body size per packet type as expected by the engine (0 = unknown).
inline uint32_t bodySize(uint16_t type) {
    switch (type) {
    case PT_OpenChannel: return 0x30;
    case PT_GetValue: return 0x40;
    case PT_Text: return 0x0c;
    case PT_GetTranscription: return 0x0c;
    case PT_OpenNotify: return 8;
    case PT_ValueNotify: return 0x48;
    case PT_Audio: return 0x10;
    case PT_Transcription: return 0x0c;
    case PT_CommandString: return 4;
    case PT_Command: return 4;
    case PT_SetValue: return 0x44;
    case PT_SpeakerParams: return 0x20;
    case PT_Notify: return 0x0c;
    case PT_BookMark: return 0x10;
    default: return 0;
    }
}

// Offset of the "extra length" field in the body, or -1 when the type has no extra data.
inline int extraLengthOffset(uint16_t type) {
    switch (type) {
    case PT_OpenChannel: return 0x2c;
    case PT_Text: return 8;
    case PT_GetTranscription: return 8;
    case PT_ValueNotify: return 0x44;
    case PT_Audio: return 0x0c;
    case PT_Transcription: return 8;
    case PT_SetValue: return 0x40;
    case PT_Notify: return 8;
    default: return -1;
    }
}

}  // namespace lucent
