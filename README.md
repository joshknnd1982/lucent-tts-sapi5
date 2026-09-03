# Lucent TTS SAPI 5 wrapper

A native SAPI 5 text-to-speech engine for the **Lucent Articulator 4.0** speech
synthesizer, the Bell Labs / Lucent "LTTS" engine of the late 1990s. It brings the
engine's sixteen voices in eight languages to modern 32-bit and 64-bit Windows
applications and screen readers (NVDA, JAWS, Narrator, Balabolka and friends) without any
SAPI 4 component, drives the engine through its own reverse-engineered packet protocol, and
adds a fully accessible configuration utility.

The installer for the current release is on the
[Releases](https://github.com/joshknnd1982/lucent-tts-sapi5/releases) page. This
repository holds the wrapper source only: the Lucent engine and its language data are
proprietary and are shipped inside the installer.

## Contents

1. [The Lucent TTS engine](#the-lucent-tts-engine)
2. [Languages and voices](#languages-and-voices)
3. [Speech parameters](#speech-parameters)
4. [Text controls](#text-controls)
5. [How the wrapper works](#how-the-wrapper-works)
6. [Installing](#installing)
7. [SmartScreen, Defender and code signing](#smartscreen-defender-and-code-signing)
8. [Configuration utility](#configuration-utility)
9. [Logs and troubleshooting](#logs-and-troubleshooting)
10. [Building from source](#building-from-source)
11. [Testing](#testing)
12. [Repository layout](#repository-layout)
13. [Credits and license](#credits-and-license)

## The Lucent TTS engine

Lucent Text-to-Speech ("LTTS", marketed as *Lucent Articulator*) is the commercial
descendant of the AT&T Bell Labs text-to-speech system. The version wrapped here
identifies itself as **Lucent Articulator 4.0 Build 010309** (March 2001) and was
distributed as "Lucent 3.x2 TTS" with telephony products such as Active Call Center. It
is abandonware: Lucent's speech business was folded into other companies long ago, the
product has not been sold or supported for two decades, and no documentation or SDK is
available any more. Everything in this project was recovered by disassembling the
binaries and probing the running engine; the protocol notes live in
[docs/PROTOCOL.md](docs/PROTOCOL.md).

The original package consists of:

| File | Role |
|------|------|
| `ttsserver.exe` | The actual synthesizer, a 5 MB 32-bit console program containing every module (front ends, prosody, unit selection, waveform synthesis). It reads packets on stdin and writes packets on stdout. |
| `ttsserv.exe` | A DCOM broker ("Lucent TTS Local Server for Microsoft Speech API 2-4.x") that launched `ttsserver.exe` and forwarded SAPI 4 calls to it. |
| `ltts.dll` | The SAPI 4 engine object with its voice property pages. |
| `ttsservps.dll`, `ltts.cpl` | COM proxy/stub and a Control Panel applet for the SAPI 4 voices. |
| `languages\<lang>\` | Per-language data: pronunciation dictionaries and finite-state models (`.fsm`, `.rfs`, `.fsi`), duration tables, phone definitions, intonation parameters (`x<lang>.inton`) and the diphone/unit inventories (`b<lang><rate><gender>.00i`, ~1 to 3 MB each). |
| `languages\common\` | Intonation data bank shared by all languages (`Scalefile`, `gen1.dep`, `genQ`, `ECH.*`, `*.pars`). |
| `chfiles\*.chn` | "Channel files": one per language, sample rate, gender and e-mail variant, listing the module pipeline and each module's options. |

Only `ttsserver.exe` and the data are needed. The engine is a concatenative synthesizer:
a language front end turns text into a phoneme tree, the `duration` and `intonation`
modules assign timing and an F0 contour, `getaie`/`cataie` select and concatenate units
from the inventory, and `wavesynt` resynthesizes the waveform with a glottal source
model. That last stage is why pitch, vocal-tract scaling, breathiness and spectral tilt
can be changed at run time. It renders about 300 times faster than real time.

### Why the SAPI 4 layer had to go

`ltts.dll` and `ttsserv.exe` are 32-bit, need the SAPI 4 runtime, register DCOM classes,
and no 64-bit application can load them. Because the real work happens in a separate
process that talks a simple byte protocol, the wrapper spawns `ttsserver.exe` directly
from both the x86 and the x64 SAPI 5 DLL and never touches the SAPI 4 pieces.

### Two engine quirks worth knowing

* **Whispering voices.** The intonation module locates its data bank by joining the
  `-directory` and `-parameters` options of the channel file. On Windows 10/11 that join
  yields an empty key when `-parameters` is an absolute path; the data bank is never
  created, every utterance then crashes inside the module (the engine catches it) and the
  waveform synthesizer runs without an F0 track, producing unvoiced, whispered speech.
  The wrapper ships the `x<lang>.inton` files next to `ttsserver.exe` and references them
  by bare name.
* **No spaces in module options.** The channel-file parser splits on whitespace and has no
  quoting, so any absolute path containing a space (`C:\Program Files\...`) breaks every
  module. The wrapper writes its channel files with paths relative to the engine folder.

## Languages and voices

| Language (engine index) | Data folder | Code page | Voices | Sample rates | Notes |
|-------------------------|-------------|-----------|--------|--------------|-------|
| US English (4) | `engusg` | 1252 | John (male), Grace (female) | 11025, 8000 Hz | Separate male and female inventories; e-mail preprocessor. |
| German (8) | `deudes` | 1252 | Rainer, Monika | 11025, 8000 Hz | Female voice is the male inventory with a higher pitch. |
| French (6) | `frafrs` | 1252 | Pierre, Madeleine | 11025, 8000 Hz | Liaison and agreement models. |
| Canadian French (7) | `fracaq` | 1252 | Jacques, Yvette | 11025, 8000 Hz | Uses row-indexed FSMs; only one channel of this language per engine process. |
| Italian (9) | `itaits` | 1252 | Carlo, Giulia | 11025, 8000 Hz | |
| Castilian Spanish (16) | `esless` | 1252 | Pedro, Juanita | 8000 Hz only | Only an 8 kHz inventory was shipped. |
| Mexican Spanish (17) | `eslmxm` | 1252 | Pablo, Carmen | 11025, 8000 Hz | |
| Mandarin Chinese (2) | `chixxm` | 936 / 950 | Ming (GB), Ming Big5 | 11025, 8000 Hz | One male inventory; GB 2312 and Big5 input channels; its own `mandinton` intonation module. |

The engine also knows the names of Cantonese, Dutch, British English, Japanese, Korean,
Portuguese, Brazilian Portuguese, Romanian, Russian and Taiwanese, but no data for them
was ever shipped with this package.

Every language has an **e-mail preprocessing** variant (`emupp` module) that reads mail
headers, quoting levels and attachments aloud in a structured way; the wrapper exposes it
as an option of the custom voice.

## Speech parameters

All values below were confirmed by measuring the engine's output. "Live" parameters are
changed per utterance; "channel" parameters are options of the waveform synthesizer and
take effect when the wrapper regenerates the channel file (a few hundred milliseconds).

| Parameter | Mechanism | Range | Default | Effect |
|-----------|-----------|-------|---------|--------|
| Top pitch, reference line, bottom pitch | SpeakerParams packet (Hz) | 40 .. 400 Hz (the engine accepts more) | per voice, e.g. John 149/89/65, Grace 215/145/120 | The three-line pitch model of the Bell Labs intonation system: the F0 contour moves between the bottom and top lines around the reference line. |
| Speaking speed | SpeakerParams `rate` or SetValue `Speed` | 3.0 (slowest) .. 0.4 (fastest) duration factor | 1.0 | Duration multiplier; the engine clamps speed-ups below roughly 0.5. |
| Volume | SetValue `Volume` | 0 .. 1 (up to 2 clips) | 1.0 | Linear output gain. |
| Front / back vocal tract scaling | SpeakerParams | 0.5 .. 1.5 | 1.0 (female voices 1.05 .. 1.2) | Warps the spectral envelope; larger values sound like a longer vocal tract. |
| Breathiness | channel file `wavesynt -aspirampl` | 0 .. 4 | 0 | Aspiration noise mixed into the glottal source. |
| Spectral tilt | channel file `wavesynt -spectilt` | 0 .. 10 | 0 | Tilts the source spectrum; softer, darker voice. |
| Sample rate / inventory | OpenChannel audio format | 11025 or 8000 Hz | 11025 Hz | Picks the 11 kHz or the 8 kHz "telephone" unit inventory. |
| Gender | OpenChannel / SpeakerParams | 1 female, 2 male | per voice | Selects the inventory where one exists; otherwise only the pitch defaults differ. |
| E-mail preprocessing | channel file variant | on / off | off | Runs the `emupp` mail parser before the front end. |

The wrapper maps SAPI's rate (-10..10) onto the speed factor logarithmically (0.4 at +10,
3.0 at -10), SAPI's pitch onto a one-octave scaling of the pitch triple, and SAPI's volume
onto the engine volume. The configuration utility exposes each parameter as 0..100 percent
of the ranges above.

## Text controls

The front ends understand the SAPI 4 control tags of their era as well as the Bell Labs
escape sequences:

| Control | Meaning |
|---------|---------|
| `\Mrk=n\` | Bookmark; the engine reports its byte offset in the audio stream (used for SAPI bookmarks, word and sentence events). |
| `\Pau=ms\` | Pause. |
| `\Spd=wpm\` | Speed in words per minute. |
| `\Pit=Hz\` | Pitch override. |
| `\!si<ms>` | Silence. |
| `\!R<0.1..2.0>` | Rate multiplier for the following text. |
| `\!*H<0..64>` | Prominence (accent strength) of the next word. |
| `\\` | A literal backslash. |

`\Vol` is not supported by the engine (volume is a channel value), and the `\!w`
escape hangs the engine, so the wrapper escapes all backslashes in user text.

## How the wrapper works

* `LucentSAPI.dll` (x86 and x64, identical source) implements the SAPI 5 engine and
  voice enumerator. Each SAPI client process gets one `ttsserver.exe` child, started on
  first use with anonymous pipes; its stderr goes to the log folder.
* One engine channel is opened per language, sample rate and channel-file variant and
  kept for the life of the process (about 12 MB per language). Male and female voices of
  the same language share a channel where the inventory is the same.
* Text is converted to the language's code page, sanitised, and decorated with `\Mrk`
  markers for every SAPI bookmark, word and sentence; the engine returns one audio packet
  per sentence and the marker offsets are turned into SAPI events with sample accuracy.
* Cancellation sends the engine's discard command and stops feeding SAPI; through a real
  SpVoice a purge completes in about 20 ms.
* Rate, pitch and volume changes reach the engine as speaker parameters and set-value
  packets; nothing is resampled or post-processed.
* Seventeen SAPI voices are registered through a token enumerator: **Lucent Custom
  Voice** (all parameters from the configuration utility) plus the sixteen named voices.

## Installing

1. Download `LucentSAPI_Setup.exe` from the Releases page and run it as administrator.
2. Choose whether you want a desktop icon for the configuration utility.
3. Select a "Lucent ..." voice in your screen reader or SAPI application.

The installer puts the DLLs, the utility and the engine under `C:\Program Files\LucentSAPI`,
registers the 32-bit DLL with the 32-bit `regsvr32` view and the 64-bit DLL with the
64-bit view, records the install folder in `HKLM\Software\LucentSAPI` (both registry
views) and writes a full setup log to `%TEMP%\Setup Log <date> #<n>.txt`. Uninstalling
removes the registrations, the files and the generated channel files.

## SmartScreen, Defender and code signing

The first time you run `LucentSAPI_Setup.exe` Windows 11 shows a blue dialog headed
**"Windows protected your PC"**, with a **Run anyway** button hidden behind **More info**.
Some browsers add their own warning on the download. This is expected, it is not a virus
detection, and it does not mean the installer has been tampered with.

### What is actually happening

That dialog is **Microsoft Defender SmartScreen**, and it fires on *reputation*, not on
content. SmartScreen scores two things: whether the file is signed by a known publisher,
and how many people have downloaded that exact file without trouble. A brand-new
installer from a small project scores zero on both, so it is "unrecognized" and you get
the prompt. Microsoft Defender's actual antivirus scanner is a separate system, and it
reports this installer clean.

You can confirm that yourself before running anything:

```
"%ProgramFiles%\Windows Defender\MpCmdRun.exe" -Scan -ScanType 3 -File "%USERPROFILE%\Downloads\LucentSAPI_Setup.exe"
```

If Defender ever does report a threat name for this file, it is a false positive. Report
it at <https://www.microsoft.com/en-us/wdsi/filesubmission> — pick "Software developer",
attach the installer, and reference this repository. Microsoft usually corrects generic
machine-learning detections within a day or two, and the correction reaches every machine
through the normal definition update.

### What this project does about it

Nothing here removes the prompt outright, but everything that lowers a file's heuristic
score has been done:

* **Every shipped binary carries a full version resource.** Through 1.0.0 the two
  `LucentSAPI.dll` builds had a completely empty version resource and `Setup.exe` had a
  blank `FileVersion`. An unsigned, metadata-free DLL that registers itself as an
  in-process COM server is close to the shape Defender's machine-learning models are
  trained to distrust. `installer\verify_metadata.ps1` now fails the build if any shipped
  file loses its company, product, description or version string.
* **`LucentConfig.exe` ships a real application manifest** declaring `asInvoker` and
  supported Windows versions, so Windows stops treating it as a pre-Vista legacy binary
  and stops guessing at an executable whose name contains "Config".
* **The installer declares its publisher, copyright, support URL and version** in its own
  version resource instead of shipping blank fields.
* **The build is ready to sign.** Set one environment variable and `build_all.bat` signs
  the two DLLs, the utility, `Setup.exe` and the generated uninstaller.

### Signing it yourself

`installer\sign.ps1` runs on every build and does nothing unless a certificate is
configured. To sign, set **one** of these before running `build_all.bat`:

```
set LUCENT_SIGN_THUMBPRINT=<40 hex characters of a cert in your store>
```

```
set LUCENT_SIGN_PFX=C:\path\to\cert.pfx
set LUCENT_SIGN_PASS=<pfx password>
```

Optionally `LUCENT_SIGN_TS` to pick a different RFC3161 timestamp server. The script
finds `signtool.exe` in the Windows SDK, signs with SHA-256 and timestamps, and prints
the resulting signature status.

### What signing does and does not buy you

Be clear-eyed about this before spending money:

| | First-download behaviour |
| --- | --- |
| Unsigned (what ships today) | "Windows protected your PC"; user clicks **Run anyway** |
| **Self-signed** | **Identical to unsigned** — Windows does not trust the root, so this buys nothing and is for local testing only |
| OV certificate (~$150–300/yr) | Still warns, but names a verified publisher, and reputation carries across releases |
| EV certificate ($400+/yr) | **Same as OV.** EV stopped bypassing SmartScreen in 2024 |
| Microsoft Store (MSIX) | No warning at all |

There is no longer any certificate that silences SmartScreen on day one, and there is no
form to submit a file for consumer SmartScreen review — reputation accumulates only from
download volume, over weeks. What a certificate does buy is *continuity*: an unsigned
release starts from zero reputation every single time, while releases signed with one
consistent identity let reputation build up across versions.

For an open-source project like this one, [SignPath Foundation](https://signpath.io)
provides free OV-level signing to qualifying projects, and Microsoft's
[Azure Artifact Signing](https://learn.microsoft.com/en-us/azure/trusted-signing/) is
about $9.99/month (individuals: USA and Canada only). Either would plug straight into
`LUCENT_SIGN_THUMBPRINT`.

Microsoft's own documentation on the topic:
[code signing options](https://learn.microsoft.com/en-us/windows/apps/package-and-deploy/code-signing-options)
and
[SmartScreen reputation](https://learn.microsoft.com/en-us/windows/apps/package-and-deploy/smartscreen-reputation).

## Configuration utility

`LucentConfig.exe` edits `%APPDATA%\LucentSAPI\settings.ini`. The custom voice re-reads
the file on every utterance, so changes reach a running screen reader immediately.

* **Language**, **Voice**, **Sample rate** (11025 Hz standard or 8000 Hz telephone),
  **E-mail preprocessing**.
* **Speaking speed, top pitch, reference line pitch, bottom pitch** (labels show the
  resulting factor or frequency).
* **Breathiness, spectral tilt, front and back vocal tract**.
* **Volume**.
* **Let SAPI applications adjust rate, pitch and volume on top of these settings** (off by
  default: the utility is then the only control of the custom voice).
* **Write log files**, **Keep engine diagnostic log**.
* **Test text**, **Test** and **Stop** (renders through the engine and plays the result),
  **Restore defaults**, **Open log folder**, **Close**.

Every value is a spin edit from 0 to 100, where 0 is the lowest and 100 the highest value
the engine allows. All 22 controls are labelled and in the tab order; the dialog was
verified with MSAA, which is what NVDA and JAWS use for Win32 dialogs.

## Logs and troubleshooting

* Wrapper and utility logs: `%LOCALAPPDATA%\LucentSAPI\logs\<program>_<component>_<arch>_<pid>.log`
* Engine diagnostics: `%LOCALAPPDATA%\LucentSAPI\logs\engine_<pid>.log`
* Generated channel files: `%LOCALAPPDATA%\LucentSAPI\channels`
* Installer log: `%TEMP%\Setup Log <date> #<n>.txt`

If a voice is silent, the engine log names the module that rejected its options. If a
voice whispers, an intonation data-bank path is wrong (see the quirks above).

## Building from source

Requirements: Visual Studio 2022 Build Tools with the x86 and x64 C++ toolsets, CMake 3.15
or later, Inno Setup 6 (the build script looks in `%LOCALAPPDATA%\Programs\Inno Setup 6`
and `%ProgramFiles(x86)%\Inno Setup 6`), and the original Lucent 3.x2 files placed as
`bin\Lucent_3x2_TTS_bin\ttsserver.exe` and `bin\Lucent_3x2_TTS_data\{languages,chfiles}`.

```bat
powershell -ExecutionPolicy Bypass -File installer\stage_engine.ps1
build_all.bat
```

`build_all.bat` builds both architectures, runs the engine self-test with each, and
produces `output\LucentSAPI_Setup.exe`. `build_arch.bat x86|x64 [target]` builds one
architecture.

## Testing

* `engine_test <engineDir> <outDir> [filter]` speaks with every voice straight through the
  engine client and checks bookmarks, cancellation, long and empty texts. Run it against an
  engine folder whose path contains a space before shipping.
* `sapi_test <LucentSAPI.dll> <outDir> [voiceIndex...]` registers the CLSIDs under HKCU (no
  administrator needed), renders every voice through a real SpVoice to a WAV file and to the
  audio device, and checks word, sentence and bookmark events and purge latency.
* `a11y_dump LucentConfig.exe` walks the configuration dialog with MSAA and fails on any
  unlabelled control or duplicate access key.

## Repository layout

```
src/            engine client, settings, SAPI engine + enumerator, configuration utility
test/           engine_test, sapi_test, a11y_dump
installer/      Inno Setup script and the engine staging script
docs/           PROTOCOL.md — the recovered engine protocol
CMakeLists.txt  both architectures from one tree
build_all.bat   full build including the installer
```

## Credits and license

The wrapper is released under the BSD 3-Clause license (see `LICENSE`); the people and
projects it builds on are listed in `CREDITS.md`. The Lucent Articulator engine, its
language data and voices are the property of their respective owners and are provided in
the installer for preservation of an otherwise unusable abandonware product; they are not
covered by the wrapper's license.
