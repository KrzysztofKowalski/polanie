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

#endif // POL_MIDI_MAP_FILES_H