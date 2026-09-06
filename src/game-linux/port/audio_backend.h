// PORT: abstrakcja backendu muzyki (audio) - jeden wirtualny interfejs dla
// trzech silników:
//   - SfzBackend   - tor MIDI+sfizz (VSCO CE), port/sfz_backend.h;
//   - Mt32Backend  - tor MT32 (FluidSynth + soundfont Hedsound), mt32_backend.h;
//   - S3mBackend   - tor S3M (libopenmpt), s3m_backend.h.
// Wybór pełni port_audio.cpp (POL_SetAudioType + POL_MusicPlay): POL_MusicPlay
// wywołuje backend przez wskaźnik - jedna ścieżka zamiast gałęzi per silnik.
//
// Kontrakt (zachowany po staremu tory SFZ, patrz port/sfz_engine.h):
//   - init():     leniwe + idempotentne, wywoływane POZA mutexem miksera
//                 (może tworzyć ciężkie obiekty);
//   - prepare():  parsuje dane utworu i ładuje dysk (soundfont/modul); dalej
//                 STARY utwór gra; wywoływane POZA mutexem (ładuje sekundy);
//   - commit():   pod mutexem miksera - zwalnia stary utwór, instaluje
//                 przygotowany, ustawia zapętlenie i startuje;
//   - abortPrepare(): porzuca stan oczekujący (błąd wczytywania);
//   - stop():     ucina utwór i zwalnia (pod mutexem);
//   - render():   w callbacku audio; DODAJE syntezę * gain do out (F32
//                 interleaved stereo, frames klatek): zwrotka < frames gdy
//                 utwór skończył się w środku bloku (caller zeruje resztę),
//                 a kolejne wywołanie po dezaktywacji daje 0;
//   - setGain():  wspólna skala głośności muzyki (0-5 -> liniowa, jak
//                 vol_gain w port_audio.cpp); czułe na czas: ustawiane przy
//                 starcie i przy POL_MusicSetVolume (pod mx);
//   - playing():  >0 dopóki render() ma co dawać (po końcu utworu z ogonem
//                 => 0; caller stawia music_active=0 jak przy torze sfzz).
// Wewnątrz preparacji/renderingu backend same dysponuje swoimi zdarzeniami
// (własny scheduler, jak polmidi -> sfizz w sfz_engine.cpp), więc interfejs
// nie udostępnia pojedynczych nut/CC - wystarczy graniczna powierzchnia.
// Nagłówek bez zależności od bibliotek (SDL/fluidsynth/openmpt) - linkowalny
// w testach.
#ifndef POL_AUDIO_BACKEND_H
#define POL_AUDIO_BACKEND_H

#include <cstddef>

class POL_AudioBackend {
public:
  virtual ~POL_AudioBackend() = default;

  // Nazwa silnika do logów (przy --audioType).
  virtual const char *name() const = 0;

  // 1 = silnik wykompilowany i może pracować; 0 = stub (np. brak libfluidsynth
  // przy buildzie) - POL_MusicPlay wypisze wtedy komunikat i zwróci 0.
  virtual int available() const { return 1; }

  // Inicjalizacja silnika (leniwa, idempotentna). rate = częstotliwość
  // miksera (port_audio: 48000). 0 = OK, 1 = błąd.
  virtual int init(float rate) = 0;

  // Przygotowanie utworu: data/len = surowe bajty (MIDI dla toru SFZ/MT32,
  // S3M dla toru openmpt), bank_path = katalog soundfontów/ścieżka SF2
  // ("" gdy silnik nie potrzebuje), name = napis do logów/komunikatów.
  // Zwraca liczbę "jednostek" (>0, np. regionów sfizz / presetów MT32 albo
  // 1 dla modułów), -1 = błąd, -2 = silnik niedostępny w buildzie.
  virtual int prepare(const unsigned char *data, size_t len,
                      const char *bank_path, const char *name) = 0;

  // Co dalej: patrz kontrakt wyżej.
  virtual void commit(int loop) = 0;
  virtual void abortPrepare() = 0;
  virtual void stop() = 0;
  virtual int playing() const = 0;
  virtual const char *currentName() const = 0;

  // Render bloku (w callbacku audio, pod mx). Patrz kontrakt.
  virtual size_t render(float *out, size_t frames) = 0;

  // Wzmocnienie toru w miksu (jak POL_SfzEngineSetGain).
  virtual void setGain(float g) = 0;
};

#endif // POL_AUDIO_BACKEND_H
