/*
 Plik : CD.CPP (oryginal: Artur Bidzinski, 1996 - MSCDEX/int 2Fh)
 PORT: wersja Linux - muzyka CD-Audio zastapiona modulami S3M odtwarzanymi
       przez libopenmpt (warstwa port/port_audio.cpp). PlayTrack(n) -> modul
       z extracted/audio/muzyka/GRAF_0xx.s3m (mapowanie w kTrackMap).
       Interfejs zgodny z game/cd.h - reszta kodu gry bez zmian.
*/

#include <stdlib.h>
#include "cd.h"
#include "polshim.h"
#include "port.h" // docelowo POL_Audio*

int CD_MaxTrack = 1;
int CD_CurrentTrack = 0;
int CD_AudioOn = 0;
int CD_Volume = 5;

// PORT: ostatnio wybrany utwor (uzywany przez battle.cpp "PlayTrack(track)")
int track = 2;

static int is_playing = 0;

int InitCD(void) {
  // PORT: bez urzadzenia CD - otwieramy tylko strumien audio (mikser)
  if (!POL_AudioInit())
    CD_CurrentTrack = 0;
  return 0; // 0 = OK (w oryginale 1 = blad)
}

void DeInitCD(void) { StopPlaying(); }

void ReadNrOfTracks(void) {
  // PORT: numer utworu = numer zadany przez gre (katalogi CD w cd.track[]
  // nie istnieja); bez pliku TOC - pominiete
}

void SetMaxTrack(int t) { CD_MaxTrack = t; }

int GetCurrentTrack(void) { return CD_CurrentTrack; }

int PlayTrack(int track) {
  // PORT: oryginal gral CD-Audio (game/cd.cpp:523); tu modul S3M.
  if (!CD_AudioOn) // BigActiveCDAudio/ActiveCDAudio z oryginalu -> jedna flaga
    return 0;
  if (track > CD_MaxTrack)
    track = CD_MaxTrack; // jak game/cd.cpp:531 (obciecie do ostatniego utworu)
  if (POL_MusicPlay(track)) {
    CD_CurrentTrack = track;
    is_playing = 1;
  }
  return 0;
}

int StopPlaying(void) {
  if (!CD_AudioOn)
    return 0; // jak oryginal: przy wylaczonym audio nic nie robimy
  POL_MusicStop();
  is_playing = 0;
  return 0;
}

void CheckCD(void) {
  // w oryginale: gdy plyta gra inny utwor niz zadany, nawraca PlayTrack
  // (game/cd.cpp:625-634). W porcie mikser sam trzyma wlasciwy utwor
  // (zapetlanie w callbacku), a niezapetlane (teksty/porazka/zwyciestwo)
  // maja po skonczeniu zamilknac - jak CD po koncu plyty. Wiec: no-op.
}

int PlayNext(void) {
  // jak game/cd.cpp:577-597: nastepny utwor, po ostatnim zawrotka na 5
  int result = CD_CurrentTrack;
  if (result < CD_MaxTrack)
    result++;
  else
    result = 5;
  return PlayTrack(result);
}
int PlayPrevious(void) {
  // jak game/cd.cpp:599-617: poprzedni utwor, ponizej 5 -> ostatni
  int result = CD_CurrentTrack;
  if (result > 5)
    result--;
  else
    result = CD_MaxTrack;
  return PlayTrack(result);
}

void setVolume(int v) {
  if (v < 0)
    v = 0;
  if (v > 5)
    v = 5;
  CD_Volume = v;
  POL_MusicSetVolume(v); // PORT: glosnosc muzyki (skala 0-5 jak oryginal)
}

int getVolume(void) { return CD_Volume; }

void OnCDAudio(void) { CD_AudioOn = 1; }
void OffCDAudio(void) { CD_AudioOn = 0; }
void BigOnCDAudio(void) { OnCDAudio(); }
void BigOffCDAudio(void) { OffCDAudio(); }

void FreeCDBuffer(void) {}