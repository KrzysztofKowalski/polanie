// PORT: mapa utworow CD (PlayTrack(n)) na dodatkowe pliki .mid (fanowskie
// aranżacje, katalog wskazywany przez POLANIE_MIDI). Wydzielona jak
// track_map.h - czysta tabela + logika szukania pliku, linkowalna w testach.
//
// Fallback toru (decyzja projektowa): gdy utwor ma wpis tutaj I plik .mid
// istnieje na dysku - gramy MIDI przez sfizz; inaczej (i przy --audioType=s3m)
// - modul S3M z kTrackMap (libopenmpt), jak dotad.
//
// Przypisania (uzasadnienie w raporty/tor-midi-sfizz.md):
//   2  menu          -> LUZYCE.MID      (55 BPM, spokojny hymn traba+struny;
//                        jedyny wolny wieloglosowy utwor fanowski -> menu)
//   3  teksty fabuly -> marzenia.mid    (99 BPM, "marzenia" = klimat narracji;
//                        gra raz, jak oryginalny tor tekstow)
//   6-14 plansze     -> "muzyka z lasu plyneca" / "wymarsz na wojne"
//                        na zmiane: las = spokojniejsze plansze, wymarsz =
//                        marsz bojowy; wszystkie zapetlane (jak plansze S3M)
//   4  porazka       -> brak .mid (S3M "dark death" pasuje tematycznie)
//   5  zwyciestwo    -> brak .mid (S3M "wiktor")
//   LUZYCE2.MID (66 nut, jeden glos) - nie podpiety: za cienki na plansze;
//                        wpis zostaje w tabeli jako przyklad konfiguracji.
//
// Dopasowanie plikow: nazwy fanowskich .mid maja mieszane kodowanie znakow
// ("wymarsz na wojn?.mid"), wiec szukamy po ASCII-podciagu (needle) z
// porownaniem bez wielkosci liter, np. "wojn" trafia w "wymarsz na wojn?.mid".
#ifndef POL_MIDI_MAP_H
#define POL_MIDI_MAP_H

#include <cstddef>

struct MidiTrackMap {
  int cd;           // numer utworu z PlayTrack(n)
  const char *dir;  // podkatalog w korzeniu utworow MIDI (POLANIE_MIDI)
  const char *needle; // ASCII-podciag nazwy pliku (case-insensitive)
  int loop;         // zapetlac po zakonczeniu?
  const char *info; // uzasadnienie / komentarz
};

static const MidiTrackMap kMidiTrackMap[] = {
    {2, "luzyce", "luzyce.", 1,
     "menu: spokojny hymn Luczycow (55 BPM, trabka+struny)"},
    {3, "utwory_mid", "marzenia", 0,
     "teksty fabuly: 'marzenia' (99 BPM, gra raz jak tor tekstow S3M)"},
    {6, "utwory_mid", "lasu", 1, "plansza: 'muzyka z lasu plyneca' (spokojna)"},
    {7, "utwory_mid", "wojn", 1, "plansza: 'wymarsz na wojne' (marsz bojowy)"},
    {8, "utwory_mid", "lasu", 1, "plansza: las"},
    {9, "utwory_mid", "wojn", 1, "plansza: wymarsz"},
    {10, "utwory_mid", "lasu", 1, "plansza: las"},
    {11, "utwory_mid", "wojn", 1, "plansza: wymarsz"},
    {12, "utwory_mid", "wojn", 1, "plansza: wymarsz"},
    {13, "utwory_mid", "wojn", 1, "plansza: wymarsz"},
    {14, "utwory_mid", "lasu", 1, "plansza: las"},
    // LUZYCE2.MID - "Hymn Luczycow" w jednym glosie; do ewentualnego podpiecia
    // np. zwyciestwa: {5, "luzyce_2", "luzyce2.", 0, "hymn Luczycow"},
};

static const MidiTrackMap *find_midi_track(int cd) {
  for (size_t i = 0; i < sizeof(kMidiTrackMap) / sizeof(kMidiTrackMap[0]); i++)
    if (kMidiTrackMap[i].cd == cd)
      return &kMidiTrackMap[i];
  return NULL;
}

// ASCII-porownanie bez wielkosci liter (bez locale - nazwy plikow sa bajtowe).
static int ascii_nocase_find(const char *hay, const char *needle) {
  if (!hay || !needle)
    return 0;
  auto low = [](char c) {
    return (c >= 'A' && c <= 'Z') ? (char)(c - 'A' + 'a') : c;
  };
  for (const char *h = hay; *h; h++) {
    const char *a = h, *b = needle;
    while (*b && low(*a) == low(*b)) {
      a++;
      b++;
    }
    if (!*b)
      return 1;
    if (!*a)
      return 0;
  }
  return 0;
}

#endif // POL_MIDI_MAP_H