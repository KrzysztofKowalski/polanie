// PORT: silnik muzyki MIDI+sfizz (tor sfz). Odpowiada za:
//   - normalizacje .sfz z VSCO CE (backslashe Windows -> '/') przy wczytywaniu
//     do pamieci (sfizz_load_string z wirtualna sciezka w katalogu vsco - bez
//     generowania kopii plikow na dysku),
//   - jeden syntezator sfizz na kanal MIDI (program change -> plik .sfz wg
//     kSfzMap z sfz_map.h; kanal 10 -> GM-StylePerc.sfz),
//   - zaplanowanie zdarzen z parsera SMF (polmidi::parse_smf) na probkach
//     48 kHz i ich wysylanie w trakcie renderu (sfizz: wysylanie = RT,
//     ladowanie .sfz = CT przed startem utworu),
//   - zapetlanie utworow planszowych (jak tor S3M).
// Wszystko renderuje do podanego bufora F32 interleaved stereo - mikser
// port_audio.cpp dodaje to do muzyki/efektow. Stan chroni mutex wlasciciela
// (port_audio.cpp wywoluje pod mx).
#ifndef POL_SFZ_ENGINE_H
#define POL_SFZ_ENGINE_H

#include <cstddef>

#ifdef __cplusplus
extern "C" {
#endif

// Inicjalizacja (leniwa; idempotentna). rate = czestotliwosc miksera.
// 0 = OK, 1 = blad (np. brak vsco wykryty przy pierwszym utworze).
int POL_SfzEngineInit(float rate);

// Zwolnienie syntezatorow (wywolywane z POL_MixerShutdown).
void POL_SfzEngineShutdown(void);

// Dwie fazy (PORT: ladowanie VSCO trwa sekundy i nie moze blokowac callbacku
// audio, wiec przygotowanie biegnie bez mutexa miksera, a podmiana stanu pod
// mutexem wlasciciela):
//   POL_SfzEnginePrepare - parsuje SMF, laduje syntezatory kanalow do stanu
//       oczekujacego (stary utwor dalej gra). Zwraca liczbe regionow (>0) albo
//       -1 (blad; wtedy wywolac Abort).
//   POL_SfzEngineCommit  - pod mutexem miksera: zwalnia stare syntezatory,
//       instaluje przygotowane, ustawia loop i startuje odtwarzanie.
//   POL_SfzEngineAbort   - zwalnia stan oczekujacy (blad wczytywania).
int POL_SfzEnginePrepare(const unsigned char *mid, size_t len,
                         const char *vsco_dir, const char *name);
void POL_SfzEngineCommit(int loop);
void POL_SfzEngineAbort(void);

// Ucina utwor i zwalnia syntezatory kanalow.
void POL_SfzEngineStop(void);

int POL_SfzEnginePlaying(void);
// Nazwa pliku biezacego utworu (albo ""; statyczny bufor).
const char *POL_SfzEngineCurrentName(void);

// Render aktywnego utworu: rozsyla zdarzenia do sfizz (delay-ordered w
// podblokach 256 probek) i DODAJE wyjscie syntezatorow * gain do out
// (F32 interleaved stereo, frames klatek). Kontrakt zwrotki (liczba KONCOWIE
// wypelnionych klatek; PORT-naprawa 2026-09-03 po retestach usera):
//   - utwor konczy sie (cursor >= end + tail) W SRODKU wywolania ->
//     zwrotka < frames; wywolujacy (mix_cb) zeruje reszte bufora,
//   - koniec+ogon wypada DOKLADNIE na granicy wywolania -> zwrotka = frames
//     (ogon wyrenderowany w calosci, nic nie ginie),
//   - kolejne wywolanie po dezaktywacji -> 0 (POL_SfzEnginePlaying()==0).
// Wczesniej mikser zakladal, ze caly bufor jest obsluzony (zwrotka 1), a ogon
// z nieaktualnymi probkami szedl do urzadzenia = artefakty "przedluzania".
size_t POL_SfzEngineRender(float *out, size_t frames);

// PORT: haczek testowy - wczytuje plik .sfz z normalizacja backslashy do
// podanego syntezatora (sfizz_synth_t* przekazany jako void*), zwraca liczbe
// regionow albo -1. Testy laduja tak syntetyczne .sfz z katalogu tmp.
int POL_SfzLoadNormalizedFile(void *synth, const char *path);

// Wzmocnienie toru MIDI przy miksu (domylnie 1.0f; relacja do S3M - patrz
// raport). Nie thread-safe (ustawiane przy starcie/wlascicielem miksera).
void POL_SfzEngineSetGain(float g);

#ifdef __cplusplus
}
#endif

#endif // POL_SFZ_ENGINE_H