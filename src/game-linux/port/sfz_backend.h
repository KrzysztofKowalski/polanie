// PORT: backend toru MIDI+sfizz (VSCO CE) - adapter nad port/sfz_engine.h.
// Nic nie przepisuje: POL_SfzEngine* wywoływane 1:1 (zachowanie, komunikaty i
// kolejność identyczne z dotychczasowym połączeniem w port_audio.cpp).
// Wybór przez audio_backend.h (POL_AudioBackend); SfzBackend nie ma własnego
// stanu - silnik pod nim jest globalny jak przed refaktorem.
#ifndef POL_SFZ_BACKEND_H
#define POL_SFZ_BACKEND_H

#include "audio_backend.h"
#include "sfz_engine.h" // POL_SfzEngine* (extern "C")

class SfzBackend : public POL_AudioBackend {
public:
  const char *name() const override { return "sfz (sfizz+VSCO)"; }

  int init(float rate) override { return POL_SfzEngineInit(rate); }

  int prepare(const unsigned char *mid, size_t len, const char *vsco_dir,
              const char *name) override {
    return POL_SfzEnginePrepare(mid, len, vsco_dir, name);
  }

  void commit(int loop) override { POL_SfzEngineCommit(loop); }
  void abortPrepare() override { POL_SfzEngineAbort(); }
  void stop() override { POL_SfzEngineStop(); }
  int playing() const override { return POL_SfzEnginePlaying(); }
  const char *currentName() const override { return POL_SfzEngineCurrentName(); }

  size_t render(float *out, size_t frames) override {
    return POL_SfzEngineRender(out, frames);
  }

  void setGain(float g) override { POL_SfzEngineSetGain(g); }
};

#endif // POL_SFZ_BACKEND_H
