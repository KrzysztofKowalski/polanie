// PORT: interfejs warstwy audio portu (game-linux) - SDL3 + libopenmpt.
// Jeden strumien SDL3 z wlasnym callbackiem miksera (port_audio.cpp):
// muzyka (S3M przez libopenmpt, render na biezaco) + glosy efektow WAV
// miksowane addytywnie do F32 stereo. Implementacja w port/port_audio.cpp.
#ifndef POL_PORT_AUDIO_H
#define POL_PORT_AUDIO_H

#ifdef __cplusplus
extern "C" {
#endif

// ---------- zarzadzanie ----------
// Otwiera strumien audio (F32 stereo 48 kHz) i startuje mikser.
// Idempotentne; wywolywane tez lazy przy pierwszym Play*.
int POL_MixerInit(void);
// Zamyka strumien, zwalnia modul muzyczny i glosy.
void POL_MixerShutdown(void);

// ---------- tor muzyki: S3M / MIDI+sfizz / auto ----------
// PORT: wybor toru muzyki. AUTO (default): gdy dla numeru utworu istnieje
// skojarzony plik .mid (port/midi_map.h, fanowskie utwory Fornalskiego) -
// gramy MIDI przez sfizz (VSCO CE); inaczej modul S3M (libopenmpt).
// S3M/SFZ wymuszaja tor (CLI --audioType=..., patrz port/audio_opts.h).
void POL_SetAudioType(int t); // POL_AUDIO_AUTO / POL_AUDIO_S3M / POL_AUDIO_SFZ
int POL_GetAudioType(void);

// ---------- muzyka (zamiennik CD-Audio) ----------
// PlayTrack(n) z cd.h: n=2 menu, 3 teksty fabuly, 4 porazka, 5 zwyciestwo,
// 6-14 plansze. Zwraca 1, gdy utwor wystartowal. Plansze (6-14) i menu (2)
// sa zapetlone; teksty/porazka/zwyciestwo graja raz (jak oryginal: od
// danego utworu do konca plyty, z StopPlaying po tekscie).
int POL_MusicPlay(int track);
int POL_MusicStop(void);   // StopPlaying()
int POL_MusicPlaying(void);
int POL_MusicCurrent(void); // ostatnio zadany numer utworu CD (dla CheckCD)
// Glosnosc muzyki 0-5 (setVolume()/getVolume() z cd.h - ta sama skala).
void POL_MusicSetVolume(int v);
int POL_MusicGetVolume(void);

// ---------- efekty / mowa (WAV) ----------
// Odtwarza plik RIFF WAV (8-bit mono 22050 Hz, takze .dat z kopi danych)
// jako jeden z glosow miksera - NIE wycisza muzyki ani innych glosow.
// Sciezki DOS ("data\\W001.dat") rozwiazuje przez POL_fopen + katalog
// ekstraktu (extracted/audio/dzwieki/W001.wav). Zwraca 1 przy sukcesie.
int POL_SfxPlay(const char *path);
void POL_SfxStopAll(void); // EndPlayWav(): ucina wszystkie glosy efektow
int POL_SfxPlaying(void);
// Glosnosc efektow 0-5 (gra nie ma osobnego ustawienia - zapas na opcje).
void POL_SfxSetVolume(int v);
int POL_SfxGetVolume(void);

// PORT: diagnostyka/testy - rozwiazanie sciezki efektu jak w POL_SfxPlay,
// ale bez grania (katalog danych, potem ekstrakt; NULL gdy sie nie udalo).
const char *POL_SfxResolve(const char *dos);

// ---------- globalny zbior probek (SOUND.DAT z pelnej wersji gry) ----------
// Kontener jak w oryginale (game/menegdma.cpp:733-787): [surowe PCM 8-bit
// unsigned mono 22050 Hz][count x int32 LE - dlugosci probek na koncu].
// Wczytywany przez MENEGERDMA::LoadGlobalData (glosy jednostek 1-183).
// Zwraca 0 przy sukcesie; idempotentne (drugi load = 0, bez ponownego
// czytania). Gdy pliku nie ma - blad, a glosy moga padac z plikow W0nn.
int POL_SfxLoadGlobalSet(const char *path, int count);
// Gra probe nr (0-based, jak MENEGERDMA::operator()) z wczytanego zbioru.
// 1 = zagrano, 0 = zbior nie wczytany / numer poza zakresem.
int POL_SfxPlayGlobalSet(int index);
int POL_SfxGlobalSetCount(void); // ile probek w zbiorze (0 = brak)

// ---------- glosy z pamieci (dla probek spoza plikow) ----------
// Gra probe (mono F32, rate Hz) jako jeden z glosow miksera. Dane sa
// trwale (cache wywolujacego) - mikser tylko czyta. 1 = zagrano.
int POL_SfxPlayPCM(const float *pcm, size_t len, int rate);

// ---------- kompatybilnosc: stare API z port.h (src/menegdma.cpp) ----------
int POL_AudioInit(void);
void POL_AudioShutdown(void);
int POL_AudioPlayFile(const char *path); // = POL_SfxPlay
void POL_AudioStop(void);                // = POL_SfxStopAll
int POL_AudioPlaying(void);              // = POL_SfxPlaying

#ifdef __cplusplus
}
#endif

#endif // POL_PORT_AUDIO_H