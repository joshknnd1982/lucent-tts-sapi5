#pragma once
//
// Repairs for engine text that a Lucent front end refuses to synthesise, and the formatting
// of the control tags the wrapper adds to that text.
//
// Some front ends reject an utterance outright rather than skipping the part they cannot
// handle: the module chain fails ("[fslmlfe.compose] ... resulted in an empty machine",
// "aChannel :: processNextModule failed"), the engine reports a clean end of stream after
// zero audio bytes, and the caller hears nothing at all. For a screen reader that is the
// worst possible outcome, so the wrapper retries a rejected utterance with progressively
// simpler text instead of passing the silence on.
//
// Measured against the shipped data (see test/french_probe4.txt):
//
//   * Standard French (fslmlfrend) rejects '%', '^' and '|' anywhere in the text; rejects
//     an utterance whose final token is punctuation unless it is terminated by '.', '!'
//     or '?'; and rejects a few ordinary tokens outright, "Alt" and "Al" among them.
//   * Italian rejects '`' and '|' the same way.
//   * German and US English rejected nothing in the same 106-case sweep.
//
// The repairs are therefore reactive, not preventive: text is sent unchanged first, and
// these functions only come into play once the engine has already returned nothing.
//
#include <string>
#include <vector>

namespace lucent {

// True when the text holds anything the engine could pronounce - a letter or digit
// outside a \Tag\ control sequence. Text without it is legitimately silent (a lone
// bookmark, say) and must not be retried.
bool hasSpeakableContent(const std::string& engineText);

// Rewrites engine text so a fragile front end will accept it, preserving \Tag\ control
// sequences and escaped backslashes untouched.
//
//   pass 1  drop the symbols that are rejected wherever they appear, and terminate the
//           utterance with '.' when it does not already end in sentence punctuation.
//   pass 2  additionally reduce to letters, digits, whitespace, intra-word apostrophes
//           and hyphens, and sentence punctuation.
//   pass 3  additionally fold A-Z to lower case. "Alt" and "Al" are rejected where "alt"
//           and "al" are accepted, so this rescues a token nothing else can.
//
// `codePage` is the encoding the text is already in. Under a double-byte code page a
// trail byte can share a value with an ASCII letter or symbol (Big5 trail bytes run
// 0x40-0x7E), so rewriting individual bytes would corrupt characters; for those the text
// is only terminated, never filtered. No double-byte language failed the sweep.
//
// Returns an empty string when the pass leaves the text unchanged, so the caller can skip
// a pointless retry.
std::string repairText(const std::string& engineText, int pass, unsigned int codePage);

// Splits engine text into individually speakable pieces - one word each, carrying any
// control tags that preceded it - for the last-resort retry. A single rejected token then
// costs one word instead of the whole utterance.
std::vector<std::string> splitIntoWords(const std::string& engineText);

// Formats a numeric control tag the wrapper adds to engine text - " \Mrk=7\ ", " \Pau=250\ " -
// with a space on each side, so it never runs into the text around it.
//
// On the e-mail preprocessing channels the emupp module runs ahead of the front end and
// decodes the quoted-printable sequence "=20" into a space wherever it appears, control tags
// included. "\Mrk=20\" reaches the front end as "\Mrk \", which is no longer a tag, so it is
// read aloud - "backslash M R K backslash" - and bookmark 20 never fires. Every value whose
// decimal form starts with 20 is hit: 20, 200-209, 2000-2099 and so on. "=20" is the only
// sequence emupp decodes ("=3D", "=41" and "=09" pass through), and the front ends read the
// number with leading zeros - "\Mrk=020\" is bookmark 20 on every channel - so those values
// get one and all others are written as they always were.
std::string numericTag(const char* name, unsigned long value);

}  // namespace lucent
