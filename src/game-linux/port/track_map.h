// PORT: mapowanie utworow CD (PlayTrack(n) z cd.h) na moduly S3M.
// Wydzielone z port_audio.cpp dla testow jednostkowych
// (game-linux/tests/test_audio.cpp) - sama tabela, bez SDL/libopenmpt.
//
// Oryginal gral CD-Audio (MSCDEX, game/cd.cpp:523 PlayTrack); moduly S3M
// w GRAF/GRAF.001-018 to kompozycje na te sama muzyke. Kotwice z tytulow
// modulow (naglowek S3M):
//   GRAF_002 "menu", GRAF_003 "intro", GRAF_005 "history",
//   GRAF_006 "wiktor", GRAF_007 "dark death",
//   GRAF_012/014 "plansza 2", GRAF_013 "plansza3", GRAF_016 "plansza6",
//   GRAF_017 "plansza7", GRAF_018 "plansza goralska".
// Stale z game/battle.cpp:20-23: TRACK_MENU=2, TRACK_TXT=3, TRACK_DEFEAT=4,
// TRACK_VICTORY=5; plansze = Track[52] (game/battle.cpp:82) o wartosciach
// 6-14. Ktore "plansze N" odpowiadaja ktorym numerom utworow CD - hipoteza
// (kolejnosc tytulow), do weryfikacji na sluch; tablica jest w jednym miejscu.
// Zapetlamy: menu (2), plansze (6-14) i teksty (3) - modul S3M jest krotszy
// niz sekwencja napisow, a oryginalny utwor CD trwal minuty (gral do konca
// plyty), wiec bez petli po paru nutach nastepowala cisza; StopPlaying()
// po napisach (battle.cpp:247) i tak go ucina. Porazka/zwyciestwo raz.
#ifndef POL_TRACK_MAP_H
#define POL_TRACK_MAP_H

struct TrackMap {
  int cd;     // numer utworu z PlayTrack(n)
  int module; // GRAF_0xx.s3m
  int loop;   // zapetlac po zakonczeniu?
};

static const TrackMap kTrackMap[] = {
    {2, 2, 1},  {3, 3, 1},   {4, 7, 0},  {5, 6, 0},  // menu/txt/defeat/victory
    {6, 12, 1}, {7, 13, 1},  {8, 14, 1}, {9, 16, 1}, // plansze
    {10, 17, 1}, {11, 18, 1}, {12, 4, 1}, {13, 11, 1}, {14, 15, 1},
};

static const TrackMap *find_track(int cd) {
  for (size_t i = 0; i < sizeof(kTrackMap) / sizeof(kTrackMap[0]); i++)
    if (kTrackMap[i].cd == cd)
      return &kTrackMap[i];
  return NULL;
}

#endif // POL_TRACK_MAP_H