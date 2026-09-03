# Credits

## The engine

* **AT&T Bell Laboratories** — the Bell Labs Text-to-Speech system (the front ends,
  the three-line intonation model, the unit inventories and the waveform synthesizer) that
  this engine descends from.
* **Lucent Technologies** — Lucent Articulator 4.0 / "Lucent 3.x2 TTS" (build 010309,
  2001), the packaged engine, its language data and voices. All rights in the engine and
  the data remain with their owners; nothing of it is contained in this repository.

## Wrapper code

* **Gozaltech** — the BestSpeech SAPI 5 wrapper whose COM helpers, registry wrapper,
  SAPI token enumerator and engine-object skeleton this project started from
  (BSD-licensed; see `LICENSE`).
* **Josh Kennedy (joshknnd1982)** — the reverse engineering of the engine protocol, the
  engine client, the SAPI 5 engine, the configuration utility, the tests and the installer.

## Tools and libraries used

* Microsoft Speech API 5 SDK headers (Windows SDK).
* Microsoft Visual Studio 2022 Build Tools and CMake.
* Inno Setup 6 by Jordan Russell and Martijn Laan.
* Capstone and pefile (Python), used during the reverse engineering of `ttsserv.exe` and
  `ttsserver.exe`; no code from them is part of the wrapper.
