// PORT: backend toru S3M (libopenmpt) - adapter nad audio_backend.h.
// Przenosi istniejący tor S3M z port/port_audio.cpp (§openmpt w mix_cb,
// POL_MusicPlay i POL_MusicStop) do klasy: modul ładowany w prepare(),
// odtwarzanie w render() z zapętleniem i wzmocnieniem (MASTERGAIN_MILLIBEL).
// Brak libopenmpt w buildzie: prepare() zwraca -2 (raz loguje), gra działa
// jak dotąd (cisza na torze S3M z logiem, reszta bez zmian).
// Nagłówek bez zależności od libopenmpt (PIMPL).
#ifndef POL_S3M_BACKEND_H
#define POL_S3M_BACKEND_H

#include <memory>

#include "audio_backend.h"

class S3mBackend : public POL_AudioBackend {
public:
  S3mBackend();
  ~S3mBackend() override;

  const char *name() const override { return "s3m (libopenmpt)"; }
  int available() const override;

  int init(float rate) override;

  // data/len = surowe bajty modulu S3M; bank_path nieużywany ("").
  // Zwraca określone w audio_backend.h: >0 po przygotowaniu, -1 błąd parsera,
  // -2 brak libopenmpt w buildzie (komunikat wypisany tu, raz).
  int prepare(const unsigned char *data, size_t len, const char *bank_path,
              const char *name) override;

  void commit(int loop) override;
  void abortPrepare() override;
  void stop() override;
  int playing() const override;
  const char *currentName() const override;
  size_t render(float *out, size_t frames) override;

  // Liniowe wzmocnienie 0..1 -> MASTERGAIN_MILLIBEL (skala jak vol_gain(), 0 =
  // wyciszenie) - własna kopia konwersji z port_audio.cpp (tor S3M nie zna
  // tego pliku).
  void setGain(float g) override;

private:
  struct Impl;
  std::unique_ptr<Impl> imp_;
};

#endif // POL_S3M_BACKEND_H
