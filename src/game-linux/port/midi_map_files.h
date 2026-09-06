// PORT: naglowek pomocniczy do midi_map_files.cpp (szukanie .mid i katalogu
// vsco). Rozdzielony od midi_map.h (czysta tabela), zeby testy mogly linkowac
// sama logike plikowa albo sama tabele.
#ifndef POL_MIDI_MAP_FILES_H
#define POL_MIDI_MAP_FILES_H

#include <cstddef>

// Szuka w <katalog fanowskich .mid>/<subdir> pliku, ktorego nazwa zawiera
// ASCII-podciag needle (bez wielkosci liter). Zwraca 1 i pelna sciezke w out
// przy trafieniu; 0 gdy katalog/needle niedostepne.
int POL_MidiFindFile(const char *subdir, const char *needle, char *out,
                     size_t outsz);

// Katalog soundfontow VSCO CE (symlink vsco/, zmienna POLANIE_VSCO);
// "" gdy niewykryty. Marker: GM-StylePerc.sfz (jest tylko w VSCO CE).
const char *POL_VscoDir(void);

// Sciezka soundfontu MT32 (Hedsound, *GM.sf2) dla toru mt32 (FluidSynth).
// Zmienna POLANIE_MT32SF2 (pelna sciezka pliku albo katalog z *GM.sf2) albo
// kandydaci "assets/soundfont" wzgledem CWD (te same zaglebienia co vsco/).
// "" gdy nie znaleziono. Wariant GM: nazwa pliku .sf2 zawiera "GM".
const char *POL_Mt32Sf2Path(void);

// Katalog z wygenerowanymi grymu WAV-ami toru MT32 (GRAF_NNN.mt32.wav) dla
// trybu "zamiast MIDI WAV leci jako muzyka" (export scripts/audio/).
// POLANIE_MT32_WAVDIR albo automat: "ephemeral/mt32-wav" (kandydaci jak vsco).
// "" gdy nie znaleziono (wtedy tor mt32 gra MIDI, jak dotad).
const char *POL_Mt32WavDir(void);

// MAPA INI: numer utworu gry (PlayTrack n) -> plik WAV. Czyta
// 'mt32-wav-map.ini' w katalogu WAVDIR (albo $POLANIE_MT32WAVMAP - pelna
// sciezka do pliku ini); wiersze: '<numer> = <plik>' ('#'/';' = komentarz;
// sciezka bezwzgledna albo wzgledna WAVDIR). Brak wpisu/ini -> domyslne
// '<wavdir>/GRAF_<modul>.mt32.wav' (modul wg kTrackMap). Zwraca 1 gdy out
// zapelnione (pelna sciezka), 0 gdy brak WAVDIR.
int POL_Mt32WavMapTrack(int track, int module, char *out, size_t outsz);

#endif // POL_MIDI_MAP_FILES_H