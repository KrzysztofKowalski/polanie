// PORT: parser kontenera SOUND.DAT (pelna wersja gry) - celowo bez SDL,
// zeby testy jednostkowe mogly linkowac ten plik osobno (bez urzadzenia
// audio). Kontener jak w oryginale (game/menegdma.cpp:733-787):
//   [surowe probki 8-bit unsigned mono 22050 Hz, sklejone]
//   [na koncu: count x int32 LE z dlugosciami kolejnych probek]
// W pelnej wersji gry SOUND.DAT trzyma WSZYSTKIE 183 glosy jednostek;
// kopi demo ma tylko pliki DATA/W001-W055.DAT, wiec bez tego kontenera
// glosy o numerach > 55 nie mialy skad sie wziac (zgloszony problem:
// "muzyka gra, efekty nie").
#ifndef POL_SOUND_DAT_H
#define POL_SOUND_DAT_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// Wczytuje kontener SOUND.DAT do wewnetrznego cache (probki jako F32 mono,
// -1..1; 0x80 = cisza). Zwraca 0 przy sukcesie; idempotentne (drugi load
// tej samej liczby probek = 0, bez ponownego czytania). Gdy pliku nie ma,
// zwraca blad - wywolujacy moze padac glosy z plikow W0nn (kopia demo).
// Kandydaci pliku: podana sciezka (przez POL_fopen - katalog danych),
// <ekstrakt>/audio/dzwieki/sound.dat, dane pelnej wersji CD (katalog
// ephemeral/cd - opcjonalny instalator scripts/install.sh --cd; wtedy tor
// efektow ma pelny zestaw probek).
int POL_SoundDatLoad(const char *path, int count);

int POL_SoundDatCount(void); // liczba probek w cache (0 = nic nie wczytano)

// Probka nr (0-based) jako F32 mono; zwraca NULL poza zakresem.
const float *POL_SoundDatSample(int index, size_t *len);

#ifdef __cplusplus
}
#endif

#endif // POL_SOUND_DAT_H