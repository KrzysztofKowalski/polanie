// PORT: implementacja backendu S3M (audio_backend.h) - tor libopenmpt.
// Kopiuje 1:1 zachowanie starego połączenia w port/port_audio.cpp: moduł
// wczytany z pamięci, render stereo 48 kHz do bufora miksera, zapętlenie
// plansz/menu, kończenie utworu (active=0 gdy utwór dograł do końca).
#include "s3m_backend.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include <string>

#if __has_include(<libopenmpt/libopenmpt.h>)
#define POL_S3M_HAVE_OPENMPT 1
#include <libopenmpt/libopenmpt.h>
#else
#define POL_S3M_HAVE_OPENMPT 0
#endif

namespace {
// tego samego kontekstu: wzmocnienie liniowe (vol_gain 0-5) -> setne decybeli
// dla OPENMPT_MODULE_RENDER_MASTERGAIN_MILLIBEL (0 mB = 1.0).
int32_t pol_s3m_vol_millibel(float g) {
  if (g <= 0.0f)
    return -96000; // wyciszone (jak -inf w log10)
  return (int32_t)lround(2000.0f * log10f(g));
}
} // namespace

struct S3mBackend::Impl {
  float rate = 48000.0f;
#if POL_S3M_HAVE_OPENMPT
  openmpt_module *mod = NULL;   // aktywny moduł (pod mutexem miksera)
  openmpt_module *pend = NULL;  // przygotowany przez prepare() (poza mutexem)
#endif
  bool active = false;
  int loop = 0;
  float gain = 1.0f;
  std::string name;
  bool missingLogged = false;

#if POL_S3M_HAVE_OPENMPT
  static void freeMod(openmpt_module **m) {
    if (*m) {
      openmpt_module_destroy(*m);
      *m = NULL;
    }
  }
#endif
};

// do not compile without backend - define minimal stubs
S3mBackend::S3mBackend() : imp_(new Impl()) {}
S3mBackend::~S3mBackend() {
#if POL_S3M_HAVE_OPENMPT
  imp_->freeMod(&imp_->mod);
  imp_->freeMod(&imp_->pend);
#endif
}

int S3mBackend::available() const {
#if POL_S3M_HAVE_OPENMPT
  return 1;
#else
  return 0;
#endif
}

int S3mBackend::init(float rate) {
  imp_->rate = rate;
  return 0;
}

int S3mBackend::prepare(const unsigned char *data, size_t len,
                        const char *, const char *name) {
#if POL_S3M_HAVE_OPENMPT
  // dwufazowo jak sfz_engine: moduł wczytywany przez Prepare (poza mutexem),
  // podmiana stanu w commit() (pod mutexem) - stary moduł gra do końca
  imp_->freeMod(&imp_->pend);
  // UWAGA: create_from_memory() jest LIBOPENMPT_DEPRECATED (pelny rebuild z
  // -Werror); create_from_memory2() istnieje od 0.3.0 - stare warianty API
  // (errfunc/erruser/error/error_message) niepotrzebne -> NULL.
  imp_->pend = openmpt_module_create_from_memory2(
      data, len, openmpt_log_func_silent, NULL, NULL, NULL, NULL, NULL,
      NULL);
  if (!imp_->pend)
    return -1; // komunikat wypisuje POL_MusicPlay (jak dotąd)
  imp_->name = name ? name : "";
  return 1;
#else
  if (!imp_->missingLogged) {
    imp_->missingLogged = 1;
    fprintf(stderr, "PORT: libopenmpt brak - muzyka S3M wylaczona\n");
  }
  (void)data;
  (void)len;
  (void)name;
  return -2;
#endif
}

void S3mBackend::commit(int loop) {
  imp_->active = 0;
#if POL_S3M_HAVE_OPENMPT
  imp_->freeMod(&imp_->mod);
  imp_->mod = imp_->pend;
  imp_->pend = NULL;
  if (imp_->mod)
    openmpt_module_set_render_param(
        imp_->mod, OPENMPT_MODULE_RENDER_MASTERGAIN_MILLIBEL,
        pol_s3m_vol_millibel(imp_->gain));
#else
  (void)loop;
#endif
  imp_->loop = loop;
  imp_->active = 1;
}

void S3mBackend::abortPrepare() {
#if POL_S3M_HAVE_OPENMPT
  imp_->freeMod(&imp_->pend);
#endif
}

void S3mBackend::stop() {
  imp_->active = 0;
#if POL_S3M_HAVE_OPENMPT
  imp_->freeMod(&imp_->mod);
#endif
  imp_->name.clear();
}

int S3mBackend::playing() const { return imp_->active; }

const char *S3mBackend::currentName() const { return imp_->name.c_str(); }

size_t S3mBackend::render(float *out, size_t frames) {
#if POL_S3M_HAVE_OPENMPT
  if (!imp_->active || !imp_->mod)
    return 0;
  // jak dawne połączenie w port_audio.cpp mix_cb: render + zapętlenie +
  // dezaktywacja po końcu utworu (zwrotka < frames -> caller zeruje resztę)
  size_t done = openmpt_module_read_interleaved_float_stereo(
      imp_->mod, (int)imp_->rate, (int)frames, out);
  if (done < frames && imp_->loop) {
    openmpt_module_set_position_seconds(imp_->mod, 0.0);
    done += openmpt_module_read_interleaved_float_stereo(
        imp_->mod, (int)imp_->rate, (int)(frames - done),
        out + done * 2); // stereo: 2 floaty na klatkę
  }
  if (done < frames)
    imp_->active = 0;
  return done;
#else
  (void)out;
  (void)frames;
  return 0;
#endif
}

void S3mBackend::setGain(float g) {
  imp_->gain = g;
#if POL_S3M_HAVE_OPENMPT
  if (imp_->mod)
    openmpt_module_set_render_param(
        imp_->mod, OPENMPT_MODULE_RENDER_MASTERGAIN_MILLIBEL,
        pol_s3m_vol_millibel(g));
#endif
}
