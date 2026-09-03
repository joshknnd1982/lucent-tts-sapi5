# Lucent Articulator 4.0 engine protocol

`ttsserver.exe` ("Lucent Articulator 4.0 Build 010309", 32-bit) is the real synthesizer
behind the Lucent SAPI 4 voices. The SAPI 4 layer (`ltts.dll`, the DCOM broker
`ttsserv.exe` and its proxy `ttsservps.dll`) is only a client: the broker starts
`ttsserver.exe` with no arguments and exchanges binary packets over the child's
**stdin/stdout**. This wrapper talks that protocol directly, so no SAPI 4 component is
needed and both the x86 and x64 SAPI 5 DLLs can drive the same 32-bit engine process.

Everything below was recovered by disassembling `ttsserv.exe` and `ttsserver.exe` and by
probing the running engine (see `scratch` tools referenced in the commit history).

## Process setup

* Command line: `ttsserver.exe -dir <channel setup directory>`; the engine also accepts
  `-file <packet capture> -output <packet file>`, `-log <file>` (redirects stderr) and
  `-help`.
* Environment: `LTTS_ROOT` — data is located at `${LTTS_ROOT}/data/languages/<lang>`;
  `common` lives next to the language folders. No registry access.
* The engine changes its working directory to the folder of its own executable at startup.
* The engine writes diagnostics to stderr; the wrapper redirects it to
  `%LOCALAPPDATA%\LucentSAPI\logs\engine_<pid>.log`.

## Packet framing

All integers are little-endian.

```
struct Header {           // 12 bytes
    uint32 cookie;        // 0x56000101
    uint16 channel;       // 0 before a channel exists
    uint16 sender;        // module id; 0 from the client, 7 = wavesynt on audio packets
    uint16 packetId;      // client chooses; replies echo it in their bodies
    uint16 type;          // see below
};
```

A fixed-size body follows the header; some packets carry extra bytes whose length is a
field in the body.

| type | name              | dir | body | extra (length field) |
|-----:|-------------------|-----|-----:|----------------------|
| 1    | OpenChannel       | in  | 0x30 | init file path (`+0x2c`) |
| 2    | GetValue          | in  | 0x40 | – |
| 3    | Text              | in  | 0x0c | text bytes (`+8`) |
| 4    | GetTranscription  | in  | 0x0c | text bytes (`+8`) |
| 5    | OpenNotify        | out | 8    | – |
| 6    | ValueNotify       | out | 0x48 | (`+0x44`) |
| 7    | Audio             | out | 0x10 | PCM (`+0x0c`) |
| 8    | Transcription     | out | 0x0c | (`+8`) |
| 9    | CommandString     | –   | 4    | – |
| 10   | Command           | in  | 4    | – |
| 11   | SetValue          | in  | 0x44 | value string (`+0x40`) |
| 12   | SpeakerParams     | in  | 0x20 | – |
| 13   | Notify            | out | 0x0c | (`+8`) |
| 15   | BookMark          | out | 0x10 | – |

### OpenChannel (1)

```
uint32 audioFormat;     // 0x12102B11 = 16-bit PCM 11025 Hz, 0x12101F40 = 16-bit PCM 8000 Hz
float  topPitch, referenceLine, bottomPitch, breathiness, frontVocalTract, backVocalTract, speakingRate;
uint32 gender;          // 1 female, 2 male
uint16 language;        // 2 Mandarin, 4 US English, 6 French, 7 Canadian French, 8 German,
                        // 9 Italian, 16 Castilian Spanish, 17 Mexican Spanish
uint8  threadingModel;  // 1
uint8  emailSupport;    // 1 on, 2 off
uint32 bookmarkRules;   // 0
uint32 initFileLength;  // bytes of channel-file path that follow (0 = pick x<lang>.<08|11><m|f>[e].chn)
```

Reply: **OpenNotify (5)** `{ uint16 requestPacketId; uint16 result; uint32 channelId; }`,
result 0 = success. The low 16 bits of `audioFormat` are the sample rate; the top nibble is
the encoding (1 = 16-bit PCM). Only the language, format, email flag and init file matter
here; the voice parameters are applied with SpeakerParams.

### SpeakerParams (12)

`{ float top, ref, bottom, breathiness, front, back, rate; uint32 gender; }` — pitch in Hz
(40..400 work), `rate` is a duration multiplier (1.0 normal, 2.0 twice as slow, values down
to about 0.4 speed up), `front`/`back` scale the vocal tract (0.5..1.5). `breathiness`
has no effect here; use the wavesynt `-aspirampl` option in the channel file instead.

### SetValue (11)

`{ char name[0x40]; uint32 valueLength; }` followed by the value as text. Known symbols:
`Speed` (duration multiplier), `Pitch` (Hz), `Volume` (0..1, values up to 2 clip). The
engine answers with Notify id 1.

### Text (3)

`{ uint32 encoding = 2; uint32 position = 0xffffffff; uint32 length; }` followed by the
text in the language's code page (1252 for the European languages, 936 for Mandarin GB,
950 for the Big5 channel). The engine answers with Notify id 3 (accepted), one **Audio**
packet per sentence, Notify id 2 (`extra = {uint32 start, uint32 end}` of consumed text)
and a final Audio packet flagged end-of-stream.

### Audio (7)

`{ uint32 reserved; uint32 audioFormat; uint32 flags; uint32 size; }` + PCM. Flags:
1 begin (empty packet), 2 middle, 4 end (the last packet has 6 or, after a discard, 4 with
no data).

### BookMark (15)

`{ uint32 kind; uint32 id; uint32 reserved; uint32 byteOffset; }` — produced by a SAPI 4
`\Mrk=n\` tag in the text. The packet precedes the Audio packet it belongs to and
`byteOffset` is relative to that packet's data. Adjacent markers collapse to the last one,
so the wrapper folds them.

### Command (10)

`{ uint32 commandId; }`: 5 = discard speech (cancel; the engine ends the stream with an
empty end packet), 1/10 = server information to stderr, 9 = list channels. 13 closes the
connection.

## Text controls understood by the front ends

SAPI 4 tags `\Mrk=n\`, `\Pau=ms\`, `\Spd=wpm\`, `\Pit=Hz\` (`\Vol` is rejected), native
escapes `\!R<0.1..2.0>` (rate), `\!si<ms>` (silence), `\!*H<0..64>` (prominence),
`\!C"…"` (comment). A literal backslash is written `\\`. Never forward `\!w…`: it hangs
the engine.

## Channel files

`data\chfiles\x<lang>.<08|11><m|f>[e].chn` list the module pipeline (front end, getaie,
duration, intonation, source, champ, cataie, wavesynt). The wrapper rewrites them with
paths relative to the engine folder (`data/languages/<lang>`) into
`%LOCALAPPDATA%\LucentSAPI\channels` and passes the copy as the OpenChannel init file.
Relative paths are mandatory: the module option parser splits each line on whitespace
with no quoting, so an absolute install path containing a space ("C:\Program Files\...")
makes every channel open fail with result 1. Three rules matter:

* The intonation module keys its data bank by joining `-directory` and `-parameters`; an
  absolute `-parameters` path makes that join empty on modern Windows and every utterance
  then comes out whispered. The parameter files (`x<lang>.inton`) are therefore shipped
  next to `ttsserver.exe` and referenced by bare name.
* The Canadian French front end (`mlfrend -canadianfrench`) cannot host two channels in one
  process, so the wrapper restarts the engine when a second, different Canadian French
  channel is needed. Male and female voices of all languages except US English share one
  channel anyway (only the pitch differs).

Useful wavesynt options that can be appended to the `wavesynt` line: `-aspirampl <0..4>`
(breathiness), `-spectilt <0..10>` (spectral tilt), `-volumescale <0..8>`.
