// PORT: parsowanie opcji CLI toru audio (--audioType=s3m|sfz|mt32|auto).
// Header-only, bez zaleznosci - testowalny w gtest bez linkowania warstwy SDL.
//
// Uzycie w src/main.cpp: przegladamy argv i kazdy argument odsylamy tu;
// wartosc >= 0 ustawiamy przez POL_SetAudioType (port_audio.h).
#ifndef POL_AUDIO_OPTS_H
#define POL_AUDIO_OPTS_H

// Tryby toru muzyki (zgodne z POL_AUDIO_* w port_audio.h).
// PORT: decyzja usera 2026-09-04 — VSCO niestabilne, domyślnie S3M (jawne
// auto/sfz nadal działa); AUTO tylko przy --audioType=auto|--audioType=midi.
// PORT: mt32 (FluidSynth + soundfont MT-32 Hedsound) też tylko jawnie —
// tor ciężki (sfload 447 MiB do RAM) i wymaga assets/soundfont/*GM.sf2.
enum PolAudioType {
  POL_AUDIO_AUTO = 0,  // --audioType=auto: .mid tam gdzie jest, inaczej S3M
  POL_AUDIO_S3M = 1,   // wymuszony tor S3M (libopenmpt); domyślny bez flagi
  POL_AUDIO_SFZ = 2,   // wymuszony tor MIDI+sfizz (VSCO CE)
  POL_AUDIO_MT32 = 3,  // wymuszony tor MIDI+FluidSynth (soundfont MT-32)
};

// Parsuje jeden argument argv. Zwraca:
//   >= 0 - wartosc PolAudioType (argument rozpoznany)
//   -1   - argument inny (nie nasz; przegladajacy argv ma go pominac)
//   -2   - argument --audioType, ale bledna wartosc (caller wypisuje help)
static inline int pol_parse_audio_type_arg(const char *arg) {
  if (!arg)
    return -1;
  // prefiks "--audioType" (case-insensitive jak reszta portu)
  const char *p = arg;
  auto low = [](char c) {
    return (c >= 'A' && c <= 'Z') ? (char)(c - 'A' + 'a') : c;
  };
  const char *pref = "--audiotype";
  for (const char *q = pref; *q; q++, p++) {
    if (!*p || low(*p) != *q)
      return -1;
  }
  if (*p == 0)
    return POL_AUDIO_AUTO; // samo "--audioType" = auto
  if (*p != '=')
    return -1; // np. "--audioTypeX"
  p++;
  auto eq = [&](const char *v) {
    for (const char *a = p, *b = v; *a || *b; a++, b++)
      if (low(*a) != *b)
        return false;
    return true;
  };
  if (eq("s3m"))
    return POL_AUDIO_S3M;
  if (eq("sfz"))
    return POL_AUDIO_SFZ;
  if (eq("mt32") || eq("mt32sfz") || eq("mt32-sfz")) // aliasy nazwy
    return POL_AUDIO_MT32;
  if (eq("auto") || eq("midi"))
    return POL_AUDIO_AUTO;
  return -2;
}

#endif // POL_AUDIO_OPTS_H