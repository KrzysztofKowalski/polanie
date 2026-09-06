// PORT: backend toru MT32 (audio_backend.h) - FluidSynth + soundfont
// MT-32 Hedsound (assets/soundfont/*GM.sf2, 447 MiB). Tor włączany
// --audioType=mt32 (patrz port/audio_opts.h).
//
// Kluczowe założenia (analiza 2026-09-06):
//   - jeden fluid_synth na 16 kanałów MIDI (kanały mapowane 1:1);
//   - GM REMAPA w kodzie: bank 0 tego SF2 ma porządek MT-32 (załączony
//     *GM.txt: "000:054 = Cello 1 -> 042 GM"), program GM z .mid nie = preset
//     banku 0. Tabela gm2preset[128] budowana z pliku GM.txt obok SF2
//     (fallback: identyczność gm2preset[g]=g);
//   - scenariusz: fanowskie MIDI używają GM 42/43/47/48/59/79/80 - przy
//     identity-mapping poprawnie brzmi tylko 48, remapa jest konieczna.
//
//
// PELNY PRE-RENDER (zmiana po artefaktach 2026-09-06): FluidSynth NIE działa
// w callbacku audio - prepare() wylicza CAŁY utwór offline do bufora F32
// stereo (render = czysta kopia z bufora, zapętlenie w kopii). To eliminuje
// JIT ładowania próbek i ciężki synth z wątku RT (zero stutterów/rejestru),
// a brzmienie = brzmieniu CLI (potwierdzone: 11,8 MB WAV z "fluidsynth -a
// file"). Głębiej w komentarzach wavmusic_backend.cpp.
//
// UWAGA RAM: fluid_synth_sfload() wczytuje CAŁE próbki SF2 do pamięci
// (~447 MiB pliku -> ok. 0.6-1 GB RAM; patrz raport audio), do tego bufor
// pre-renderu: ~23 MB/min (max 300 s utworu = ~230 MB, limit w
// wavmusic_backend.cpp). To najcięższy z trzech backendów - do toru S3M nie jest
// potrzebny. Chorus/reverb FluidSynth są wyłączone (synth przezroczysty).
//
// TRYB WAV ("zamiast MIDI leci WAV jako muzyka"): gdy prepare() dostanie
// bank_path kończący się na ".wav" (POLANIE_MT32_WAVDIR + GRAF_NNN.mt32.wav,
// wygenerowany przez scripts/audio/mt32-export-all.sh) — backend wczytuje
// gotowy pre-render zamiast MIDI/SF2 (PCM16/24 mono/stereo -> F32 @ mix rate,
// resampling liniowy; zapętlanie przed ~2 s ogona). Zero SF2, zero czekania.
//
// Nagłówek bez zależności od FluidSynth (PIMPL); gdy build nie miał
//
// Nagłówek bez zależności od FluidSynth (PIMPL); gdy build nie miał
// libfluidsynth (POL_HAVE_MT32 niezdefiniowane w Makefile) - classa działa
// jako stub (available()=0, prepare()=-1) i gra działa jak przed dodaniem.
#ifndef POL_WAVMUSIC_BACKEND_H
#define POL_WAVMUSIC_BACKEND_H

#include <memory>

#include "audio_backend.h"

class WavMusicBackend : public POL_AudioBackend {
public:
  WavMusicBackend();
  ~WavMusicBackend() override;

  const char *name() const override { return "mt32 (FluidSynth)"; }
  int available() const override;

  int init(float rate) override;

  // mid/len = SMF (ten sam parser polmidi co tor sfz); sf2_path = pełna
  // ścieżka do *GM.sf2 (POL_Mt32Sf2Path z port/midi_map_files.cpp).
  // Zwraca >0 = liczba obsadzonych kanałów MIDI, -1 = błąd, -2 = brak
  // libfluidsynth.
  int prepare(const unsigned char *mid, size_t len, const char *sf2_path,
              const char *name) override;

  void commit(int loop) override;
  void abortPrepare() override;
  void stop() override;
  int playing() const override;
  const char *currentName() const override;
  size_t render(float *out, size_t frames) override;
  void setGain(float g) override;

private:
  // wysylka jednego zdarzenia do fluid (z remapa GM; delay = podblock)
  void dispatch(int type, int channel, int a, int b, int pitch, int delay);
  struct Impl;
  std::unique_ptr<Impl> imp_;
};

#endif // POL_WAVMUSIC_BACKEND_H
